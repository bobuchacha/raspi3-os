#define ROS_JPEG_EXPORTS 1
#include "app/jpeg.h"

#include "app/kernel_gui.h"

namespace {

    constexpr U8 kJpegMarkerPrefix = 0xFFU;
    constexpr U8 kJpegMarkerSoi = 0xD8U;
    constexpr U8 kJpegMarkerEoi = 0xD9U;
    constexpr U8 kJpegMarkerSos = 0xDAU;
    constexpr U8 kJpegMarkerDqt = 0xDBU;
    constexpr U8 kJpegMarkerSof0 = 0xC0U;
    constexpr U8 kJpegMarkerDht = 0xC4U;

    constexpr I32 kIdctScaleBits = 14;
    constexpr I32 kIdctRound = 1 << (kIdctScaleBits - 1);
    constexpr I32 kYcbcrScaleBits = 16;

    constexpr U8 kJpegZigZag[64] = {
        0, 1, 8, 16, 9, 2, 3, 10,
        17, 24, 32, 25, 18, 11, 4, 5,
        12, 19, 26, 33, 40, 48, 41, 34,
        27, 20, 13, 6, 7, 14, 21, 28,
        35, 42, 49, 56, 57, 50, 43, 36,
        29, 22, 15, 23, 30, 37, 44, 51,
        58, 59, 52, 45, 38, 31, 39, 46,
        53, 60, 61, 54, 47, 55, 62, 63,
    };

    constexpr I32 kIdctMatrix[8][8] = {
        { 5793, 8035, 7568, 6811, 5793, 4551, 3135, 1598 },
        { 5793, 6811, 3135, -1598, -5793, -8035, -7568, -4551 },
        { 5793, 4551, -3135, -8035, -5793, 1598, 7568, 6811 },
        { 5793, 1598, -7568, -4551, 5793, 6811, -3135, -8035 },
        { 5793, -1598, -7568, 4551, 5793, -6811, -3135, 8035 },
        { 5793, -4551, -3135, 8035, -5793, -1598, 7568, -6811 },
        { 5793, -6811, 3135, 1598, -5793, 8035, -7568, 4551 },
        { 5793, -8035, 7568, -6811, 5793, -4551, 3135, -1598 },
    };

    struct JpegHuffmanTable {
        bool valid;
        U8 counts[16];
        U8 symbols[256];
        I32 min_code[17];
        I32 max_code[17];
        I32 value_offset[17];
    };

    struct JpegComponent {
        U8 id;
        U8 quant_table;
        U8 dc_table;
        U8 ac_table;
        U8 horizontal_sample;
        U8 vertical_sample;
        I32 dc_predictor;
    };

    struct JpegDecoder {
        const U8* data;
        U32 size;
        U32 width;
        U32 height;
        U32 entropy_offset;
        U32 entropy_size;
        U16 quant_tables[4][64];
        bool quant_valid[4];
        JpegHuffmanTable dc_tables[4];
        JpegHuffmanTable ac_tables[4];
        JpegComponent components[3];
        U8 component_count;
    };

    struct JpegBitReader {
        const U8* data;
        U32 size;
        U32 byte_offset;
        U32 bit_buffer;
        U32 bit_count;
        bool error;
    };

    static U16 jpeg_read_be_u16(const U8* data) {
        return static_cast<U16>((static_cast<U16>(data[0]) << 8) | static_cast<U16>(data[1]));
    }

    static I32 jpeg_clamp_to_byte(I32 value) {
        if (value < 0) {
            return 0;
        }
        if (value > 255) {
            return 255;
        }
        return value;
    }

    static U32 jpeg_encode_surface_color(U32 pixel_format, U32 color) {
        if (pixel_format == ROS_KERNEL_GUI_PIXEL_FORMAT_XBGR8888) {
            return ((color & 0x000000FFU) << 16)
                | (color & 0x0000FF00U)
                | ((color & 0x00FF0000U) >> 16);
        }

        return color;
    }

