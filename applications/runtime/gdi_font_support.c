#include "user_runtime.h"
#include "app/gdi.h"
#include "app/mini_font.h"

#ifndef GDI_STATUS_INVALID_ARGUMENT
#define GDI_STATUS_INVALID_ARGUMENT (-1L)
#endif

#ifndef GDI_STATUS_ERROR
#define GDI_STATUS_ERROR (-4L)
#endif

typedef struct GdiFontHandleStruct GdiFontHandle;

/*
 * These helpers are defined in `gdi_support.c` and are shared through source
 * inclusion so the font renderer can stay in the same DLL image without adding
 * another private header.
 */
static void gdi_zero_memory(void* destination, unsigned long size);
static unsigned long gdi_encode_color(unsigned long pixel_format, unsigned long color);
static long gdi_font_scale_metric_signed(long value, long scale_16);
static long gdi_font_load_single_path(const char* path, unsigned long pixel_height, RosGdiFont* font);
static long gdi_font_load_raster_file_static(const char* path, unsigned long pixel_height, RosGdiFont* font);
static long gdi_font_load_raster_file_shared(const char* path, unsigned long pixel_height, RosGdiFont* font);
static long gdi_font_load_ttf_stream(const char* path, unsigned long pixel_height, RosGdiFont* font);
static long gdi_font_parse_raster_file(GdiFontHandle* handle, const unsigned char* buffer, unsigned long file_size, unsigned long pixel_height);
static void gdi_font_apply_public_metrics(RosGdiFont* font, GdiFontHandle* handle);
static long gdi_font_read_file_exact(const char* path, unsigned long offset, unsigned char* buffer, unsigned long size);
static int gdi_font_buffer_has_prefix(const unsigned char* buffer, unsigned long buffer_size, const char* prefix, unsigned long prefix_size);
static unsigned long gdi_font_le_u32(const unsigned char* data);
static long gdi_font_le_s32(const unsigned char* data);
static long gdi_font_query_file_size(const char* path, unsigned long* size);

#define GDI_FONT_USER_SYS_MALLOC 1UL
#define GDI_FONT_USER_SYS_FREE 2UL

/*
 * Allocate process-local heap memory without depending on ros_support symbols.
 *
 * GDI is built as a thin wrapper DLL, so leaving hosted `malloc` unresolved
 * causes the builder to synthesize a fake `kernel` import that the loader later
 * fails to resolve. These helpers keep the font runtime self-contained.
 *
 * @param size Requested byte count.
 * @return Allocated buffer, or null on failure.
 */
static void* gdi_font_heap_alloc(unsigned long size) {
    typedef struct GdiFontAllocationHeader {
        unsigned long payload_bytes;
    } GdiFontAllocationHeader;
    const unsigned long payload_bytes = size ? size : 1UL;
    const unsigned long total_bytes = payload_bytes + sizeof(GdiFontAllocationHeader);
    unsigned long address = invokeSyscall1(GDI_FONT_USER_SYS_MALLOC, total_bytes);
    GdiFontAllocationHeader* header;

    if ((long)address < 0L || address == 0UL) {
        return 0;
    }

    header = (GdiFontAllocationHeader*)address;
    header->payload_bytes = payload_bytes;
    return (void*)(header + 1);
}

/*
 * Release process-local heap memory previously returned by the font runtime.
 *
 * @param ptr Buffer to release.
 * @return Nothing.
 */
static void gdi_font_heap_free(void* ptr) {
    typedef struct GdiFontAllocationHeader {
        unsigned long payload_bytes;
    } GdiFontAllocationHeader;
    GdiFontAllocationHeader* header;

    if (!ptr) {
        return;
    }

    if ((long)(unsigned long)ptr < 0L) {
        return;
    }

    header = ((GdiFontAllocationHeader*)ptr) - 1;
    (void)invokeSyscall1(GDI_FONT_USER_SYS_FREE, (unsigned long)header);
}

/*
 * Resize one process-local heap allocation owned by the font runtime.
 *
 * @param ptr Existing payload pointer, or null.
 * @param size New requested payload size.
 * @return Resized payload pointer, or null on failure.
 */
static void* gdi_font_heap_realloc(void* ptr, unsigned long size) {
    typedef struct GdiFontAllocationHeader {
        unsigned long payload_bytes;
    } GdiFontAllocationHeader;
    GdiFontAllocationHeader* header;
    unsigned long copy_bytes;
    unsigned char* source;
    unsigned char* destination;
    void* replacement;
    unsigned long index;

    if (!ptr) {
        return gdi_font_heap_alloc(size);
    }
    if (size == 0UL) {
        gdi_font_heap_free(ptr);
        return 0;
    }

    header = ((GdiFontAllocationHeader*)ptr) - 1;
    copy_bytes = (header->payload_bytes < size) ? header->payload_bytes : size;
    replacement = gdi_font_heap_alloc(size);
    if (!replacement) {
        return 0;
    }

    source = (unsigned char*)ptr;
    destination = (unsigned char*)replacement;
    for (index = 0UL; index < copy_bytes; ++index) {
        destination[index] = source[index];
    }

    gdi_font_heap_free(ptr);
    return replacement;
}

#define malloc(size) gdi_font_heap_alloc((unsigned long)(size))
#define free(ptr) gdi_font_heap_free((ptr))
#define realloc(ptr, size) gdi_font_heap_realloc((ptr), (unsigned long)(size))

#define GDI_FONT_STATUS_IO (-5L)
#define GDI_FONT_STATUS_FORMAT (-6L)
#define GDI_FONT_STATUS_NOT_SUPPORTED (-7L)

#define GDI_FONT_FILE_CHUNK 256UL
#define GDI_FONT_DESCRIPTOR_LINE_MAX 4096UL
#define GDI_FONT_SAMPLE_GRID_DEFAULT 4UL
#define GDI_FONT_ASCII_CACHE_SIZE 128UL
#define GDI_FONT_PIXEL_HEIGHT_DEFAULT 14UL

#define GDI_RASTER_FONT_MAGIC "ROSRTF1"
#define GDI_RASTER_FONT_MAGIC_SIZE 7UL
#define GDI_RASTER_FONT_VERSION 1UL
#define GDI_RASTER_FONT_HEADER_SIZE 108UL
#define GDI_RASTER_FONT_GLYPH_ENTRY_SIZE 32UL
#define GDI_RASTER_FONT_FAMILY_BYTES 64UL

#define GDI_RASTER_FONT_HEADER_VERSION_OFFSET 8UL
#define GDI_RASTER_FONT_HEADER_SIZE_OFFSET 12UL
#define GDI_RASTER_FONT_HEADER_PIXEL_HEIGHT_OFFSET 16UL
#define GDI_RASTER_FONT_HEADER_LINE_HEIGHT_OFFSET 20UL
#define GDI_RASTER_FONT_HEADER_ASCENT_OFFSET 24UL
#define GDI_RASTER_FONT_HEADER_DESCENT_OFFSET 28UL
#define GDI_RASTER_FONT_HEADER_GLYPH_COUNT_OFFSET 32UL
#define GDI_RASTER_FONT_HEADER_GLYPH_TABLE_OFFSET 36UL
#define GDI_RASTER_FONT_HEADER_GLYPH_ENTRY_SIZE_OFFSET 40UL
#define GDI_RASTER_FONT_HEADER_FAMILY_OFFSET 44UL

#define GDI_RASTER_FONT_ENTRY_COVERAGE_OFFSET 0UL
#define GDI_RASTER_FONT_ENTRY_COVERAGE_SIZE_OFFSET 4UL
#define GDI_RASTER_FONT_ENTRY_ADVANCE_OFFSET 8UL
#define GDI_RASTER_FONT_ENTRY_BITMAP_LEFT_OFFSET 12UL
#define GDI_RASTER_FONT_ENTRY_BITMAP_TOP_OFFSET 16UL
#define GDI_RASTER_FONT_ENTRY_WIDTH_OFFSET 20UL
#define GDI_RASTER_FONT_ENTRY_HEIGHT_OFFSET 24UL

#define GDI_FONT_KIND_MINI 1UL
#define GDI_FONT_KIND_TRUETYPE 2UL
#define GDI_FONT_KIND_BITMAP 3UL

#define GDI_FONT_STORAGE_OWNS_SELF 0x01UL
#define GDI_FONT_STORAGE_OWNS_FILE_BUFFER 0x02UL
#define GDI_FONT_STORAGE_OWNS_GLYPHS 0x04UL
#define GDI_FONT_STORAGE_SHARED_CACHE_HANDLE 0x08UL

#define GDI_FONT_DESCRIPTOR_SOURCE_UNKNOWN 0UL
#define GDI_FONT_DESCRIPTOR_SOURCE_MINI 1UL
#define GDI_FONT_DESCRIPTOR_SOURCE_BITMAP 2UL

#define GDI_FONT_BITMAP_FIELD_ADVANCE 0x01UL
#define GDI_FONT_BITMAP_FIELD_LEFT 0x02UL
#define GDI_FONT_BITMAP_FIELD_TOP 0x04UL
#define GDI_FONT_BITMAP_FIELD_WIDTH 0x08UL
#define GDI_FONT_BITMAP_FIELD_HEIGHT 0x10UL
#define GDI_FONT_STATIC_RASTER_SLOT_COUNT 4UL
#define GDI_FONT_SHARED_RASTER_CACHE_ENTRY_COUNT 8UL
#define GDI_FONT_STATIC_RASTER_MAX_FILE_SIZE 65536UL
#define GDI_FONT_SHARED_RASTER_REGION_BYTES (2UL * 1024UL * 1024UL)
#define GDI_FONT_ENABLE_SHARED_RASTER_CACHE 0UL
#define GDI_FONT_SHARED_RASTER_OBJECT_MAGIC 0x31524647UL
#define GDI_FONT_SHARED_RASTER_OBJECT_VERSION 1UL
#define GDI_FONT_SHARED_RASTER_STATE_EMPTY 0UL
#define GDI_FONT_SHARED_RASTER_STATE_INITIALIZING 1UL
#define GDI_FONT_SHARED_RASTER_STATE_READY 2UL
#define GDI_FONT_SHARED_RASTER_STATE_FAILED 3UL
#define GDI_FONT_SHARED_RASTER_NAME_BYTES 64UL
#define GDI_FONT_SHARED_RASTER_PATH_BYTES 128UL
#define GDI_FONT_BITMAP_FIELD_COVERAGE 0x20UL
#define GDI_FONT_BITMAP_FIELD_ALL \
    (GDI_FONT_BITMAP_FIELD_ADVANCE | GDI_FONT_BITMAP_FIELD_LEFT | GDI_FONT_BITMAP_FIELD_TOP | GDI_FONT_BITMAP_FIELD_WIDTH | GDI_FONT_BITMAP_FIELD_HEIGHT | GDI_FONT_BITMAP_FIELD_COVERAGE)

#define GDI_FONT_TTF_TAG(a, b, c, d) \
    ((((unsigned long)(a) & 0xFFUL) << 24) | (((unsigned long)(b) & 0xFFUL) << 16) | (((unsigned long)(c) & 0xFFUL) << 8) | ((unsigned long)(d) & 0xFFUL))

#define GDI_TTF_FLAG_ON_CURVE 0x01U
#define GDI_TTF_FLAG_X_SHORT 0x02U
#define GDI_TTF_FLAG_Y_SHORT 0x04U
#define GDI_TTF_FLAG_REPEAT 0x08U
#define GDI_TTF_FLAG_X_SAME 0x10U
#define GDI_TTF_FLAG_Y_SAME 0x20U

#define GDI_FONT_DEFAULT_RASTER_PATH_12 "C:\\fonts\\tahoma-12.rtf"
#define GDI_FONT_DEFAULT_RASTER_PATH_14 "C:\\fonts\\tahoma-14.rtf"
#define GDI_FONT_DEFAULT_RASTER_PATH_16 "C:\\fonts\\tahoma-16.rtf"
#define GDI_FONT_DEFAULT_RASTER_PATH_18 "C:\\fonts\\tahoma-18.rtf"
#define GDI_FONT_DEFAULT_SYSTEM_UI_RASTER_PATH "C:\\fonts\\system_ui.rtf"
#define GDI_FONT_DEFAULT_FALLBACK_PATH "C:\\fonts\\system_ui.font"

typedef struct GdiGlyphBitmapStruct {
    int ready;
    int valid;
    int coverage_owned;
    long bitmap_left;
    long bitmap_top;
    unsigned long width;
    unsigned long height;
    unsigned long advance;
    unsigned char* coverage;
} GdiGlyphBitmap;

typedef struct GdiMiniFontDescriptorStruct {
    char family[64];
    unsigned long glyph_width;
    unsigned long glyph_height;
    unsigned long advance;
    unsigned long line_height;
    unsigned long sample_grid;
    long ascent;
    long descent;
} GdiMiniFontDescriptor;

typedef struct GdiBitmapFontDescriptorStruct {
    char family[64];
} GdiBitmapFontDescriptor;

typedef struct GdiTtfFontStateStruct {
    const unsigned char* data;
    unsigned long size;
    const unsigned char* cmap;
    unsigned long cmap_size;
    unsigned short cmap_format;
    const unsigned char* head;
    const unsigned char* hhea;
    const unsigned char* hmtx;
    const unsigned char* loca;
    unsigned long loca_size;
    const unsigned char* glyf;
    unsigned long glyf_file_offset;
    unsigned long glyf_size;
    unsigned short units_per_em;
    unsigned short num_glyphs;
    unsigned short num_hmetrics;
    short ascent_units;
    short descent_units;
    short line_gap_units;
    short index_to_loc_format;
    long scale_16;
    char source_path[128];
} GdiTtfFontState;

typedef struct GdiFontHandleStruct {
    unsigned long kind;
    unsigned long pixel_height;
    unsigned long line_height;
    long ascent;
    long descent;
    long line_gap;
    void* file_buffer;
    unsigned long file_size;
    unsigned long storage_flags;
    unsigned long static_slot_index;
    void* shared_cache_entry;
    union {
        GdiBitmapFontDescriptor bitmap;
        GdiMiniFontDescriptor mini;
        GdiTtfFontState ttf;
    } data;
    GdiGlyphBitmap* glyphs;
} GdiFontHandle;

typedef struct GdiStaticRasterSlotStruct {
    int in_use;
    GdiFontHandle handle;
    GdiGlyphBitmap glyphs[GDI_FONT_ASCII_CACHE_SIZE];
    unsigned char file_buffer[GDI_FONT_STATIC_RASTER_MAX_FILE_SIZE + 1UL];
} GdiStaticRasterSlot;

typedef struct GdiSharedRasterGlyphStruct {
    unsigned long coverage_offset;
    unsigned long coverage_size;
    unsigned long advance;
    long bitmap_left;
    long bitmap_top;
    unsigned long width;
    unsigned long height;
    unsigned long valid;
} GdiSharedRasterGlyph;

typedef struct GdiSharedRasterFontObjectStruct {
    unsigned long magic;
    unsigned long version;
    unsigned long state;
    long status;
    unsigned long pixel_height;
    unsigned long line_height;
    long ascent;
    long descent;
    long line_gap;
    unsigned long file_size;
    char family[GDI_RASTER_FONT_FAMILY_BYTES + 1UL];
    char source_path[GDI_FONT_SHARED_RASTER_PATH_BYTES];
    GdiSharedRasterGlyph glyphs[GDI_FONT_ASCII_CACHE_SIZE];
    unsigned char file_bytes[1];
} GdiSharedRasterFontObject;

typedef struct GdiSharedRasterCacheEntryStruct {
    int in_use;
    unsigned long reference_count;
    char path[GDI_FONT_SHARED_RASTER_PATH_BYTES];
    char object_name[GDI_FONT_SHARED_RASTER_NAME_BYTES];
    GdiSharedRasterFontObject* object;
    GdiFontHandle handle;
    GdiGlyphBitmap glyphs[GDI_FONT_ASCII_CACHE_SIZE];
} GdiSharedRasterCacheEntry;

#define GDI_SHARED_RASTER_OBJECT_HEADER_BYTES ((unsigned long)(sizeof(GdiSharedRasterFontObject) - 1UL))

typedef struct GdiRasterFontAssetStruct {
    unsigned long pixel_height;
    const char* path;
} GdiRasterFontAsset;

typedef struct GdiPointFxStruct {
    long x;
    long y;
    int on_curve;
} GdiPointFx;

typedef struct GdiLineSegmentFxStruct {
    long x0;
    long y0;
    long x1;
    long y1;
} GdiLineSegmentFx;

typedef struct GdiTtfSegmentBuilderStruct {
    GdiLineSegmentFx* segments;
    unsigned long count;
    unsigned long capacity;
} GdiTtfSegmentBuilder;

static GdiFontHandle g_gdi_builtin_system_ui_font;
static GdiGlyphBitmap g_gdi_builtin_system_ui_glyphs[GDI_FONT_ASCII_CACHE_SIZE];
static GdiStaticRasterSlot g_gdi_static_raster_slots[GDI_FONT_STATIC_RASTER_SLOT_COUNT];
static GdiSharedRasterCacheEntry g_gdi_shared_raster_cache[GDI_FONT_SHARED_RASTER_CACHE_ENTRY_COUNT];
static const GdiRasterFontAsset g_gdi_default_raster_assets[] = {
    { 12UL, GDI_FONT_DEFAULT_RASTER_PATH_12 },
    { 14UL, GDI_FONT_DEFAULT_RASTER_PATH_14 },
    { 16UL, GDI_FONT_DEFAULT_RASTER_PATH_16 },
    { 18UL, GDI_FONT_DEFAULT_RASTER_PATH_18 },
};

/*
 * Copy one bounded C string without depending on hosted helpers.
 *
 * @param destination Output buffer.
 * @param destination_size Size of the output buffer in bytes.
 * @param source Optional source string.
 * @return Nothing.
 */
static void gdi_font_copy_text(char* destination, unsigned long destination_size, const char* source) {
    unsigned long index;

    if (!destination || destination_size == 0UL) {
        return;
    }

    if (!source) {
        destination[0] = '\0';
        return;
    }

    for (index = 0UL; index + 1UL < destination_size && source[index] != '\0'; ++index) {
        destination[index] = source[index];
    }
    destination[index] = '\0';
}

static GdiStaticRasterSlot* gdi_font_reserve_static_raster_slot(void) {
    unsigned long index;

    for (index = 0UL; index < GDI_FONT_STATIC_RASTER_SLOT_COUNT; ++index) {
        if (!g_gdi_static_raster_slots[index].in_use) {
            g_gdi_static_raster_slots[index].in_use = 1;
            gdi_zero_memory(&g_gdi_static_raster_slots[index].handle, sizeof(g_gdi_static_raster_slots[index].handle));
            gdi_zero_memory(g_gdi_static_raster_slots[index].glyphs, sizeof(g_gdi_static_raster_slots[index].glyphs));
            return &g_gdi_static_raster_slots[index];
        }
    }

    return 0;
}

static void gdi_font_release_static_raster_slot(unsigned long slot_index) {
    if (slot_index >= GDI_FONT_STATIC_RASTER_SLOT_COUNT) {
        return;
    }

    gdi_zero_memory(&g_gdi_static_raster_slots[slot_index].handle, sizeof(g_gdi_static_raster_slots[slot_index].handle));
    gdi_zero_memory(g_gdi_static_raster_slots[slot_index].glyphs, sizeof(g_gdi_static_raster_slots[slot_index].glyphs));
    g_gdi_static_raster_slots[slot_index].in_use = 0;
}

/*
 * Compare two ASCII strings for equality.
 *
 * @param left First string.
 * @param right Second string.
 * @return Non-zero when both strings contain the same bytes.
 */
static int gdi_font_text_equals(const char* left, const char* right) {
    unsigned long index = 0UL;

    if (left == right) {
        return 1;
    }
    if (!left || !right) {
        return 0;
    }

    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return 0;
        }
        ++index;
    }

    return left[index] == right[index];
}

