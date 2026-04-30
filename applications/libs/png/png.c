#define ROS_BUILDING_PNG_DLL 1
#define ROS_PNG_EXPORTS 1

#include "app/png.h"

#include "app/kernel.h"

DLL_EXPORT(PngGetInfo);
DLL_EXPORT(PngGetInfoFromMemory);
DLL_EXPORT(PngRenderFileToMemory);
DLL_EXPORT(PngRenderMemoryToMemory);

#define PNG_SIGNATURE_SIZE 8U
#define PNG_CHUNK_IHDR 0x49484452UL
#define PNG_CHUNK_IDAT 0x49444154UL
#define PNG_CHUNK_IEND 0x49454E44UL
#define PNG_CHUNK_PLTE 0x504C5445UL

#define PNG_COLOR_TYPE_GRAYSCALE 0U
#define PNG_COLOR_TYPE_RGB 2U
#define PNG_COLOR_TYPE_PALETTE 3U
#define PNG_COLOR_TYPE_GRAY_ALPHA 4U
#define PNG_COLOR_TYPE_RGBA 6U

#define PNG_FILTER_NONE 0U
#define PNG_FILTER_SUB 1U
#define PNG_FILTER_UP 2U
#define PNG_FILTER_AVERAGE 3U
#define PNG_FILTER_PAETH 4U

#define PNG_MAX_HUFFMAN_NODES 2048U

typedef struct PngHeader {
    U32 width;
    U32 height;
    U8 bit_depth;
    U8 color_type;
    U8 compression_method;
    U8 filter_method;
    U8 interlace_method;
} PngHeader;

typedef struct PngBitReader {
    const U8* data;
    Size size;
    Size byte_offset;
    U32 bit_buffer;
    U32 bit_count;
} PngBitReader;

typedef struct PngHuffmanNode {
    I32 left;
    I32 right;
    I32 symbol;
} PngHuffmanNode;

typedef struct PngHuffmanTree {
    PngHuffmanNode nodes[PNG_MAX_HUFFMAN_NODES];
    I32 node_count;
} PngHuffmanTree;

static const U8 kPngSignature[PNG_SIGNATURE_SIZE] = {
    0x89U, 'P', 'N', 'G', 0x0DU, 0x0AU, 0x1AU, 0x0AU,
};

static const U16 kDeflateLengthBase[29] = {
    3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U,
    11U, 13U, 15U, 17U, 19U, 23U, 27U, 31U,
    35U, 43U, 51U, 59U, 67U, 83U, 99U, 115U,
    131U, 163U, 195U, 227U, 258U,
};

static const U8 kDeflateLengthExtra[29] = {
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    1U, 1U, 1U, 1U, 2U, 2U, 2U, 2U,
    3U, 3U, 3U, 3U, 4U, 4U, 4U, 4U,
    5U, 5U, 5U, 5U, 0U,
};

static const U16 kDeflateDistanceBase[30] = {
    1U, 2U, 3U, 4U, 5U, 7U, 9U, 13U,
    17U, 25U, 33U, 49U, 65U, 97U, 129U, 193U,
    257U, 385U, 513U, 769U, 1025U, 1537U, 2049U, 3073U,
    4097U, 6145U, 8193U, 12289U, 16385U, 24577U,
};

static const U8 kDeflateDistanceExtra[30] = {
    0U, 0U, 0U, 0U, 1U, 1U, 2U, 2U,
    3U, 3U, 4U, 4U, 5U, 5U, 6U, 6U,
    7U, 7U, 8U, 8U, 9U, 9U, 10U, 10U,
    11U, 11U, 12U, 12U, 13U, 13U,
};

/*
 * Allocate one kernel-backed temporary buffer.
 *
 * The PNG helper keeps all parsing and inflate work freestanding, so any
 * scratch space it needs must come from the same EL0 allocator path used by
 * other userspace runtime helpers.
 *
 * @param bytes Requested allocation size.
 * @return Pointer to the allocated block, or NULL on failure.
 */
static void* png_allocate(Size bytes) {
    if (bytes == 0U) {
        return 0;
    }

    return (void*)(uintptr_t)user_kernel_alloc((unsigned long)bytes);
}

/*
 * Release one kernel-backed temporary buffer.
 *
 * @param buffer Allocation previously returned by png_allocate.
 * @return Nothing.
 */
static void png_release(void* buffer) {
    if (buffer != 0) {
        (void)user_kernel_free(buffer);
    }
}

/*
 * Yield periodically during CPU-heavy PNG inflate and row reconstruction work.
 *
 * The PNG decoder often runs on the caller thread after async staging has
 * already finished, so the remaining inflate and row-filter loops need their
 * own cooperative pause points to keep window/input processing observable.
 *
 * @param iteration Zero-based loop counter or progress marker.
 * @param stride Number of completed units between yields.
 * @param more_work_expected Non-zero when more decode work remains.
 * @return Nothing.
 */
static void png_cooperative_work_yield(Size iteration, Size stride, int more_work_expected) {
    if (!more_work_expected || stride == 0U) {
        return;
    }
    if (((iteration + 1U) % stride) != 0U) {
        return;
    }

    (void)sleepMs(1UL);
}

/*
 * Read one big-endian 32-bit value from a PNG byte stream.
 *
 * @param data Four bytes of big-endian input.
 * @return Parsed 32-bit value.
 */
static U32 png_read_be_u32(const U8* data) {
    return ((U32)data[0] << 24)
        | ((U32)data[1] << 16)
        | ((U32)data[2] << 8)
        | (U32)data[3];
}

/*
 * Read one little-endian 16-bit value from a DEFLATE stream.
 *
 * @param data Two bytes of little-endian input.
 * @return Parsed 16-bit value.
 */
static U16 png_read_le_u16(const U8* data) {
    return (U16)((U16)data[0] | ((U16)data[1] << 8));
}

/*
 * Compare one PNG signature against the expected magic bytes.
 *
 * @param data File bytes beginning at offset zero.
 * @param size File size in bytes.
 * @return Non-zero when the signature matches.
 */
static int png_has_signature(const U8* data, Size size) {
    Size index;

    if ((data == 0) || (size < PNG_SIGNATURE_SIZE)) {
        return 0;
    }

    for (index = 0U; index < PNG_SIGNATURE_SIZE; ++index) {
        if (data[index] != kPngSignature[index]) {
            return 0;
        }
    }

    return 1;
}

/*
 * Return the number of channels implied by one PNG color type.
 *
 * The loader only accepts 8-bit PNG images, so the color type fully determines
 * how many bytes belong to one pixel before filter reconstruction.
 *
 * @param color_type PNG color type code from IHDR.
 * @return Channel count on success, or zero when the type is unsupported.
 */