    static void jpeg_fill_target(const JpegRenderTarget* target, U32 color) {
        const U32 encoded = jpeg_encode_surface_color(target->pixel_format, color);

        for (U32 row = 0U; row < target->height; ++row) {
            U32* pixels = reinterpret_cast<U32*>(static_cast<U8*>(target->pixels) + (row * target->pitch));

            for (U32 column = 0U; column < target->width; ++column) {
                pixels[column] = encoded;
            }
        }
    }

    static Status jpeg_build_huffman_table(JpegHuffmanTable* table) {
        I32 code = 0;
        I32 symbol_offset = 0;

        if (table == nullptr) {
            return StatusInvalidArgument;
        }

        for (I32 length = 1; length <= 16; ++length) {
            const I32 count = table->counts[length - 1];

            if (count == 0) {
                table->min_code[length] = -1;
                table->max_code[length] = -1;
                table->value_offset[length] = symbol_offset;
            }
            else {
                table->min_code[length] = code;
                table->max_code[length] = code + count - 1;
                table->value_offset[length] = symbol_offset - code;
                symbol_offset += count;
                code += count;
            }

            code <<= 1;
        }

        table->valid = true;
        return StatusOK;
    }

    static Status jpeg_parse_quant_tables(JpegDecoder* decoder, const U8* segment, U32 length) {
        U32 offset = 0U;

        if ((decoder == nullptr) || (segment == nullptr)) {
            return StatusInvalidArgument;
        }

        while (offset < length) {
            const U8 table_spec = segment[offset++];
            const U8 precision = static_cast<U8>(table_spec >> 4);
            const U8 table_id = static_cast<U8>(table_spec & 0x0FU);

            if ((precision != 0U) || (table_id >= 4U) || ((length - offset) < 64U)) {
                return StatusNotSupported;
            }

            for (U32 index = 0U; index < 64U; ++index) {
                const U8 zigzag_index = kJpegZigZag[index];
                decoder->quant_tables[table_id][zigzag_index] = segment[offset + index];
            }

            decoder->quant_valid[table_id] = true;
            offset += 64U;
        }

        return StatusOK;
    }

    static Status jpeg_parse_huffman_tables(JpegDecoder* decoder, const U8* segment, U32 length) {
        U32 offset = 0U;

        if ((decoder == nullptr) || (segment == nullptr)) {
            return StatusInvalidArgument;
        }

        while (offset < length) {
            JpegHuffmanTable* table;
            U8 table_spec;
            U8 table_class;
            U8 table_id;
            U32 symbol_count = 0U;

            if ((length - offset) < 17U) {
                return StatusFault;
            }

            table_spec = segment[offset++];
            table_class = static_cast<U8>(table_spec >> 4);
            table_id = static_cast<U8>(table_spec & 0x0FU);
            if (table_id >= 4U) {
                return StatusNotSupported;
            }

            table = (table_class == 0U) ? &decoder->dc_tables[table_id] : &decoder->ac_tables[table_id];
            if (table_class > 1U) {
                return StatusNotSupported;
            }

            for (U32 index = 0U; index < 16U; ++index) {
                table->counts[index] = segment[offset + index];
                symbol_count += table->counts[index];
            }
            offset += 16U;

            if ((length - offset) < symbol_count) {
                return StatusFault;
            }

            for (U32 index = 0U; index < symbol_count; ++index) {
                table->symbols[index] = segment[offset + index];
            }
            offset += symbol_count;

            if (jpeg_build_huffman_table(table) != StatusOK) {
                return StatusFault;
            }
        }

        return StatusOK;
    }