/*
 * Emit one short debug line while bringing up file-backed font loading.
 *
 * The TTF path is still under active integration, so targeted stage markers
 * make it possible to distinguish file-read problems from parser rejections in
 * the headless widget demo probe without adding a dedicated debugger.
 *
 * @param text Null-terminated line to write to the process console.
 * @return Nothing.
 */
static void gdi_font_debug_line(const char* text) {
    if (!text) {
        return;
    }

    writeLine(text);
}

/*
 * Compare two ASCII strings ignoring case.
 *
 * @param left First string.
 * @param right Second string.
 * @return Non-zero when the strings match after ASCII folding.
 */
static int gdi_font_text_equals_ignore_case(const char* left, const char* right) {
    unsigned long index = 0UL;

    if (left == right) {
        return 1;
    }
    if (!left || !right) {
        return 0;
    }

    while (left[index] != '\0' && right[index] != '\0') {
        char left_ch = left[index];
        char right_ch = right[index];

        if (left_ch >= 'A' && left_ch <= 'Z') {
            left_ch = (char)(left_ch + ('a' - 'A'));
        }
        if (right_ch >= 'A' && right_ch <= 'Z') {
            right_ch = (char)(right_ch + ('a' - 'A'));
        }
        if (left_ch != right_ch) {
            return 0;
        }
        ++index;
    }

    return left[index] == right[index];
}

/*
 * Hash one font path case-insensitively into a compact shared-object key.
 *
 * @param text Null-terminated path text.
 * @return Stable FNV-1a hash of the path.
 */
static unsigned long gdi_font_hash_ignore_case(const char* text) {
    unsigned long hash = 2166136261UL;
    unsigned long index = 0UL;

    if (!text) {
        return hash;
    }

    while (text[index] != '\0') {
        unsigned char ch = (unsigned char)text[index];

        if (ch >= 'A' && ch <= 'Z') {
            ch = (unsigned char)(ch + ('a' - 'A'));
        }
        hash ^= (unsigned long)ch;
        hash *= 16777619UL;
        ++index;
    }

    return hash;
}

/*
 * Build the named shared-memory identifier for one raster font.
 *
 * @param destination Output buffer for the object name.
 * @param destination_size Size of the output buffer in bytes.
 * @param path Absolute font path.
 * @return Nothing.
 */
static void gdi_font_build_shared_raster_name(char* destination, unsigned long destination_size, const char* path) {
    static const char hex_digits[] = "0123456789abcdef";
    const char* prefix = "gdi-font-";
    unsigned long prefix_index = 0UL;
    unsigned long write_index = 0UL;
    unsigned long hash = gdi_font_hash_ignore_case(path);
    int shift;

    if (!destination || destination_size == 0UL) {
        return;
    }

    while (prefix[prefix_index] != '\0' && (write_index + 1UL) < destination_size) {
        destination[write_index++] = prefix[prefix_index++];
    }
    for (shift = (int)(sizeof(hash) * 8U) - 4; shift >= 0 && (write_index + 1UL) < destination_size; shift -= 4) {
        destination[write_index++] = hex_digits[(hash >> shift) & 0xFUL];
    }
    destination[write_index] = '\0';
}

/*
 * Return one existing process-local shared raster cache entry for a path.
 *
 * @param path Absolute font path.
 * @return Matching cache entry, or null when this process has not bound it yet.
 */
static GdiSharedRasterCacheEntry* gdi_font_find_shared_raster_cache_entry(const char* path) {
    unsigned long index;

    if (!path) {
        return 0;
    }

    for (index = 0UL; index < GDI_FONT_SHARED_RASTER_CACHE_ENTRY_COUNT; ++index) {
        if (g_gdi_shared_raster_cache[index].in_use && gdi_font_text_equals_ignore_case(g_gdi_shared_raster_cache[index].path, path)) {
            return &g_gdi_shared_raster_cache[index];
        }
    }

    return 0;
}

/*
 * Reserve one free process-local shared raster cache entry.
 *
 * @return Free cache entry, or null when the local cache is full.
 */
static GdiSharedRasterCacheEntry* gdi_font_reserve_shared_raster_cache_entry(void) {
    unsigned long index;

    for (index = 0UL; index < GDI_FONT_SHARED_RASTER_CACHE_ENTRY_COUNT; ++index) {
        if (!g_gdi_shared_raster_cache[index].in_use) {
            gdi_zero_memory(&g_gdi_shared_raster_cache[index], sizeof(g_gdi_shared_raster_cache[index]));
            g_gdi_shared_raster_cache[index].in_use = 1;
            return &g_gdi_shared_raster_cache[index];
        }
    }

    return 0;
}

/*
 * Parse one raster font directly into a shared immutable object.
 *
 * The first process populates the file bytes plus glyph metadata in named
 * shared memory. Later processes reuse the same object and avoid both VFS IO
 * and per-process heap-backed glyph parsing.
 *
 * @param object Shared-memory object to initialize.
 * @param path Absolute font path.
 * @param pixel_height Requested size for validation.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_font_populate_shared_raster_object(GdiSharedRasterFontObject* object, const char* path, unsigned long pixel_height) {
    unsigned long version;
    unsigned long header_size;
    unsigned long source_pixel_height;
    unsigned long line_height;
    long ascent;
    long descent;
    unsigned long glyph_count;
    unsigned long glyph_table_offset;
    unsigned long glyph_entry_size;
    unsigned long glyph_index;
    unsigned long file_size = 0UL;
    char family[GDI_RASTER_FONT_FAMILY_BYTES + 1UL];
    long status;

    if (!object || !path) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    object->magic = GDI_FONT_SHARED_RASTER_OBJECT_MAGIC;
    object->version = GDI_FONT_SHARED_RASTER_OBJECT_VERSION;
    object->state = GDI_FONT_SHARED_RASTER_STATE_INITIALIZING;
    object->status = GDI_STATUS_ERROR;
    object->file_size = 0UL;
    gdi_font_copy_text(object->source_path, sizeof(object->source_path), path);

    status = gdi_font_query_file_size(path, &file_size);
    if (status < 0L) {
        object->state = GDI_FONT_SHARED_RASTER_STATE_FAILED;
        object->status = status;
        return status;
    }
    if (file_size == 0UL || file_size > GDI_FONT_STATIC_RASTER_MAX_FILE_SIZE) {
        object->state = GDI_FONT_SHARED_RASTER_STATE_FAILED;
        object->status = ROS_USER_IPC_STATUS_NO_SPACE;
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }
    if ((GDI_SHARED_RASTER_OBJECT_HEADER_BYTES + file_size) > GDI_FONT_SHARED_RASTER_REGION_BYTES) {
        object->state = GDI_FONT_SHARED_RASTER_STATE_FAILED;
        object->status = ROS_USER_IPC_STATUS_NO_SPACE;
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    status = gdi_font_read_file_exact(path, 0UL, object->file_bytes, file_size);
    if (status < 0L) {
        object->state = GDI_FONT_SHARED_RASTER_STATE_FAILED;
        object->status = status;
        return status;
    }

    if (file_size < GDI_RASTER_FONT_HEADER_SIZE) {
        object->state = GDI_FONT_SHARED_RASTER_STATE_FAILED;
        object->status = GDI_FONT_STATUS_FORMAT;
        return GDI_FONT_STATUS_FORMAT;
    }
    if (!gdi_font_buffer_has_prefix(object->file_bytes, file_size, GDI_RASTER_FONT_MAGIC, GDI_RASTER_FONT_MAGIC_SIZE)) {
        object->state = GDI_FONT_SHARED_RASTER_STATE_FAILED;
        object->status = GDI_FONT_STATUS_FORMAT;
        return GDI_FONT_STATUS_FORMAT;
    }

    version = gdi_font_le_u32(object->file_bytes + GDI_RASTER_FONT_HEADER_VERSION_OFFSET);
    header_size = gdi_font_le_u32(object->file_bytes + GDI_RASTER_FONT_HEADER_SIZE_OFFSET);
    source_pixel_height = gdi_font_le_u32(object->file_bytes + GDI_RASTER_FONT_HEADER_PIXEL_HEIGHT_OFFSET);
    line_height = gdi_font_le_u32(object->file_bytes + GDI_RASTER_FONT_HEADER_LINE_HEIGHT_OFFSET);
    ascent = gdi_font_le_s32(object->file_bytes + GDI_RASTER_FONT_HEADER_ASCENT_OFFSET);
    descent = gdi_font_le_s32(object->file_bytes + GDI_RASTER_FONT_HEADER_DESCENT_OFFSET);
    glyph_count = gdi_font_le_u32(object->file_bytes + GDI_RASTER_FONT_HEADER_GLYPH_COUNT_OFFSET);
    glyph_table_offset = gdi_font_le_u32(object->file_bytes + GDI_RASTER_FONT_HEADER_GLYPH_TABLE_OFFSET);
    glyph_entry_size = gdi_font_le_u32(object->file_bytes + GDI_RASTER_FONT_HEADER_GLYPH_ENTRY_SIZE_OFFSET);

    if (version != GDI_RASTER_FONT_VERSION || header_size < GDI_RASTER_FONT_HEADER_SIZE || header_size > file_size || source_pixel_height == 0UL || line_height == 0UL || ascent <= 0L || descent < 0L) {
        object->state = GDI_FONT_SHARED_RASTER_STATE_FAILED;
        object->status = GDI_FONT_STATUS_FORMAT;
        return GDI_FONT_STATUS_FORMAT;
    }
    if (glyph_count != GDI_FONT_ASCII_CACHE_SIZE || glyph_entry_size < GDI_RASTER_FONT_GLYPH_ENTRY_SIZE || glyph_table_offset < header_size) {
        object->state = GDI_FONT_SHARED_RASTER_STATE_FAILED;
        object->status = GDI_FONT_STATUS_FORMAT;
        return GDI_FONT_STATUS_FORMAT;
    }
    if (glyph_table_offset > file_size || (glyph_count * glyph_entry_size) > (file_size - glyph_table_offset)) {
        object->state = GDI_FONT_SHARED_RASTER_STATE_FAILED;
        object->status = GDI_FONT_STATUS_FORMAT;
        return GDI_FONT_STATUS_FORMAT;
    }
    if (pixel_height != 0UL && pixel_height != source_pixel_height) {
        object->state = GDI_FONT_SHARED_RASTER_STATE_FAILED;
        object->status = GDI_FONT_STATUS_NOT_SUPPORTED;
        return GDI_FONT_STATUS_NOT_SUPPORTED;
    }

    gdi_zero_memory(family, sizeof(family));
    for (glyph_index = 0UL; glyph_index < GDI_RASTER_FONT_FAMILY_BYTES; ++glyph_index) {
        unsigned char ch = object->file_bytes[GDI_RASTER_FONT_HEADER_FAMILY_OFFSET + glyph_index];

        family[glyph_index] = (char)ch;
        if (ch == '\0') {
            break;
        }
    }

    object->pixel_height = source_pixel_height;
    object->line_height = line_height;
    object->ascent = ascent;
    object->descent = descent;
    object->line_gap = (long)line_height - ascent - descent;
    object->file_size = file_size;
    gdi_font_copy_text(object->family, sizeof(object->family), family[0] ? family : "Bitmap UI");
    for (glyph_index = 0UL; glyph_index < GDI_FONT_ASCII_CACHE_SIZE; ++glyph_index) {
        const unsigned char* entry = object->file_bytes + glyph_table_offset + (glyph_index * glyph_entry_size);
        unsigned long coverage_offset = gdi_font_le_u32(entry + GDI_RASTER_FONT_ENTRY_COVERAGE_OFFSET);
        unsigned long coverage_size = gdi_font_le_u32(entry + GDI_RASTER_FONT_ENTRY_COVERAGE_SIZE_OFFSET);
        unsigned long width = gdi_font_le_u32(entry + GDI_RASTER_FONT_ENTRY_WIDTH_OFFSET);
        unsigned long height = gdi_font_le_u32(entry + GDI_RASTER_FONT_ENTRY_HEIGHT_OFFSET);
        unsigned long expected_size = width * height;

        if (width != 0UL && expected_size / width != height) {
            object->state = GDI_FONT_SHARED_RASTER_STATE_FAILED;
            object->status = GDI_FONT_STATUS_FORMAT;
            return GDI_FONT_STATUS_FORMAT;
        }
        if (coverage_size != expected_size) {
            object->state = GDI_FONT_SHARED_RASTER_STATE_FAILED;
            object->status = GDI_FONT_STATUS_FORMAT;
            return GDI_FONT_STATUS_FORMAT;
        }
        if (coverage_size != 0UL && (coverage_offset > file_size || coverage_size > (file_size - coverage_offset))) {
            object->state = GDI_FONT_SHARED_RASTER_STATE_FAILED;
            object->status = GDI_FONT_STATUS_FORMAT;
            return GDI_FONT_STATUS_FORMAT;
        }

        object->glyphs[glyph_index].coverage_offset = coverage_offset;
        object->glyphs[glyph_index].coverage_size = coverage_size;
        object->glyphs[glyph_index].advance = gdi_font_le_u32(entry + GDI_RASTER_FONT_ENTRY_ADVANCE_OFFSET);
        object->glyphs[glyph_index].bitmap_left = gdi_font_le_s32(entry + GDI_RASTER_FONT_ENTRY_BITMAP_LEFT_OFFSET);
        object->glyphs[glyph_index].bitmap_top = gdi_font_le_s32(entry + GDI_RASTER_FONT_ENTRY_BITMAP_TOP_OFFSET);
        object->glyphs[glyph_index].width = width;
        object->glyphs[glyph_index].height = height;
        object->glyphs[glyph_index].valid = 1UL;
    }

    object->status = ROS_USER_IPC_STATUS_OK;
    object->state = GDI_FONT_SHARED_RASTER_STATE_READY;
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Bind one process-local handle view over an initialized shared raster object.
 *
 * @param entry Process-local cache entry to initialize.
 * @param object Shared raster-font object.
 * @param path Absolute font path.
 * @param object_name Shared-memory object identifier.
 * @return Zero on success, or a negative status code on validation failure.
 */