static U32 png_channels_for_color_type(U8 color_type) {
    switch (color_type) {
    case PNG_COLOR_TYPE_GRAYSCALE:
        return 1U;
    case PNG_COLOR_TYPE_RGB:
        return 3U;
    case PNG_COLOR_TYPE_GRAY_ALPHA:
        return 2U;
    case PNG_COLOR_TYPE_RGBA:
        return 4U;
    default:
        return 0U;
    }
}

/*
 * Parse one IHDR chunk into a compact header structure.
 *
 * PNGs used by the current asset pipeline are 8-bit, non-interlaced images, so
 * the parser rejects other depths and interlace modes instead of trying to
 * guess at a lossy fallback.
 *
 * @param data Raw IHDR chunk payload.
 * @param length Chunk payload size.
 * @param header Receives the parsed header when validation succeeds.
 * @return Zero on success, or a negative status code on failure.
 */
static long png_parse_ihdr(const U8* data, Size length, PngHeader* header) {
    U32 channels;

    if ((data == 0) || (header == 0) || (length != 13U)) {
        return StatusInvalidArgument;
    }

    header->width = png_read_be_u32(data + 0U);
    header->height = png_read_be_u32(data + 4U);
    header->bit_depth = data[8U];
    header->color_type = data[9U];
    header->compression_method = data[10U];
    header->filter_method = data[11U];
    header->interlace_method = data[12U];

    if ((header->width == 0U)
        || (header->height == 0U)
        || (header->compression_method != 0U)
        || (header->filter_method != 0U)
        || (header->interlace_method != 0U)
        || (header->bit_depth != 8U)) {
        return StatusNotSupported;
    }

    channels = png_channels_for_color_type(header->color_type);
    if (channels == 0U) {
        return StatusNotSupported;
    }

    if ((header->width > 0x3FFFFFFFU) || (header->height > 0x3FFFFFFFU)) {
        return StatusNoSpace;
    }

    return StatusOK;
}

/*
 * Grow one heap buffer while preserving its current payload bytes.
 *
 * This helper keeps the file reader and the IDAT collector on the same simple
 * doubling strategy so the decoder does not need a hosted realloc primitive.
 *
 * @param buffer_io Receives the resized buffer.
 * @param capacity_io Current capacity on entry and new capacity on success.
 * @param used_bytes Number of bytes that must survive the resize.
 * @param required_bytes Minimum capacity required after the resize.
 * @return Zero on success, or a negative status code on failure.
 */
static long png_reallocate_bytes(U8** buffer_io, Size* capacity_io, Size used_bytes, Size required_bytes) {
    U8* buffer;
    U8* replacement;
    Size capacity;
    Size new_capacity;
    Size index;
    Size max_size;

    if ((buffer_io == 0) || (capacity_io == 0)) {
        return StatusInvalidArgument;
    }

    buffer = *buffer_io;
    capacity = *capacity_io;
    if (required_bytes <= capacity) {
        return StatusOK;
    }

    max_size = (Size)~(Size)0;
    new_capacity = (capacity == 0U) ? 4096U : capacity;
    while (new_capacity < required_bytes) {
        if (new_capacity > (max_size / 2U)) {
            return StatusNoMemory;
        }
        new_capacity *= 2U;
    }

    replacement = (U8*)png_allocate(new_capacity);
    if (replacement == 0) {
        return StatusNoMemory;
    }

    for (index = 0U; index < used_bytes; ++index) {
        replacement[index] = buffer[index];
    }

    png_release(buffer);
    *buffer_io = replacement;
    *capacity_io = new_capacity;
    return StatusOK;
}

/*
 * Load one complete file into a kernel-backed byte array.
 *
 * The PNG parser needs random access over the full file so it can collect IHDR,
 * IDAT, and ancillary chunks without leaning on a streaming filesystem API.
 *
 * @param path VFS path to the PNG file.
 * @param bytes_out Receives the loaded file buffer.
 * @param size_out Receives the loaded file size.
 * @return Zero on success, or a negative status code on failure.
 */
static long png_read_entire_file(const char* path, U8** bytes_out, Size* size_out) {
    U8* buffer;
    Size capacity;
    Size size;
    Size available;
    long read_result;
    long status;

    if ((path == 0) || (bytes_out == 0) || (size_out == 0)) {
        return StatusInvalidArgument;
    }

    buffer = (U8*)png_allocate(4096U);
    if (buffer == 0) {
        return StatusNoMemory;
    }

    capacity = 4096U;
    size = 0U;
    for (;;) {
        available = capacity - size;
        if (available == 0U) {
            status = png_reallocate_bytes(&buffer, &capacity, size, capacity * 2U);
            if (status != StatusOK) {
                png_release(buffer);
                return status;
            }
            continue;
        }

        read_result = readFile(path, (unsigned long)size, (char*)buffer + size, (unsigned long)available);
        if (read_result < 0) {
            png_release(buffer);
            return read_result;
        }

        if (read_result == 0) {
            break;
        }

        size += (Size)read_result;
        if ((Size)read_result < available) {
            break;
        }
    }

    if (size == 0U) {
        png_release(buffer);
        return StatusIoError;
    }

    *bytes_out = buffer;
    *size_out = size;
    return StatusOK;
}

/*
 * Append bytes to one growable IDAT collection buffer.
 *
 * PNG stores compressed image data across one or more IDAT chunks, so the
 * decoder gathers them into one contiguous zlib stream before inflation.
 *
 * @param buffer_io Receives the gathered stream buffer.
 * @param capacity_io Current capacity and updated capacity on success.
 * @param size_io Current payload length and updated length on success.
 * @param data Bytes to append.
 * @param length Number of bytes to append.
 * @return Zero on success, or a negative status code on failure.
 */
static long png_append_bytes(U8** buffer_io, Size* capacity_io, Size* size_io, const U8* data, Size length) {
    Size required_bytes;
    long status;
    Size index;

    if ((buffer_io == 0) || (capacity_io == 0) || (size_io == 0) || ((length != 0U) && (data == 0))) {
        return StatusInvalidArgument;
    }

    required_bytes = *size_io + length;
    status = png_reallocate_bytes(buffer_io, capacity_io, *size_io, required_bytes);
    if (status != StatusOK) {
        return status;
    }

    for (index = 0U; index < length; ++index) {
        (*buffer_io)[*size_io + index] = data[index];
    }
    *size_io = required_bytes;
    return StatusOK;
}

/*
 * Parse one PNG file into an IHDR record and a single concatenated IDAT stream.
 *
 * Unknown ancillary chunks are skipped so that normal metadata does not break
 * the decoder, but unknown critical chunks still fail fast.
 *
 * @param file_bytes Complete PNG file bytes.
 * @param file_size Complete PNG file size.
 * @param header Receives the parsed image header.
 * @param idat_bytes_out Receives the concatenated IDAT payload.
 * @param idat_size_out Receives the compressed byte count.
 * @return Zero on success, or a negative status code on failure.
 */