    static Status jpeg_parse_start_of_frame(JpegDecoder* decoder, const U8* segment, U32 length) {
        U32 offset = 0U;

        if ((decoder == nullptr) || (segment == nullptr) || (length < 6U)) {
            return StatusInvalidArgument;
        }
        if (segment[offset++] != 8U) {
            return StatusNotSupported;
        }

        decoder->height = jpeg_read_be_u16(segment + offset);
        offset += 2U;
        decoder->width = jpeg_read_be_u16(segment + offset);
        offset += 2U;
        decoder->component_count = segment[offset++];
        if ((decoder->width == 0U) || (decoder->height == 0U) || (decoder->component_count != 3U) || (length != (6U + (decoder->component_count * 3U)))) {
            return StatusNotSupported;
        }

        for (U32 index = 0U; index < decoder->component_count; ++index) {
            const U8 sampling = segment[offset + 1U];

            decoder->components[index].id = segment[offset];
            decoder->components[index].horizontal_sample = static_cast<U8>(sampling >> 4);
            decoder->components[index].vertical_sample = static_cast<U8>(sampling & 0x0FU);
            decoder->components[index].quant_table = segment[offset + 2U];
            decoder->components[index].dc_table = 0U;
            decoder->components[index].ac_table = 0U;
            decoder->components[index].dc_predictor = 0;
            offset += 3U;

            if ((decoder->components[index].id != static_cast<U8>(index + 1U))
                || (decoder->components[index].horizontal_sample != 1U)
                || (decoder->components[index].vertical_sample != 1U)
                || (decoder->components[index].quant_table >= 4U)) {
                return StatusNotSupported;
            }
        }

        return StatusOK;
    }

    static Status jpeg_parse_start_of_scan(JpegDecoder* decoder, const U8* segment, U32 length) {
        U32 offset = 0U;
        U8 scan_component_count;

        if ((decoder == nullptr) || (segment == nullptr) || (length < 6U)) {
            return StatusInvalidArgument;
        }

        scan_component_count = segment[offset++];
        if ((scan_component_count != decoder->component_count) || (length != (1U + (scan_component_count * 2U) + 3U))) {
            return StatusNotSupported;
        }

        for (U32 scan_index = 0U; scan_index < scan_component_count; ++scan_index) {
            const U8 component_id = segment[offset++];
            const U8 table_selectors = segment[offset++];
            bool matched = false;

            for (U32 component_index = 0U; component_index < decoder->component_count; ++component_index) {
                if (decoder->components[component_index].id == component_id) {
                    decoder->components[component_index].dc_table = static_cast<U8>(table_selectors >> 4);
                    decoder->components[component_index].ac_table = static_cast<U8>(table_selectors & 0x0FU);
                    matched = true;
                    break;
                }
            }

            if (!matched) {
                return StatusFault;
            }
        }

        if ((segment[offset] != 0U) || (segment[offset + 1U] != 63U) || (segment[offset + 2U] != 0U)) {
            return StatusNotSupported;
        }

        return StatusOK;
    }