static long gdi_font_bind_shared_raster_cache_entry(
    GdiSharedRasterCacheEntry* entry,
    GdiSharedRasterFontObject* object,
    const char* path,
    const char* object_name) {
    unsigned long glyph_index;

    if (!entry || !object || !path || !object_name) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }
    if (object->state != GDI_FONT_SHARED_RASTER_STATE_READY || object->magic != GDI_FONT_SHARED_RASTER_OBJECT_MAGIC || object->version != GDI_FONT_SHARED_RASTER_OBJECT_VERSION) {
        return object->status < 0L ? object->status : GDI_STATUS_ERROR;
    }

    gdi_zero_memory(&entry->handle, sizeof(entry->handle));
    gdi_zero_memory(entry->glyphs, sizeof(entry->glyphs));
    gdi_font_copy_text(entry->path, sizeof(entry->path), path);
    gdi_font_copy_text(entry->object_name, sizeof(entry->object_name), object_name);
    entry->object = object;
    entry->handle.kind = GDI_FONT_KIND_BITMAP;
    entry->handle.pixel_height = object->pixel_height;
    entry->handle.line_height = object->line_height;
    entry->handle.ascent = object->ascent;
    entry->handle.descent = object->descent;
    entry->handle.line_gap = object->line_gap;
    entry->handle.file_buffer = object->file_bytes;
    entry->handle.file_size = object->file_size;
    entry->handle.storage_flags = GDI_FONT_STORAGE_SHARED_CACHE_HANDLE;
    entry->handle.static_slot_index = 0UL;
    entry->handle.shared_cache_entry = entry;
    entry->handle.glyphs = entry->glyphs;
    gdi_font_copy_text(entry->handle.data.bitmap.family, sizeof(entry->handle.data.bitmap.family), object->family);

    for (glyph_index = 0UL; glyph_index < GDI_FONT_ASCII_CACHE_SIZE; ++glyph_index) {
        entry->glyphs[glyph_index].ready = (int)object->glyphs[glyph_index].valid;
        entry->glyphs[glyph_index].valid = (int)object->glyphs[glyph_index].valid;
        entry->glyphs[glyph_index].coverage_owned = 0;
        entry->glyphs[glyph_index].coverage = object->glyphs[glyph_index].coverage_size != 0UL
            ? (unsigned char*)(object->file_bytes + object->glyphs[glyph_index].coverage_offset)
            : 0;
        entry->glyphs[glyph_index].advance = object->glyphs[glyph_index].advance;
        entry->glyphs[glyph_index].bitmap_left = object->glyphs[glyph_index].bitmap_left;
        entry->glyphs[glyph_index].bitmap_top = object->glyphs[glyph_index].bitmap_top;
        entry->glyphs[glyph_index].width = object->glyphs[glyph_index].width;
        entry->glyphs[glyph_index].height = object->glyphs[glyph_index].height;
    }

    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Load one raster font through the cross-process shared-font cache.
 *
 * @param path Absolute raster-font path.
 * @param pixel_height Requested rendered size.
 * @param font Receives the cached public wrapper on success.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_font_load_raster_file_shared(const char* path, unsigned long pixel_height, RosGdiFont* font) {
    GdiSharedRasterCacheEntry* entry;
    GdiSharedRasterFontObject* object;
    char object_name[GDI_FONT_SHARED_RASTER_NAME_BYTES];
    unsigned long file_size = 0UL;
    unsigned long object_bytes;
    void* object_view = 0;
    long status;

    if (!path || !font) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    entry = gdi_font_find_shared_raster_cache_entry(path);
    if (entry != 0) {
        object = entry->object;
        if (!object || object->state != GDI_FONT_SHARED_RASTER_STATE_READY) {
            return object && object->status < 0L ? object->status : GDI_STATUS_ERROR;
        }
        if (pixel_height != 0UL && pixel_height != object->pixel_height) {
            return GDI_FONT_STATUS_NOT_SUPPORTED;
        }

        entry->reference_count += 1UL;
        gdi_font_apply_public_metrics(font, &entry->handle);
        return ROS_USER_IPC_STATUS_OK;
    }

    gdi_font_build_shared_raster_name(object_name, sizeof(object_name), path);
    status = gdi_font_query_file_size(path, &file_size);
    if (status < 0L) {
        return status;
    }
    if (file_size == 0UL || file_size > GDI_FONT_STATIC_RASTER_MAX_FILE_SIZE) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    object_bytes = GDI_SHARED_RASTER_OBJECT_HEADER_BYTES + file_size;
    if (object_bytes > GDI_FONT_SHARED_RASTER_REGION_BYTES) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    status = acquireSharedMemoryRegion(object_name, object_bytes, &object_view);
    if (status < 0L || !object_view) {
        return status < 0L ? status : GDI_STATUS_ERROR;
    }

    object = (GdiSharedRasterFontObject*)object_view;
    if (object->magic != GDI_FONT_SHARED_RASTER_OBJECT_MAGIC || object->version != GDI_FONT_SHARED_RASTER_OBJECT_VERSION || object->state == GDI_FONT_SHARED_RASTER_STATE_EMPTY) {
        status = gdi_font_populate_shared_raster_object(object, path, pixel_height);
        if (status < 0L) {
            return status;
        }
    }
    if (object->state == GDI_FONT_SHARED_RASTER_STATE_INITIALIZING) {
        return GDI_STATUS_ERROR;
    }
    if (object->state != GDI_FONT_SHARED_RASTER_STATE_READY) {
        return object->status < 0L ? object->status : GDI_STATUS_ERROR;
    }
    if (!gdi_font_text_equals_ignore_case(object->source_path, path)) {
        return GDI_STATUS_ERROR;
    }
    if (pixel_height != 0UL && pixel_height != object->pixel_height) {
        return GDI_FONT_STATUS_NOT_SUPPORTED;
    }

    entry = gdi_font_reserve_shared_raster_cache_entry();
    if (!entry) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    status = gdi_font_bind_shared_raster_cache_entry(entry, object, path, object_name);
    if (status < 0L) {
        gdi_zero_memory(entry, sizeof(*entry));
        return status;
    }

    entry->reference_count = 1UL;
    gdi_font_apply_public_metrics(font, &entry->handle);
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Release one cached glyph coverage buffer.
 *
 * @param glyph Glyph cache entry to clear.
 * @return Nothing.
 */
static void gdi_font_destroy_glyph(GdiGlyphBitmap* glyph) {
    if (!glyph) {
        return;
    }

    if (glyph->coverage && glyph->coverage_owned) {
        free(glyph->coverage);
    }
    gdi_zero_memory(glyph, sizeof(*glyph));
}

/*
 * Release all glyph caches owned by one font handle.
 *
 * @param handle Loaded font handle.
 * @return Nothing.
 */
static void gdi_font_release_glyphs(GdiFontHandle* handle) {
    unsigned long index;

    if (!handle || !handle->glyphs) {
        return;
    }

    for (index = 0UL; index < GDI_FONT_ASCII_CACHE_SIZE; ++index) {
        gdi_font_destroy_glyph(&handle->glyphs[index]);
    }

    if ((handle->storage_flags & GDI_FONT_STORAGE_OWNS_GLYPHS) != 0UL) {
        free(handle->glyphs);
    }
    handle->glyphs = 0;
    handle->storage_flags &= ~GDI_FONT_STORAGE_OWNS_GLYPHS;
}

/*
 * Allocate the per-font ASCII glyph cache only when rendering first needs it.
 *
 * Loading a large TTF already consumes a substantial heap buffer, so delaying
 * the fixed-size glyph cache keeps font initialization below the userspace heap
 * ceiling while preserving the existing cached-rendering behavior afterward.
 *
 * @param handle Loaded font handle that owns the cache.
 * @return Zero on success, or `ROS_USER_IPC_STATUS_NO_SPACE` on failure.
 */
static long gdi_font_ensure_glyph_cache(GdiFontHandle* handle) {
    unsigned long bytes;

    if (!handle) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }
    if (handle->glyphs) {
        return ROS_USER_IPC_STATUS_OK;
    }

    bytes = sizeof(GdiGlyphBitmap) * GDI_FONT_ASCII_CACHE_SIZE;
    handle->glyphs = (GdiGlyphBitmap*)malloc(bytes);
    if (!handle->glyphs) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    gdi_zero_memory(handle->glyphs, bytes);
    handle->storage_flags |= GDI_FONT_STORAGE_OWNS_GLYPHS;
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Destroy one loaded font handle and all data owned by it.
 *
 * @param handle Font handle returned by the loader.
 * @return Nothing.
 */
static void gdi_font_destroy_handle(GdiFontHandle* handle) {
    GdiSharedRasterCacheEntry* shared_cache;
    unsigned long slot_index;
    unsigned long storage_flags;

    if (!handle) {
        return;
    }

    shared_cache = (GdiSharedRasterCacheEntry*)handle->shared_cache_entry;
    if (shared_cache != 0) {
        if (shared_cache->reference_count != 0UL) {
            shared_cache->reference_count -= 1UL;
        }
        return;
    }

    slot_index = handle->static_slot_index;
    storage_flags = handle->storage_flags;
    gdi_font_release_glyphs(handle);
    if (handle->file_buffer && (storage_flags & GDI_FONT_STORAGE_OWNS_FILE_BUFFER) != 0UL) {
        free(handle->file_buffer);
    }
    gdi_zero_memory(handle, sizeof(*handle));
    if (slot_index != 0UL) {
        gdi_font_release_static_raster_slot(slot_index - 1UL);
        return;
    }
    if ((storage_flags & GDI_FONT_STORAGE_OWNS_SELF) != 0UL) {
        free(handle);
    }
}

/*
 * Clear one public font wrapper structure.
 *
 * @param font Public GDI font wrapper.
 * @return Nothing.
 */
static void gdi_font_clear_public(RosGdiFont* font) {
    if (!font) {
        return;
    }

    gdi_zero_memory(font, sizeof(*font));
}

/*
 * Publish private handle metrics into the public font wrapper.
 *
 * @param font Public wrapper returned to callers.
 * @param handle Private loaded font handle.
 * @return Nothing.
 */
static void gdi_font_apply_public_metrics(RosGdiFont* font, GdiFontHandle* handle) {
    if (!font || !handle) {
        return;
    }

    font->pixel_height = handle->pixel_height;
    font->line_height = handle->line_height;
    font->ascent = handle->ascent;
    font->descent = handle->descent;
    font->line_gap = handle->line_gap;
    font->handle = handle;
}

/*
 * Populate the built-in fallback glyph cache from the shared raster font table.
 *
 * The compiled fallback font is fixed at the 12 px descriptor embedded in
 * `mini_font.h`, so GDI can publish a ready-to-use bitmap font without any
 * heap-backed rasterization when the staged font assets cannot be read.
 *
 * @param glyphs Static glyph cache owned by the built-in fallback handle.
 * @return Nothing.
 */
static void gdi_font_seed_builtin_system_ui_glyphs(GdiGlyphBitmap* glyphs) {
    unsigned long index;

    if (!glyphs) {
        return;
    }

    gdi_zero_memory(glyphs, sizeof(g_gdi_builtin_system_ui_glyphs));
    for (index = 0UL; index < GDI_FONT_ASCII_CACHE_SIZE; ++index) {
        const RosMiniFontGlyph* glyph = rosMiniFontGlyph(index);

        glyphs[index].ready = 1;
        glyphs[index].valid = 1;
        glyphs[index].coverage_owned = 0;
        glyphs[index].bitmap_left = glyph->bitmap_left;
        glyphs[index].bitmap_top = glyph->bitmap_top;
        glyphs[index].width = glyph->width;
        glyphs[index].height = glyph->height;
        glyphs[index].advance = glyph->advance;
        glyphs[index].coverage = (unsigned char*)rosMiniFontGlyphCoverage(glyph);
    }
}

/*
 * Read one whole VFS file into a process-owned heap buffer.
 *
 * @param path Absolute VFS path.
 * @param data Receives the allocated buffer.
 * @param size Receives the byte count loaded from disk.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_font_read_file_all(const char* path, unsigned char** data, unsigned long* size) {
    char path_copy[128];
    unsigned char* buffer;
    unsigned long known_size = 0UL;
    unsigned long capacity = GDI_FONT_FILE_CHUNK;
    unsigned long total = 0UL;

    if (!path || !data || !size) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    /*
     * Syscalls are most reliable when path pointers live in the caller's local
     * stack or heap rather than inside a loaded DLL image, so normalize the
     * path into process-local storage before the first read.
     */
    gdi_font_copy_text(path_copy, sizeof(path_copy), path);
    path = path_copy;

    if (gdi_font_query_file_size(path, &known_size) >= 0L && known_size != 0UL) {
        capacity = known_size;
    }

    buffer = (unsigned char*)malloc(capacity + 1UL);
    if (!buffer) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    for (;;) {
        long read;

        /*
         * Treat the enumerated file size only as an allocation hint and keep
         * reading until the kernel reports EOF. The file syscall does not
         * promise that one request is satisfied in full, so a short read is
         * still forward progress rather than a reliable end-of-file marker.
         * Continuing until `readFile` returns zero keeps long `.font`
         * descriptors intact even when the VFS surfaces partial reads.
         */
        if ((capacity - total) < GDI_FONT_FILE_CHUNK) {
            unsigned long new_capacity = capacity * 2UL;
            unsigned char* replacement = (unsigned char*)realloc(buffer, new_capacity + 1UL);

            if (!replacement) {
                free(buffer);
                return ROS_USER_IPC_STATUS_NO_SPACE;
            }

            buffer = replacement;
            capacity = new_capacity;
        }

        read = readFile(path, total, (char*)(buffer + total), GDI_FONT_FILE_CHUNK);
        if (read < 0) {
            free(buffer);
            return read;
        }
        if (read == 0L) {
            break;
        }

        total += (unsigned long)read;
    }

    buffer[total] = 0U;
    *data = buffer;
    *size = total;
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Read one exact byte range from a VFS file into caller-owned storage.
 *
 * @param path Absolute VFS path.
 * @param offset Starting byte offset inside the file.
 * @param buffer Destination byte buffer.
 * @param size Exact byte count required.
 * @return Zero on success, or a negative status code on error or premature EOF.
 */
static long gdi_font_read_file_exact(const char* path, unsigned long offset, unsigned char* buffer, unsigned long size) {
    char path_copy[128];
    unsigned long total = 0UL;

    if (!path || (!buffer && size != 0UL)) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }
    if (size == 0UL) {
        return ROS_USER_IPC_STATUS_OK;
    }

    gdi_font_copy_text(path_copy, sizeof(path_copy), path);
    path = path_copy;

    while (total < size) {
        unsigned long request = size - total;
        long read;

        if (request > GDI_FONT_FILE_CHUNK) {
            request = GDI_FONT_FILE_CHUNK;
        }

        read = readFile(path, offset + total, (char*)(buffer + total), request);
        if (read < 0L) {
            return read;
        }
        if (read == 0L) {
            return GDI_FONT_STATUS_IO;
        }

        total += (unsigned long)read;
    }

    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Return the ASCII lowercase variant of one byte.
 *
 * @param ch ASCII byte to fold.
 * @return Lowercase byte when alphabetic, otherwise the input byte.
 */
static char gdi_font_lower_ascii(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch + ('a' - 'A'));
    }

    return ch;
}

/*
 * Query one VFS file size through directory enumeration so large fonts can be
 * allocated once at their final size instead of through repeated growth copies.
 *
 * @param path Absolute VFS file path.
 * @param size Receives the discovered byte size.
 * @return Zero on success, `ROS_USER_IPC_STATUS_NOT_FOUND` when absent, or a negative syscall status code.
 */
static long gdi_font_query_file_size(const char* path, unsigned long* size) {
    char directory[128];
    char file_name[128];
    const char* separator = 0;
    unsigned long index;
    UserDirectoryEntry entry;

    if (!path || !size) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0UL; path[index] != '\0'; ++index) {
        if (path[index] == '\\' || path[index] == '/') {
            separator = path + index;
        }
    }
    if (!separator || separator[1] == '\0') {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    gdi_font_copy_text(file_name, sizeof(file_name), separator + 1);
    if (file_name[0] == '\0') {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    if (separator == (path + 2) && path[1] == ':' && (path[2] == '\\' || path[2] == '/')) {
        directory[0] = path[0];
        directory[1] = path[1];
        directory[2] = path[2];
        directory[3] = '\0';
    }
    else {
        unsigned long directory_length = (unsigned long)(separator - path);

        if (directory_length == 0UL || directory_length >= sizeof(directory)) {
            return GDI_STATUS_INVALID_ARGUMENT;
        }

        for (index = 0UL; index < directory_length; ++index) {
            directory[index] = path[index];
        }
        directory[directory_length] = '\0';
    }

    for (index = 0UL;; ++index) {
        long status = readDirectoryEntry(directory, index, &entry);

        if (status < 0L) {
            return status;
        }
        if (status == 0L) {
            return ROS_USER_IPC_STATUS_NOT_FOUND;
        }
        if (gdi_font_text_equals_ignore_case(entry.name, file_name)) {
            *size = entry.size;
            return ROS_USER_IPC_STATUS_OK;
        }
    }
}

/*
 * Check whether one path ends with the requested suffix ignoring ASCII case.
 *
 * @param path Source path.
 * @param suffix Expected trailing suffix.
 * @return Non-zero when the suffix matches.
 */
static int gdi_font_has_extension(const char* path, const char* suffix) {
    unsigned long path_length = 0UL;
    unsigned long suffix_length = 0UL;
    unsigned long index;

    if (!path || !suffix) {
        return 0;
    }

    while (path[path_length] != '\0') {
        ++path_length;
    }
    while (suffix[suffix_length] != '\0') {
        ++suffix_length;
    }
    if (suffix_length > path_length) {
        return 0;
    }

    for (index = 0UL; index < suffix_length; ++index) {
        if (gdi_font_lower_ascii(path[path_length - suffix_length + index]) != gdi_font_lower_ascii(suffix[index])) {
            return 0;
        }
    }

    return 1;
}

/*
 * Remove ASCII whitespace from both ends of one mutable line buffer.
 *
 * @param text Mutable line buffer.
 * @return Pointer to the trimmed first byte inside the same buffer.
 */
static char* gdi_font_trim_ascii(char* text) {
    char* start = text;
    char* end;

    if (!text) {
        return 0;
    }

    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        ++start;
    }

    end = start;
    while (*end != '\0') {
        ++end;
    }
    while (end > start) {
        char current = *(end - 1);

        if (current != ' ' && current != '\t' && current != '\r' && current != '\n') {
            break;
        }
        *(end - 1) = '\0';
        --end;
    }

    return start;
}

/*
 * Parse one unsigned decimal number from ASCII text.
 *
 * @param text ASCII decimal string.
 * @param value Receives the parsed number on success.
 * @return Non-zero when parsing succeeded.
 */
static int gdi_font_parse_ulong(const char* text, unsigned long* value) {
    unsigned long parsed = 0UL;
    unsigned long index = 0UL;

    if (!text || !value || text[0] == '\0') {
        return 0;
    }

    while (text[index] != '\0') {
        char ch = text[index];

        if (ch < '0' || ch > '9') {
            return 0;
        }
        parsed = (parsed * 10UL) + (unsigned long)(ch - '0');
        ++index;
    }

    *value = parsed;
    return 1;
}

/*
 * Parse one signed decimal number from ASCII text.
 *
 * @param text ASCII decimal string with an optional leading minus sign.
 * @param value Receives the parsed number on success.
 * @return Non-zero when parsing succeeded.
 */
static int gdi_font_parse_long(const char* text, long* value) {
    unsigned long magnitude = 0UL;
    unsigned long index = 0UL;
    long sign = 1L;

    if (!text || !value || text[0] == '\0') {
        return 0;
    }

    if (text[index] == '-') {
        sign = -1L;
        ++index;
    }
    if (text[index] == '\0') {
        return 0;
    }

    while (text[index] != '\0') {
        char ch = text[index];

        if (ch < '0' || ch > '9') {
            return 0;
        }
        magnitude = (magnitude * 10UL) + (unsigned long)(ch - '0');
        ++index;
    }

    *value = (long)magnitude * sign;
    return 1;
}

/*
 * Check whether one string begins with the requested ASCII prefix.
 *
 * @param text Source string to examine.
 * @param prefix Prefix that must match from the first byte.
 * @return Non-zero when the prefix matches.
 */
static int gdi_font_text_starts_with(const char* text, const char* prefix) {
    unsigned long index = 0UL;

    if (!text || !prefix) {
        return 0;
    }

    while (prefix[index] != '\0') {
        if (text[index] != prefix[index]) {
            return 0;
        }
        ++index;
    }

    return 1;
}

/*
 * Decode one hexadecimal nibble from ASCII text.
 *
 * @param ch ASCII hex digit.
 * @param value Receives the decoded nibble on success.
 * @return Non-zero when the byte was a valid hex digit.
 */
static int gdi_font_parse_hex_nibble(char ch, unsigned char* value) {
    if (!value) {
        return 0;
    }

    if (ch >= '0' && ch <= '9') {
        *value = (unsigned char)(ch - '0');
        return 1;
    }
    if (ch >= 'a' && ch <= 'f') {
        *value = (unsigned char)(10 + (ch - 'a'));
        return 1;
    }
    if (ch >= 'A' && ch <= 'F') {
        *value = (unsigned char)(10 + (ch - 'A'));
        return 1;
    }

    return 0;
}

/*
 * Decode one ASCII hex string into an exact byte buffer.
 *
 * @param text Source hex string.
 * @param destination Output byte buffer.
 * @param size Expected decoded byte count.
 * @return Non-zero when the string length and contents are valid.
 */
static int gdi_font_parse_hex_bytes(const char* text, unsigned char* destination, unsigned long size) {
    unsigned long index;

    if (!text || (!destination && size != 0UL)) {
        return 0;
    }

    for (index = 0UL; index < size; ++index) {
        unsigned char high_nibble;
        unsigned char low_nibble;

        if (!gdi_font_parse_hex_nibble(text[index * 2UL], &high_nibble) || !gdi_font_parse_hex_nibble(text[(index * 2UL) + 1UL], &low_nibble)) {
            return 0;
        }
        destination[index] = (unsigned char)((high_nibble << 4) | low_nibble);
    }

    return text[size * 2UL] == '\0';
}

/*
 * Check whether one binary buffer begins with the requested raw byte prefix.
 *
 * @param buffer Source file bytes.
 * @param buffer_size Byte size of the source buffer.
 * @param prefix Expected leading byte sequence.
 * @param prefix_size Number of bytes in the prefix.
 * @return Non-zero when the prefix matches exactly.
 */
static int gdi_font_buffer_has_prefix(const unsigned char* buffer, unsigned long buffer_size, const char* prefix, unsigned long prefix_size) {
    unsigned long index;

    if (!buffer || !prefix || prefix_size == 0UL || buffer_size < prefix_size) {
        return 0;
    }

    for (index = 0UL; index < prefix_size; ++index) {
        if (buffer[index] != (unsigned char)prefix[index]) {
            return 0;
        }
    }

    return 1;
}

/*
 * Scale one positive metric with 16.16 fixed-point rounding.
 *
 * @param value Input metric in design units.
 * @param scale_16 Fixed-point scale factor.
 * @return Rounded scaled metric clamped to at least one pixel when non-zero.
 */
static unsigned long gdi_font_scale_metric(unsigned long value, long scale_16) {
    unsigned long scaled;

    if (value == 0UL) {
        return 0UL;
    }

    scaled = (unsigned long)((((unsigned long long)value * (unsigned long long)scale_16) + 0x8000ULL) >> 16);
    return scaled ? scaled : 1UL;
}

/*
 * Initialize the built-in System UI fallback metrics without relying on a VFS
 * read. This keeps the default GUI font available even when the dynamic loader
 * path cannot read the staged descriptor from inside a DLL.
 *
 * @param handle Destination font handle.
 * @param pixel_height Requested line height in pixels.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_font_initialize_builtin_system_ui(GdiFontHandle* handle, unsigned long pixel_height) {
    if (!handle) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    (void)pixel_height;

    handle->kind = GDI_FONT_KIND_BITMAP;
    handle->pixel_height = ROS_MINI_FONT_PIXEL_HEIGHT;
    handle->line_height = ROS_MINI_FONT_LINE_HEIGHT;
    handle->ascent = ROS_MINI_FONT_ASCENT;
    handle->descent = ROS_MINI_FONT_DESCENT;
    handle->line_gap = (long)handle->line_height - handle->ascent - handle->descent;
    handle->glyphs = g_gdi_builtin_system_ui_glyphs;
    gdi_font_seed_builtin_system_ui_glyphs(handle->glyphs);
    gdi_font_copy_text(handle->data.bitmap.family, sizeof(handle->data.bitmap.family), "Tahoma");
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Publish the built-in System UI font through one process-local static handle.
 *
 * @param pixel_height Requested rendered height.
 * @param font Receives the caller-visible font wrapper.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_font_prepare_builtin_system_ui(unsigned long pixel_height, RosGdiFont* font) {
    long status;

    if (!font) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    gdi_zero_memory(&g_gdi_builtin_system_ui_font, sizeof(g_gdi_builtin_system_ui_font));
    status = gdi_font_initialize_builtin_system_ui(&g_gdi_builtin_system_ui_font, pixel_height);
    if (status < 0L) {
        return status;
    }

    gdi_font_apply_public_metrics(font, &g_gdi_builtin_system_ui_font);
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Scale one signed metric with 16.16 fixed-point rounding.
 *
 * @param value Input signed metric in design units.
 * @param scale_16 Fixed-point scale factor.
 * @return Rounded scaled metric.
 */
static long gdi_font_scale_metric_signed(long value, long scale_16) {
    long sign = 1L;
    unsigned long magnitude;
    unsigned long scaled;

    if (value < 0L) {
        sign = -1L;
        value = -value;
    }

    magnitude = (unsigned long)value;
    scaled = (unsigned long)((((unsigned long long)magnitude * (unsigned long long)scale_16) + 0x8000ULL) >> 16);
    return (long)scaled * sign;
}