static long png_parse_png_file(
    const U8* file_bytes,
    Size file_size,
    PngHeader* header,
    U8** idat_bytes_out,
    Size* idat_size_out) {

    Size offset;
    U32 chunk_length;
    U32 chunk_type;
    long status;
    U8* idat_bytes;
    Size idat_capacity;
    Size idat_size;
    int saw_ihdr;
    int saw_iend;

    if ((file_bytes == 0) || (header == 0) || (idat_bytes_out == 0) || (idat_size_out == 0)) {
        return StatusInvalidArgument;
    }

    if (!png_has_signature(file_bytes, file_size)) {
        return StatusNotSupported;
    }

    idat_bytes = 0;
    idat_capacity = 0U;
    idat_size = 0U;
    saw_ihdr = 0;
    saw_iend = 0;

    offset = PNG_SIGNATURE_SIZE;
    while ((offset + 8U) <= file_size) {
        chunk_length = png_read_be_u32(file_bytes + offset);
        offset += 4U;
        chunk_type = png_read_be_u32(file_bytes + offset);
        offset += 4U;

        if ((offset + (Size)chunk_length + 4U) > file_size) {
            png_release(idat_bytes);
            return StatusIoError;
        }

        if (chunk_type == PNG_CHUNK_IHDR) {
            status = png_parse_ihdr(file_bytes + offset, (Size)chunk_length, header);
            if (status != StatusOK) {
                png_release(idat_bytes);
                return status;
            }
            saw_ihdr = 1;
        }
        else if (chunk_type == PNG_CHUNK_IDAT) {
            status = png_append_bytes(&idat_bytes, &idat_capacity, &idat_size, file_bytes + offset, (Size)chunk_length);
            if (status != StatusOK) {
                png_release(idat_bytes);
                return status;
            }
        }
        else if (chunk_type == PNG_CHUNK_IEND) {
            saw_iend = 1;
            break;
        }
        else if (chunk_type == PNG_CHUNK_PLTE) {
            /*
             * Palette data is recognized so truecolor PNGs carrying the chunk do
             * not fail, but this renderer only consumes direct 8-bit RGB/RGBA
             * scanlines and therefore does not use the palette payload.
             */
        }
        else if ((chunk_type & 0x20000000UL) == 0UL) {
            png_release(idat_bytes);
            return StatusNotSupported;
        }

        offset += (Size)chunk_length + 4U;
    }

    if ((!saw_ihdr) || (!saw_iend) || (idat_size == 0U)) {
        png_release(idat_bytes);
        return StatusIoError;
    }

    *idat_bytes_out = idat_bytes;
    *idat_size_out = idat_size;
    return StatusOK;
}

/*
 * Compute the Adler-32 checksum of one byte array.
 *
 * PNG wraps the DEFLATE stream in a small zlib header/footer pair, so the
 * decoder verifies the trailing checksum instead of trusting the compressed
 * payload blindly.
 *
 * @param data Input bytes.
 * @param size Number of bytes in the input.
 * @return Adler-32 checksum.
 */
static U32 png_adler32(const U8* data, Size size) {
    U32 s1;
    U32 s2;
    Size index;

    s1 = 1U;
    s2 = 0U;
    for (index = 0U; index < size; ++index) {
        s1 = (s1 + data[index]) % 65521U;
        s2 = (s2 + s1) % 65521U;
    }

    return (s2 << 16) | s1;
}

/*
 * Initialize one DEFLATE bit reader.
 *
 * @param reader Reader state to reset.
 * @param data Compressed DEFLATE payload.
 * @param size Payload size in bytes.
 * @return Nothing.
 */
static void png_bit_reader_init(PngBitReader* reader, const U8* data, Size size) {
    if (reader == 0) {
        return;
    }

    reader->data = data;
    reader->size = size;
    reader->byte_offset = 0U;
    reader->bit_buffer = 0U;
    reader->bit_count = 0U;
}

/*
 * Ensure that at least one requested number of bits is buffered.
 *
 * @param reader Active bit reader.
 * @param bits Number of bits needed for the next decode step.
 * @return Zero on success, or a negative status code on failure.
 */
static long png_bit_reader_ensure_bits(PngBitReader* reader, U32 bits) {
    if (reader == 0) {
        return StatusInvalidArgument;
    }

    while (reader->bit_count < bits) {
        if (reader->byte_offset >= reader->size) {
            return StatusFault;
        }

        reader->bit_buffer |= ((U32)reader->data[reader->byte_offset]) << reader->bit_count;
        reader->bit_count += 8U;
        reader->byte_offset += 1U;
    }

    return StatusOK;
}

/*
 * Read one unsigned value from the DEFLATE bit stream.
 *
 * @param reader Active bit reader.
 * @param bits Number of low-order bits to extract.
 * @param value_out Receives the extracted value.
 * @return Zero on success, or a negative status code on failure.
 */
static long png_bit_reader_read_bits(PngBitReader* reader, U32 bits, U32* value_out) {
    U32 value;
    long status;

    if ((reader == 0) || (value_out == 0)) {
        return StatusInvalidArgument;
    }

    if (bits == 0U) {
        *value_out = 0U;
        return StatusOK;
    }

    status = png_bit_reader_ensure_bits(reader, bits);
    if (status != StatusOK) {
        return status;
    }

    value = reader->bit_buffer & ((1U << bits) - 1U);
    reader->bit_buffer >>= bits;
    reader->bit_count -= bits;
    *value_out = value;
    return StatusOK;
}

/*
 * Discard any partially consumed bits and move to the next byte boundary.
 *
 * Uncompressed DEFLATE blocks start on a byte boundary, so the decoder must
 * throw away whatever padding bits remain after reading the block header.
 *
 * @param reader Active bit reader.
 * @return Nothing.
 */
static void png_bit_reader_align_byte(PngBitReader* reader) {
    if (reader == 0) {
        return;
    }

    reader->bit_buffer = 0U;
    reader->bit_count = 0U;
}

/*
 * Reset one Huffman tree to an empty root node.
 *
 * @param tree Tree to clear.
 * @return Zero on success, or a negative status code on failure.
 */
static long png_huffman_reset(PngHuffmanTree* tree) {
    if (tree == 0) {
        return StatusInvalidArgument;
    }

    tree->node_count = 1;
    tree->nodes[0].left = -1;
    tree->nodes[0].right = -1;
    tree->nodes[0].symbol = -1;
    return StatusOK;
}

/*
 * Insert one canonical Huffman code into a bitwise tree.
 *
 * The DEFLATE bit stream is read least-significant-bit first, so the code is
 * threaded through the tree in the same low-bit-first order.
 *
 * @param tree Destination Huffman tree.
 * @param code Canonical Huffman code.
 * @param length Number of bits in the code.
 * @param symbol Leaf symbol stored at the end of the code path.
 * @return Zero on success, or a negative status code on failure.
 */