    static Status jpeg_parse_stream(JpegDecoder* decoder, const U8* jpeg_bytes, U32 jpeg_size) {
        U32 offset = 0U;

        if ((decoder == nullptr) || (jpeg_bytes == nullptr) || (jpeg_size < 4U)) {
            return StatusInvalidArgument;
        }
        if ((jpeg_bytes[0] != kJpegMarkerPrefix) || (jpeg_bytes[1] != kJpegMarkerSoi) || (jpeg_bytes[jpeg_size - 2U] != kJpegMarkerPrefix) || (jpeg_bytes[jpeg_size - 1U] != kJpegMarkerEoi)) {
            return StatusNotSupported;
        }

        memzero(decoder, sizeof(*decoder));
        decoder->data = jpeg_bytes;
        decoder->size = jpeg_size;
        offset = 2U;

        while (offset + 3U < jpeg_size) {
            U8 marker;
            U16 marker_length;
            const U8* segment;

            if (jpeg_bytes[offset++] != kJpegMarkerPrefix) {
                return StatusFault;
            }
            while ((offset < jpeg_size) && (jpeg_bytes[offset] == kJpegMarkerPrefix)) {
                ++offset;
            }
            if (offset >= jpeg_size) {
                return StatusFault;
            }

            marker = jpeg_bytes[offset++];
            if ((marker == kJpegMarkerSoi) || (marker == kJpegMarkerEoi) || ((marker >= 0xD0U) && (marker <= 0xD7U))) {
                continue;
            }
            if (offset + 2U > jpeg_size) {
                return StatusFault;
            }

            marker_length = jpeg_read_be_u16(jpeg_bytes + offset);
            offset += 2U;
            if ((marker_length < 2U) || (offset + marker_length - 2U > jpeg_size)) {
                return StatusFault;
            }

            segment = jpeg_bytes + offset;
            switch (marker) {
            case kJpegMarkerDqt:
                if (jpeg_parse_quant_tables(decoder, segment, marker_length - 2U) != StatusOK) {
                    return StatusNotSupported;
                }
                break;
            case kJpegMarkerDht:
                if (jpeg_parse_huffman_tables(decoder, segment, marker_length - 2U) != StatusOK) {
                    return StatusNotSupported;
                }
                break;
            case kJpegMarkerSof0:
                if (jpeg_parse_start_of_frame(decoder, segment, marker_length - 2U) != StatusOK) {
                    return StatusNotSupported;
                }
                break;
            case kJpegMarkerSos:
                if (jpeg_parse_start_of_scan(decoder, segment, marker_length - 2U) != StatusOK) {
                    return StatusNotSupported;
                }
                decoder->entropy_offset = offset + marker_length - 2U;
                decoder->entropy_size = (jpeg_size - 2U) - decoder->entropy_offset;
                return StatusOK;
            default:
                break;
            }

            offset += marker_length - 2U;
        }

        return StatusFault;
    }

    static void jpeg_bit_reader_init(JpegBitReader* reader, const U8* data, U32 size) {
        reader->data = data;
        reader->size = size;
        reader->byte_offset = 0U;
        reader->bit_buffer = 0U;
        reader->bit_count = 0U;
        reader->error = false;
    }

    static void jpeg_bit_reader_fill(JpegBitReader* reader) {
        while (!reader->error && reader->bit_count <= 24U && reader->byte_offset < reader->size) {
            U8 byte = reader->data[reader->byte_offset++];

            if (byte == kJpegMarkerPrefix) {
                if (reader->byte_offset >= reader->size) {
                    reader->error = true;
                    return;
                }

                if (reader->data[reader->byte_offset] != 0x00U) {
                    reader->error = true;
                    return;
                }

                ++reader->byte_offset;
            }

            reader->bit_buffer = (reader->bit_buffer << 8) | byte;
            reader->bit_count += 8U;
        }
    }

    static I32 jpeg_read_bits(JpegBitReader* reader, U32 bit_count) {
        I32 value;

        if ((reader == nullptr) || (bit_count > 16U)) {
            return -1;
        }
        while (reader->bit_count < bit_count) {
            jpeg_bit_reader_fill(reader);
            if (reader->error) {
                return -1;
            }
        }

        value = static_cast<I32>((reader->bit_buffer >> (reader->bit_count - bit_count)) & ((1U << bit_count) - 1U));
        reader->bit_count -= bit_count;
        return value;
    }

    static I32 jpeg_decode_huffman_symbol(JpegBitReader* reader, const JpegHuffmanTable* table) {
        I32 code = 0;

        if ((reader == nullptr) || (table == nullptr) || !table->valid) {
            return -1;
        }

        for (I32 length = 1; length <= 16; ++length) {
            const I32 bit = jpeg_read_bits(reader, 1U);

            if (bit < 0) {
                return -1;
            }

            code = (code << 1) | bit;
            if ((table->min_code[length] >= 0) && (code <= table->max_code[length])) {
                return table->symbols[table->value_offset[length] + code];
            }
        }

        return -1;
    }