/*
 * Parse one mini-font descriptor file and derive the requested pixel metrics.
 *
 * @param handle Destination font handle.
 * @param buffer Mutable descriptor file contents.
 * @param pixel_height Requested line height in pixels.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_font_parse_mini_descriptor(GdiFontHandle* handle, unsigned char* buffer, unsigned long pixel_height) {
    unsigned long design_height = 0UL;
    unsigned long glyph_width = 0UL;
    unsigned long glyph_height = 0UL;
    unsigned long advance = 0UL;
    unsigned long line_height = 0UL;
    unsigned long sample_grid = GDI_FONT_SAMPLE_GRID_DEFAULT;
    unsigned long requested_height;
    unsigned long offset = 0UL;
    long ascent = 0L;
    long descent = 0L;
    long scale_16;
    char family[64];
    int header_seen = 0;
    int source_valid = 0;

    if (!handle || !buffer) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    gdi_zero_memory(family, sizeof(family));
    while (1) {
        char line_buffer[GDI_FONT_DESCRIPTOR_LINE_MAX];
        unsigned long line_length = 0UL;
        int finished = 0;
        char* equals;
        char* key;
        char* value;

        while (buffer[offset] != '\0' && buffer[offset] != '\n') {
            char ch = (char)buffer[offset++];

            if (ch == '\r') {
                continue;
            }
            if ((line_length + 1UL) >= sizeof(line_buffer)) {
                return GDI_FONT_STATUS_FORMAT;
            }
            line_buffer[line_length++] = ch;
        }
        if (buffer[offset] == '\0') {
            finished = 1;
        }
        else {
            ++offset;
        }
        line_buffer[line_length] = '\0';

        key = gdi_font_trim_ascii(line_buffer);
        if (!header_seen) {
            header_seen = 1;
            if (!gdi_font_text_equals(key, "ROSFONT1")) {
                return GDI_FONT_STATUS_FORMAT;
            }
            if (finished) {
                break;
            }
            continue;
        }

        if (key[0] == '\0' || key[0] == '#') {
            if (finished) {
                break;
            }
            continue;
        }

        equals = key;
        while (*equals != '\0' && *equals != '=') {
            ++equals;
        }
        if (*equals != '=') {
            return GDI_FONT_STATUS_FORMAT;
        }

        *equals = '\0';
        value = gdi_font_trim_ascii(equals + 1);
        key = gdi_font_trim_ascii(key);

        if (gdi_font_text_equals_ignore_case(key, "family")) {
            gdi_font_copy_text(family, sizeof(family), value);
        }
        else if (gdi_font_text_equals_ignore_case(key, "source")) {
            source_valid = gdi_font_text_equals_ignore_case(value, "mini");
        }
        else if (gdi_font_text_equals_ignore_case(key, "design_height")) {
            if (!gdi_font_parse_ulong(value, &design_height)) {
                return GDI_FONT_STATUS_FORMAT;
            }
        }
        else if (gdi_font_text_equals_ignore_case(key, "glyph_width")) {
            if (!gdi_font_parse_ulong(value, &glyph_width)) {
                return GDI_FONT_STATUS_FORMAT;
            }
        }
        else if (gdi_font_text_equals_ignore_case(key, "glyph_height")) {
            if (!gdi_font_parse_ulong(value, &glyph_height)) {
                return GDI_FONT_STATUS_FORMAT;
            }
        }
        else if (gdi_font_text_equals_ignore_case(key, "advance")) {
            if (!gdi_font_parse_ulong(value, &advance)) {
                return GDI_FONT_STATUS_FORMAT;
            }
        }
        else if (gdi_font_text_equals_ignore_case(key, "line_height")) {
            if (!gdi_font_parse_ulong(value, &line_height)) {
                return GDI_FONT_STATUS_FORMAT;
            }
        }
        else if (gdi_font_text_equals_ignore_case(key, "ascent")) {
            unsigned long parsed;

            if (!gdi_font_parse_ulong(value, &parsed)) {
                return GDI_FONT_STATUS_FORMAT;
            }
            ascent = (long)parsed;
        }
        else if (gdi_font_text_equals_ignore_case(key, "descent")) {
            unsigned long parsed;

            if (!gdi_font_parse_ulong(value, &parsed)) {
                return GDI_FONT_STATUS_FORMAT;
            }
            descent = (long)parsed;
        }
        else if (gdi_font_text_equals_ignore_case(key, "sample_grid")) {
            if (!gdi_font_parse_ulong(value, &sample_grid)) {
                return GDI_FONT_STATUS_FORMAT;
            }
        }

        if (finished) {
            break;
        }
    }

    if (!header_seen) {
        return GDI_FONT_STATUS_FORMAT;
    }

    if (!source_valid || design_height == 0UL || glyph_width == 0UL || glyph_height == 0UL || advance == 0UL || line_height == 0UL || ascent <= 0L) {
        return GDI_FONT_STATUS_FORMAT;
    }

    requested_height = pixel_height ? pixel_height : line_height;
    scale_16 = (long)((((unsigned long long)requested_height) << 16) / design_height);
    if (scale_16 <= 0L) {
        return GDI_FONT_STATUS_FORMAT;
    }

    handle->kind = GDI_FONT_KIND_MINI;
    handle->pixel_height = requested_height;
    handle->line_height = gdi_font_scale_metric(line_height, scale_16);
    handle->ascent = gdi_font_scale_metric_signed(ascent, scale_16);
    handle->descent = gdi_font_scale_metric_signed(descent, scale_16);
    handle->line_gap = (long)handle->line_height - handle->ascent - handle->descent;
    handle->data.mini.glyph_width = gdi_font_scale_metric(glyph_width, scale_16);
    handle->data.mini.glyph_height = gdi_font_scale_metric(glyph_height, scale_16);
    handle->data.mini.advance = gdi_font_scale_metric(advance, scale_16);
    handle->data.mini.line_height = handle->line_height;
    handle->data.mini.ascent = handle->ascent;
    handle->data.mini.descent = handle->descent;
    handle->data.mini.sample_grid = sample_grid ? sample_grid : GDI_FONT_SAMPLE_GRID_DEFAULT;
    gdi_font_copy_text(handle->data.mini.family, sizeof(handle->data.mini.family), family[0] ? family : "System UI");
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Identify the descriptor source type stored in one ROSFONT text file.
 *
 * @param buffer Null-terminated descriptor text.
 * @return One `GDI_FONT_DESCRIPTOR_SOURCE_*` value.
 */
static unsigned long gdi_font_descriptor_source_kind(const unsigned char* buffer) {
    unsigned long offset = 0UL;
    int header_seen = 0;

    if (!buffer) {
        return GDI_FONT_DESCRIPTOR_SOURCE_UNKNOWN;
    }

    while (1) {
        char line_buffer[GDI_FONT_DESCRIPTOR_LINE_MAX];
        unsigned long line_length = 0UL;
        int finished = 0;
        char* equals;
        char* key;
        char* value;

        while (buffer[offset] != '\0' && buffer[offset] != '\n') {
            char ch = (char)buffer[offset++];

            if (ch == '\r') {
                continue;
            }
            if ((line_length + 1UL) >= sizeof(line_buffer)) {
                return GDI_FONT_DESCRIPTOR_SOURCE_UNKNOWN;
            }
            line_buffer[line_length++] = ch;
        }
        if (buffer[offset] == '\0') {
            finished = 1;
        }
        else {
            ++offset;
        }
        line_buffer[line_length] = '\0';

        key = gdi_font_trim_ascii(line_buffer);
        if (!header_seen) {
            header_seen = 1;
            if (!gdi_font_text_equals(key, "ROSFONT1")) {
                return GDI_FONT_DESCRIPTOR_SOURCE_UNKNOWN;
            }
            if (finished) {
                break;
            }
            continue;
        }

        if (key[0] == '\0' || key[0] == '#') {
            if (finished) {
                break;
            }
            continue;
        }

        equals = key;
        while (*equals != '\0' && *equals != '=') {
            ++equals;
        }
        if (*equals != '=') {
            if (finished) {
                break;
            }
            continue;
        }

        *equals = '\0';
        value = gdi_font_trim_ascii(equals + 1);
        key = gdi_font_trim_ascii(key);
        if (gdi_font_text_equals_ignore_case(key, "source")) {
            if (gdi_font_text_equals_ignore_case(value, "mini")) {
                return GDI_FONT_DESCRIPTOR_SOURCE_MINI;
            }
            if (gdi_font_text_equals_ignore_case(value, "bitmap")) {
                return GDI_FONT_DESCRIPTOR_SOURCE_BITMAP;
            }
            return GDI_FONT_DESCRIPTOR_SOURCE_UNKNOWN;
        }

        if (finished) {
            break;
        }
    }

    return GDI_FONT_DESCRIPTOR_SOURCE_UNKNOWN;
}

/*
 * Parse one prerasterized bitmap descriptor into the font glyph cache.
 *
 * The descriptor already contains baked alpha coverage per ASCII glyph, so the
 * loader fills the cache up front and avoids any runtime outline processing.
 *
 * @param handle Destination font handle.
 * @param buffer Mutable descriptor file contents.
 * @param pixel_height Requested pixel height, which must match the baked file.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_font_parse_bitmap_descriptor(GdiFontHandle* handle, unsigned char* buffer, unsigned long pixel_height) {
    unsigned long source_pixel_height = 0UL;
    unsigned long line_height = 0UL;
    unsigned long glyph_count = 0UL;
    unsigned long offset = 0UL;
    long ascent = 0L;
    long descent = 0L;
    char family[64];
    int header_seen = 0;
    int source_valid = 0;
    GdiGlyphBitmap* glyphs = 0;
    unsigned long glyph_fields[GDI_FONT_ASCII_CACHE_SIZE];
    unsigned long glyph_index;
    long status = ROS_USER_IPC_STATUS_OK;
    const char* debug_reason = 0;

    if (!handle || !buffer) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    gdi_zero_memory(family, sizeof(family));
    gdi_zero_memory(glyph_fields, sizeof(glyph_fields));
    glyphs = handle->glyphs;
    if (!glyphs) {
        glyphs = (GdiGlyphBitmap*)malloc(sizeof(GdiGlyphBitmap) * GDI_FONT_ASCII_CACHE_SIZE);
        if (!glyphs) {
            return ROS_USER_IPC_STATUS_NO_SPACE;
        }
        handle->storage_flags |= GDI_FONT_STORAGE_OWNS_GLYPHS;
    }
    gdi_zero_memory(glyphs, sizeof(GdiGlyphBitmap) * GDI_FONT_ASCII_CACHE_SIZE);

    while (1) {
        char line_buffer[GDI_FONT_DESCRIPTOR_LINE_MAX];
        unsigned long line_length = 0UL;
        int finished = 0;
        char* equals;
        char* key;
        char* value;

        while (buffer[offset] != '\0' && buffer[offset] != '\n') {
            char ch = (char)buffer[offset++];

            if (ch == '\r') {
                continue;
            }
            if ((line_length + 1UL) >= sizeof(line_buffer)) {
                debug_reason = "gdi.dll: bitmap descriptor line too long";
                status = GDI_FONT_STATUS_FORMAT;
                goto gdi_bitmap_descriptor_fail;
            }
            line_buffer[line_length++] = ch;
        }
        if (buffer[offset] == '\0') {
            finished = 1;
        }
        else {
            ++offset;
        }
        line_buffer[line_length] = '\0';

        key = gdi_font_trim_ascii(line_buffer);
        if (!header_seen) {
            header_seen = 1;
            if (!gdi_font_text_equals(key, "ROSFONT1")) {
                debug_reason = "gdi.dll: bitmap descriptor missing ROSFONT1 header";
                status = GDI_FONT_STATUS_FORMAT;
                goto gdi_bitmap_descriptor_fail;
            }
            if (finished) {
                break;
            }
            continue;
        }

        if (key[0] == '\0' || key[0] == '#') {
            if (finished) {
                break;
            }
            continue;
        }

        equals = key;
        while (*equals != '\0' && *equals != '=') {
            ++equals;
        }
        if (*equals != '=') {
            debug_reason = "gdi.dll: bitmap descriptor line missing equals";
            status = GDI_FONT_STATUS_FORMAT;
            goto gdi_bitmap_descriptor_fail;
        }

        *equals = '\0';
        value = gdi_font_trim_ascii(equals + 1);
        key = gdi_font_trim_ascii(key);

        if (gdi_font_text_equals_ignore_case(key, "family")) {
            gdi_font_copy_text(family, sizeof(family), value);
        }
        else if (gdi_font_text_equals_ignore_case(key, "source")) {
            source_valid = gdi_font_text_equals_ignore_case(value, "bitmap");
        }
        else if (gdi_font_text_equals_ignore_case(key, "pixel_height")) {
            if (!gdi_font_parse_ulong(value, &source_pixel_height)) {
                debug_reason = "gdi.dll: bitmap descriptor invalid pixel_height";
                status = GDI_FONT_STATUS_FORMAT;
                goto gdi_bitmap_descriptor_fail;
            }
        }
        else if (gdi_font_text_equals_ignore_case(key, "line_height")) {
            if (!gdi_font_parse_ulong(value, &line_height)) {
                debug_reason = "gdi.dll: bitmap descriptor invalid line_height";
                status = GDI_FONT_STATUS_FORMAT;
                goto gdi_bitmap_descriptor_fail;
            }
        }
        else if (gdi_font_text_equals_ignore_case(key, "ascent")) {
            if (!gdi_font_parse_long(value, &ascent)) {
                debug_reason = "gdi.dll: bitmap descriptor invalid ascent";
                status = GDI_FONT_STATUS_FORMAT;
                goto gdi_bitmap_descriptor_fail;
            }
        }
        else if (gdi_font_text_equals_ignore_case(key, "descent")) {
            if (!gdi_font_parse_long(value, &descent)) {
                debug_reason = "gdi.dll: bitmap descriptor invalid descent";
                status = GDI_FONT_STATUS_FORMAT;
                goto gdi_bitmap_descriptor_fail;
            }
        }
        else if (gdi_font_text_equals_ignore_case(key, "glyph_count")) {
            if (!gdi_font_parse_ulong(value, &glyph_count)) {
                debug_reason = "gdi.dll: bitmap descriptor invalid glyph_count";
                status = GDI_FONT_STATUS_FORMAT;
                goto gdi_bitmap_descriptor_fail;
            }
        }
        else if (gdi_font_text_starts_with(key, "glyph.")) {
            char* index_text = key + 6;
            char* field = index_text;

            while (*field != '\0' && *field != '.') {
                ++field;
            }
            if (*field != '.') {
                debug_reason = "gdi.dll: bitmap descriptor invalid glyph key";
                status = GDI_FONT_STATUS_FORMAT;
                goto gdi_bitmap_descriptor_fail;
            }

            *field = '\0';
            ++field;
            if (!gdi_font_parse_ulong(index_text, &glyph_index) || glyph_index >= GDI_FONT_ASCII_CACHE_SIZE) {
                debug_reason = "gdi.dll: bitmap descriptor glyph index out of range";
                status = GDI_FONT_STATUS_FORMAT;
                goto gdi_bitmap_descriptor_fail;
            }

            if (gdi_font_text_equals_ignore_case(field, "advance")) {
                unsigned long parsed;

                if (!gdi_font_parse_ulong(value, &parsed)) {
                    debug_reason = "gdi.dll: bitmap descriptor invalid glyph advance";
                    status = GDI_FONT_STATUS_FORMAT;
                    goto gdi_bitmap_descriptor_fail;
                }
                glyphs[glyph_index].advance = parsed;
                glyph_fields[glyph_index] |= GDI_FONT_BITMAP_FIELD_ADVANCE;
            }
            else if (gdi_font_text_equals_ignore_case(field, "bitmap_left")) {
                long parsed;

                if (!gdi_font_parse_long(value, &parsed)) {
                    debug_reason = "gdi.dll: bitmap descriptor invalid glyph left";
                    status = GDI_FONT_STATUS_FORMAT;
                    goto gdi_bitmap_descriptor_fail;
                }
                glyphs[glyph_index].bitmap_left = parsed;
                glyph_fields[glyph_index] |= GDI_FONT_BITMAP_FIELD_LEFT;
            }
            else if (gdi_font_text_equals_ignore_case(field, "bitmap_top")) {
                long parsed;

                if (!gdi_font_parse_long(value, &parsed)) {
                    debug_reason = "gdi.dll: bitmap descriptor invalid glyph top";
                    status = GDI_FONT_STATUS_FORMAT;
                    goto gdi_bitmap_descriptor_fail;
                }
                glyphs[glyph_index].bitmap_top = parsed;
                glyph_fields[glyph_index] |= GDI_FONT_BITMAP_FIELD_TOP;
            }
            else if (gdi_font_text_equals_ignore_case(field, "width")) {
                unsigned long parsed;

                if (!gdi_font_parse_ulong(value, &parsed)) {
                    debug_reason = "gdi.dll: bitmap descriptor invalid glyph width";
                    status = GDI_FONT_STATUS_FORMAT;
                    goto gdi_bitmap_descriptor_fail;
                }
                glyphs[glyph_index].width = parsed;
                glyph_fields[glyph_index] |= GDI_FONT_BITMAP_FIELD_WIDTH;
            }
            else if (gdi_font_text_equals_ignore_case(field, "height")) {
                unsigned long parsed;

                if (!gdi_font_parse_ulong(value, &parsed)) {
                    debug_reason = "gdi.dll: bitmap descriptor invalid glyph height";
                    status = GDI_FONT_STATUS_FORMAT;
                    goto gdi_bitmap_descriptor_fail;
                }
                glyphs[glyph_index].height = parsed;
                glyph_fields[glyph_index] |= GDI_FONT_BITMAP_FIELD_HEIGHT;
            }
            else if (gdi_font_text_equals_ignore_case(field, "coverage")) {
                unsigned long pixel_count;

                if ((glyph_fields[glyph_index] & (GDI_FONT_BITMAP_FIELD_WIDTH | GDI_FONT_BITMAP_FIELD_HEIGHT)) != (GDI_FONT_BITMAP_FIELD_WIDTH | GDI_FONT_BITMAP_FIELD_HEIGHT)) {
                    debug_reason = "gdi.dll: bitmap descriptor coverage appeared before size";
                    status = GDI_FONT_STATUS_FORMAT;
                    goto gdi_bitmap_descriptor_fail;
                }
                if ((glyph_fields[glyph_index] & GDI_FONT_BITMAP_FIELD_COVERAGE) != 0UL) {
                    debug_reason = "gdi.dll: bitmap descriptor duplicate glyph coverage";
                    status = GDI_FONT_STATUS_FORMAT;
                    goto gdi_bitmap_descriptor_fail;
                }

                pixel_count = glyphs[glyph_index].width * glyphs[glyph_index].height;
                if (pixel_count == 0UL) {
                    if (value[0] != '\0') {
                        debug_reason = "gdi.dll: bitmap descriptor zero-sized glyph had coverage data";
                        status = GDI_FONT_STATUS_FORMAT;
                        goto gdi_bitmap_descriptor_fail;
                    }
                }
                else {
                    glyphs[glyph_index].coverage = (unsigned char*)malloc(pixel_count);
                    if (!glyphs[glyph_index].coverage) {
                        status = ROS_USER_IPC_STATUS_NO_SPACE;
                        goto gdi_bitmap_descriptor_fail;
                    }
                    if (!gdi_font_parse_hex_bytes(value, glyphs[glyph_index].coverage, pixel_count)) {
                        debug_reason = "gdi.dll: bitmap descriptor glyph coverage hex was invalid";
                        status = GDI_FONT_STATUS_FORMAT;
                        goto gdi_bitmap_descriptor_fail;
                    }
                }

                glyph_fields[glyph_index] |= GDI_FONT_BITMAP_FIELD_COVERAGE;
            }
            else {
                debug_reason = "gdi.dll: bitmap descriptor unknown glyph field";
                status = GDI_FONT_STATUS_FORMAT;
                goto gdi_bitmap_descriptor_fail;
            }
        }
        else {
            debug_reason = "gdi.dll: bitmap descriptor unknown top-level key";
            status = GDI_FONT_STATUS_FORMAT;
            goto gdi_bitmap_descriptor_fail;
        }

        if (finished) {
            break;
        }
    }

    if (!header_seen || !source_valid || source_pixel_height == 0UL || line_height == 0UL || ascent <= 0L || descent < 0L || glyph_count != GDI_FONT_ASCII_CACHE_SIZE) {
        debug_reason = "gdi.dll: bitmap descriptor missing required metadata";
        status = GDI_FONT_STATUS_FORMAT;
        goto gdi_bitmap_descriptor_fail;
    }
    if (pixel_height != 0UL && pixel_height != source_pixel_height) {
        debug_reason = "gdi.dll: bitmap descriptor height mismatch";
        status = GDI_FONT_STATUS_NOT_SUPPORTED;
        goto gdi_bitmap_descriptor_fail;
    }

    for (glyph_index = 0UL; glyph_index < GDI_FONT_ASCII_CACHE_SIZE; ++glyph_index) {
        if (glyph_fields[glyph_index] != GDI_FONT_BITMAP_FIELD_ALL) {
            debug_reason = "gdi.dll: bitmap descriptor missing glyph fields";
            status = GDI_FONT_STATUS_FORMAT;
            goto gdi_bitmap_descriptor_fail;
        }

        glyphs[glyph_index].ready = 1;
        glyphs[glyph_index].valid = 1;
    }

    handle->kind = GDI_FONT_KIND_BITMAP;
    handle->pixel_height = source_pixel_height;
    handle->line_height = line_height;
    handle->ascent = ascent;
    handle->descent = descent;
    handle->line_gap = (long)line_height - ascent - descent;
    handle->glyphs = glyphs;
    gdi_font_copy_text(handle->data.bitmap.family, sizeof(handle->data.bitmap.family), family[0] ? family : "Bitmap UI");
    return ROS_USER_IPC_STATUS_OK;

gdi_bitmap_descriptor_fail:
    if (debug_reason) {
        gdi_font_debug_line(debug_reason);
    }
    if (glyphs) {
        for (glyph_index = 0UL; glyph_index < GDI_FONT_ASCII_CACHE_SIZE; ++glyph_index) {
            gdi_font_destroy_glyph(&glyphs[glyph_index]);
        }
        if ((handle->storage_flags & GDI_FONT_STORAGE_OWNS_GLYPHS) != 0UL) {
            free(glyphs);
            handle->storage_flags &= ~GDI_FONT_STORAGE_OWNS_GLYPHS;
        }
        handle->glyphs = 0;
    }
    return status;
}

/*
 * Dispatch one ROSFONT descriptor to the parser that matches its source type.
 *
 * @param handle Destination font handle.
 * @param buffer Mutable descriptor file contents.
 * @param pixel_height Requested pixel height.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_font_parse_descriptor(GdiFontHandle* handle, unsigned char* buffer, unsigned long pixel_height) {
    unsigned long source_kind = gdi_font_descriptor_source_kind(buffer);

    if (source_kind == GDI_FONT_DESCRIPTOR_SOURCE_MINI) {
        return gdi_font_parse_mini_descriptor(handle, buffer, pixel_height);
    }
    if (source_kind == GDI_FONT_DESCRIPTOR_SOURCE_BITMAP) {
        return gdi_font_parse_bitmap_descriptor(handle, buffer, pixel_height);
    }

    return GDI_FONT_STATUS_FORMAT;
}

/*
 * Read one little-endian unsigned 32-bit value.
 *
 * @param data Source byte buffer.
 * @return Decoded unsigned value.
 */