static long png_huffman_insert(PngHuffmanTree* tree, U32 code, U32 length, I32 symbol) {
    I32 node_index;
    U32 bit_index;
    U32 bit;
    I32* child;
    I32 next_index;

    if ((tree == 0) || (length == 0U)) {
        return StatusInvalidArgument;
    }

    node_index = 0;
    for (bit_index = 0U; bit_index < length; ++bit_index) {
        bit = (code >> bit_index) & 1U;
        child = bit ? &tree->nodes[node_index].right : &tree->nodes[node_index].left;

        if (bit_index == (length - 1U)) {
            if (*child < 0) {
                if (tree->node_count >= (I32)PNG_MAX_HUFFMAN_NODES) {
                    return StatusNoMemory;
                }

                next_index = tree->node_count++;
                *child = next_index;
                tree->nodes[next_index].left = -1;
                tree->nodes[next_index].right = -1;
                tree->nodes[next_index].symbol = -1;
            }

            node_index = *child;
            tree->nodes[node_index].symbol = symbol;
            return StatusOK;
        }

        if (*child < 0) {
            if (tree->node_count >= (I32)PNG_MAX_HUFFMAN_NODES) {
                return StatusNoMemory;
            }

            next_index = tree->node_count++;
            *child = next_index;
            tree->nodes[next_index].left = -1;
            tree->nodes[next_index].right = -1;
            tree->nodes[next_index].symbol = -1;
        }

        node_index = *child;
    }

    return StatusOK;
}

/*
 * Reverse one canonical Huffman code into DEFLATE bit-stream order.
 *
 * Canonical codes are assigned most-significant-bit first, but DEFLATE stores
 * Huffman bits least-significant-bit first inside the zlib stream. Reversing
 * the populated prefix before inserting it keeps the tree layout aligned with
 * the bit reader used during decode.
 *
 * @param code Canonical Huffman code in normal bit order.
 * @param length Number of valid bits in the code.
 * @return Code with the low `length` bits reversed.
 */
static U32 png_reverse_bits(U32 code, U32 length) {
    U32 reversed;
    U32 bit_index;

    reversed = 0U;
    for (bit_index = 0U; bit_index < length; ++bit_index) {
        reversed = (reversed << 1U) | ((code >> bit_index) & 1U);
    }

    return reversed;
}

/*
 * Build one canonical Huffman tree from a code-length table.
 *
 * @param tree Destination tree.
 * @param lengths Code length table indexed by symbol.
 * @param symbol_count Number of symbols in the table.
 * @return Zero on success, or a negative status code on failure.
 */
static long png_huffman_build(PngHuffmanTree* tree, const U8* lengths, U32 symbol_count) {
    U32 counts[16];
    U32 next_code[16];
    U32 code;
    U32 length;
    U32 symbol;
    long status;
    int saw_symbol;

    if ((tree == 0) || (lengths == 0) || (symbol_count == 0U)) {
        return StatusInvalidArgument;
    }

    status = png_huffman_reset(tree);
    if (status != StatusOK) {
        return status;
    }

    memzero(counts, sizeof(counts));
    memzero(next_code, sizeof(next_code));
    saw_symbol = 0;

    for (symbol = 0U; symbol < symbol_count; ++symbol) {
        length = lengths[symbol];
        if (length > 15U) {
            return StatusNotSupported;
        }
        if (length != 0U) {
            counts[length] += 1U;
            saw_symbol = 1;
        }
    }

    if (!saw_symbol) {
        return StatusNotSupported;
    }

    code = 0U;
    for (length = 1U; length <= 15U; ++length) {
        code = (code + counts[length - 1U]) << 1U;
        next_code[length] = code;
    }

    for (symbol = 0U; symbol < symbol_count; ++symbol) {
        length = lengths[symbol];
        if (length != 0U) {
            code = png_reverse_bits(next_code[length], length);
            status = png_huffman_insert(tree, code, length, (I32)symbol);
            if (status != StatusOK) {
                return status;
            }
            next_code[length] += 1U;
        }
    }

    return StatusOK;
}

/*
 * Decode one Huffman symbol from the current bit stream.
 *
 * @param reader Active bit reader.
 * @param tree Huffman tree used for the decode.
 * @param symbol_out Receives the decoded symbol.
 * @return Zero on success, or a negative status code on failure.
 */
static long png_huffman_decode_symbol(PngBitReader* reader, const PngHuffmanTree* tree, I32* symbol_out) {
    I32 node_index;
    long status;
    U32 bit;

    if ((reader == 0) || (tree == 0) || (symbol_out == 0)) {
        return StatusInvalidArgument;
    }

    node_index = 0;
    for (;;) {
        if ((node_index < 0) || (node_index >= tree->node_count)) {
            return StatusFault;
        }

        if (tree->nodes[node_index].symbol >= 0) {
            *symbol_out = tree->nodes[node_index].symbol;
            return StatusOK;
        }

        status = png_bit_reader_read_bits(reader, 1U, &bit);
        if (status != StatusOK) {
            return status;
        }

        node_index = bit ? tree->nodes[node_index].right : tree->nodes[node_index].left;
    }
}

/*
 * Build the fixed DEFLATE Huffman trees required by PNG zlib streams.
 *
 * @param literal_tree Receives the literal/length tree.
 * @param distance_tree Receives the distance tree.
 * @return Zero on success, or a negative status code on failure.
 */
static long png_build_fixed_trees(PngHuffmanTree* literal_tree, PngHuffmanTree* distance_tree) {
    U8 literal_lengths[288];
    U8 distance_lengths[32];
    U32 symbol;

    if ((literal_tree == 0) || (distance_tree == 0)) {
        return StatusInvalidArgument;
    }

    for (symbol = 0U; symbol < 288U; ++symbol) {
        if (symbol <= 143U) {
            literal_lengths[symbol] = 8U;
        }
        else if (symbol <= 255U) {
            literal_lengths[symbol] = 9U;
        }
        else if (symbol <= 279U) {
            literal_lengths[symbol] = 7U;
        }
        else {
            literal_lengths[symbol] = 8U;
        }
    }

    for (symbol = 0U; symbol < 32U; ++symbol) {
        distance_lengths[symbol] = 5U;
    }

    if (png_huffman_build(literal_tree, literal_lengths, 288U) != StatusOK) {
        return StatusFault;
    }
    if (png_huffman_build(distance_tree, distance_lengths, 32U) != StatusOK) {
        return StatusFault;
    }

    return StatusOK;
}