    static I32 jpeg_receive_extend(JpegBitReader* reader, U32 bit_count) {
        I32 value;

        if (bit_count == 0U) {
            return 0;
        }

        value = jpeg_read_bits(reader, bit_count);
        if (value < 0) {
            return value;
        }
        if (value < static_cast<I32>(1U << (bit_count - 1U))) {
            value -= static_cast<I32>((1U << bit_count) - 1U);
        }

        return value;
    }

    static Status jpeg_decode_block(
        JpegBitReader* reader,
        const JpegHuffmanTable* dc_table,
        const JpegHuffmanTable* ac_table,
        const U16 quant_table[64],
        I32* dc_predictor,
        I32 coefficients[64]) {
        I32 symbol;
        I32 diff;

        if ((reader == nullptr) || (dc_table == nullptr) || (ac_table == nullptr) || (quant_table == nullptr) || (dc_predictor == nullptr) || (coefficients == nullptr)) {
            return StatusInvalidArgument;
        }

        memzero(coefficients, sizeof(I32) * 64U);
        symbol = jpeg_decode_huffman_symbol(reader, dc_table);
        if (symbol < 0) {
            return StatusFault;
        }

        diff = jpeg_receive_extend(reader, static_cast<U32>(symbol));
        if (diff < -32767) {
            return StatusFault;
        }

        *dc_predictor += diff;
        coefficients[0] = (*dc_predictor) * static_cast<I32>(quant_table[0]);

        for (U32 zigzag_index = 1U; zigzag_index < 64U;) {
            U32 run_length;
            U32 value_bits;
            I32 value;
            U32 natural_index;

            symbol = jpeg_decode_huffman_symbol(reader, ac_table);
            if (symbol < 0) {
                return StatusFault;
            }
            if (symbol == 0) {
                break;
            }

            run_length = static_cast<U32>((symbol >> 4) & 0x0FU);
            value_bits = static_cast<U32>(symbol & 0x0FU);
            if ((run_length == 15U) && (value_bits == 0U)) {
                zigzag_index += 16U;
                continue;
            }

            zigzag_index += run_length;
            if (zigzag_index >= 64U) {
                return StatusFault;
            }

            value = jpeg_receive_extend(reader, value_bits);
            natural_index = kJpegZigZag[zigzag_index];
            coefficients[natural_index] = value * static_cast<I32>(quant_table[natural_index]);
            ++zigzag_index;
        }

        return StatusOK;
    }

    static void jpeg_inverse_dct(const I32 coefficients[64], U8 samples[64]) {
        I64 row_tmp[8][8];

        for (U32 row = 0U; row < 8U; ++row) {
            for (U32 column = 0U; column < 8U; ++column) {
                I64 sum = 0;

                for (U32 source = 0U; source < 8U; ++source) {
                    sum += static_cast<I64>(coefficients[row * 8U + source]) * static_cast<I64>(kIdctMatrix[column][source]);
                }

                row_tmp[row][column] = sum;
            }
        }

        for (U32 row = 0U; row < 8U; ++row) {
            for (U32 column = 0U; column < 8U; ++column) {
                I64 sum = 0;
                I32 sample;

                for (U32 source = 0U; source < 8U; ++source) {
                    sum += row_tmp[source][column] * static_cast<I64>(kIdctMatrix[row][source]);
                }

                sample = static_cast<I32>((sum + (1LL << ((kIdctScaleBits * 2) - 1))) >> (kIdctScaleBits * 2));
                sample = jpeg_clamp_to_byte(sample + 128);
                samples[row * 8U + column] = static_cast<U8>(sample);
            }
        }
    }