static unsigned long gdi_font_le_u32(const unsigned char* data) {
    return ((unsigned long)data[0]) |
        (((unsigned long)data[1]) << 8) |
        (((unsigned long)data[2]) << 16) |
        (((unsigned long)data[3]) << 24);
}

/*
 * Read one little-endian signed 32-bit value.
 *
 * @param data Source byte buffer.
 * @return Decoded signed value.
 */
static long gdi_font_le_s32(const unsigned char* data) {
    unsigned long raw = gdi_font_le_u32(data);

    if ((raw & 0x80000000UL) != 0UL) {
        return (long)(raw - 0x100000000ULL);
    }

    return (long)raw;
}

/*
 * Parse one binary raster-font container into ready-to-draw glyph entries.
 *
 * The `.rtf` format stores a fixed header plus one lookup-table row per ASCII
 * glyph. Each row carries the coverage blob offset and byte size, so the
 * renderer can bind every glyph to its precomputed alpha coverage without
 * text parsing or per-glyph heap copies during startup.
 *
 * @param handle Destination font handle.
 * @param buffer Loaded `.rtf` file bytes.
 * @param file_size Byte size of the loaded file.
 * @param pixel_height Requested pixel height.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_font_parse_raster_file(GdiFontHandle* handle, const unsigned char* buffer, unsigned long file_size, unsigned long pixel_height) {
    unsigned long version;
    unsigned long header_size;
    unsigned long source_pixel_height;
    unsigned long line_height;
    long ascent;
    long descent;
    unsigned long glyph_count;
    unsigned long glyph_table_offset;
    unsigned long glyph_entry_size;
    GdiGlyphBitmap* glyphs = 0;
    unsigned long glyph_index;
    char family[GDI_RASTER_FONT_FAMILY_BYTES + 1UL];
    long status = ROS_USER_IPC_STATUS_OK;

    if (!handle || !buffer) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }
    if (file_size < GDI_RASTER_FONT_HEADER_SIZE) {
        return GDI_FONT_STATUS_FORMAT;
    }
    if (!gdi_font_buffer_has_prefix(buffer, file_size, GDI_RASTER_FONT_MAGIC, GDI_RASTER_FONT_MAGIC_SIZE)) {
        return GDI_FONT_STATUS_FORMAT;
    }

    version = gdi_font_le_u32(buffer + GDI_RASTER_FONT_HEADER_VERSION_OFFSET);
    header_size = gdi_font_le_u32(buffer + GDI_RASTER_FONT_HEADER_SIZE_OFFSET);
    source_pixel_height = gdi_font_le_u32(buffer + GDI_RASTER_FONT_HEADER_PIXEL_HEIGHT_OFFSET);
    line_height = gdi_font_le_u32(buffer + GDI_RASTER_FONT_HEADER_LINE_HEIGHT_OFFSET);
    ascent = gdi_font_le_s32(buffer + GDI_RASTER_FONT_HEADER_ASCENT_OFFSET);
    descent = gdi_font_le_s32(buffer + GDI_RASTER_FONT_HEADER_DESCENT_OFFSET);
    glyph_count = gdi_font_le_u32(buffer + GDI_RASTER_FONT_HEADER_GLYPH_COUNT_OFFSET);
    glyph_table_offset = gdi_font_le_u32(buffer + GDI_RASTER_FONT_HEADER_GLYPH_TABLE_OFFSET);
    glyph_entry_size = gdi_font_le_u32(buffer + GDI_RASTER_FONT_HEADER_GLYPH_ENTRY_SIZE_OFFSET);

    if (version != GDI_RASTER_FONT_VERSION || header_size < GDI_RASTER_FONT_HEADER_SIZE || header_size > file_size || source_pixel_height == 0UL || line_height == 0UL || ascent <= 0L || descent < 0L) {
        return GDI_FONT_STATUS_FORMAT;
    }
    if (glyph_count != GDI_FONT_ASCII_CACHE_SIZE || glyph_entry_size < GDI_RASTER_FONT_GLYPH_ENTRY_SIZE || glyph_table_offset < header_size) {
        return GDI_FONT_STATUS_FORMAT;
    }
    if (glyph_table_offset > file_size || (glyph_count * glyph_entry_size) > (file_size - glyph_table_offset)) {
        return GDI_FONT_STATUS_FORMAT;
    }
    if (pixel_height != 0UL && pixel_height != source_pixel_height) {
        return GDI_FONT_STATUS_NOT_SUPPORTED;
    }

    gdi_zero_memory(family, sizeof(family));
    for (glyph_index = 0UL; glyph_index < GDI_RASTER_FONT_FAMILY_BYTES; ++glyph_index) {
        unsigned char ch = buffer[GDI_RASTER_FONT_HEADER_FAMILY_OFFSET + glyph_index];

        family[glyph_index] = (char)ch;
        if (ch == '\0') {
            break;
        }
    }

    glyphs = (GdiGlyphBitmap*)malloc(sizeof(GdiGlyphBitmap) * GDI_FONT_ASCII_CACHE_SIZE);
    if (!glyphs) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }
    gdi_zero_memory(glyphs, sizeof(GdiGlyphBitmap) * GDI_FONT_ASCII_CACHE_SIZE);

    for (glyph_index = 0UL; glyph_index < GDI_FONT_ASCII_CACHE_SIZE; ++glyph_index) {
        const unsigned char* entry = buffer + glyph_table_offset + (glyph_index * glyph_entry_size);
        unsigned long coverage_offset = gdi_font_le_u32(entry + GDI_RASTER_FONT_ENTRY_COVERAGE_OFFSET);
        unsigned long coverage_size = gdi_font_le_u32(entry + GDI_RASTER_FONT_ENTRY_COVERAGE_SIZE_OFFSET);
        unsigned long width = gdi_font_le_u32(entry + GDI_RASTER_FONT_ENTRY_WIDTH_OFFSET);
        unsigned long height = gdi_font_le_u32(entry + GDI_RASTER_FONT_ENTRY_HEIGHT_OFFSET);
        unsigned long expected_size = width * height;

        if (width != 0UL && expected_size / width != height) {
            status = GDI_FONT_STATUS_FORMAT;
            goto gdi_raster_font_fail;
        }
        if (coverage_size != expected_size) {
            status = GDI_FONT_STATUS_FORMAT;
            goto gdi_raster_font_fail;
        }
        if (coverage_size != 0UL) {
            if (coverage_offset > file_size || coverage_size > (file_size - coverage_offset)) {
                status = GDI_FONT_STATUS_FORMAT;
                goto gdi_raster_font_fail;
            }
            glyphs[glyph_index].coverage = (unsigned char*)(buffer + coverage_offset);
        }

        glyphs[glyph_index].advance = gdi_font_le_u32(entry + GDI_RASTER_FONT_ENTRY_ADVANCE_OFFSET);
        glyphs[glyph_index].bitmap_left = gdi_font_le_s32(entry + GDI_RASTER_FONT_ENTRY_BITMAP_LEFT_OFFSET);
        glyphs[glyph_index].bitmap_top = gdi_font_le_s32(entry + GDI_RASTER_FONT_ENTRY_BITMAP_TOP_OFFSET);
        glyphs[glyph_index].width = width;
        glyphs[glyph_index].height = height;
        glyphs[glyph_index].ready = 1;
        glyphs[glyph_index].valid = 1;
        glyphs[glyph_index].coverage_owned = 0;
    }

    handle->kind = GDI_FONT_KIND_BITMAP;
    handle->pixel_height = source_pixel_height;
    handle->line_height = line_height;
    handle->ascent = ascent;
    handle->descent = descent;
    handle->line_gap = (long)line_height - ascent - descent;
    handle->glyphs = glyphs;
    gdi_font_copy_text(handle->data.bitmap.family, sizeof(handle->data.bitmap.family), family[0] ? family : "Bitmap UI");
    return ROS_USER_IPC_STATUS_OK;

gdi_raster_font_fail:
    if (glyphs) {
        for (glyph_index = 0UL; glyph_index < GDI_FONT_ASCII_CACHE_SIZE; ++glyph_index) {
            gdi_font_destroy_glyph(&glyphs[glyph_index]);
        }
        free(glyphs);
    }
    return status;
}

/*
 * Read one big-endian unsigned 16-bit value.
 *
 * @param data Source byte buffer.
 * @return Decoded unsigned value.
 */
static unsigned long gdi_font_be_u16(const unsigned char* data) {
    return (((unsigned long)data[0]) << 8) | (unsigned long)data[1];
}

/*
 * Read one big-endian signed 16-bit value.
 *
 * @param data Source byte buffer.
 * @return Decoded signed value.
 */
static long gdi_font_be_s16(const unsigned char* data) {
    unsigned long raw = gdi_font_be_u16(data);

    if ((raw & 0x8000UL) != 0UL) {
        return (long)(raw - 0x10000UL);
    }

    return (long)raw;
}

/*
 * Read one big-endian unsigned 32-bit value.
 *
 * @param data Source byte buffer.
 * @return Decoded unsigned value.
 */
static unsigned long gdi_font_be_u32(const unsigned char* data) {
    return (((unsigned long)data[0]) << 24) |
        (((unsigned long)data[1]) << 16) |
        (((unsigned long)data[2]) << 8) |
        (unsigned long)data[3];
}

/*
 * Locate one sfnt table record inside a table directory buffer.
 *
 * @param directory Font header plus table directory bytes.
 * @param directory_size Byte size of the directory buffer.
 * @param file_size Total font file size.
 * @param tag FourCC table tag.
 * @param table_offset Receives the byte offset inside the file.
 * @param table_size Receives the table byte size.
 * @return Non-zero when the table record exists and points inside the file.
 */
static int gdi_ttf_find_table_record(const unsigned char* directory, unsigned long directory_size, unsigned long file_size, unsigned long tag, unsigned long* table_offset, unsigned long* table_size) {
    unsigned long table_count;
    unsigned long index;

    if (!directory || directory_size < 12UL || !table_offset || !table_size) {
        return 0;
    }

    table_count = gdi_font_be_u16(directory + 4UL);
    if (directory_size < (12UL + (table_count * 16UL))) {
        return 0;
    }

    for (index = 0UL; index < table_count; ++index) {
        const unsigned char* record = directory + 12UL + (index * 16UL);
        unsigned long record_tag = gdi_font_be_u32(record);
        unsigned long offset = gdi_font_be_u32(record + 8UL);
        unsigned long length = gdi_font_be_u32(record + 12UL);

        if (record_tag != tag) {
            continue;
        }
        if (offset > file_size || length > (file_size - offset)) {
            return 0;
        }

        *table_offset = offset;
        *table_size = length;
        return 1;
    }

    return 0;
}

/*
 * Find one sfnt table inside a TrueType font buffer.
 *
 * @param data Font file bytes.
 * @param size Font file size.
 * @param tag FourCC table tag.
 * @param table Receives the table pointer on success.
 * @param table_size Receives the table byte size on success.
 * @return Non-zero when the table was found and fully fits in the buffer.
 */
static int gdi_ttf_find_table(const unsigned char* data, unsigned long size, unsigned long tag, const unsigned char** table, unsigned long* table_size) {
    unsigned long table_count;
    unsigned long index;

    if (!data || size < 12UL || !table || !table_size) {
        return 0;
    }

    table_count = gdi_font_be_u16(data + 4UL);
    if (size < (12UL + (table_count * 16UL))) {
        return 0;
    }

    for (index = 0UL; index < table_count; ++index) {
        const unsigned char* record = data + 12UL + (index * 16UL);
        unsigned long record_tag = gdi_font_be_u32(record);
        unsigned long offset = gdi_font_be_u32(record + 8UL);
        unsigned long length = gdi_font_be_u32(record + 12UL);

        if (record_tag != tag) {
            continue;
        }
        if (offset > size || length > (size - offset)) {
            return 0;
        }

        *table = data + offset;
        *table_size = length;
        return 1;
    }

    return 0;
}

/*
 * Select the best Unicode cmap subtable available in one TrueType font.
 *
 * @param cmap Base pointer to the cmap table.
 * @param cmap_size Byte size of the cmap table.
 * @param subtable Receives the selected subtable pointer.
 * @param subtable_size Receives the selected subtable size.
 * @param format Receives the selected format identifier.
 * @return Non-zero when a supported cmap subtable was found.
 */
static int gdi_ttf_select_cmap(const unsigned char* cmap, unsigned long cmap_size, const unsigned char** subtable, unsigned long* subtable_size, unsigned short* format) {
    unsigned long record_count;
    unsigned long index;
    const unsigned char* best_format12 = 0;
    unsigned long best_format12_size = 0UL;
    const unsigned char* best_format4 = 0;
    unsigned long best_format4_size = 0UL;

    if (!cmap || cmap_size < 4UL || !subtable || !subtable_size || !format) {
        return 0;
    }

    record_count = gdi_font_be_u16(cmap + 2UL);
    if (cmap_size < (4UL + (record_count * 8UL))) {
        return 0;
    }

    for (index = 0UL; index < record_count; ++index) {
        const unsigned char* record = cmap + 4UL + (index * 8UL);
        unsigned long platform_id = gdi_font_be_u16(record);
        unsigned long encoding_id = gdi_font_be_u16(record + 2UL);
        unsigned long offset = gdi_font_be_u32(record + 4UL);
        const unsigned char* candidate;
        unsigned long candidate_size;
        unsigned long candidate_format;

        if (!(platform_id == 0UL || platform_id == 3UL)) {
            continue;
        }
        if (offset > cmap_size || (cmap_size - offset) < 4UL) {
            continue;
        }

        candidate = cmap + offset;
        candidate_format = gdi_font_be_u16(candidate);
        if (candidate_format == 4UL) {
            candidate_size = gdi_font_be_u16(candidate + 2UL);
            if (candidate_size <= (cmap_size - offset) && (platform_id == 0UL || encoding_id == 1UL || encoding_id == 0UL)) {
                best_format4 = candidate;
                best_format4_size = candidate_size;
            }
        }
        else if (candidate_format == 12UL) {
            if ((cmap_size - offset) < 16UL) {
                continue;
            }
            candidate_size = gdi_font_be_u32(candidate + 4UL);
            if (candidate_size <= (cmap_size - offset) && (platform_id == 0UL || encoding_id == 10UL)) {
                best_format12 = candidate;
                best_format12_size = candidate_size;
            }
        }
    }

    if (best_format12) {
        *subtable = best_format12;
        *subtable_size = best_format12_size;
        *format = 12U;
        return 1;
    }
    if (best_format4) {
        *subtable = best_format4;
        *subtable_size = best_format4_size;
        *format = 4U;
        return 1;
    }

    return 0;
}