/*
 * Store one dynamic Huffman code length into the correct table.
 *
 * @param literal_lengths Literal/length code-length table.
 * @param distance_lengths Distance code-length table.
 * @param hlit Literal/length symbol count.
 * @param hdist Distance symbol count.
 * @param index Flattened symbol index inside the combined tables.
 * @param value Code length to store.
 * @return Zero on success, or a negative status code on failure.
 */
static long png_store_dynamic_length(
    U8* literal_lengths,
    U8* distance_lengths,
    U32 hlit,
    U32 hdist,
    U32 index,
    U8 value) {

    if (index < hlit) {
        literal_lengths[index] = value;
        return StatusOK;
    }

    index -= hlit;
    if (index < hdist) {
        distance_lengths[index] = value;
        return StatusOK;
    }

    return StatusFault;
}

/*
 * Build the dynamic DEFLATE Huffman trees stored inside one compressed block.
 *
 * @param reader Active bit reader.
 * @param literal_tree Receives the literal/length tree.
 * @param distance_tree Receives the distance tree.
 * @return Zero on success, or a negative status code on failure.
 */
static long png_build_dynamic_trees(PngBitReader* reader, PngHuffmanTree* literal_tree, PngHuffmanTree* distance_tree) {
    static const U8 kCodeLengthOrder[19] = {
        16U, 17U, 18U, 0U, 8U, 7U, 9U, 6U, 10U, 5U, 11U, 4U, 12U, 3U, 13U, 2U, 14U, 1U, 15U,
    };

    U8 code_length_lengths[19];
    U8 literal_lengths[288];
    U8 distance_lengths[32];
    PngHuffmanTree code_length_tree;
    U32 hlit;
    U32 hdist;
    U32 hclen;
    U32 index;
    U32 total;
    U32 extra_bits;
    U32 repeat_count;
    I32 decoded_symbol;
    U8 repeat_value;
    long status;

    if ((reader == 0) || (literal_tree == 0) || (distance_tree == 0)) {
        return StatusInvalidArgument;
    }

    memzero(code_length_lengths, sizeof(code_length_lengths));
    memzero(literal_lengths, sizeof(literal_lengths));
    memzero(distance_lengths, sizeof(distance_lengths));

    status = png_bit_reader_read_bits(reader, 5U, &hlit);
    if (status != StatusOK) {
        return status;
    }
    status = png_bit_reader_read_bits(reader, 5U, &hdist);
    if (status != StatusOK) {
        return status;
    }
    status = png_bit_reader_read_bits(reader, 4U, &hclen);
    if (status != StatusOK) {
        return status;
    }

    hlit += 257U;
    hdist += 1U;
    hclen += 4U;
    if ((hlit > 288U) || (hdist > 32U)) {
        return StatusNotSupported;
    }

    for (index = 0U; index < hclen; ++index) {
        status = png_bit_reader_read_bits(reader, 3U, &extra_bits);
        if (status != StatusOK) {
            return status;
        }
        code_length_lengths[kCodeLengthOrder[index]] = (U8)extra_bits;
    }

    status = png_huffman_build(&code_length_tree, code_length_lengths, 19U);
    if (status != StatusOK) {
        return status;
    }

    total = hlit + hdist;
    index = 0U;
    repeat_value = 0U;
    while (index < total) {
        status = png_huffman_decode_symbol(reader, &code_length_tree, &decoded_symbol);
        if (status != StatusOK) {
            return status;
        }

        if ((decoded_symbol >= 0) && (decoded_symbol <= 15)) {
            repeat_value = (U8)decoded_symbol;
            status = png_store_dynamic_length(literal_lengths, distance_lengths, hlit, hdist, index, repeat_value);
            if (status != StatusOK) {
                return status;
            }
            ++index;
            continue;
        }

        if (decoded_symbol == 16) {
            if (index == 0U) {
                return StatusFault;
            }

            status = png_bit_reader_read_bits(reader, 2U, &extra_bits);
            if (status != StatusOK) {
                return status;
            }
            repeat_count = 3U + extra_bits;
            while (repeat_count != 0U) {
                status = png_store_dynamic_length(literal_lengths, distance_lengths, hlit, hdist, index, repeat_value);
                if (status != StatusOK) {
                    return status;
                }
                ++index;
                --repeat_count;
                if (index > total) {
                    return StatusFault;
                }
            }
            continue;
        }

        if (decoded_symbol == 17) {
            status = png_bit_reader_read_bits(reader, 3U, &extra_bits);
            if (status != StatusOK) {
                return status;
            }
            repeat_count = 3U + extra_bits;
            repeat_value = 0U;
            while (repeat_count != 0U) {
                status = png_store_dynamic_length(literal_lengths, distance_lengths, hlit, hdist, index, repeat_value);
                if (status != StatusOK) {
                    return status;
                }
                ++index;
                --repeat_count;
                if (index > total) {
                    return StatusFault;
                }
            }
            continue;
        }

        if (decoded_symbol == 18) {
            status = png_bit_reader_read_bits(reader, 7U, &extra_bits);
            if (status != StatusOK) {
                return status;
            }
            repeat_count = 11U + extra_bits;
            repeat_value = 0U;
            while (repeat_count != 0U) {
                status = png_store_dynamic_length(literal_lengths, distance_lengths, hlit, hdist, index, repeat_value);
                if (status != StatusOK) {
                    return status;
                }
                ++index;
                --repeat_count;
                if (index > total) {
                    return StatusFault;
                }
            }
            continue;
        }

        return StatusNotSupported;
    }

    status = png_huffman_build(literal_tree, literal_lengths, 288U);
    if (status != StatusOK) {
        return status;
    }

    status = png_huffman_build(distance_tree, distance_lengths, 32U);
    if (status != StatusOK) {
        return status;
    }

    return StatusOK;
}

/*
 * Inflate one DEFLATE stream into a raw PNG scanline buffer.
 *
 * @param compressed Zlib-wrapped compressed stream.
 * @param compressed_size Stream size in bytes.
 * @param output Inflated output buffer.
 * @param output_size Expected output size in bytes.
 * @return Zero on success, or a negative status code on failure.
 */