    static Status jpeg_decode_rgb_image(JpegDecoder* decoder, U8** rgb_out) {
        const U32 blocks_x = (decoder->width + 7U) / 8U;
        const U32 blocks_y = (decoder->height + 7U) / 8U;
        const U64 pixel_bytes = static_cast<U64>(decoder->width) * static_cast<U64>(decoder->height) * 3ULL;
        U8* rgb;
        JpegBitReader reader;
        U8 component_samples[3][64];
        I32 coefficients[64];

        if ((decoder == nullptr) || (rgb_out == nullptr)) {
            return StatusInvalidArgument;
        }
        if (pixel_bytes == 0ULL || pixel_bytes > 0xFFFFFFFFULL) {
            return StatusNoSpace;
        }

        rgb = static_cast<U8*>(user_shared_heap_malloc(static_cast<size_t>(pixel_bytes)));
        if (rgb == nullptr) {
            return StatusNoMemory;
        }

        jpeg_bit_reader_init(&reader, decoder->data + decoder->entropy_offset, decoder->entropy_size);
        for (U32 block_y = 0U; block_y < blocks_y; ++block_y) {
            for (U32 block_x = 0U; block_x < blocks_x; ++block_x) {
                for (U32 component_index = 0U; component_index < decoder->component_count; ++component_index) {
                    JpegComponent* component = &decoder->components[component_index];
                    Status status = jpeg_decode_block(
                        &reader,
                        &decoder->dc_tables[component->dc_table],
                        &decoder->ac_tables[component->ac_table],
                        decoder->quant_tables[component->quant_table],
                        &component->dc_predictor,
                        coefficients);

                    if (status != StatusOK) {
                        user_shared_heap_free(rgb);
                        return status;
                    }

                    jpeg_inverse_dct(coefficients, component_samples[component_index]);
                }

                for (U32 local_y = 0U; local_y < 8U; ++local_y) {
                    const U32 source_y = (block_y * 8U) + local_y;

                    if (source_y >= decoder->height) {
                        break;
                    }

                    for (U32 local_x = 0U; local_x < 8U; ++local_x) {
                        const U32 source_x = (block_x * 8U) + local_x;
                        const U32 pixel_index = (local_y * 8U) + local_x;
                        U8* pixel;
                        I32 y_sample;
                        I32 cb_sample;
                        I32 cr_sample;
                        I32 red;
                        I32 green;
                        I32 blue;

                        if (source_x >= decoder->width) {
                            break;
                        }

                        y_sample = component_samples[0][pixel_index];
                        cb_sample = static_cast<I32>(component_samples[1][pixel_index]) - 128;
                        cr_sample = static_cast<I32>(component_samples[2][pixel_index]) - 128;
                        red = y_sample + ((91881 * cr_sample + (1 << (kYcbcrScaleBits - 1))) >> kYcbcrScaleBits);
                        green = y_sample - ((22554 * cb_sample + 46802 * cr_sample + (1 << (kYcbcrScaleBits - 1))) >> kYcbcrScaleBits);
                        blue = y_sample + ((116130 * cb_sample + (1 << (kYcbcrScaleBits - 1))) >> kYcbcrScaleBits);

                        pixel = rgb + (((source_y * decoder->width) + source_x) * 3U);
                        pixel[0] = static_cast<U8>(jpeg_clamp_to_byte(red));
                        pixel[1] = static_cast<U8>(jpeg_clamp_to_byte(green));
                        pixel[2] = static_cast<U8>(jpeg_clamp_to_byte(blue));
                    }
                }
            }
        }

        *rgb_out = rgb;
        return StatusOK;
    }