/*
 * Initialize one private TrueType font state after the file has been loaded.
 *
 * @param handle Destination font handle.
 * @param buffer Loaded font bytes.
 * @param size Loaded font size.
 * @param pixel_height Requested line height in pixels.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_ttf_initialize(GdiFontHandle* handle, unsigned char* buffer, unsigned long size, unsigned long pixel_height) {
    const unsigned char* cmap;
    const unsigned char* head;
    const unsigned char* hhea;
    const unsigned char* hmtx;
    const unsigned char* loca;
    const unsigned char* glyf;
    const unsigned char* maxp;
    unsigned long cmap_size;
    unsigned long head_size;
    unsigned long hhea_size;
    unsigned long hmtx_size;
    unsigned long loca_size;
    unsigned long glyf_size;
    unsigned long maxp_size;
    const unsigned char* selected_cmap;
    unsigned long selected_cmap_size;
    unsigned short selected_cmap_format;
    long units_per_em;
    long ascent_units;
    long descent_units;
    long line_gap_units;
    long em_height;
    long requested_height;
    long scale_16;

    if (!handle || !buffer || size < 12UL) {
        gdi_font_debug_line("gdi.dll: ttf init invalid input");
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    if (!(gdi_font_be_u32(buffer) == 0x00010000UL || gdi_font_be_u32(buffer) == GDI_FONT_TTF_TAG('t', 'r', 'u', 'e'))) {
        gdi_font_debug_line("gdi.dll: ttf init bad sfnt header");
        return -61L;
    }
    if (!gdi_ttf_find_table(buffer, size, GDI_FONT_TTF_TAG('c', 'm', 'a', 'p'), &cmap, &cmap_size)) {
        gdi_font_debug_line("gdi.dll: ttf init missing cmap");
        return -62L;
    }
    if (!gdi_ttf_find_table(buffer, size, GDI_FONT_TTF_TAG('h', 'e', 'a', 'd'), &head, &head_size)) {
        gdi_font_debug_line("gdi.dll: ttf init missing head");
        return -63L;
    }
    if (!gdi_ttf_find_table(buffer, size, GDI_FONT_TTF_TAG('h', 'h', 'e', 'a'), &hhea, &hhea_size)) {
        gdi_font_debug_line("gdi.dll: ttf init missing hhea");
        return -64L;
    }
    if (!gdi_ttf_find_table(buffer, size, GDI_FONT_TTF_TAG('h', 'm', 't', 'x'), &hmtx, &hmtx_size)) {
        gdi_font_debug_line("gdi.dll: ttf init missing hmtx");
        return -65L;
    }
    if (!gdi_ttf_find_table(buffer, size, GDI_FONT_TTF_TAG('l', 'o', 'c', 'a'), &loca, &loca_size)) {
        gdi_font_debug_line("gdi.dll: ttf init missing loca");
        return -66L;
    }
    if (!gdi_ttf_find_table(buffer, size, GDI_FONT_TTF_TAG('g', 'l', 'y', 'f'), &glyf, &glyf_size)) {
        gdi_font_debug_line("gdi.dll: ttf init missing glyf");
        return -67L;
    }
    if (!gdi_ttf_find_table(buffer, size, GDI_FONT_TTF_TAG('m', 'a', 'x', 'p'), &maxp, &maxp_size)) {
        gdi_font_debug_line("gdi.dll: ttf init missing maxp");
        return -68L;
    }
    if (head_size < 54UL || hhea_size < 36UL || maxp_size < 6UL) {
        gdi_font_debug_line("gdi.dll: ttf init short required table");
        return -69L;
    }
    if (!gdi_ttf_select_cmap(cmap, cmap_size, &selected_cmap, &selected_cmap_size, &selected_cmap_format)) {
        gdi_font_debug_line("gdi.dll: ttf init no supported cmap");
        return -70L;
    }

    units_per_em = gdi_font_be_u16(head + 18UL);
    ascent_units = gdi_font_be_s16(hhea + 4UL);
    descent_units = gdi_font_be_s16(hhea + 6UL);
    line_gap_units = gdi_font_be_s16(hhea + 8UL);
    em_height = ascent_units - descent_units;
    if (units_per_em <= 0L || em_height <= 0L) {
        gdi_font_debug_line("gdi.dll: ttf init invalid vertical metrics");
        return -71L;
    }

    requested_height = (long)(pixel_height ? pixel_height : GDI_FONT_PIXEL_HEIGHT_DEFAULT);
    scale_16 = (long)((((unsigned long long)requested_height) << 16) / (unsigned long long)em_height);
    if (scale_16 <= 0L) {
        gdi_font_debug_line("gdi.dll: ttf init invalid scale");
        return -72L;
    }

    handle->kind = GDI_FONT_KIND_TRUETYPE;
    handle->pixel_height = (unsigned long)requested_height;
    handle->ascent = gdi_font_scale_metric_signed(ascent_units, scale_16);
    handle->descent = gdi_font_scale_metric_signed(-descent_units, scale_16);
    handle->line_gap = gdi_font_scale_metric_signed(line_gap_units, scale_16);
    handle->line_height = (unsigned long)(handle->ascent + handle->descent + handle->line_gap);
    if (handle->line_height == 0UL) {
        handle->line_height = (unsigned long)requested_height;
    }

    handle->data.ttf.data = buffer;
    handle->data.ttf.size = size;
    handle->data.ttf.cmap = selected_cmap;
    handle->data.ttf.cmap_size = selected_cmap_size;
    handle->data.ttf.cmap_format = selected_cmap_format;
    handle->data.ttf.head = head;
    handle->data.ttf.hhea = hhea;
    handle->data.ttf.hmtx = hmtx;
    handle->data.ttf.loca = loca;
    handle->data.ttf.loca_size = loca_size;
    handle->data.ttf.glyf = glyf;
    handle->data.ttf.glyf_size = glyf_size;
    handle->data.ttf.units_per_em = (unsigned short)units_per_em;
    handle->data.ttf.num_glyphs = (unsigned short)gdi_font_be_u16(maxp + 4UL);
    handle->data.ttf.num_hmetrics = (unsigned short)gdi_font_be_u16(hhea + 34UL);
    handle->data.ttf.ascent_units = (short)ascent_units;
    handle->data.ttf.descent_units = (short)descent_units;
    handle->data.ttf.line_gap_units = (short)line_gap_units;
    handle->data.ttf.index_to_loc_format = (short)gdi_font_be_s16(head + 50UL);
    handle->data.ttf.scale_16 = scale_16;
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Resolve one BMP codepoint through a cmap format 4 subtable.
 *
 * @param cmap Format 4 subtable base.
 * @param cmap_size Format 4 subtable size.
 * @param codepoint Unicode codepoint to resolve.
 * @return Glyph index, or zero when the mapping is absent.
 */
static unsigned long gdi_ttf_find_glyph_format4(const unsigned char* cmap, unsigned long cmap_size, unsigned long codepoint) {
    unsigned long seg_count;
    unsigned long index;

    if (!cmap || cmap_size < 24UL || codepoint > 0xFFFFUL) {
        return 0UL;
    }

    seg_count = gdi_font_be_u16(cmap + 6UL) / 2UL;
    if (cmap_size < (16UL + (seg_count * 8UL))) {
        return 0UL;
    }

    for (index = 0UL; index < seg_count; ++index) {
        const unsigned char* end_codes = cmap + 14UL;
        const unsigned char* start_codes = end_codes + (seg_count * 2UL) + 2UL;
        const unsigned char* id_deltas = start_codes + (seg_count * 2UL);
        const unsigned char* id_range_offsets = id_deltas + (seg_count * 2UL);
        unsigned long end_code = gdi_font_be_u16(end_codes + (index * 2UL));
        unsigned long start_code = gdi_font_be_u16(start_codes + (index * 2UL));
        unsigned long range_offset = gdi_font_be_u16(id_range_offsets + (index * 2UL));
        long delta = gdi_font_be_s16(id_deltas + (index * 2UL));

        if (codepoint < start_code || codepoint > end_code) {
            continue;
        }
        if (range_offset == 0UL) {
            return (unsigned long)((codepoint + (unsigned long)delta) & 0xFFFFUL);
        }

        {
            const unsigned char* glyph_offset_base = id_range_offsets + (index * 2UL);
            unsigned long glyph_offset = range_offset + ((codepoint - start_code) * 2UL);

            if (glyph_offset > (unsigned long)(cmap_size - (unsigned long)(glyph_offset_base - cmap))) {
                return 0UL;
            }

            {
                unsigned long glyph_index = gdi_font_be_u16(glyph_offset_base + glyph_offset);

                if (glyph_index == 0UL) {
                    return 0UL;
                }
                return (unsigned long)((glyph_index + (unsigned long)delta) & 0xFFFFUL);
            }
        }
    }

    return 0UL;
}

/*
 * Resolve one Unicode codepoint through a cmap format 12 subtable.
 *
 * @param cmap Format 12 subtable base.
 * @param cmap_size Format 12 subtable size.
 * @param codepoint Unicode codepoint to resolve.
 * @return Glyph index, or zero when the mapping is absent.
 */
static unsigned long gdi_ttf_find_glyph_format12(const unsigned char* cmap, unsigned long cmap_size, unsigned long codepoint) {
    unsigned long group_count;
    unsigned long index;

    if (!cmap || cmap_size < 16UL) {
        return 0UL;
    }

    group_count = gdi_font_be_u32(cmap + 12UL);
    if (cmap_size < (16UL + (group_count * 12UL))) {
        return 0UL;
    }

    for (index = 0UL; index < group_count; ++index) {
        const unsigned char* group = cmap + 16UL + (index * 12UL);
        unsigned long start = gdi_font_be_u32(group);
        unsigned long end = gdi_font_be_u32(group + 4UL);
        unsigned long glyph_start = gdi_font_be_u32(group + 8UL);

        if (codepoint < start || codepoint > end) {
            continue;
        }

        return glyph_start + (codepoint - start);
    }

    return 0UL;
}

/*
 * Resolve one byte-oriented UI character to a glyph index.
 *
 * @param font Loaded TrueType font state.
 * @param codepoint Current byte treated as a Unicode codepoint.
 * @return Glyph index, or zero when the mapping is absent.
 */
static unsigned long gdi_ttf_find_glyph_index(const GdiTtfFontState* font, unsigned long codepoint) {
    if (!font) {
        return 0UL;
    }

    if (font->cmap_format == 4U) {
        return gdi_ttf_find_glyph_format4(font->cmap, font->cmap_size, codepoint);
    }
    if (font->cmap_format == 12U) {
        return gdi_ttf_find_glyph_format12(font->cmap, font->cmap_size, codepoint);
    }

    return 0UL;
}

/*
 * Resolve horizontal metrics for one glyph index.
 *
 * @param font Loaded TrueType font state.
 * @param glyph_index Glyph index to query.
 * @param advance Receives the advance width in font units.
 * @param left_side_bearing Receives the left-side bearing in font units.
 * @return Non-zero when the metrics were found.
 */
static int gdi_ttf_get_hmetrics(const GdiTtfFontState* font, unsigned long glyph_index, long* advance, long* left_side_bearing) {
    unsigned long metric_index;

    if (!font || !advance || !left_side_bearing || glyph_index >= font->num_glyphs || font->num_hmetrics == 0U) {
        return 0;
    }

    metric_index = glyph_index;
    if (metric_index >= font->num_hmetrics) {
        metric_index = font->num_hmetrics - 1UL;
    }

    *advance = (long)gdi_font_be_u16(font->hmtx + (metric_index * 4UL));
    if (glyph_index < font->num_hmetrics) {
        *left_side_bearing = gdi_font_be_s16(font->hmtx + (metric_index * 4UL) + 2UL);
    }
    else {
        unsigned long extra_index = glyph_index - font->num_hmetrics;

        *left_side_bearing = gdi_font_be_s16(font->hmtx + (font->num_hmetrics * 4UL) + (extra_index * 2UL));
    }

    return 1;
}

/*
 * Resolve the glyf byte range for one glyph index.
 *
 * @param font Loaded TrueType font state.
 * @param glyph_index Glyph index to query.
 * @param glyph_storage Receives a temporary heap buffer when the glyph bytes
 * need to be read from disk.
 * @param glyph Receives the glyph data pointer.
 * @param glyph_size Receives the glyph byte size.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_ttf_get_glyph_bytes(const GdiTtfFontState* font, unsigned long glyph_index, unsigned char** glyph_storage, const unsigned char** glyph, unsigned long* glyph_size) {
    unsigned long start_offset;
    unsigned long end_offset;

    if (!font || !glyph_storage || !glyph || !glyph_size || glyph_index >= font->num_glyphs) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    if (font->index_to_loc_format == 0) {
        if (((glyph_index + 1UL) * 2UL) > font->loca_size) {
            return GDI_FONT_STATUS_FORMAT;
        }
        start_offset = gdi_font_be_u16(font->loca + (glyph_index * 2UL)) * 2UL;
        end_offset = gdi_font_be_u16(font->loca + ((glyph_index + 1UL) * 2UL)) * 2UL;
    }
    else {
        if (((glyph_index + 1UL) * 4UL) > font->loca_size) {
            return GDI_FONT_STATUS_FORMAT;
        }
        start_offset = gdi_font_be_u32(font->loca + (glyph_index * 4UL));
        end_offset = gdi_font_be_u32(font->loca + ((glyph_index + 1UL) * 4UL));
    }
    if (end_offset < start_offset || end_offset > font->glyf_size) {
        return GDI_FONT_STATUS_FORMAT;
    }

    *glyph_storage = 0;
    *glyph_size = end_offset - start_offset;
    if (*glyph_size == 0UL) {
        *glyph = 0;
        return ROS_USER_IPC_STATUS_OK;
    }

    if (font->glyf) {
        *glyph = font->glyf + start_offset;
        return ROS_USER_IPC_STATUS_OK;
    }
    if (font->source_path[0] == '\0') {
        return GDI_FONT_STATUS_FORMAT;
    }

    *glyph_storage = (unsigned char*)malloc(*glyph_size);
    if (!*glyph_storage) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    if (gdi_font_read_file_exact(font->source_path, font->glyf_file_offset + start_offset, *glyph_storage, *glyph_size) < 0L) {
        free(*glyph_storage);
        *glyph_storage = 0;
        return GDI_FONT_STATUS_IO;
    }

    *glyph = *glyph_storage;
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Convert one design-space coordinate to 16.16 pixel space.
 *
 * @param value Font-units coordinate.
 * @param scale_16 16.16 scale factor.
 * @return Fixed-point pixel coordinate.
 */
static long gdi_ttf_scale_point(long value, long scale_16) {
    return (long)((((long long)value * (long long)scale_16) + 0x8000LL) >> 16);
}

/*
 * Append one line segment to a temporary segment builder.
 *
 * @param builder Target segment builder.
 * @param x0 First point X coordinate.
 * @param y0 First point Y coordinate.
 * @param x1 Second point X coordinate.
 * @param y1 Second point Y coordinate.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_ttf_append_segment(GdiTtfSegmentBuilder* builder, long x0, long y0, long x1, long y1) {
    if (!builder || !builder->segments || builder->count >= builder->capacity) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    builder->segments[builder->count].x0 = x0;
    builder->segments[builder->count].y0 = y0;
    builder->segments[builder->count].x1 = x1;
    builder->segments[builder->count].y1 = y1;
    ++builder->count;
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Subdivide one quadratic Bezier curve until it is flat enough for scan fill.
 *
 * @param builder Target segment builder.
 * @param x0 Curve start X coordinate in 16.16 pixels.
 * @param y0 Curve start Y coordinate in 16.16 pixels.
 * @param cx Curve control-point X coordinate.
 * @param cy Curve control-point Y coordinate.
 * @param x1 Curve end X coordinate.
 * @param y1 Curve end Y coordinate.
 * @param depth Current recursive subdivision depth.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_ttf_flatten_quad(GdiTtfSegmentBuilder* builder, long x0, long y0, long cx, long cy, long x1, long y1, unsigned long depth) {
    long flatness_x = x0 - (2L * cx) + x1;
    long flatness_y = y0 - (2L * cy) + y1;
    long flatness = flatness_x < 0L ? -flatness_x : flatness_x;
    long flatness_y_abs = flatness_y < 0L ? -flatness_y : flatness_y;

    if (flatness_y_abs > flatness) {
        flatness = flatness_y_abs;
    }
    if (flatness <= 4096L || depth >= 8UL) {
        return gdi_ttf_append_segment(builder, x0, y0, x1, y1);
    }

    {
        long x01 = (x0 + cx) / 2L;
        long y01 = (y0 + cy) / 2L;
        long x12 = (cx + x1) / 2L;
        long y12 = (cy + y1) / 2L;
        long x012 = (x01 + x12) / 2L;
        long y012 = (y01 + y12) / 2L;
        long status = gdi_ttf_flatten_quad(builder, x0, y0, x01, y01, x012, y012, depth + 1UL);

        if (status < 0L) {
            return status;
        }
        return gdi_ttf_flatten_quad(builder, x012, y012, x12, y12, x1, y1, depth + 1UL);
    }
}

/*
 * Build a flattened segment list for one simple TrueType glyph outline.
 *
 * @param glyph Glyph table bytes.
 * @param glyph_size Glyph byte size.
 * @param scale_16 Font scale factor.
 * @param builder Destination segment builder.
 * @param min_x Receives the left-most fixed-point X coordinate.
 * @param min_y Receives the top-most fixed-point Y coordinate.
 * @param max_x Receives the right-most fixed-point X coordinate.
 * @param max_y Receives the bottom-most fixed-point Y coordinate.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_ttf_build_simple_segments(const unsigned char* glyph, unsigned long glyph_size, long scale_16, GdiTtfSegmentBuilder* builder, long* min_x, long* min_y, long* max_x, long* max_y) {
    long contour_count;
    unsigned long point_count;
    const unsigned char* cursor;
    unsigned long* end_points;
    unsigned char* flags;
    GdiPointFx* points;
    GdiPointFx* work_points;
    unsigned long point_index;
    unsigned long contour_start;
    unsigned long contour_index;
    long status = ROS_USER_IPC_STATUS_OK;

    if (!glyph || glyph_size < 10UL || !builder || !min_x || !min_y || !max_x || !max_y) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    contour_count = gdi_font_be_s16(glyph);
    if (contour_count < 0L) {
        return GDI_FONT_STATUS_NOT_SUPPORTED;
    }
    if (contour_count == 0L) {
        *min_x = 0L;
        *min_y = 0L;
        *max_x = 0L;
        *max_y = 0L;
        return ROS_USER_IPC_STATUS_OK;
    }

    if (glyph_size < (10UL + ((unsigned long)contour_count * 2UL))) {
        return GDI_FONT_STATUS_FORMAT;
    }

    end_points = (unsigned long*)malloc(sizeof(unsigned long) * (unsigned long)contour_count);
    if (!end_points) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    for (point_index = 0UL; point_index < (unsigned long)contour_count; ++point_index) {
        end_points[point_index] = gdi_font_be_u16(glyph + 10UL + (point_index * 2UL));
    }
    point_count = end_points[(unsigned long)contour_count - 1UL] + 1UL;
    cursor = glyph + 10UL + ((unsigned long)contour_count * 2UL);
    if ((cursor - glyph) + 2UL > glyph_size) {
        free(end_points);
        return GDI_FONT_STATUS_FORMAT;
    }

    cursor += 2UL + gdi_font_be_u16(cursor);
    if ((unsigned long)(cursor - glyph) > glyph_size) {
        free(end_points);
        return GDI_FONT_STATUS_FORMAT;
    }

    flags = (unsigned char*)malloc(point_count ? point_count : 1UL);
    points = (GdiPointFx*)malloc(sizeof(GdiPointFx) * (point_count ? point_count : 1UL));
    work_points = (GdiPointFx*)malloc(sizeof(GdiPointFx) * ((point_count * 2UL) + 4UL));
    if (!flags || !points || !work_points) {
        free(end_points);
        free(flags);
        free(points);
        free(work_points);
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    for (point_index = 0UL; point_index < point_count;) {
        unsigned char flag;
        unsigned long repeat = 0UL;

        if ((unsigned long)(cursor - glyph) >= glyph_size) {
            status = GDI_FONT_STATUS_FORMAT;
            break;
        }

        flag = *cursor++;
        if ((flag & GDI_TTF_FLAG_REPEAT) != 0U) {
            if ((unsigned long)(cursor - glyph) >= glyph_size) {
                status = GDI_FONT_STATUS_FORMAT;
                break;
            }
            repeat = (unsigned long)(*cursor++);
        }

        do {
            flags[point_index++] = flag;
        } while (repeat-- != 0UL && point_index < point_count);
    }

    if (status >= 0L) {
        long coordinate = 0L;

        for (point_index = 0UL; point_index < point_count; ++point_index) {
            unsigned char flag = flags[point_index];
            long delta = 0L;

            if ((flag & GDI_TTF_FLAG_X_SHORT) != 0U) {
                if ((unsigned long)(cursor - glyph) >= glyph_size) {
                    status = GDI_FONT_STATUS_FORMAT;
                    break;
                }
                delta = (long)(*cursor++);
                if ((flag & GDI_TTF_FLAG_X_SAME) == 0U) {
                    delta = -delta;
                }
            }
            else if ((flag & GDI_TTF_FLAG_X_SAME) == 0U) {
                if (((unsigned long)(cursor - glyph) + 2UL) > glyph_size) {
                    status = GDI_FONT_STATUS_FORMAT;
                    break;
                }
                delta = gdi_font_be_s16(cursor);
                cursor += 2UL;
            }

            coordinate += delta;
            points[point_index].x = gdi_ttf_scale_point(coordinate, scale_16);
            points[point_index].on_curve = ((flag & GDI_TTF_FLAG_ON_CURVE) != 0U) ? 1 : 0;
        }

        coordinate = 0L;
        for (point_index = 0UL; status >= 0L && point_index < point_count; ++point_index) {
            unsigned char flag = flags[point_index];
            long delta = 0L;

            if ((flag & GDI_TTF_FLAG_Y_SHORT) != 0U) {
                if ((unsigned long)(cursor - glyph) >= glyph_size) {
                    status = GDI_FONT_STATUS_FORMAT;
                    break;
                }
                delta = (long)(*cursor++);
                if ((flag & GDI_TTF_FLAG_Y_SAME) == 0U) {
                    delta = -delta;
                }
            }
            else if ((flag & GDI_TTF_FLAG_Y_SAME) == 0U) {
                if (((unsigned long)(cursor - glyph) + 2UL) > glyph_size) {
                    status = GDI_FONT_STATUS_FORMAT;
                    break;
                }
                delta = gdi_font_be_s16(cursor);
                cursor += 2UL;
            }

            coordinate += delta;
            points[point_index].y = -gdi_ttf_scale_point(coordinate, scale_16);
        }
    }

    if (status >= 0L && point_count != 0UL) {
        *min_x = points[0].x;
        *max_x = points[0].x;
        *min_y = points[0].y;
        *max_y = points[0].y;

        for (point_index = 1UL; point_index < point_count; ++point_index) {
            if (points[point_index].x < *min_x) {
                *min_x = points[point_index].x;
            }
            if (points[point_index].x > *max_x) {
                *max_x = points[point_index].x;
            }
            if (points[point_index].y < *min_y) {
                *min_y = points[point_index].y;
            }
            if (points[point_index].y > *max_y) {
                *max_y = points[point_index].y;
            }
        }
    }

    contour_start = 0UL;
    for (contour_index = 0UL; status >= 0L && contour_index < (unsigned long)contour_count; ++contour_index) {
        unsigned long contour_end = end_points[contour_index];
        unsigned long raw_count = contour_end - contour_start + 1UL;
        unsigned long work_count = 0UL;
        unsigned long work_index;

        if (raw_count == 0UL) {
            contour_start = contour_end + 1UL;
            continue;
        }

        if (!points[contour_start].on_curve) {
            if (!points[contour_end].on_curve) {
                work_points[work_count].x = (points[contour_end].x + points[contour_start].x) / 2L;
                work_points[work_count].y = (points[contour_end].y + points[contour_start].y) / 2L;
                work_points[work_count].on_curve = 1;
                ++work_count;
            }
            else {
                work_points[work_count++] = points[contour_end];
            }
        }

        for (point_index = 0UL; point_index < raw_count; ++point_index) {
            unsigned long current_index = contour_start + point_index;
            unsigned long next_index = contour_start + ((point_index + 1UL) % raw_count);

            work_points[work_count++] = points[current_index];
            if (!points[current_index].on_curve && !points[next_index].on_curve) {
                work_points[work_count].x = (points[current_index].x + points[next_index].x) / 2L;
                work_points[work_count].y = (points[current_index].y + points[next_index].y) / 2L;
                work_points[work_count].on_curve = 1;
                ++work_count;
            }
        }

        if (work_count == 0UL || !work_points[0].on_curve) {
            status = GDI_FONT_STATUS_FORMAT;
            break;
        }

        for (work_index = 0UL; work_index < work_count; ++work_index) {
            GdiPointFx p0 = work_points[work_index];
            GdiPointFx p1 = work_points[(work_index + 1UL) % work_count];

            if (p0.on_curve && p1.on_curve) {
                status = gdi_ttf_append_segment(builder, p0.x, p0.y, p1.x, p1.y);
            }
            else if (p0.on_curve && !p1.on_curve) {
                GdiPointFx p2 = work_points[(work_index + 2UL) % work_count];

                if (!p2.on_curve) {
                    status = GDI_FONT_STATUS_FORMAT;
                }
                else {
                    status = gdi_ttf_flatten_quad(builder, p0.x, p0.y, p1.x, p1.y, p2.x, p2.y, 0UL);
                    ++work_index;
                }
            }

            if (status < 0L) {
                break;
            }
        }

        contour_start = contour_end + 1UL;
    }

    free(end_points);
    free(flags);
    free(points);
    free(work_points);
    return status;
}

/*
 * Return the floor of one signed 16.16 fixed-point value.
 *
 * @param value Signed 16.16 fixed-point value.
 * @return Integer floor.
 */