static long png_inflate_zlib(const U8* compressed, Size compressed_size, U8* output, Size output_size) {
    PngBitReader reader;
    PngHuffmanTree literal_tree;
    PngHuffmanTree distance_tree;
    U32 cmf;
    U32 flg;
    U32 final_block;
    U32 block_type;
    Size output_index;
    I32 symbol;
    Size length_value;
    Size distance_value;
    U32 extra_value;
    U32 checksum;
    U16 block_length;
    U16 block_length_complement;
    long status;
    Size copy_index;
    Size distance_offset;
    Size block_limit;

    if ((compressed == 0) || (output == 0)) {
        return StatusInvalidArgument;
    }

    if (compressed_size < 6U) {
        return StatusIoError;
    }

    cmf = compressed[0U];
    flg = compressed[1U];
    if (((cmf & 0x0FU) != 8U) || ((((U32)cmf << 8U) | flg) % 31U) != 0U || ((flg & 0x20U) != 0U)) {
        return StatusNotSupported;
    }

    png_bit_reader_init(&reader, compressed + 2U, compressed_size - 6U);
    output_index = 0U;

    do {
        status = png_bit_reader_read_bits(&reader, 1U, &final_block);
        if (status != StatusOK) {
            return status;
        }

        status = png_bit_reader_read_bits(&reader, 2U, &block_type);
        if (status != StatusOK) {
            return status;
        }

        if (block_type == 0U) {
            png_bit_reader_align_byte(&reader);
            if ((reader.byte_offset + 4U) > reader.size) {
                return StatusFault;
            }

            block_length = png_read_le_u16(reader.data + reader.byte_offset);
            block_length_complement = png_read_le_u16(reader.data + reader.byte_offset + 2U);
            reader.byte_offset += 4U;
            if ((U16)(block_length ^ 0xFFFFU) != block_length_complement) {
                return StatusFault;
            }
            if ((reader.byte_offset + (Size)block_length) > reader.size) {
                return StatusFault;
            }
            if ((output_index + (Size)block_length) > output_size) {
                return StatusNoSpace;
            }

            for (copy_index = 0U; copy_index < (Size)block_length; ++copy_index) {
                output[output_index + copy_index] = reader.data[reader.byte_offset + copy_index];
            }
            reader.byte_offset += (Size)block_length;
            output_index += (Size)block_length;
            continue;
        }

        if (block_type == 1U) {
            status = png_build_fixed_trees(&literal_tree, &distance_tree);
        }
        else if (block_type == 2U) {
            status = png_build_dynamic_trees(&reader, &literal_tree, &distance_tree);
        }
        else {
            return StatusNotSupported;
        }

        if (status != StatusOK) {
            return status;
        }

        for (;;) {
            status = png_huffman_decode_symbol(&reader, &literal_tree, &symbol);
            if (status != StatusOK) {
                return status;
            }

            if ((symbol >= 0) && (symbol < 256)) {
                if (output_index >= output_size) {
                    return StatusNoSpace;
                }

                output[output_index++] = (U8)symbol;
                continue;
            }

            if (symbol == 256) {
                break;
            }

            if ((symbol < 257) || (symbol > 285)) {
                return StatusNotSupported;
            }

            length_value = (Size)kDeflateLengthBase[(U32)symbol - 257U];
            if (kDeflateLengthExtra[(U32)symbol - 257U] != 0U) {
                status = png_bit_reader_read_bits(&reader, kDeflateLengthExtra[(U32)symbol - 257U], &extra_value);
                if (status != StatusOK) {
                    return status;
                }
                length_value += (Size)extra_value;
            }

            status = png_huffman_decode_symbol(&reader, &distance_tree, &symbol);
            if (status != StatusOK) {
                return status;
            }

            if ((symbol < 0) || (symbol > 29)) {
                return StatusNotSupported;
            }

            distance_value = (Size)kDeflateDistanceBase[(U32)symbol];
            if (kDeflateDistanceExtra[(U32)symbol] != 0U) {
                status = png_bit_reader_read_bits(&reader, kDeflateDistanceExtra[(U32)symbol], &extra_value);
                if (status != StatusOK) {
                    return status;
                }
                distance_value += (Size)extra_value;
            }

            if ((distance_value == 0U) || (distance_value > output_index)) {
                return StatusFault;
            }

            block_limit = output_index + length_value;
            if (block_limit > output_size) {
                return StatusNoSpace;
            }

            distance_offset = output_index - distance_value;
            for (copy_index = 0U; copy_index < length_value; ++copy_index) {
                output[output_index++] = output[distance_offset + copy_index];
            }

            png_cooperative_work_yield(output_index / 4096U, 1U, output_index < output_size);
        }
    } while (final_block == 0U);

    if (output_index != output_size) {
        return StatusIoError;
    }

    checksum = png_read_be_u32(compressed + (compressed_size - 4U));
    if (checksum != png_adler32(output, output_size)) {
        return StatusIoError;
    }

    return StatusOK;
}

/*
 * Undo one PNG filter row in place.
 *
 * @param row Current filtered row bytes.
 * @param previous_row Prior unfiltered row bytes, or NULL for the first row.
 * @param row_bytes Number of bytes in one filtered row excluding the filter tag.
 * @param bytes_per_pixel Number of source bytes that make up one pixel.
 * @param filter_type PNG filter code stored in the leading row byte.
 * @return Zero on success, or a negative status code on failure.
 */
static U8 png_paeth_predictor(U8 left, U8 up, U8 up_left);

/*
 * Undo one PNG filter row in place.
 *
 * @param row Current filtered row bytes.
 * @param previous_row Prior unfiltered row bytes, or NULL for the first row.
 * @param row_bytes Number of bytes in one filtered row excluding the filter tag.
 * @param bytes_per_pixel Number of source bytes that make up one pixel.
 * @param filter_type PNG filter code stored in the leading row byte.
 * @return Zero on success, or a negative status code on failure.
 */
static long png_unfilter_row(U8* row, const U8* previous_row, Size row_bytes, U32 bytes_per_pixel, U8 filter_type) {
    Size index;
    U8 left;
    U8 up;
    U8 up_left;

    if ((row == 0) || (bytes_per_pixel == 0U)) {
        return StatusInvalidArgument;
    }

    switch (filter_type) {
    case PNG_FILTER_NONE:
        return StatusOK;

    case PNG_FILTER_SUB:
        for (index = bytes_per_pixel; index < row_bytes; ++index) {
            row[index] = (U8)(row[index] + row[index - bytes_per_pixel]);
        }
        return StatusOK;

    case PNG_FILTER_UP:
        if (previous_row != 0) {
            for (index = 0U; index < row_bytes; ++index) {
                row[index] = (U8)(row[index] + previous_row[index]);
            }
        }
        return StatusOK;

    case PNG_FILTER_AVERAGE:
        for (index = 0U; index < row_bytes; ++index) {
            left = (index >= bytes_per_pixel) ? row[index - bytes_per_pixel] : 0U;
            up = (previous_row != 0) ? previous_row[index] : 0U;
            row[index] = (U8)(row[index] + (U8)((left + up) >> 1U));
        }
        return StatusOK;

    case PNG_FILTER_PAETH:
        for (index = 0U; index < row_bytes; ++index) {
            left = (index >= bytes_per_pixel) ? row[index - bytes_per_pixel] : 0U;
            up = (previous_row != 0) ? previous_row[index] : 0U;
            up_left = ((previous_row != 0) && (index >= bytes_per_pixel)) ? previous_row[index - bytes_per_pixel] : 0U;
            row[index] = (U8)(row[index] + png_paeth_predictor(left, up, up_left));
        }
        return StatusOK;

    default:
        return StatusNotSupported;
    }
}