    /*
     * Render one decoded RGB image with a centered cover fit.
     *
     * GWES desktop wallpapers should preserve the source aspect ratio and fill
     * the whole target surface. The old path letterboxed images; the new path
     * crops whichever source dimension exceeds the target aspect ratio, then
     * scales that centered crop over the full destination.
     *
     * @param rgb Decoded RGB888 source pixels.
     * @param source_width Source image width in pixels.
     * @param source_height Source image height in pixels.
     * @param target Destination surface.
     * @param background_color Fallback color for invalid or empty inputs.
     * @return Nothing.
     */
    static void jpeg_render_rgb_to_target(
        const U8* rgb,
        U32 source_width,
        U32 source_height,
        const JpegRenderTarget* target,
        U32 background_color) {
        U32 crop_width;
        U32 crop_height;
        U32 crop_x;
        U32 crop_y;

        jpeg_fill_target(target, background_color);
        if ((rgb == nullptr) || (source_width == 0U) || (source_height == 0U) || (target->width == 0U) || (target->height == 0U)) {
            return;
        }

        if ((static_cast<U64>(source_width) * static_cast<U64>(target->height))
            >= (static_cast<U64>(source_height) * static_cast<U64>(target->width))) {
            crop_height = source_height;
            crop_width = static_cast<U32>((static_cast<U64>(source_height) * static_cast<U64>(target->width)) / static_cast<U64>(target->height));
            if (crop_width == 0U) {
                crop_width = 1U;
            }
            if (crop_width > source_width) {
                crop_width = source_width;
            }
            crop_x = (source_width - crop_width) / 2U;
            crop_y = 0U;
        }
        else {
            crop_width = source_width;
            crop_height = static_cast<U32>((static_cast<U64>(source_width) * static_cast<U64>(target->height)) / static_cast<U64>(target->width));
            if (crop_height == 0U) {
                crop_height = 1U;
            }
            if (crop_height > source_height) {
                crop_height = source_height;
            }
            crop_x = 0U;
            crop_y = (source_height - crop_height) / 2U;
        }

        for (U32 row = 0U; row < target->height; ++row) {
            const U32 source_y = crop_y + static_cast<U32>((static_cast<U64>(row) * static_cast<U64>(crop_height)) / static_cast<U64>(target->height));
            U32* destination_row = reinterpret_cast<U32*>(static_cast<U8*>(target->pixels) + (row * target->pitch));

            for (U32 column = 0U; column < target->width; ++column) {
                const U32 source_x = crop_x + static_cast<U32>((static_cast<U64>(column) * static_cast<U64>(crop_width)) / static_cast<U64>(target->width));
                const U8* source_pixel = rgb + (((source_y * source_width) + source_x) * 3U);
                const U32 color = (static_cast<U32>(source_pixel[0]) << 16)
                    | (static_cast<U32>(source_pixel[1]) << 8)
                    | static_cast<U32>(source_pixel[2]);

                destination_row[column] = jpeg_encode_surface_color(target->pixel_format, color);
            }
        }
    }

} // namespace

extern "C" long jpeg_render_to_surface(const U8* jpeg_bytes, U32 jpeg_size, const JpegRenderTarget* target, U32 background_color) {
    JpegDecoder decoder = {};
    U8* rgb = nullptr;
    Status status;

    if ((jpeg_bytes == nullptr) || (target == nullptr) || (target->pixels == nullptr) || (target->pitch < (target->width * sizeof(U32)))) {
        return StatusInvalidArgument;
    }

    status = jpeg_parse_stream(&decoder, jpeg_bytes, jpeg_size);
    if (status != StatusOK) {
        return status;
    }

    for (U32 index = 0U; index < decoder.component_count; ++index) {
        const JpegComponent* component = &decoder.components[index];

        if (!decoder.quant_valid[component->quant_table]
            || (component->dc_table >= 4U)
            || (component->ac_table >= 4U)
            || !decoder.dc_tables[component->dc_table].valid
            || !decoder.ac_tables[component->ac_table].valid) {
            return StatusNotSupported;
        }
    }

    status = jpeg_decode_rgb_image(&decoder, &rgb);
    if (status != StatusOK) {
        return status;
    }

    jpeg_render_rgb_to_target(rgb, decoder.width, decoder.height, target, background_color);
    user_shared_heap_free(rgb);
    return StatusOK;
}