static long gdi_ttf_fx_floor(long value) {
    if (value >= 0L) {
        return value >> 16;
    }

    return -(((-value) + 0xFFFFL) >> 16);
}

/*
 * Return the ceil of one signed 16.16 fixed-point value.
 *
 * @param value Signed 16.16 fixed-point value.
 * @return Integer ceil.
 */
static long gdi_ttf_fx_ceil(long value) {
    if (value >= 0L) {
        return (value + 0xFFFFL) >> 16;
    }

    return -((-value) >> 16);
}

/*
 * Test whether one sample point falls inside an outline using even-odd fill.
 *
 * @param builder Flattened outline segment builder.
 * @param sample_x Sample X coordinate in 16.16 pixels.
 * @param sample_y Sample Y coordinate in 16.16 pixels.
 * @return Non-zero when the sample is inside the outline.
 */
static int gdi_ttf_point_inside(const GdiTtfSegmentBuilder* builder, long sample_x, long sample_y) {
    unsigned long crossings = 0UL;
    unsigned long index;

    if (!builder) {
        return 0;
    }

    for (index = 0UL; index < builder->count; ++index) {
        long y0 = builder->segments[index].y0;
        long y1 = builder->segments[index].y1;

        if (((y0 > sample_y) && (y1 > sample_y)) || ((y0 <= sample_y) && (y1 <= sample_y)) || y0 == y1) {
            continue;
        }

        {
            long x0 = builder->segments[index].x0;
            long x1 = builder->segments[index].x1;
            long intersection = x0 + (long)((((long long)(sample_y - y0)) * (long long)(x1 - x0)) / (long long)(y1 - y0));

            if (intersection > sample_x) {
                ++crossings;
            }
        }
    }

    return (crossings & 1UL) != 0UL;
}

/*
 * Rasterize one simple TrueType glyph into an alpha bitmap cache entry.
 *
 * @param font Loaded TrueType font state.
 * @param codepoint ASCII UI byte treated as a Unicode codepoint.
 * @param glyph Destination glyph cache entry.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_ttf_rasterize_glyph(const GdiTtfFontState* font, unsigned long codepoint, GdiGlyphBitmap* glyph) {
    unsigned long glyph_index;
    long advance_units;
    long left_side_bearing_units;
    unsigned char* glyph_storage = 0;
    const unsigned char* glyph_bytes;
    unsigned long glyph_size;
    GdiTtfSegmentBuilder builder;
    long min_x_fx = 0L;
    long min_y_fx = 0L;
    long max_x_fx = 0L;
    long max_y_fx = 0L;
    long status;

    if (!font || !glyph) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    glyph_index = gdi_ttf_find_glyph_index(font, codepoint);
    if (!gdi_ttf_get_hmetrics(font, glyph_index, &advance_units, &left_side_bearing_units)) {
        return GDI_FONT_STATUS_FORMAT;
    }
    status = gdi_ttf_get_glyph_bytes(font, glyph_index, &glyph_storage, &glyph_bytes, &glyph_size);
    if (status < 0L) {
        return status;
    }

    glyph->advance = gdi_font_scale_metric((unsigned long)(advance_units > 0L ? advance_units : 0L), font->scale_16);
    glyph->bitmap_top = gdi_font_scale_metric_signed(font->ascent_units, font->scale_16);
    glyph->bitmap_left = gdi_font_scale_metric_signed(left_side_bearing_units, font->scale_16);

    if (glyph_size == 0UL) {
        free(glyph_storage);
        glyph->ready = 1;
        glyph->valid = 1;
        return ROS_USER_IPC_STATUS_OK;
    }

    builder.capacity = ((glyph_size / 2UL) * 24UL) + 64UL;
    builder.count = 0UL;
    builder.segments = (GdiLineSegmentFx*)malloc(sizeof(GdiLineSegmentFx) * builder.capacity);
    if (!builder.segments) {
        free(glyph_storage);
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    status = gdi_ttf_build_simple_segments(glyph_bytes, glyph_size, font->scale_16, &builder, &min_x_fx, &min_y_fx, &max_x_fx, &max_y_fx);
    if (status >= 0L && builder.count != 0UL) {
        long min_x = gdi_ttf_fx_floor(min_x_fx);
        long min_y = gdi_ttf_fx_floor(min_y_fx);
        long max_x = gdi_ttf_fx_ceil(max_x_fx);
        long max_y = gdi_ttf_fx_ceil(max_y_fx);
        unsigned long width = (max_x > min_x) ? (unsigned long)(max_x - min_x) : 0UL;
        unsigned long height = (max_y > min_y) ? (unsigned long)(max_y - min_y) : 0UL;

        glyph->bitmap_left = min_x;
        glyph->bitmap_top = -min_y;
        glyph->width = width;
        glyph->height = height;
        if (width != 0UL && height != 0UL) {
            unsigned long pixel_count = width * height;
            unsigned long pixel_index;
            unsigned char* coverage = (unsigned char*)malloc(pixel_count);

            if (!coverage) {
                status = ROS_USER_IPC_STATUS_NO_SPACE;
            }
            else {
                static const long sample_offsets[4] = {
                    (1L << 16) / 8L,
                    (3L << 16) / 8L,
                    (5L << 16) / 8L,
                    (7L << 16) / 8L,
                };

                for (pixel_index = 0UL; pixel_index < pixel_count; ++pixel_index) {
                    coverage[pixel_index] = 0U;
                }

                for (unsigned long row = 0UL; row < height; ++row) {
                    for (unsigned long column = 0UL; column < width; ++column) {
                        unsigned long hits = 0UL;

                        for (unsigned long sample_y = 0UL; sample_y < 4UL; ++sample_y) {
                            long probe_y = ((min_y + (long)row) << 16) + sample_offsets[sample_y];

                            for (unsigned long sample_x = 0UL; sample_x < 4UL; ++sample_x) {
                                long probe_x = ((min_x + (long)column) << 16) + sample_offsets[sample_x];

                                if (gdi_ttf_point_inside(&builder, probe_x, probe_y)) {
                                    ++hits;
                                }
                            }
                        }

                        coverage[(row * width) + column] = (unsigned char)((hits * 255UL) / 16UL);
                    }
                }

                glyph->coverage = coverage;
                glyph->coverage_owned = 1;
            }
        }
    }

    free(builder.segments);
    free(glyph_storage);
    if (status < 0L) {
        return status;
    }

    glyph->ready = 1;
    glyph->valid = 1;
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Rasterize one descriptor-backed mini font glyph into an alpha bitmap cache.
 *
 * @param font Parsed descriptor-backed font handle.
 * @param codepoint ASCII UI byte.
 * @param glyph Destination glyph cache entry.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_mini_rasterize_glyph(const GdiMiniFontDescriptor* font, unsigned long codepoint, GdiGlyphBitmap* glyph) {
    const RosMiniFontGlyph* mini_glyph;
    const uint8_t* mini_coverage;
    unsigned long pixel_count;
    unsigned long index;

    if (!font || !glyph) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    mini_glyph = rosMiniFontGlyph(codepoint);
    mini_coverage = rosMiniFontGlyphCoverage(mini_glyph);
    glyph->bitmap_left = mini_glyph->bitmap_left;
    glyph->bitmap_top = mini_glyph->bitmap_top;
    glyph->width = mini_glyph->width;
    glyph->height = mini_glyph->height;
    glyph->advance = mini_glyph->advance;
    pixel_count = glyph->width * glyph->height;
    if (pixel_count == 0UL) {
        glyph->ready = 1;
        glyph->valid = 1;
        return ROS_USER_IPC_STATUS_OK;
    }

    glyph->coverage = (unsigned char*)malloc(pixel_count);
    if (!glyph->coverage) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }
    glyph->coverage_owned = 1;

    for (index = 0UL; index < pixel_count; ++index) {
        glyph->coverage[index] = mini_coverage[index];
    }

    glyph->ready = 1;
    glyph->valid = 1;
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Build one glyph bitmap for the given ASCII UI byte.
 *
 * @param handle Loaded font handle.
 * @param codepoint ASCII UI byte.
 * @param glyph Destination glyph cache entry.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_font_build_glyph(const GdiFontHandle* handle, unsigned long codepoint, GdiGlyphBitmap* glyph) {
    if (!handle || !glyph) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    if (handle->kind == GDI_FONT_KIND_MINI) {
        return gdi_mini_rasterize_glyph(&handle->data.mini, codepoint, glyph);
    }
    if (handle->kind == GDI_FONT_KIND_TRUETYPE) {
        return gdi_ttf_rasterize_glyph(&handle->data.ttf, codepoint, glyph);
    }

    return GDI_FONT_STATUS_NOT_SUPPORTED;
}

/*
 * Acquire one cached glyph for the current byte-oriented UI character.
 *
 * @param font Public font wrapper.
 * @param codepoint ASCII UI byte.
 * @param glyph Receives a stable cache pointer.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_font_get_glyph(const RosGdiFont* font, unsigned long codepoint, const GdiGlyphBitmap** glyph) {
    GdiFontHandle* handle;
    unsigned long glyph_index;
    long status;

    if (!font || !font->handle || !glyph) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    handle = (GdiFontHandle*)font->handle;
    status = gdi_font_ensure_glyph_cache(handle);
    if (status < 0L) {
        return status;
    }

    glyph_index = codepoint;
    if (glyph_index >= GDI_FONT_ASCII_CACHE_SIZE) {
        glyph_index = (unsigned long)'?';
    }

    if (!handle->glyphs[glyph_index].ready) {
        status = gdi_font_build_glyph(handle, glyph_index, &handle->glyphs[glyph_index]);
        if (status < 0L && glyph_index != (unsigned long)'?') {
            glyph_index = (unsigned long)'?';
            if (!handle->glyphs[glyph_index].ready) {
                status = gdi_font_build_glyph(handle, glyph_index, &handle->glyphs[glyph_index]);
            }
        }
        if (status < 0L) {
            return status;
        }
    }

    *glyph = &handle->glyphs[glyph_index];
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Decode one surface pixel into RGB components.
 *
 * @param pixel_format Surface pixel format.
 * @param encoded Encoded pixel value as stored in the surface.
 * @param red Receives the red component.
 * @param green Receives the green component.
 * @param blue Receives the blue component.
 * @return Nothing.
 */
static void gdi_surface_decode_pixel(unsigned long pixel_format, unsigned long encoded, unsigned long* red, unsigned long* green, unsigned long* blue) {
    if (!red || !green || !blue) {
        return;
    }

    if (pixel_format == ROS_KERNEL_GUI_PIXEL_FORMAT_XBGR8888) {
        *red = encoded & 0xFFUL;
        *green = (encoded >> 8) & 0xFFUL;
        *blue = (encoded >> 16) & 0xFFUL;
    }
    else {
        *red = (encoded >> 16) & 0xFFUL;
        *green = (encoded >> 8) & 0xFFUL;
        *blue = encoded & 0xFFUL;
    }
}