/*
 * Compute the Paeth predictor used by PNG filter type 4.
 *
 * @param left Left neighbor byte.
 * @param up Above neighbor byte.
 * @param up_left Diagonal neighbor byte.
 * @return Predictor byte.
 */
static U8 png_paeth_predictor(U8 left, U8 up, U8 up_left) {
    I32 p;
    I32 pa;
    I32 pb;
    I32 pc;

    p = (I32)left + (I32)up - (I32)up_left;
    pa = p < (I32)left ? (I32)left - p : p - (I32)left;
    pb = p < (I32)up ? (I32)up - p : p - (I32)up;
    pc = p < (I32)up_left ? (I32)up_left - p : p - (I32)up_left;

    if ((pa <= pb) && (pa <= pc)) {
        return left;
    }
    if (pb <= pc) {
        return up;
    }
    return up_left;
}

/*
 * Render one decoded PNG row into a 32-bit ARGB canvas.
 *
 * @param row Unfiltered source row bytes.
 * @param source_width Width of the source image in pixels.
 * @param canvas Destination pixel buffer.
 * @param canvas_width Destination canvas width in pixels.
 * @param canvas_height Destination canvas height in pixels.
 * @param dest_x Canvas X offset where the row should be written.
 * @param dest_y Canvas Y offset where the row should be written.
 * @param color_type PNG color type used by the source row.
 * @return Zero on success, or a negative status code on failure.
 */
static long png_blit_row(
    const U8* row,
    U32 source_width,
    U32* canvas,
    U32 canvas_width,
    U32 canvas_height,
    U32 dest_x,
    U32 dest_y,
    U8 color_type) {

    U64 canvas_index;
    U64 canvas_limit_x;
    U32 source_x;
    U32 pixel_index;
    U32 red;
    U32 green;
    U32 blue;
    U32 alpha;

    if ((row == 0) || (canvas == 0) || (canvas_width == 0U) || (canvas_height == 0U)) {
        return StatusInvalidArgument;
    }

    if (dest_y >= canvas_height) {
        return StatusOK;
    }

    canvas_limit_x = (U64)canvas_width;
    if ((U64)dest_x >= canvas_limit_x) {
        return StatusOK;
    }

    if (color_type == PNG_COLOR_TYPE_RGBA) {
        for (source_x = 0U; source_x < source_width; ++source_x) {
            if (((U64)dest_x + (U64)source_x) >= canvas_limit_x) {
                break;
            }
            pixel_index = source_x * 4U;
            red = row[pixel_index + 0U];
            green = row[pixel_index + 1U];
            blue = row[pixel_index + 2U];
            alpha = row[pixel_index + 3U];
            canvas_index = ((U64)dest_y * (U64)canvas_width) + (U64)dest_x + (U64)source_x;
            canvas[canvas_index] = ((U32)alpha << 24U) | (red << 16U) | (green << 8U) | blue;
        }
        return StatusOK;
    }

    if (color_type == PNG_COLOR_TYPE_RGB) {
        for (source_x = 0U; source_x < source_width; ++source_x) {
            if (((U64)dest_x + (U64)source_x) >= canvas_limit_x) {
                break;
            }
            pixel_index = source_x * 3U;
            red = row[pixel_index + 0U];
            green = row[pixel_index + 1U];
            blue = row[pixel_index + 2U];
            alpha = 255U;
            canvas_index = ((U64)dest_y * (U64)canvas_width) + (U64)dest_x + (U64)source_x;
            canvas[canvas_index] = (alpha << 24U) | (red << 16U) | (green << 8U) | blue;
        }
        return StatusOK;
    }

    if (color_type == PNG_COLOR_TYPE_GRAYSCALE) {
        for (source_x = 0U; source_x < source_width; ++source_x) {
            if (((U64)dest_x + (U64)source_x) >= canvas_limit_x) {
                break;
            }
            red = row[source_x];
            alpha = 255U;
            canvas_index = ((U64)dest_y * (U64)canvas_width) + (U64)dest_x + (U64)source_x;
            canvas[canvas_index] = (alpha << 24U) | (red << 16U) | (red << 8U) | red;
        }
        return StatusOK;
    }

    if (color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        for (source_x = 0U; source_x < source_width; ++source_x) {
            if (((U64)dest_x + (U64)source_x) >= canvas_limit_x) {
                break;
            }
            pixel_index = source_x * 2U;
            red = row[pixel_index + 0U];
            alpha = row[pixel_index + 1U];
            canvas_index = ((U64)dest_y * (U64)canvas_width) + (U64)dest_x + (U64)source_x;
            canvas[canvas_index] = (alpha << 24U) | (red << 16U) | (red << 8U) | red;
        }
        return StatusOK;
    }

    return StatusNotSupported;
}

/*
 * Parse one in-memory PNG header and report the intrinsic image size.
 *
 * Both the file-based and memory-based entrypoints share the same IHDR parser
 * so staged byte streams and direct file reads report identical dimensions.
 *
 * @param bytes Pointer to the complete PNG byte stream.
 * @param size Byte count stored in `bytes`.
 * @param width Receives the decoded image width.
 * @param height Receives the decoded image height.
 * @return Zero on success, or a negative status code on failure.
 */
static long png_get_info_from_bytes(const U8* bytes, Size size, U32* width, U32* height) {
    PngHeader header;
    long status;
    U32 chunk_length;
    U32 chunk_type;

    if ((bytes == 0) || (width == 0) || (height == 0)) {
        return StatusInvalidArgument;
    }
    if (size < 33U) {
        return StatusIoError;
    }
    if (!png_has_signature(bytes, size)) {
        return StatusNotSupported;
    }

    chunk_length = png_read_be_u32(bytes + PNG_SIGNATURE_SIZE);
    chunk_type = png_read_be_u32(bytes + PNG_SIGNATURE_SIZE + 4U);
    if ((chunk_length != 13U) || (chunk_type != PNG_CHUNK_IHDR)) {
        return StatusNotSupported;
    }

    status = png_parse_ihdr(bytes + PNG_SIGNATURE_SIZE + 8U, 13U, &header);
    if (status != StatusOK) {
        return status;
    }

    *width = header.width;
    *height = header.height;
    return StatusOK;
}

/*
 * Decode and draw one PNG byte stream into the caller canvas.
 *
 * The file-based export now forwards through this helper so callers that stage
 * bytes asynchronously can reuse the same parser and inflate pipeline without
 * reopening the source file on the UI thread.
 *
 * @param bytes Pointer to the complete PNG byte stream.
 * @param size Byte count stored in `bytes`.
 * @param memory Destination canvas description.
 * @return Zero on success, or a negative status code on failure.
 */
static long png_render_memory_to_memory(const U8* bytes, Size size, RosPngMemory* memory) {
    U8* idat_bytes;
    Size idat_size;
    U8* raw_bytes;
    Size raw_size;
    Size row_bytes;
    Size row_index;
    Size scanline_offset;
    U8* current_row;
    U8* previous_row;
    U32 channels;
    U32 bytes_per_pixel;
    PngHeader header;
    U32* canvas;
    U32 canvas_width;
    U32 canvas_height;
    long status;

    idat_bytes = 0;
    raw_bytes = 0;
    current_row = 0;
    previous_row = 0;

    if ((bytes == 0) || (memory == 0) || (memory->buffer == 0) || (memory->width == 0U) || (memory->height == 0U)) {
        return StatusInvalidArgument;
    }

    status = png_parse_png_file(bytes, size, &header, &idat_bytes, &idat_size);
    if (status != StatusOK) {
        return status;
    }

    channels = png_channels_for_color_type(header.color_type);
    if (channels == 0U) {
        png_release(idat_bytes);
        return StatusNotSupported;
    }

    bytes_per_pixel = channels;
    row_bytes = ((Size)header.width * (Size)bytes_per_pixel) + 1U;
    raw_size = row_bytes * (Size)header.height;
    raw_bytes = (U8*)png_allocate(raw_size);
    if (raw_bytes == 0) {
        png_release(idat_bytes);
        return StatusNoMemory;
    }

    status = png_inflate_zlib(idat_bytes, idat_size, raw_bytes, raw_size);
    png_release(idat_bytes);
    if (status != StatusOK) {
        png_release(raw_bytes);
        return status;
    }

    current_row = (U8*)png_allocate(row_bytes - 1U);
    if (current_row == 0) {
        png_release(raw_bytes);
        return StatusNoMemory;
    }

    previous_row = (U8*)png_allocate(row_bytes - 1U);
    if (previous_row == 0) {
        png_release(current_row);
        png_release(raw_bytes);
        return StatusNoMemory;
    }

    canvas = (U32*)memory->buffer;
    canvas_width = memory->width;
    canvas_height = memory->height;

    if ((memory->x >= canvas_width) || (memory->y >= canvas_height)) {
        png_release(previous_row);
        png_release(current_row);
        png_release(raw_bytes);
        return StatusOK;
    }

    scanline_offset = 0U;
    for (row_index = 0U; row_index < (Size)header.height; ++row_index) {
        U8 filter_type;
        U8* swap_row;

        filter_type = raw_bytes[scanline_offset];
        scanline_offset += 1U;

        for (Size index = 0U; index < row_bytes - 1U; ++index) {
            current_row[index] = raw_bytes[scanline_offset + index];
        }

        status = png_unfilter_row(current_row, (row_index == 0U) ? 0 : previous_row, row_bytes - 1U, bytes_per_pixel, filter_type);
        if (status != StatusOK) {
            png_release(previous_row);
            png_release(current_row);
            png_release(raw_bytes);
            return status;
        }

        status = png_blit_row(current_row, header.width, canvas, canvas_width, canvas_height, memory->x, memory->y + (U32)row_index, header.color_type);
        if (status != StatusOK) {
            png_release(previous_row);
            png_release(current_row);
            png_release(raw_bytes);
            return status;
        }

        for (Size index = 0U; index < row_bytes - 1U; ++index) {
            previous_row[index] = current_row[index];
        }

        swap_row = previous_row;
        previous_row = current_row;
        current_row = swap_row;

        scanline_offset += row_bytes - 1U;
        png_cooperative_work_yield(row_index, 8U, row_index + 1U < (Size)header.height);
    }

    png_release(previous_row);
    png_release(current_row);
    png_release(raw_bytes);
    return StatusOK;
}

/*
 * Read one PNG file and report its intrinsic dimensions.
 *
 * @param path Absolute or relative VFS path to the PNG file.
 * @param width Receives the decoded image width.
 * @param height Receives the decoded image height.
 * @return Zero on success, or a negative status code on failure.
 */
long PngGetInfo(const char* path, U32* width, U32* height) {
    U8 header_bytes[33];
    long read_result;

    if ((path == 0) || (width == 0) || (height == 0)) {
        return StatusInvalidArgument;
    }

    read_result = readFile(path, 0U, (char*)header_bytes, sizeof(header_bytes));
    if (read_result < 33L) {
        return (read_result < 0L) ? read_result : StatusIoError;
    }

    return png_get_info_from_bytes(header_bytes, sizeof(header_bytes), width, height);
}

/*
 * Parse one PNG byte stream and report its intrinsic dimensions.
 *
 * @param bytes Pointer to the complete PNG byte stream.
 * @param size Byte count stored in `bytes`.
 * @param width Receives the decoded image width.
 * @param height Receives the decoded image height.
 * @return Zero on success, or a negative status code on failure.
 */
long PngGetInfoFromMemory(const void* bytes, U32 size, U32* width, U32* height) {
    return png_get_info_from_bytes((const U8*)bytes, (Size)size, width, height);
}

/*
 * Read one PNG file and render it into a caller-provided memory canvas.
 *
 * @param path Absolute or relative VFS path to the PNG file.
 * @param memory Destination canvas description and pixel buffer.
 * @return Zero on success, or a negative status code on failure.
 */
long PngRenderFileToMemory(const char* path, RosPngMemory* memory) {
    U8* file_bytes;
    Size file_size;
    long status;

    if (path == 0) {
        return StatusInvalidArgument;
    }

    status = png_read_entire_file(path, &file_bytes, &file_size);
    if (status != StatusOK) {
        return status;
    }

    status = png_render_memory_to_memory(file_bytes, file_size, memory);
    png_release(file_bytes);
    return status;
}

/*
 * Render one PNG byte stream into a caller-provided memory canvas.
 *
 * @param bytes Pointer to the complete PNG byte stream.
 * @param size Byte count stored in `bytes`.
 * @param memory Destination canvas description and pixel buffer.
 * @return Zero on success, or a negative status code on failure.
 */
long PngRenderMemoryToMemory(const void* bytes, U32 size, RosPngMemory* memory) {
    return png_render_memory_to_memory((const U8*)bytes, (Size)size, memory);
}

/*
 * DLL entrypoint used by the loader to attach this module to a process.
 *
 * The PNG helper has no process-local state, so the entrypoint only needs to
 * acknowledge attach and detach notifications.
 *
 * @param image_base Module image base supplied by the loader.
 * @param reason Loader notification reason.
 * @return Non-zero success code for the caller.
 */
int png_entry(void* image_base, U32 reason) {
    (void)image_base;
    (void)reason;

    return 1;
}