/*
 * Blend one RGB pixel into the target surface with 8-bit alpha.
 *
 * @param surface Target mapped surface.
 * @param x Pixel X coordinate.
 * @param y Pixel Y coordinate.
 * @param color Source RGB color packed as 0x00RRGGBB.
 * @param alpha Source alpha from 0 to 255.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_surface_blend_pixel(const RosGdiSurface* surface, unsigned long x, unsigned long y, unsigned long color, unsigned long alpha) {
    unsigned int* pixel;

    if (!surface || !surface->pixels || x >= surface->width || y >= surface->height) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }
    if (alpha == 0UL) {
        return ROS_USER_IPC_STATUS_OK;
    }

    pixel = (unsigned int*)((unsigned char*)surface->pixels + (y * surface->pitch)) + x;
    if (alpha >= 255UL) {
        *pixel = (unsigned int)gdi_encode_color(surface->pixel_format, color);
        return ROS_USER_IPC_STATUS_OK;
    }

    {
        unsigned long src_red = (color >> 16) & 0xFFUL;
        unsigned long src_green = (color >> 8) & 0xFFUL;
        unsigned long src_blue = color & 0xFFUL;
        unsigned long dst_red;
        unsigned long dst_green;
        unsigned long dst_blue;
        unsigned long inv_alpha = 255UL - alpha;
        unsigned long out_red;
        unsigned long out_green;
        unsigned long out_blue;
        unsigned long packed;

        gdi_surface_decode_pixel(surface->pixel_format, *pixel, &dst_red, &dst_green, &dst_blue);
        out_red = ((src_red * alpha) + (dst_red * inv_alpha) + 127UL) / 255UL;
        out_green = ((src_green * alpha) + (dst_green * inv_alpha) + 127UL) / 255UL;
        out_blue = ((src_blue * alpha) + (dst_blue * inv_alpha) + 127UL) / 255UL;
        packed = (out_red << 16) | (out_green << 8) | out_blue;
        *pixel = (unsigned int)gdi_encode_color(surface->pixel_format, packed);
    }

    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Load one TrueType font while keeping only the small metric tables resident.
 *
 * The full `glyf` table in desktop fonts is often much larger than the rest of
 * the file combined. Keeping only the directory, metrics tables, and cmap in
 * memory lets userspace apps load real TTF files inside the current heap limit,
 * and individual glyph outlines are pulled from disk later on demand.
 *
 * @param path Absolute font path.
 * @param pixel_height Requested rendered height.
 * @param font Receives the public font wrapper.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_font_load_ttf_stream(const char* path, unsigned long pixel_height, RosGdiFont* font) {
    char path_copy[128];
    unsigned char header[12];
    unsigned char* directory = 0;
    unsigned char* table_bundle = 0;
    GdiFontHandle* handle = 0;
    unsigned long file_size = 0UL;
    unsigned long directory_size;
    unsigned long cmap_offset;
    unsigned long cmap_size;
    unsigned long head_offset;
    unsigned long head_size;
    unsigned long hhea_offset;
    unsigned long hhea_size;
    unsigned long hmtx_offset;
    unsigned long hmtx_size;
    unsigned long loca_offset;
    unsigned long loca_size;
    unsigned long glyf_offset;
    unsigned long glyf_size;
    unsigned long maxp_offset;
    unsigned long maxp_size;
    unsigned long bundle_size;
    unsigned char* cursor;
    const unsigned char* cmap;
    const unsigned char* head;
    const unsigned char* hhea;
    const unsigned char* hmtx;
    const unsigned char* loca;
    const unsigned char* maxp;
    const unsigned char* selected_cmap;
    unsigned long selected_cmap_size;
    unsigned short selected_cmap_format;
    long units_per_em;
    long ascent_units;
    long descent_units;
    long line_gap_units;
    long em_height;
    long requested_height;
    long scale_16;
    long status;

    if (!path || !font) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    gdi_font_copy_text(path_copy, sizeof(path_copy), path);
    path = path_copy;

    status = gdi_font_query_file_size(path, &file_size);
    if (status < 0L) {
        return status;
    }
    if (file_size < sizeof(header)) {
        return GDI_FONT_STATUS_FORMAT;
    }

    status = gdi_font_read_file_exact(path, 0UL, header, sizeof(header));
    if (status < 0L) {
        return status;
    }
    if (!(gdi_font_be_u32(header) == 0x00010000UL || gdi_font_be_u32(header) == GDI_FONT_TTF_TAG('t', 'r', 'u', 'e'))) {
        return GDI_FONT_STATUS_FORMAT;
    }

    directory_size = 12UL + (gdi_font_be_u16(header + 4UL) * 16UL);
    directory = (unsigned char*)malloc(directory_size ? directory_size : 1UL);
    if (!directory) {
        return -81L;
    }

    status = gdi_font_read_file_exact(path, 0UL, directory, directory_size);
    if (status < 0L) {
        free(directory);
        return status;
    }

    if (!gdi_ttf_find_table_record(directory, directory_size, file_size, GDI_FONT_TTF_TAG('c', 'm', 'a', 'p'), &cmap_offset, &cmap_size) ||
        !gdi_ttf_find_table_record(directory, directory_size, file_size, GDI_FONT_TTF_TAG('h', 'e', 'a', 'd'), &head_offset, &head_size) ||
        !gdi_ttf_find_table_record(directory, directory_size, file_size, GDI_FONT_TTF_TAG('h', 'h', 'e', 'a'), &hhea_offset, &hhea_size) ||
        !gdi_ttf_find_table_record(directory, directory_size, file_size, GDI_FONT_TTF_TAG('h', 'm', 't', 'x'), &hmtx_offset, &hmtx_size) ||
        !gdi_ttf_find_table_record(directory, directory_size, file_size, GDI_FONT_TTF_TAG('l', 'o', 'c', 'a'), &loca_offset, &loca_size) ||
        !gdi_ttf_find_table_record(directory, directory_size, file_size, GDI_FONT_TTF_TAG('g', 'l', 'y', 'f'), &glyf_offset, &glyf_size) ||
        !gdi_ttf_find_table_record(directory, directory_size, file_size, GDI_FONT_TTF_TAG('m', 'a', 'x', 'p'), &maxp_offset, &maxp_size)) {
        free(directory);
        return GDI_FONT_STATUS_FORMAT;
    }
    if (head_size < 54UL || hhea_size < 36UL || maxp_size < 6UL) {
        free(directory);
        return GDI_FONT_STATUS_FORMAT;
    }

    handle = (GdiFontHandle*)malloc(sizeof(*handle));
    if (!handle) {
        free(directory);
        return -82L;
    }
    gdi_zero_memory(handle, sizeof(*handle));

    bundle_size = cmap_size + head_size + hhea_size + hmtx_size + loca_size + maxp_size;
    table_bundle = (unsigned char*)malloc(bundle_size ? bundle_size : 1UL);
    if (!table_bundle) {
        free(directory);
        free(handle);
        return -83L;
    }

    cursor = table_bundle;
    cmap = cursor;
    status = gdi_font_read_file_exact(path, cmap_offset, cursor, cmap_size);
    if (status < 0L) {
        goto gdi_ttf_stream_fail;
    }
    cursor += cmap_size;

    head = cursor;
    status = gdi_font_read_file_exact(path, head_offset, cursor, head_size);
    if (status < 0L) {
        goto gdi_ttf_stream_fail;
    }
    cursor += head_size;

    hhea = cursor;
    status = gdi_font_read_file_exact(path, hhea_offset, cursor, hhea_size);
    if (status < 0L) {
        goto gdi_ttf_stream_fail;
    }
    cursor += hhea_size;

    hmtx = cursor;
    status = gdi_font_read_file_exact(path, hmtx_offset, cursor, hmtx_size);
    if (status < 0L) {
        goto gdi_ttf_stream_fail;
    }
    cursor += hmtx_size;

    loca = cursor;
    status = gdi_font_read_file_exact(path, loca_offset, cursor, loca_size);
    if (status < 0L) {
        goto gdi_ttf_stream_fail;
    }
    cursor += loca_size;

    maxp = cursor;
    status = gdi_font_read_file_exact(path, maxp_offset, cursor, maxp_size);
    if (status < 0L) {
        goto gdi_ttf_stream_fail;
    }

    if (!gdi_ttf_select_cmap(cmap, cmap_size, &selected_cmap, &selected_cmap_size, &selected_cmap_format)) {
        status = -84L;
        goto gdi_ttf_stream_fail;
    }

    units_per_em = gdi_font_be_u16(head + 18UL);
    ascent_units = gdi_font_be_s16(hhea + 4UL);
    descent_units = gdi_font_be_s16(hhea + 6UL);
    line_gap_units = gdi_font_be_s16(hhea + 8UL);
    em_height = ascent_units - descent_units;
    if (units_per_em <= 0L || em_height <= 0L) {
        status = -85L;
        goto gdi_ttf_stream_fail;
    }

    requested_height = (long)(pixel_height ? pixel_height : GDI_FONT_PIXEL_HEIGHT_DEFAULT);
    scale_16 = (long)((((unsigned long long)requested_height) << 16) / (unsigned long long)em_height);
    if (scale_16 <= 0L) {
        status = -86L;
        goto gdi_ttf_stream_fail;
    }

    handle->file_buffer = table_bundle;
    handle->file_size = bundle_size;
    handle->kind = GDI_FONT_KIND_TRUETYPE;
    handle->pixel_height = (unsigned long)requested_height;
    handle->ascent = gdi_font_scale_metric_signed(ascent_units, scale_16);
    handle->descent = gdi_font_scale_metric_signed(-descent_units, scale_16);
    handle->line_gap = gdi_font_scale_metric_signed(line_gap_units, scale_16);
    handle->line_height = (unsigned long)(handle->ascent + handle->descent + handle->line_gap);
    if (handle->line_height == 0UL) {
        handle->line_height = (unsigned long)requested_height;
    }

    handle->data.ttf.data = table_bundle;
    handle->data.ttf.size = bundle_size;
    handle->data.ttf.cmap = selected_cmap;
    handle->data.ttf.cmap_size = selected_cmap_size;
    handle->data.ttf.cmap_format = selected_cmap_format;
    handle->data.ttf.head = head;
    handle->data.ttf.hhea = hhea;
    handle->data.ttf.hmtx = hmtx;
    handle->data.ttf.loca = loca;
    handle->data.ttf.loca_size = loca_size;
    handle->data.ttf.glyf = 0;
    handle->data.ttf.glyf_file_offset = glyf_offset;
    handle->data.ttf.glyf_size = glyf_size;
    handle->data.ttf.units_per_em = (unsigned short)units_per_em;
    handle->data.ttf.num_glyphs = (unsigned short)gdi_font_be_u16(maxp + 4UL);
    handle->data.ttf.num_hmetrics = (unsigned short)gdi_font_be_u16(hhea + 34UL);
    handle->data.ttf.ascent_units = (short)ascent_units;
    handle->data.ttf.descent_units = (short)descent_units;
    handle->data.ttf.line_gap_units = (short)line_gap_units;
    handle->data.ttf.index_to_loc_format = (short)gdi_font_be_s16(head + 50UL);
    handle->data.ttf.scale_16 = scale_16;
    gdi_font_copy_text(handle->data.ttf.source_path, sizeof(handle->data.ttf.source_path), path);

    free(directory);
    gdi_font_apply_public_metrics(font, handle);
    return ROS_USER_IPC_STATUS_OK;

gdi_ttf_stream_fail:
    free(directory);
    free(table_bundle);
    free(handle);
    return status;
}

/*
 * Load the nearest staged raster UI font for the requested height.
 *
 * The default GDI path now prefers prerasterized `.rtf` assets so text
 * rendering no longer depends on the heavier TTF parser during normal app UI
 * startup. The compiled-in 12 px raster fallback still remains as the
 * last-resort option when those files are unavailable.
 *
 * @param pixel_height Requested rendered height.
 * @param font Receives the loaded font wrapper.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_font_load_default_raster_font(unsigned long pixel_height, RosGdiFont* font) {
    unsigned long requested_height = pixel_height ? pixel_height : GDI_FONT_PIXEL_HEIGHT_DEFAULT;
    unsigned long asset_count = sizeof(g_gdi_default_raster_assets) / sizeof(g_gdi_default_raster_assets[0]);
    unsigned long used_mask = 0UL;
    unsigned long attempt_index;
    long status = GDI_FONT_STATUS_NOT_SUPPORTED;

    if (!font) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    for (attempt_index = 0UL; attempt_index < asset_count; ++attempt_index) {
        unsigned long best_index = 0UL;
        unsigned long best_distance = 0UL;
        int best_found = 0;
        unsigned long asset_index;

        for (asset_index = 0UL; asset_index < asset_count; ++asset_index) {
            unsigned long distance;

            if ((used_mask & (1UL << asset_index)) != 0UL) {
                continue;
            }

            if (g_gdi_default_raster_assets[asset_index].pixel_height > requested_height) {
                distance = g_gdi_default_raster_assets[asset_index].pixel_height - requested_height;
            }
            else {
                distance = requested_height - g_gdi_default_raster_assets[asset_index].pixel_height;
            }

            if (!best_found || distance < best_distance || (distance == best_distance && g_gdi_default_raster_assets[asset_index].pixel_height < g_gdi_default_raster_assets[best_index].pixel_height)) {
                best_index = asset_index;
                best_distance = distance;
                best_found = 1;
            }
        }

        if (!best_found) {
            break;
        }

        used_mask |= (1UL << best_index);
        status = gdi_font_load_single_path(
            g_gdi_default_raster_assets[best_index].path,
            g_gdi_default_raster_assets[best_index].pixel_height,
            font);
        if (status >= 0L) {
            return status;
        }
    }

    /*
     * `system_ui.rtf` is a prerasterized asset generated by `tools/fntmaker.py`,
     * so load it at the baked size recorded in the file header rather than
     * forcing the current request height onto a fixed bitmap face.
     */
    status = gdi_font_load_single_path(GDI_FONT_DEFAULT_SYSTEM_UI_RASTER_PATH, 0UL, font);
    if (status >= 0L) {
        return status;
    }

    status = gdi_font_load_single_path(GDI_FONT_DEFAULT_FALLBACK_PATH, requested_height, font);
    if (status >= 0L) {
        return status;
    }

    return gdi_font_prepare_builtin_system_ui(requested_height, font);
}

long GdiLoadFont(const char* path, unsigned long pixel_height, RosGdiFont* font) {
    if (!font) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    GdiUnloadFont(font);
    if (!path || path[0] == '\0') {
        return gdi_font_load_default_raster_font(pixel_height, font);
    }

    return gdi_font_load_single_path(path, pixel_height, font);
}

static long gdi_font_load_raster_file_static(const char* path, unsigned long pixel_height, RosGdiFont* font) {
    GdiStaticRasterSlot* slot;
    GdiFontHandle* handle;
    unsigned long file_size = 0UL;
    long status;

    if (!path || !font) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

#if GDI_FONT_ENABLE_SHARED_RASTER_CACHE
    status = gdi_font_load_raster_file_shared(path, pixel_height, font);
    if (status >= 0L) {
        return status;
    }
#endif

    status = gdi_font_query_file_size(path, &file_size);
    if (status < 0L) {
        return status;
    }
    if (file_size == 0UL || file_size > GDI_FONT_STATIC_RASTER_MAX_FILE_SIZE) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    slot = gdi_font_reserve_static_raster_slot();
    if (!slot) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    handle = &slot->handle;
    handle->file_buffer = slot->file_buffer;
    handle->file_size = file_size;
    handle->glyphs = slot->glyphs;
    handle->static_slot_index = (unsigned long)((slot - g_gdi_static_raster_slots) + 1);

    status = gdi_font_read_file_exact(path, 0UL, slot->file_buffer, file_size);
    if (status < 0L) {
        gdi_font_destroy_handle(handle);
        return status;
    }

    slot->file_buffer[file_size] = 0U;
    status = gdi_font_parse_raster_file(handle, slot->file_buffer, file_size, pixel_height);
    if (status < 0L) {
        gdi_font_destroy_handle(handle);
        return status;
    }

    gdi_font_apply_public_metrics(font, handle);
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Load one explicit font path without any automatic fallback policy.
 *
 * @param path Absolute font path.
 * @param pixel_height Requested rendered size.
 * @param font Receives the loaded handle and metrics.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_font_load_single_path(const char* path, unsigned long pixel_height, RosGdiFont* font) {
    GdiFontHandle* handle;
    unsigned char* file_buffer = 0;
    unsigned long file_size = 0UL;
    long status;
    const char* effective_path = path;

    if (!font) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    if (!effective_path || effective_path[0] == '\0') {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    if (gdi_font_has_extension(effective_path, ".rtf")) {
        return gdi_font_load_raster_file_static(effective_path, pixel_height, font);
    }

    if (gdi_font_has_extension(effective_path, ".ttf")) {
        return gdi_font_load_ttf_stream(effective_path, pixel_height, font);
    }

    handle = (GdiFontHandle*)malloc(sizeof(*handle));
    if (!handle) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    gdi_zero_memory(handle, sizeof(*handle));
    handle->storage_flags = GDI_FONT_STORAGE_OWNS_SELF;
    status = gdi_font_read_file_all(effective_path, &file_buffer, &file_size);
    if (status < 0L) {
        free(handle);
        return status;
    }

    handle->file_buffer = file_buffer;
    handle->file_size = file_size;
    handle->storage_flags |= GDI_FONT_STORAGE_OWNS_FILE_BUFFER;
    if (gdi_font_buffer_has_prefix(file_buffer, file_size, GDI_RASTER_FONT_MAGIC, GDI_RASTER_FONT_MAGIC_SIZE) || gdi_font_has_extension(effective_path, ".rtf")) {
        status = gdi_font_parse_raster_file(handle, file_buffer, file_size, pixel_height);
    }
    else if (gdi_font_text_starts_with((const char*)file_buffer, "ROSFONT1") || gdi_font_has_extension(effective_path, ".font")) {
        status = gdi_font_parse_descriptor(handle, file_buffer, pixel_height);
    }
    else {
        status = gdi_ttf_initialize(handle, file_buffer, file_size, pixel_height);
    }

    if (status < 0L) {
        gdi_font_destroy_handle(handle);
        return status;
    }

    gdi_font_apply_public_metrics(font, handle);
    return ROS_USER_IPC_STATUS_OK;
}

long GdiUnloadFont(RosGdiFont* font) {
    GdiFontHandle* handle;

    if (!font) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    handle = (GdiFontHandle*)font->handle;
    if (handle && handle != &g_gdi_builtin_system_ui_font) {
        gdi_font_destroy_handle(handle);
    }
    gdi_font_clear_public(font);
    return ROS_USER_IPC_STATUS_OK;
}

long GdiMeasureText(const RosGdiFont* font, const char* text, unsigned long* width, unsigned long* height) {
    GdiFontHandle* handle;
    unsigned long current_width = 0UL;
    unsigned long max_width = 0UL;
    unsigned long lines = 1UL;
    unsigned long index = 0UL;
    const GdiGlyphBitmap* glyph;
    long status;

    if (!font || !font->handle || !text || !width || !height) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    handle = (GdiFontHandle*)font->handle;

    while (text[index] != '\0') {
        unsigned char ch = (unsigned char)text[index];

        if (ch == '\r') {
            ++index;
            continue;
        }
        if (ch == '\n') {
            if (current_width > max_width) {
                max_width = current_width;
            }
            current_width = 0UL;
            ++lines;
            ++index;
            continue;
        }

        if (handle->kind == GDI_FONT_KIND_MINI) {
            current_width += handle->data.mini.advance;
        }
        else {
            status = gdi_font_get_glyph(font, (unsigned long)ch, &glyph);
            if (status < 0L) {
                return status;
            }
            current_width += glyph->advance;
        }
        ++index;
    }

    if (current_width > max_width) {
        max_width = current_width;
    }
    *width = max_width;
    *height = lines * font->line_height;
    return ROS_USER_IPC_STATUS_OK;
}

long GdiDrawTextSurfaceEx(const RosGdiSurface* surface,
    const RosGdiFont* font,
    unsigned long x,
    unsigned long y,
    const char* text,
    unsigned long foreground_color,
    int opaque_background,
    unsigned long background_color) {
    GdiFontHandle* handle;
    unsigned long cursor_x = x;
    unsigned long cursor_y = y;
    unsigned long index = 0UL;
    unsigned long text_width = 0UL;
    unsigned long text_height = 0UL;
    const GdiGlyphBitmap* glyph;
    long status;

    if (!surface || !font || !font->handle || !text) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    handle = (GdiFontHandle*)font->handle;
    if (opaque_background) {
        status = GdiMeasureText(font, text, &text_width, &text_height);
        if (status < 0L) {
            return status;
        }
        if (text_width != 0UL && text_height != 0UL) {
            status = gdi_software_fill_rect(surface, x, y, text_width, text_height, background_color);
            if (status < 0L) {
                return status;
            }
        }
    }

    while (text[index] != '\0') {
        unsigned char ch = (unsigned char)text[index];

        if (ch == '\r') {
            ++index;
            continue;
        }
        if (ch == '\n') {
            cursor_x = x;
            cursor_y += font->line_height;
            ++index;
            continue;
        }

        if (handle->kind == GDI_FONT_KIND_MINI) {
            const RosMiniFontGlyph* mini_glyph = rosMiniFontGlyph((unsigned long)ch);
            const uint8_t* mini_coverage = rosMiniFontGlyphCoverage(mini_glyph);
            unsigned long row;
            unsigned long baseline_y = cursor_y + (unsigned long)font->ascent;

            for (row = 0UL; row < mini_glyph->height; ++row) {
                unsigned long column;
                long draw_y = (long)baseline_y - mini_glyph->bitmap_top + (long)row;

                if (draw_y < 0L) {
                    continue;
                }

                for (column = 0UL; column < mini_glyph->width; ++column) {
                    unsigned char alpha = mini_coverage[(row * mini_glyph->width) + column];
                    long draw_x = (long)cursor_x + mini_glyph->bitmap_left + (long)column;

                    if (alpha == 0U || draw_x < 0L) {
                        continue;
                    }
                    (void)gdi_surface_blend_pixel(surface, (unsigned long)draw_x, (unsigned long)draw_y, foreground_color, (unsigned long)alpha);
                }
            }

            cursor_x += mini_glyph->advance;
            ++index;
            continue;
        }

        status = gdi_font_get_glyph(font, (unsigned long)ch, &glyph);
        if (status < 0L) {
            return status;
        }

        if (glyph->coverage && glyph->width != 0UL && glyph->height != 0UL) {
            unsigned long row;
            unsigned long baseline_y = cursor_y + (unsigned long)font->ascent;

            for (row = 0UL; row < glyph->height; ++row) {
                unsigned long column;
                long draw_y = (long)baseline_y - glyph->bitmap_top + (long)row;

                if (draw_y < 0L) {
                    continue;
                }

                for (column = 0UL; column < glyph->width; ++column) {
                    unsigned char alpha = glyph->coverage[(row * glyph->width) + column];
                    long draw_x = (long)cursor_x + glyph->bitmap_left + (long)column;

                    if (alpha == 0U || draw_x < 0L) {
                        continue;
                    }
                    (void)gdi_surface_blend_pixel(surface, (unsigned long)draw_x, (unsigned long)draw_y, foreground_color, (unsigned long)alpha);
                }
            }
        }

        cursor_x += glyph->advance;
        ++index;
    }

    return ROS_USER_IPC_STATUS_OK;
}

long GdiDrawTextSurface(const RosGdiSurface* surface, const RosGdiFont* font, unsigned long x, unsigned long y, const char* text, unsigned long color) {
    return GdiDrawTextSurfaceEx(surface, font, x, y, text, color, 0, 0UL);
}

long GdiDrawTextEx(HWND hwnd,
    const RosGdiFont* font,
    unsigned long x,
    unsigned long y,
    const char* text,
    unsigned long foreground_color,
    int opaque_background,
    unsigned long background_color) {
    RosGdiSurface surface;
    unsigned long text_width = 0UL;
    unsigned long text_height = 0UL;
    long status;

    if (hwnd == 0UL || !font || !text) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    status = GdiGetWindowSurface(hwnd, &surface);
    if (status < 0L) {
        return status;
    }

    status = GdiDrawTextSurfaceEx(&surface, font, x, y, text, foreground_color, opaque_background, background_color);
    if (status >= 0L) {
        (void)GdiMeasureText(font, text, &text_width, &text_height);
    }
    if (GdiReleaseWindowSurface(hwnd) < 0L && status >= 0L) {
        status = GDI_STATUS_ERROR;
    }
    if (status < 0L) {
        return status;
    }
    if (text_width == 0UL || text_height == 0UL) {
        return ROS_USER_IPC_STATUS_OK;
    }

    return GdiInvalidateRect(hwnd, x, y, text_width, text_height);
}

long GdiDrawText(HWND hwnd, const RosGdiFont* font, unsigned long x, unsigned long y, const char* text, unsigned long color) {
    return GdiDrawTextEx(hwnd, font, x, y, text, color, 0, 0UL);
}
