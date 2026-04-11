#include "render.h"

#include "user_runtime.h"
#include "app/kernel.h"
#include "app/kernel_gui.h"
#include "app/mini_font.h"
#include "compositor.h"
#include "gdi_bridge.h"
#include "jpeg_render.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

namespace {

#ifndef DEBUG_ENABLE_GWES_RENDER_TRACE
#define DEBUG_ENABLE_GWES_RENDER_TRACE 1
#endif

    constexpr unsigned long kMaxWindowCount = MAX_WINDOWS;
    constexpr unsigned long kWindowDefaultWidth = 360UL;
    constexpr unsigned long kWindowDefaultHeight = 220UL;
    constexpr unsigned long kChildDefaultWidth = 120UL;
    constexpr unsigned long kChildDefaultHeight = 24UL;
    constexpr unsigned long kWindowCascadeStep = 28UL;
    constexpr unsigned long kCompositeRowCapacity = 4096UL;
    constexpr unsigned long kCompositeTileRows = 16UL;

    constexpr U32 kDesktopColor = 0x00101822U;
    constexpr U32 kDesktopSignatureText = 0x00F7FBFFU;
    constexpr U32 kDesktopSignatureShadow = 0x00111A24U;
    constexpr U32 kDialogFrameOuter = 0x00324D73U;
    constexpr U32 kDialogFrameInner = 0x00AEC3DDU;
    constexpr U32 kDialogFrameLight = 0x00E7F0FBU;
    constexpr U32 kDialogTitleTop = 0x003E77B4U;
    constexpr U32 kDialogTitleBottom = 0x001E5491U;
    constexpr U32 kDialogTitleShadow = 0x00122B47U;
    constexpr U32 kDialogTitleText = 0x00FFFFFFU;
    constexpr U32 kDialogCloseTop = 0x00F7B78AUL;
    constexpr U32 kDialogCloseBottom = 0x00D66A33UL;
    constexpr U32 kDialogCloseBorder = 0x008B2E12UL;
    constexpr U32 kDialogCloseGlyph = 0x00FFFFFFUL;
    constexpr U32 kDialogMaxTop = 0x00C8DBF6UL;
    constexpr U32 kDialogMaxBottom = 0x00719BCFUL;
    constexpr U32 kDialogMaxBorder = 0x00325783UL;
    constexpr U32 kDialogMaxGlyph = 0x00FFFFFFUL;
    constexpr U32 kDialogMinTop = 0x00F9E7A6UL;
    constexpr U32 kDialogMinBottom = 0x00D1A94BUL;
    constexpr U32 kDialogMinBorder = 0x00806A27UL;
    constexpr U32 kDialogMinGlyph = 0x00FFFFFFUL;
    constexpr U32 kInteractionPreviewLight = 0x00FFFFFFUL;
    constexpr U32 kInteractionPreviewDark = 0x00000000UL;
    constexpr U32 kInteractionPreviewThickness = 2U;
    constexpr char kGwesSystemUiRasterPath[] = "C:\\fonts\\system_ui.rtf";
    constexpr char kGwesWallpaperPath[] = "C:\\wallpapers\\bliss.jpg";
    constexpr char kGwesDefaultCursorPath[] = ROS_WINDOW_CURSOR_DEFAULT_PATH;
    constexpr char kGwesRasterFontMagic[] = "ROSRTF1";
    constexpr unsigned long kGwesRasterFontMagicSize = 7UL;
    constexpr unsigned long kGwesRasterFontVersion = 1UL;
    constexpr unsigned long kGwesRasterFontHeaderSize = 108UL;
    constexpr unsigned long kGwesRasterFontGlyphEntrySize = 32UL;
    constexpr unsigned long kGwesRasterFontGlyphCount = 128UL;
    constexpr unsigned long kGwesRasterFontHeaderVersionOffset = 8UL;
    constexpr unsigned long kGwesRasterFontHeaderSizeOffset = 12UL;
    constexpr unsigned long kGwesRasterFontHeaderPixelHeightOffset = 16UL;
    constexpr unsigned long kGwesRasterFontHeaderLineHeightOffset = 20UL;
    constexpr unsigned long kGwesRasterFontHeaderAscentOffset = 24UL;
    constexpr unsigned long kGwesRasterFontHeaderDescentOffset = 28UL;
    constexpr unsigned long kGwesRasterFontHeaderGlyphCountOffset = 32UL;
    constexpr unsigned long kGwesRasterFontHeaderGlyphTableOffset = 36UL;
    constexpr unsigned long kGwesRasterFontHeaderGlyphEntrySizeOffset = 40UL;
    constexpr unsigned long kGwesRasterFontEntryCoverageOffset = 0UL;
    constexpr unsigned long kGwesRasterFontEntryCoverageSizeOffset = 4UL;
    constexpr unsigned long kGwesRasterFontEntryAdvanceOffset = 8UL;
    constexpr unsigned long kGwesRasterFontEntryBitmapLeftOffset = 12UL;
    constexpr unsigned long kGwesRasterFontEntryBitmapTopOffset = 16UL;
    constexpr unsigned long kGwesRasterFontEntryWidthOffset = 20UL;
    constexpr unsigned long kGwesRasterFontEntryHeightOffset = 24UL;
    constexpr unsigned long kGwesFileReadChunk = 64UL * 1024UL;
    constexpr char kGwesCursorMagic[] = "CUR\0";
    constexpr unsigned long kGwesCursorMagicSize = 4UL;
    constexpr unsigned long kGwesCursorHeaderSize = 188UL;
    constexpr unsigned long kGwesCursorWidthOffset = 4UL;
    constexpr unsigned long kGwesCursorHeightOffset = 8UL;
    constexpr unsigned long kGwesCursorHotspotXOffset = 12UL;
    constexpr unsigned long kGwesCursorHotspotYOffset = 16UL;
    constexpr unsigned long kGwesCursorDataOffsetOffset = 20UL;
    constexpr unsigned long kGwesCursorDataSizeOffset = 24UL;

    constexpr U32 kDialogBorderThickness = 4U;
    constexpr U32 kDialogTitleHeight = 28U;
    constexpr U32 kDialogCaptionPaddingX = 10U;
    constexpr U32 kDialogCaptionPaddingY = 8U;
    constexpr U32 kDialogCloseSize = 18U;
    constexpr U32 kDialogCloseMargin = 5U;
    constexpr U32 kDialogControlButtonGap = 4U;
    constexpr unsigned long kDesktopSignatureMarginX = 18UL;
    constexpr unsigned long kDesktopSignatureMarginY = 16UL;
    constexpr unsigned long kDesktopSignatureLineGap = 2UL;
    constexpr char kDesktopSignatureLine1[] = "Canvas Operating System";
    constexpr char kDesktopSignatureLine2[] = "Version 0.1.0 - development";

    struct GwesWindowRecord {
        Window window;
        GuiWindowSurfaceView surface_view;
    };

    struct GwesRenderState {
        int ready;
        RosKernelGuiDisplayInfo display;
        Compositor compositor;
        GwesWindowRecord window_records[kMaxWindowCount];
        unsigned long cascade_index;
        struct {
            int visible;
            unsigned long x;
            unsigned long y;
            const struct GwesCursorAsset* active_cursor;
        } pointer;
        struct {
            int active;
            Rect frame;
        } interaction_preview;
        RosGdiFont desktop_signature_font;
        int desktop_signature_font_loaded;
    };

    struct GwesRasterGlyph {
        const unsigned char* coverage;
        unsigned long advance;
        long bitmap_left;
        long bitmap_top;
        unsigned long width;
        unsigned long height;
    };

    struct GwesRasterFont {
        int attempted;
        int loaded;
        unsigned char* file_buffer;
        unsigned long file_size;
        unsigned long pixel_height;
        unsigned long line_height;
        long ascent;
        long descent;
        GwesRasterGlyph glyphs[kGwesRasterFontGlyphCount];
    };

    struct GwesRasterFontCacheEntry {
        char* path;
        GwesRasterFont font;
        GwesRasterFontCacheEntry* next;
    };

    struct GwesCursorAsset {
        int attempted;
        int loaded;
        unsigned char* file_buffer;
        unsigned long file_size;
        unsigned long width;
        unsigned long height;
        unsigned long hotspot_x;
        unsigned long hotspot_y;
        const unsigned char* pixel_data;
        unsigned long pixel_data_size;
    };

    struct GwesCursorCacheEntry {
        char* path;
        GwesCursorAsset cursor;
        GwesCursorCacheEntry* next;
    };

    static GwesRenderState g_render_state;
    static GwesRasterFontCacheEntry* g_gwes_font_cache;
    static GwesCursorCacheEntry* g_gwes_cursor_cache;
    static GwesRasterFont* g_gwes_system_ui_font;
    static U32 g_gwes_composite_tile[kCompositeRowCapacity * kCompositeTileRows];
    static U32* g_gwes_composite_row = g_gwes_composite_tile;
    static U32 gwes_encode_desktop_color(U32 color);

    /*
     * Point the row-oriented compositing helpers at one row inside the current
     * multi-row staging tile.
     *
     * The existing paint helpers are all row-based. Rebinding the active row
     * pointer lets GWES reuse them unchanged while packing several rows into one
     * present call.
     *
     * @param row_offset Zero-based row offset inside the staging tile.
     * @param row_width Width of one composed row in pixels.
     * @return Nothing.
     */
    static void gwes_bind_composite_row(unsigned long row_offset, unsigned long row_width) {
        g_gwes_composite_row = &g_gwes_composite_tile[row_offset * row_width];
    }

    /*
     * Write one formatted renderer trace line when the local GWES render trace
     * switch is enabled.
     *
     * @param fmt Printf-style format string.
     * @return Nothing.
     */
    static void gwes_render_tracef(const char* fmt, ...) {
#if DEBUG_ENABLE_GWES_RENDER_TRACE
        char line[224];
        va_list args;

        va_start(args, fmt);
        vsnprintf(line, sizeof(line), fmt, args);
        va_end(args);
        writeLine(line);
#else
        (void)fmt;
#endif
    }

    /*
     * Forward one request into the kernel GUI service.
     *
     * @param command `ROS_KERNEL_GUI_CONTROL_*` selector.
     * @param value Optional scalar or user pointer payload.
     * @return Zero on success, or a negative status code on failure.
     */
    static long gwes_control_gui(unsigned long command, unsigned long value) {
        return call_sys_gui_control(command, value);
    }

    /*
     * Return the smaller of two unsigned values.
     *
     * @param lhs Left operand.
     * @param rhs Right operand.
     * @return Minimum of the two values.
     */
    static unsigned long gwes_min_ul(unsigned long lhs, unsigned long rhs) {
        return lhs < rhs ? lhs : rhs;
    }

    /*
     * Return the larger of two unsigned values.
     *
     * @param lhs Left operand.
     * @param rhs Right operand.
     * @return Maximum of the two values.
     */
    static unsigned long gwes_max_ul(unsigned long lhs, unsigned long rhs) {
        return lhs > rhs ? lhs : rhs;
    }

    /*
     * Clamp one signed coordinate to the non-negative desktop range.
     *
     * @param value Signed coordinate requested by the caller.
     * @return Zero or the original value converted to `U32`.
     */
    static U32 gwes_clamp_non_negative(long value) {
        return value < 0 ? 0U : static_cast<U32>(value);
    }

    /*
     * Return whether two compositor rectangles describe the same bounds.
     *
     * @param lhs First rectangle.
     * @param rhs Second rectangle.
     * @return Non-zero when all fields match exactly.
     */
    static int gwes_rect_equals(const Rect& lhs, const Rect& rhs) {
        return lhs.x == rhs.x
            && lhs.y == rhs.y
            && lhs.width == rhs.width
            && lhs.height == rhs.height;
    }

    /*
     * Convert one requested preview frame into a clamped desktop rectangle.
     *
     * The placeholder should mirror the geometry that a real move or resize
     * commit would accept, so negative coordinates are clamped here rather than
     * letting the preview drift outside the visible desktop.
     *
     * @param x Requested frame X coordinate.
     * @param y Requested frame Y coordinate.
     * @param width Requested frame width.
     * @param height Requested frame height.
     * @return Normalized preview rectangle.
     */
    static Rect gwes_make_interaction_preview_rect(long x, long y, unsigned long width, unsigned long height) {
        return Rect{
            gwes_clamp_non_negative(x),
            gwes_clamp_non_negative(y),
            static_cast<U32>(width),
            static_cast<U32>(height)
        };
    }

    /*
     * Return whether one point lies on the dashed placeholder frame border.
     *
     * @param frame Preview frame in desktop coordinates.
     * @param x Desktop X coordinate to test.
     * @param y Desktop Y coordinate to test.
     * @return Non-zero when the point should receive the outline color.
     */
    static int gwes_preview_contains_border_pixel(const Rect& frame, U32 x, U32 y) {
        const U32 thickness = gwes_min_ul(kInteractionPreviewThickness, gwes_min_ul(frame.width, frame.height));

        if (frame.is_empty()
            || thickness == 0U
            || x < frame.x
            || x >= frame.right()
            || y < frame.y
            || y >= frame.bottom()) {
            return 0;
        }

        return y < frame.y + thickness
            || y >= frame.bottom() - thickness
            || x < frame.x + thickness
            || x >= frame.right() - thickness;
    }

    /*
     * Blend one Win9x-style dashed preview frame into the current scanline.
     *
     * The outline is composited after window content so the placeholder stays
     * visible even while the real window remains stationary underneath it.
     *
     * @param x Scanline chunk start X coordinate.
     * @param width Scanline chunk width.
     * @param row Desktop row currently being composed.
     * @return Nothing.
     */
    static void gwes_overlay_interaction_preview_span(U32 x, unsigned long width, U32 row) {
        Rect clipped = g_render_state.interaction_preview.frame;
        const U32 chunk_right = static_cast<U32>(x + width);
        U32 start_x;
        U32 end_x;
        U32 column;

        if (!g_render_state.interaction_preview.active || clipped.is_empty()) {
            return;
        }

        if (clipped.x >= g_render_state.compositor.desktop.width || clipped.y >= g_render_state.compositor.desktop.height) {
            return;
        }

        if (clipped.right() > g_render_state.compositor.desktop.width) {
            clipped.width = g_render_state.compositor.desktop.width - clipped.x;
        }
        if (clipped.bottom() > g_render_state.compositor.desktop.height) {
            clipped.height = g_render_state.compositor.desktop.height - clipped.y;
        }
        if (clipped.is_empty() || row < clipped.y || row >= clipped.bottom()) {
            return;
        }

        start_x = gwes_max_ul(x, clipped.x);
        end_x = gwes_min_ul(chunk_right, clipped.right());
        if (start_x >= end_x) {
            return;
        }

        for (column = start_x; column < end_x; ++column) {
            if (!gwes_preview_contains_border_pixel(clipped, column, row)) {
                continue;
            }

            g_gwes_composite_row[column - x] = (((column + row) & 1U) == 0U)
                ? kInteractionPreviewLight
                : kInteractionPreviewDark;
        }
    }

    /*
     * Build one rectangle from unsigned coordinates.
     *
     * @param x Rectangle X coordinate.
     * @param y Rectangle Y coordinate.
     * @param width Rectangle width.
     * @param height Rectangle height.
     * @return Populated rectangle value.
     */
    static Rect gwes_make_rect(unsigned long x, unsigned long y, unsigned long width, unsigned long height) {
        return Rect{
            static_cast<U32>(x),
            static_cast<U32>(y),
            static_cast<U32>(width),
            static_cast<U32>(height)
        };
    }

    /*
     * Return one desktop-sized rectangle.
     *
     * @return Rectangle covering the current display surface.
     */
    static Rect gwes_desktop_rect(void) {
        return gwes_make_rect(0UL, 0UL, g_render_state.compositor.desktop.width, g_render_state.compositor.desktop.height);
    }

    /*
     * Clear one caller-owned byte range without depending on hosted libc.
     *
     * @param destination Buffer to clear.
     * @param size Number of bytes to clear.
     * @return Nothing.
     */
    static void gwes_zero_memory(void* destination, unsigned long size) {
        unsigned char* bytes = static_cast<unsigned char*>(destination);
        unsigned long index;

        if (!bytes) {
            return;
        }

        for (index = 0UL; index < size; ++index) {
            bytes[index] = 0U;
        }
    }

    static int gwes_buffer_has_prefix(const unsigned char* buffer, unsigned long buffer_size, const char* prefix, unsigned long prefix_size) {
        unsigned long index;

        if (!buffer || !prefix || prefix_size == 0UL || buffer_size < prefix_size) {
            return 0;
        }

        for (index = 0UL; index < prefix_size; ++index) {
            if (buffer[index] != static_cast<unsigned char>(prefix[index])) {
                return 0;
            }
        }

        return 1;
    }

    static unsigned long gwes_read_le_u32(const unsigned char* data) {
        return static_cast<unsigned long>(data[0])
            | (static_cast<unsigned long>(data[1]) << 8)
            | (static_cast<unsigned long>(data[2]) << 16)
            | (static_cast<unsigned long>(data[3]) << 24);
    }

    static unsigned long gwes_read_le_u16(const unsigned char* data) {
        return static_cast<unsigned long>(data[0])
            | (static_cast<unsigned long>(data[1]) << 8);
    }

    static long gwes_read_le_s32(const unsigned char* data) {
        const unsigned long raw = gwes_read_le_u32(data);

        if ((raw & 0x80000000UL) != 0UL) {
            return static_cast<long>(raw - 0x100000000ULL);
        }

        return static_cast<long>(raw);
    }

    /*
     * Measure one null-terminated string.
     *
     * @param text Source string.
     * @return Character count excluding the terminator.
     */
    static unsigned long gwes_text_length(const char* text) {
        unsigned long length = 0UL;

        if (!text) {
            return 0UL;
        }

        while (text[length] != '\0') {
            ++length;
        }

        return length;
    }

    /*
     * Duplicate one null-terminated string onto the GWES heap.
     *
     * @param text Source string.
     * @return Heap-owned copy, or null on failure.
     */
    static char* gwes_duplicate_text(const char* text) {
        const unsigned long length = gwes_text_length(text);
        char* copy;
        unsigned long index;

        copy = static_cast<char*>(malloc(length + 1UL));
        if (!copy) {
            return nullptr;
        }

        for (index = 0UL; index < length; ++index) {
            copy[index] = text[index];
        }
        copy[length] = '\0';
        return copy;
    }

    /*
     * Compare two null-terminated strings for exact equality.
     *
     * @param lhs Left string.
     * @param rhs Right string.
     * @return True when both strings match byte-for-byte.
     */
    static bool gwes_text_equals(const char* lhs, const char* rhs) {
        unsigned long index = 0UL;

        if (lhs == rhs) {
            return true;
        }
        if (!lhs || !rhs) {
            return false;
        }

        while (lhs[index] != '\0' && rhs[index] != '\0') {
            if (lhs[index] != rhs[index]) {
                return false;
            }
            ++index;
        }

        return lhs[index] == rhs[index];
    }

    /*
     * Release one cached raster font and its backing file buffer.
     *
     * @param font Font record to reset.
     * @return Nothing.
     */
    static void gwes_release_raster_font(GwesRasterFont* font) {
        if (!font) {
            return;
        }

        if (font->file_buffer) {
            free(font->file_buffer);
        }

        gwes_zero_memory(font, sizeof(*font));
    }

    /*
     * Release one cached cursor asset and its backing file buffer.
     *
     * Cursor files are small and immutable after load, so the cache keeps the
     * raw buffer alive for direct overlay reads. Releasing the asset therefore
     * must also release that retained file storage.
     *
     * @param cursor Cursor record to reset.
     * @return Nothing.
     */
    static void gwes_release_cursor_asset(GwesCursorAsset* cursor) {
        if (!cursor) {
            return;
        }

        if (cursor->file_buffer) {
            free(cursor->file_buffer);
        }

        gwes_zero_memory(cursor, sizeof(*cursor));
    }

    /*
     * Look up one font-cache entry by path.
     *
     * @param path Cache key.
     * @return Matching cache entry, or null when not cached.
     */
    static GwesRasterFontCacheEntry* gwes_find_font_cache_entry(const char* path) {
        GwesRasterFontCacheEntry* entry = g_gwes_font_cache;

        while (entry) {
            if (gwes_text_equals(entry->path, path)) {
                return entry;
            }
            entry = entry->next;
        }

        return nullptr;
    }

    /*
     * Allocate one new cache entry and link it into the GWES registry.
     *
     * @param path Cache key.
     * @return Linked cache entry, or null on failure.
     */
    static GwesRasterFontCacheEntry* gwes_create_font_cache_entry(const char* path) {
        GwesRasterFontCacheEntry* entry;

        entry = static_cast<GwesRasterFontCacheEntry*>(malloc(sizeof(GwesRasterFontCacheEntry)));
        if (!entry) {
            return nullptr;
        }

        gwes_zero_memory(entry, sizeof(*entry));
        entry->path = gwes_duplicate_text(path);
        if (!entry->path) {
            free(entry);
            return nullptr;
        }

        entry->next = g_gwes_font_cache;
        g_gwes_font_cache = entry;
        return entry;
    }

    /*
     * Look up one cursor-cache entry by its DOS-style asset path.
     *
     * @param path Cache key.
     * @return Matching cache entry, or null when the asset was never requested.
     */
    static GwesCursorCacheEntry* gwes_find_cursor_cache_entry(const char* path) {
        GwesCursorCacheEntry* entry = g_gwes_cursor_cache;

        while (entry) {
            if (gwes_text_equals(entry->path, path)) {
                return entry;
            }
            entry = entry->next;
        }

        return nullptr;
    }

    /*
     * Allocate one new cursor-cache entry and link it into the registry.
     *
     * @param path Cache key.
     * @return Linked cache entry, or null on allocation failure.
     */
    static GwesCursorCacheEntry* gwes_create_cursor_cache_entry(const char* path) {
        GwesCursorCacheEntry* entry;

        entry = static_cast<GwesCursorCacheEntry*>(malloc(sizeof(GwesCursorCacheEntry)));
        if (!entry) {
            return nullptr;
        }

        gwes_zero_memory(entry, sizeof(*entry));
        entry->path = gwes_duplicate_text(path);
        if (!entry->path) {
            free(entry);
            return nullptr;
        }

        entry->next = g_gwes_cursor_cache;
        g_gwes_cursor_cache = entry;
        return entry;
    }

    /*
     * Release the entire GWES raster-font cache.
     *
     * @return Nothing.
     */
    static void gwes_release_font_cache(void) {
        GwesRasterFontCacheEntry* entry = g_gwes_font_cache;

        while (entry) {
            GwesRasterFontCacheEntry* next = entry->next;

            gwes_release_raster_font(&entry->font);
            free(entry->path);
            free(entry);
            entry = next;
        }

        g_gwes_font_cache = nullptr;
        g_gwes_system_ui_font = nullptr;
    }

    /*
     * Release every cached cursor asset.
     *
     * @return Nothing.
     */
    static void gwes_release_cursor_cache(void) {
        GwesCursorCacheEntry* entry = g_gwes_cursor_cache;

        while (entry) {
            GwesCursorCacheEntry* next = entry->next;

            gwes_release_cursor_asset(&entry->cursor);
            free(entry->path);
            free(entry);
            entry = next;
        }

        g_gwes_cursor_cache = nullptr;
        g_render_state.pointer.active_cursor = nullptr;
    }

    /*
     * Return the loaded system UI raster font when available.
     *
     * @return Loaded cached font, or null when GWES must fall back.
     */
    static const GwesRasterFont* gwes_system_ui_font(void) {
        if (!g_gwes_system_ui_font || !g_gwes_system_ui_font->loaded) {
            return nullptr;
        }

        return g_gwes_system_ui_font;
    }

    /*
     * Read one full file into a heap-owned byte buffer.
     *
     * @param path DOS-style file path.
     * @param data Receives the heap-owned contents.
     * @param size Receives the loaded byte count.
     * @return Zero on success, or a negative status code on failure.
     */
    static long gwes_read_file_all(const char* path, unsigned char** data, unsigned long* size) {
        char* path_copy;
        unsigned char* buffer;
        unsigned long capacity = kGwesFileReadChunk;
        unsigned long total = 0UL;

        if (!path || !data || !size) {
            return -1L;
        }



        path_copy = gwes_duplicate_text(path);
        if (!path_copy) {
            writeLine("GWES failed to duplicate file path for reading");
            return -1L;
        }


        buffer = static_cast<unsigned char*>(malloc(capacity + 1UL));
        if (!buffer) {
            writeLine("GWES failed to allocate initial file buffer for reading");
            free(path_copy);
            return -1L;
        }

        for (;;) {
            long read_result;
            unsigned long request = kGwesFileReadChunk;

            if ((capacity - total) < kGwesFileReadChunk) {
                unsigned long new_capacity = capacity * 2UL;
                unsigned char* replacement = static_cast<unsigned char*>(realloc(buffer, new_capacity + 1UL));

                if (!replacement) {
                    free(buffer);
                    free(path_copy);
                    writeLine("GWES failed to reallocate file buffer for reading");
                    return -1L;
                }

                buffer = replacement;
                capacity = new_capacity;
            }

            if ((capacity - total) < request) {
                request = capacity - total;
            }

            read_result = readFile(path_copy, total, reinterpret_cast<char*>(buffer + total), request);
            if (read_result < 0L) {
                free(buffer);
                free(path_copy);
                return read_result;
            }
            if (read_result == 0L) {
                break;
            }

            total += static_cast<unsigned long>(read_result);
        }

        buffer[total] = 0U;
        *data = buffer;
        *size = total;
        free(path_copy);
        return 0L;
    }

    static void gwes_release_desktop_surface(void) {
        if (g_render_state.compositor.desktop.pixels) {
            free(g_render_state.compositor.desktop.pixels);
            g_render_state.compositor.desktop.pixels = nullptr;
        }
        g_render_state.compositor.desktop.pitch = 0U;
    }

    static long gwes_allocate_desktop_surface(void) {
        const unsigned long row_bytes = g_render_state.display.width * sizeof(U32);
        const unsigned long total_bytes = row_bytes * g_render_state.display.height;
        U32* pixels;

        if (g_render_state.display.width == 0U || g_render_state.display.height == 0U || total_bytes == 0UL) {
            return -1L;
        }

        pixels = static_cast<U32*>(malloc(total_bytes));
        if (!pixels) {
            return -1L;
        }

        g_render_state.compositor.desktop.width = g_render_state.display.width;
        g_render_state.compositor.desktop.height = g_render_state.display.height;
        g_render_state.compositor.desktop.pitch = static_cast<U32>(row_bytes);
        g_render_state.compositor.desktop.pixels = pixels;
        return 0L;
    }

    static void gwes_desktop_surface_view(RosGdiSurface* surface) {
        if (!surface) {
            return;
        }

        gwes_zero_memory(surface, sizeof(*surface));
        surface->hwnd = 0UL;
        surface->width = g_render_state.compositor.desktop.width;
        surface->height = g_render_state.compositor.desktop.height;
        surface->pitch = g_render_state.compositor.desktop.pitch;
        surface->pixel_format = g_render_state.display.pixel_format;
        surface->pixels = g_render_state.compositor.desktop.pixels;
    }

    static void gwes_fill_desktop_surface(U32 color) {
        if (!g_render_state.compositor.desktop.pixels) {
            return;
        }

        for (unsigned long row = 0UL; row < g_render_state.compositor.desktop.height; ++row) {
            U32* pixels = reinterpret_cast<U32*>(reinterpret_cast<U8*>(g_render_state.compositor.desktop.pixels) + (row * g_render_state.compositor.desktop.pitch));

            for (unsigned long column = 0UL; column < g_render_state.compositor.desktop.width; ++column) {
                pixels[column] = gwes_encode_desktop_color(color);
            }
        }
    }

    static long gwes_render_wallpaper_to_desktop(void) {
        unsigned char* buffer = nullptr;
        unsigned long file_size = 0UL;
        JpegRenderTarget target = {};
        long status;

        status = gwes_read_file_all(kGwesWallpaperPath, &buffer, &file_size);

        if (status < 0L) {
            return status;
        }

        target.width = g_render_state.compositor.desktop.width;
        target.height = g_render_state.compositor.desktop.height;
        target.pitch = g_render_state.compositor.desktop.pitch;
        target.pixel_format = g_render_state.display.pixel_format;
        target.pixels = g_render_state.compositor.desktop.pixels;
        status = jpeg_render_to_surface(buffer, static_cast<U32>(file_size), &target, kDesktopColor);
        if (status < 0L) {
            gwes_render_tracef("gwes.exe: wallpaper decode failed path=%s size=%lu status=%ld", kGwesWallpaperPath, file_size, status);
        }
        free(buffer);
        return status;
    }

    static void gwes_render_signature_to_desktop_surface(void) {
        RosGdiSurface desktop_surface = {};
        unsigned long line1_width;
        unsigned long line2_width;
        unsigned long line_height;
        unsigned long block_height;
        unsigned long line1_x;
        unsigned long line2_x;
        unsigned long block_y;
        unsigned long line2_y;

        if (!g_render_state.desktop_signature_font_loaded || !g_render_state.compositor.desktop.pixels) {
            return;
        }

        gwes_desktop_surface_view(&desktop_surface);
        line1_width = 0UL;
        line2_width = 0UL;
        line_height = g_render_state.desktop_signature_font.line_height;
        if (gwes_gdi_measure_text(&g_render_state.desktop_signature_font, kDesktopSignatureLine1, &line1_width, &line_height) < 0L) {
            return;
        }
        if (gwes_gdi_measure_text(&g_render_state.desktop_signature_font, kDesktopSignatureLine2, &line2_width, &line_height) < 0L) {
            return;
        }

        line_height = g_render_state.desktop_signature_font.line_height;
        block_height = (line_height * 2UL) + kDesktopSignatureLineGap;
        line1_x = desktop_surface.width > (line1_width + kDesktopSignatureMarginX)
            ? (desktop_surface.width - line1_width - kDesktopSignatureMarginX)
            : 0UL;
        line2_x = desktop_surface.width > (line2_width + kDesktopSignatureMarginX)
            ? (desktop_surface.width - line2_width - kDesktopSignatureMarginX)
            : 0UL;
        block_y = desktop_surface.height > (block_height + kDesktopSignatureMarginY)
            ? (desktop_surface.height - block_height - kDesktopSignatureMarginY)
            : 0UL;
        line2_y = block_y + line_height + kDesktopSignatureLineGap;

        (void)gwes_gdi_draw_text_surface(&desktop_surface, &g_render_state.desktop_signature_font, line1_x + 1UL, block_y + 1UL, kDesktopSignatureLine1, kDesktopSignatureShadow);
        (void)gwes_gdi_draw_text_surface(&desktop_surface, &g_render_state.desktop_signature_font, line2_x + 1UL, line2_y + 1UL, kDesktopSignatureLine2, kDesktopSignatureShadow);
        (void)gwes_gdi_draw_text_surface(&desktop_surface, &g_render_state.desktop_signature_font, line1_x, block_y, kDesktopSignatureLine1, kDesktopSignatureText);
        (void)gwes_gdi_draw_text_surface(&desktop_surface, &g_render_state.desktop_signature_font, line2_x, line2_y, kDesktopSignatureLine2, kDesktopSignatureText);
    }

    static void gwes_render_desktop_surface(void) {
        long wallpaper_status;

        gwes_fill_desktop_surface(kDesktopColor);
        wallpaper_status = gwes_render_wallpaper_to_desktop();
        if (wallpaper_status < 0L) {
            gwes_render_tracef("gwes.exe: wallpaper fallback status=%ld", wallpaper_status);
            writeLine("gwes.exe: wallpaper unavailable, using solid desktop color");
        }
        gwes_render_signature_to_desktop_surface();
    }

    /*
     * Parse one `.cur32` cursor payload into a retained cursor asset.
     *
     * The renderer reads cursor pixels directly from the retained file buffer on
     * every damaged scanline. Validating the bounds here keeps the hot path down
     * to simple pointer arithmetic and alpha blending.
     *
     * @param cursor Cursor record to populate.
     * @param buffer Heap-owned file contents.
     * @param file_size File size in bytes.
     * @return Zero on success, or a negative status code on malformed input.
     */
    static long gwes_parse_cursor_asset(GwesCursorAsset* cursor, unsigned char* buffer, unsigned long file_size) {
        unsigned long width;
        unsigned long height;
        unsigned long hotspot_x;
        unsigned long hotspot_y;
        unsigned long data_offset;
        unsigned long data_size;
        unsigned long required_data_size;

        if (!cursor || !buffer || file_size < kGwesCursorHeaderSize) {
            return -1L;
        }
        if (!gwes_buffer_has_prefix(buffer, file_size, kGwesCursorMagic, kGwesCursorMagicSize)) {
            return -1L;
        }

        width = gwes_read_le_u32(buffer + kGwesCursorWidthOffset);
        height = gwes_read_le_u32(buffer + kGwesCursorHeightOffset);
        hotspot_x = gwes_read_le_u32(buffer + kGwesCursorHotspotXOffset);
        hotspot_y = gwes_read_le_u32(buffer + kGwesCursorHotspotYOffset);
        data_offset = gwes_read_le_u32(buffer + kGwesCursorDataOffsetOffset);
        data_size = gwes_read_le_u32(buffer + kGwesCursorDataSizeOffset);
        required_data_size = width * height * 4UL;

        if (width == 0UL
            || height == 0UL
            || hotspot_x >= width
            || hotspot_y >= height
            || data_offset < kGwesCursorHeaderSize
            || data_offset > file_size
            || required_data_size == 0UL
            || data_size < required_data_size
            || data_size >(file_size - data_offset)) {
            return -1L;
        }

        gwes_zero_memory(cursor, sizeof(*cursor));
        cursor->attempted = 1;
        cursor->loaded = 1;
        cursor->file_buffer = buffer;
        cursor->file_size = file_size;
        cursor->width = width;
        cursor->height = height;
        cursor->hotspot_x = hotspot_x;
        cursor->hotspot_y = hotspot_y;
        cursor->pixel_data = buffer + data_offset;
        cursor->pixel_data_size = data_size;
        return 0L;
    }

    /*
     * Load one cursor asset into the renderer cache.
     *
     * Cursor assets are immutable once loaded, so the first request owns the
     * only file read and all later requests reuse the parsed metadata.
     *
     * @param path DOS-style cursor path.
     * @return Cached cursor asset, or null when the load failed.
     */
    static const GwesCursorAsset* gwes_load_cursor_asset(const char* path) {
        GwesCursorCacheEntry* entry;
        unsigned char* buffer = nullptr;
        unsigned long file_size = 0UL;

        if (!path || path[0] == '\0') {
            return nullptr;
        }

        entry = gwes_find_cursor_cache_entry(path);
        if (!entry) {
            entry = gwes_create_cursor_cache_entry(path);
            if (!entry) {
                return nullptr;
            }
        }

        if (entry->cursor.attempted) {
            return entry->cursor.loaded ? &entry->cursor : nullptr;
        }

        entry->cursor.attempted = 1;
        if (gwes_read_file_all(path, &buffer, &file_size) < 0L) {
            return nullptr;
        }
        if (gwes_parse_cursor_asset(&entry->cursor, buffer, file_size) < 0L) {
            free(buffer);
            gwes_zero_memory(&entry->cursor, sizeof(entry->cursor));
            entry->cursor.attempted = 1;
            return nullptr;
        }

        return &entry->cursor;
    }

    /*
     * Resolve one cursor request path to a loaded asset.
     *
     * GWES always keeps a default arrow cursor available when possible, so a
     * bad per-window path degrades gracefully instead of leaving a stale or
     * invisible pointer behind.
     *
     * @param path Requested cursor path, or null for the default.
     * @return Loaded cursor asset, or null when even the default could not load.
     */
    static const GwesCursorAsset* gwes_resolve_cursor_asset(const char* path) {
        const GwesCursorAsset* cursor = nullptr;

        if (path && path[0] != '\0') {
            cursor = gwes_load_cursor_asset(path);
        }
        if (!cursor && (!path || !gwes_text_equals(path, kGwesDefaultCursorPath))) {
            cursor = gwes_load_cursor_asset(kGwesDefaultCursorPath);
        }

        return cursor;
    }

    /*
     * Parse one heap-owned raster font buffer into a font record.
     *
     * @param font Destination font record.
     * @param buffer Heap-owned file contents.
     * @param file_size File size in bytes.
     * @return Zero on success, or a negative status code on failure.
     */
    static long gwes_parse_raster_font(GwesRasterFont* font, unsigned char* buffer, unsigned long file_size) {
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

        if (!font || !buffer || file_size == 0UL) {
            return -1L;
        }

        if (file_size < kGwesRasterFontHeaderSize
            || !gwes_buffer_has_prefix(buffer, file_size, kGwesRasterFontMagic, kGwesRasterFontMagicSize)) {
            return -1L;
        }

        version = gwes_read_le_u32(buffer + kGwesRasterFontHeaderVersionOffset);
        header_size = gwes_read_le_u32(buffer + kGwesRasterFontHeaderSizeOffset);
        source_pixel_height = gwes_read_le_u32(buffer + kGwesRasterFontHeaderPixelHeightOffset);
        line_height = gwes_read_le_u32(buffer + kGwesRasterFontHeaderLineHeightOffset);
        ascent = gwes_read_le_s32(buffer + kGwesRasterFontHeaderAscentOffset);
        descent = gwes_read_le_s32(buffer + kGwesRasterFontHeaderDescentOffset);
        glyph_count = gwes_read_le_u32(buffer + kGwesRasterFontHeaderGlyphCountOffset);
        glyph_table_offset = gwes_read_le_u32(buffer + kGwesRasterFontHeaderGlyphTableOffset);
        glyph_entry_size = gwes_read_le_u32(buffer + kGwesRasterFontHeaderGlyphEntrySizeOffset);

        if (version != kGwesRasterFontVersion
            || header_size < kGwesRasterFontHeaderSize
            || header_size > file_size
            || source_pixel_height == 0UL
            || line_height == 0UL
            || ascent <= 0L
            || descent < 0L
            || glyph_count != kGwesRasterFontGlyphCount
            || glyph_entry_size < kGwesRasterFontGlyphEntrySize
            || glyph_table_offset < header_size
            || glyph_table_offset > file_size
            || (glyph_count * glyph_entry_size) >(file_size - glyph_table_offset)) {
            return -1L;
        }

        gwes_release_raster_font(font);
        font->attempted = 1;
        font->file_buffer = buffer;
        font->file_size = file_size;
        font->pixel_height = source_pixel_height;
        font->line_height = line_height;
        font->ascent = ascent;
        font->descent = descent;

        for (glyph_index = 0UL; glyph_index < kGwesRasterFontGlyphCount; ++glyph_index) {
            const unsigned char* entry = buffer + glyph_table_offset + (glyph_index * glyph_entry_size);
            const unsigned long coverage_offset = gwes_read_le_u32(entry + kGwesRasterFontEntryCoverageOffset);
            const unsigned long coverage_size = gwes_read_le_u32(entry + kGwesRasterFontEntryCoverageSizeOffset);
            const unsigned long width = gwes_read_le_u32(entry + kGwesRasterFontEntryWidthOffset);
            const unsigned long height = gwes_read_le_u32(entry + kGwesRasterFontEntryHeightOffset);
            const unsigned long expected_size = width * height;

            if ((width != 0UL && expected_size / width != height)
                || coverage_size != expected_size
                || (coverage_size != 0UL && (coverage_offset > file_size || coverage_size > (file_size - coverage_offset)))) {
                gwes_release_raster_font(font);
                font->attempted = 1;
                return -1L;
            }

            font->glyphs[glyph_index].coverage = coverage_size != 0UL ? (buffer + coverage_offset) : nullptr;
            font->glyphs[glyph_index].advance = gwes_read_le_u32(entry + kGwesRasterFontEntryAdvanceOffset);
            font->glyphs[glyph_index].bitmap_left = gwes_read_le_s32(entry + kGwesRasterFontEntryBitmapLeftOffset);
            font->glyphs[glyph_index].bitmap_top = gwes_read_le_s32(entry + kGwesRasterFontEntryBitmapTopOffset);
            font->glyphs[glyph_index].width = width;
            font->glyphs[glyph_index].height = height;
        }

        font->loaded = 1;
        return 0L;
    }

    /*
     * Load one raster font through the GWES cache.
     *
     * @param path Raster font path.
     * @param font Receives the loaded cached font.
     * @return Zero on success, or a negative status code on failure.
     */
    static long gwes_load_raster_font(const char* path, GwesRasterFont** font) {
        GwesRasterFontCacheEntry* entry;
        unsigned char* buffer = nullptr;
        unsigned long file_size = 0UL;
        long status;

        if (!path || !font) {
            return -1L;
        }

        entry = gwes_find_font_cache_entry(path);
        if (!entry) {
            entry = gwes_create_font_cache_entry(path);
            if (!entry) {
                return -1L;
            }
        }

        if (entry->font.attempted) {
            *font = entry->font.loaded ? &entry->font : nullptr;
            return entry->font.loaded ? 0L : -1L;
        }

        entry->font.attempted = 1;
        status = gwes_read_file_all(path, &buffer, &file_size);
        if (status < 0L) {
            *font = nullptr;
            return status;
        }

        status = gwes_parse_raster_font(&entry->font, buffer, file_size);
        if (status < 0L) {
            free(buffer);
            *font = nullptr;
            return status;
        }

        *font = &entry->font;
        writeLine("gwes.exe: loaded C:\\fonts\\system_ui.rtf for window chrome");
        return 0L;
    }

    /*
     * Load the system UI raster font used by GWES chrome drawing.
     *
     * @return Zero on success, or a negative status code on failure.
     */
    static long gwes_load_system_ui_raster_font(void) {
        long status = gwes_load_raster_font(kGwesSystemUiRasterPath, &g_gwes_system_ui_font);

        if (status < 0L) {
            writeLine("gwes.exe: system_ui.rtf unavailable, using mini-font chrome");
        }

        return status;
    }

    /*
     * Copy one null-terminated string into a fixed record buffer.
     *
     * @param destination Destination buffer.
     * @param size Destination buffer size.
     * @param text Optional source string.
     * @return Nothing.
     */
    static void gwes_copy_text(char* destination, unsigned long size, const char* text) {
        unsigned long index = 0UL;

        if (!destination || size == 0UL) {
            return;
        }
        if (!text) {
            destination[0] = '\0';
            return;
        }

        while ((index + 1UL) < size && text[index] != '\0') {
            destination[index] = text[index];
            ++index;
        }
        destination[index] = '\0';
    }

    static void gwes_decode_desktop_color(U32 color, unsigned long* red, unsigned long* green, unsigned long* blue) {
        if (!red || !green || !blue) {
            return;
        }

        if (g_render_state.display.pixel_format == ROS_KERNEL_GUI_PIXEL_FORMAT_XBGR8888) {
            *red = color & 0xFFUL;
            *green = (color >> 8) & 0xFFUL;
            *blue = (color >> 16) & 0xFFUL;
            return;
        }

        *red = (color >> 16) & 0xFFUL;
        *green = (color >> 8) & 0xFFUL;
        *blue = color & 0xFFUL;
    }

    static void gwes_blend_row_pixel(U32* pixel, U32 color, unsigned long alpha) {
        unsigned long dst_red;
        unsigned long dst_green;
        unsigned long dst_blue;
        const unsigned long inv_alpha = 255UL - alpha;
        unsigned long out_red;
        unsigned long out_green;
        unsigned long out_blue;

        if (!pixel || alpha == 0UL) {
            return;
        }
        if (alpha >= 255UL) {
            *pixel = gwes_encode_desktop_color(color);
            return;
        }

        gwes_decode_desktop_color(*pixel, &dst_red, &dst_green, &dst_blue);
        out_red = ((((color >> 16) & 0xFFUL) * alpha) + (dst_red * inv_alpha) + 127UL) / 255UL;
        out_green = ((((color >> 8) & 0xFFUL) * alpha) + (dst_green * inv_alpha) + 127UL) / 255UL;
        out_blue = (((color & 0xFFUL) * alpha) + (dst_blue * inv_alpha) + 127UL) / 255UL;
        *pixel = gwes_encode_desktop_color(static_cast<U32>((out_red << 16) | (out_green << 8) | out_blue));
    }

    /*
     * Clip one cursor image rectangle to the visible desktop.
     *
     * The cursor hotspot can place the source bitmap partially off-screen, so
     * damage tracking and overlay composition both need a clipped screen-space
     * rectangle rather than the raw hotspot-relative origin.
     *
     * @param cursor Active cursor asset.
     * @param x Desktop pointer X coordinate.
     * @param y Desktop pointer Y coordinate.
     * @return Visible cursor rectangle, or an empty rectangle when fully off-screen.
     */
    static Rect gwes_cursor_rect(const GwesCursorAsset* cursor, unsigned long x, unsigned long y) {
        long origin_x;
        long origin_y;
        long clipped_left;
        long clipped_top;
        long clipped_right;
        long clipped_bottom;

        if (!cursor || !cursor->loaded || !g_render_state.ready) {
            return Rect{ 0U, 0U, 0U, 0U };
        }

        origin_x = static_cast<long>(x) - static_cast<long>(cursor->hotspot_x);
        origin_y = static_cast<long>(y) - static_cast<long>(cursor->hotspot_y);
        clipped_left = origin_x > 0L ? origin_x : 0L;
        clipped_top = origin_y > 0L ? origin_y : 0L;
        clipped_right = origin_x + static_cast<long>(cursor->width);
        clipped_bottom = origin_y + static_cast<long>(cursor->height);

        if (clipped_right <= 0L
            || clipped_bottom <= 0L
            || clipped_left >= static_cast<long>(g_render_state.compositor.desktop.width)
            || clipped_top >= static_cast<long>(g_render_state.compositor.desktop.height)) {
            return Rect{ 0U, 0U, 0U, 0U };
        }

        if (clipped_right > static_cast<long>(g_render_state.compositor.desktop.width)) {
            clipped_right = static_cast<long>(g_render_state.compositor.desktop.width);
        }
        if (clipped_bottom > static_cast<long>(g_render_state.compositor.desktop.height)) {
            clipped_bottom = static_cast<long>(g_render_state.compositor.desktop.height);
        }

        return Rect{
            static_cast<U32>(clipped_left),
            static_cast<U32>(clipped_top),
            static_cast<U32>(clipped_right - clipped_left),
            static_cast<U32>(clipped_bottom - clipped_top)
        };
    }

    /*
     * Composite the active cursor over one composed scanline chunk.
     *
     * The cursor is applied after all desktop and window content so the pointer
     * remains visible without mutating client surfaces or the framebuffer's
     * retained background.
     *
     * @param chunk_x Desktop X coordinate of the row buffer start.
     * @param chunk_width Number of pixels in the row buffer.
     * @param row_y Desktop Y coordinate being composed.
     * @return Nothing.
     */
    static void gwes_overlay_cursor_span(unsigned long chunk_x, unsigned long chunk_width, unsigned long row_y) {
        const GwesCursorAsset* cursor = g_render_state.pointer.active_cursor;
        Rect visible_cursor;
        long origin_x;
        long origin_y;
        unsigned long start_x;
        unsigned long end_x;
        unsigned long absolute_x;
        unsigned long source_y;

        if (!g_render_state.pointer.visible || !cursor || !cursor->loaded) {
            return;
        }

        visible_cursor = gwes_cursor_rect(cursor, g_render_state.pointer.x, g_render_state.pointer.y);
        if (visible_cursor.is_empty() || row_y < visible_cursor.y || row_y >= visible_cursor.bottom()) {
            return;
        }

        origin_x = static_cast<long>(g_render_state.pointer.x) - static_cast<long>(cursor->hotspot_x);
        origin_y = static_cast<long>(g_render_state.pointer.y) - static_cast<long>(cursor->hotspot_y);
        source_y = static_cast<unsigned long>(static_cast<long>(row_y) - origin_y);
        start_x = gwes_max_ul(chunk_x, visible_cursor.x);
        end_x = gwes_min_ul(chunk_x + chunk_width, visible_cursor.right());

        for (absolute_x = start_x; absolute_x < end_x; ++absolute_x) {
            const unsigned long source_x = static_cast<unsigned long>(static_cast<long>(absolute_x) - origin_x);
            const unsigned long pixel_offset = ((source_y * cursor->width) + source_x) * 4UL;
            const unsigned char* pixel = cursor->pixel_data + pixel_offset;
            const unsigned long alpha = pixel[3];
            const U32 color = static_cast<U32>((static_cast<unsigned long>(pixel[0]) << 16)
                | (static_cast<unsigned long>(pixel[1]) << 8)
                | static_cast<unsigned long>(pixel[2]));

            if (alpha == 0UL) {
                continue;
            }

            gwes_blend_row_pixel(&g_gwes_composite_row[absolute_x - chunk_x], color, alpha);
        }
    }

    static unsigned long gwes_measure_text_width(const char* text) {
        unsigned long width = 0UL;
        unsigned long index = 0UL;
        const GwesRasterFont* font = gwes_system_ui_font();

        if (!text) {
            return 0UL;
        }

        if (font) {
            while (text[index] != '\0') {
                const unsigned char ch = static_cast<unsigned char>(text[index]);
                const GwesRasterGlyph& glyph = font->glyphs[ch];

                width += glyph.advance;
                ++index;
            }
            return width;
        }

        (void)index;
        width = rosMiniFontMeasureText(text);
        return width;
    }

    static unsigned long gwes_measure_text_height(void) {
        const GwesRasterFont* font = gwes_system_ui_font();

        if (font) {
            return font->line_height;
        }

        return ROS_MINI_FONT_LINE_HEIGHT;
    }

    /*
     * Forward declaration for the ancestor clip helper used before its full
     * definition later in this file.
     */
    static Rect gwes_clip_to_ancestors(const Window& window, const Rect& rect);

    /*
     * Report whether one compositor window is currently visible.
     *
     * @param window Window record to inspect.
     * @return True when the visible flag is set.
     */
    static bool gwes_window_is_visible(const Window& window) {
        return has_window_flag(window.flags, WindowFlags::Visible);
    }

    /*
     * Report whether one compositor window owns server-drawn dialog chrome.
     *
     * @param window Window record to inspect.
     * @return True when the decorated flag is set.
     */
    static bool gwes_window_is_decorated(const Window& window) {
        return has_window_flag(window.flags, WindowFlags::Decorated);
    }

    /*
     * Report whether one compositor window is a child control.
     *
     * @param window Window record to inspect.
     * @return True when the child flag is set.
     */
    static bool gwes_window_is_child(const Window& window) {
        return has_window_flag(window.flags, WindowFlags::Child);
    }

    /*
     * Rebuild one client-visible style word from compositor flags.
     *
     * The retained renderer stores only the flag form after create-time layout
     * finishes, but resize requests still need the original child/decorated
     * semantics so the layout helper can clamp the new bounds consistently.
     *
     * @param window Window record to inspect.
     * @return Equivalent `ROS_WINDOW_STYLE_*` bitmask.
     */
    static unsigned long gwes_window_style_bits(const Window& window) {
        unsigned long style = 0UL;

        if (gwes_window_is_visible(window)) {
            style |= ROS_WINDOW_STYLE_VISIBLE;
        }
        if (gwes_window_is_decorated(window)) {
            style |= ROS_WINDOW_STYLE_DECORATED;
        }
        if (gwes_window_is_child(window)) {
            style |= ROS_WINDOW_STYLE_CHILD;
        }

        return style;
    }

    /*
     * Encode one server-generated color into the active display pixel format.
     *
     * @param color RGB color in `0x00RRGGBB` order.
     * @return Encoded framebuffer pixel.
     */
    static U32 gwes_encode_desktop_color(U32 color) {
        if (g_render_state.display.pixel_format != ROS_KERNEL_GUI_PIXEL_FORMAT_XBGR8888) {
            return color;
        }

        return ((color & 0x000000FFU) << 16)
            | (color & 0x0000FF00U)
            | ((color & 0x00FF0000U) >> 16);
    }

    /*
     * Blend two RGB colors with an eight-bit interpolation factor.
     *
     * @param first Start color.
     * @param second End color.
     * @param factor Blend factor in the inclusive range [0, 255].
     * @return Interpolated color.
     */
    static U32 gwes_blend_color(U32 first, U32 second, unsigned long factor) {
        const unsigned long inv = 255UL - factor;
        const unsigned long red = ((((first >> 16) & 0xFFUL) * inv) + (((second >> 16) & 0xFFUL) * factor)) / 255UL;
        const unsigned long green = ((((first >> 8) & 0xFFUL) * inv) + (((second >> 8) & 0xFFUL) * factor)) / 255UL;
        const unsigned long blue = (((first & 0xFFUL) * inv) + ((second & 0xFFUL) * factor)) / 255UL;

        return static_cast<U32>((red << 16) | (green << 8) | blue);
    }

    /*
     * Fill the reusable composition scanline with one encoded desktop color.
     *
     * @param width Number of pixels to fill.
     * @param color RGB desktop color.
     * @return Nothing.
     */
    static void gwes_fill_composite_row(unsigned long width, U32 color) {
        const U32 encoded = gwes_encode_desktop_color(color);
        unsigned long index;

        for (index = 0UL; index < width; ++index) {
            g_gwes_composite_row[index] = encoded;
        }
    }

    /*
     * Seed one composition chunk from the retained desktop surface.
     *
     * @param chunk_x Desktop X coordinate of the row buffer start.
     * @param chunk_width Number of pixels in the chunk.
     * @param row_y Desktop Y coordinate being composed.
     * @return Nothing.
     */
    static void gwes_fill_desktop_background_row(unsigned long chunk_x, unsigned long chunk_width, unsigned long row_y) {
        const U32* source_row;
        unsigned long index;

        if (!g_render_state.compositor.desktop.pixels || row_y >= g_render_state.compositor.desktop.height) {
            gwes_fill_composite_row(chunk_width, kDesktopColor);
            return;
        }

        source_row = reinterpret_cast<const U32*>(
            reinterpret_cast<const U8*>(g_render_state.compositor.desktop.pixels)
            + (row_y * g_render_state.compositor.desktop.pitch));
        for (index = 0UL; index < chunk_width; ++index) {
            g_gwes_composite_row[index] = source_row[chunk_x + index];
        }
    }

    /*
     * Present one composed scanline chunk to the framebuffer service.
     *
     * @param x Screen-space X coordinate.
     * @param y Screen-space Y coordinate.
     * @param width Number of pixels in the composed row.
     * @return Nothing.
     */
    static void gwes_present_tile(unsigned long x, unsigned long y, unsigned long width, unsigned long height) {
        RosKernelGuiPresentBuffer buffer;

        if (!g_render_state.ready || width == 0UL || height == 0UL) {
            return;
        }

        buffer.version = ROS_KERNEL_GUI_PRESENT_BUFFER_VERSION;
        buffer.x = static_cast<U32>(x);
        buffer.y = static_cast<U32>(y);
        buffer.width = static_cast<U32>(width);
        buffer.height = static_cast<U32>(height);
        buffer.pitch = static_cast<U32>(width * sizeof(U32));
        buffer.pixels = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(g_gwes_composite_tile));
        buffer.reserved = 0U;
        (void)gwes_control_gui(ROS_KERNEL_GUI_CONTROL_DISPLAY_PRESENT, static_cast<unsigned long>(reinterpret_cast<uintptr_t>(&buffer)));
    }

    /*
     * Find one retained window record by `HWND`.
     *
     * @param hwnd Stable GWES window handle.
     * @return Matching record, or NULL when the handle is unknown.
     */
    static GwesWindowRecord* gwes_find_window_record(unsigned long hwnd) {
        int index;

        for (index = 0; index < g_render_state.compositor.window_count; ++index) {
            GwesWindowRecord* record = &g_render_state.window_records[index];

            if (static_cast<unsigned long>(record->window.id) == hwnd) {
                return record;
            }
        }

        return nullptr;
    }

    /*
     * Choose one default top-level frame for a new undecorated or decorated window.
     *
     * @param surface_width Desktop width in pixels.
     * @param surface_height Desktop height in pixels.
     * @return Default window frame.
     */
    static Rect gwes_default_frame(unsigned long surface_width, unsigned long surface_height) {
        const unsigned long available_width = surface_width > 64U ? surface_width - 64U : surface_width;
        const unsigned long available_height = surface_height > 64U ? surface_height - 64U : surface_height;
        const unsigned long width = gwes_min_ul(kWindowDefaultWidth, available_width);
        const unsigned long height = gwes_min_ul(kWindowDefaultHeight, available_height);
        const unsigned long step = g_render_state.cascade_index++ % 8UL;
        const unsigned long x = 32U + (step * kWindowCascadeStep);
        const unsigned long y = 40U + (step * kWindowCascadeStep);
        const unsigned long clamped_x = (x + width > surface_width) ? (surface_width > width ? surface_width - width : 0U) : x;
        const unsigned long clamped_y = (y + height > surface_height) ? (surface_height > height ? surface_height - height : 0U) : y;

        return gwes_make_rect(clamped_x, clamped_y, width, height);
    }

    /*
     * Queue one damaged desktop rectangle for the next compose pass.
     *
     * @param rect Screen-space damage rectangle.
     * @return Nothing.
     */
    static void gwes_mark_damage(const Rect& rect) {
        if (!rect.is_empty()) {
            g_render_state.compositor.damage.add(rect);
        }
    }

    /*
     * Queue only the visible border bands of one placeholder frame.
     *
     * Repainting the full preview rectangle on every drag sample is far more
     * expensive than the Win9x-style outline actually needs. Restricting damage
     * to the four outline bands keeps placeholder motion responsive even for
     * large windows.
     *
     * @param rect Preview frame in desktop coordinates.
     * @return Nothing.
     */
    static void gwes_mark_preview_damage(const Rect& rect) {
        Rect clipped = rect;
        const U32 thickness = gwes_min_ul(kInteractionPreviewThickness, gwes_min_ul(rect.width, rect.height));
        U32 middle_height;

        if (clipped.is_empty() || thickness == 0U || !g_render_state.ready) {
            return;
        }

        if (clipped.x >= g_render_state.compositor.desktop.width || clipped.y >= g_render_state.compositor.desktop.height) {
            return;
        }
        if (clipped.right() > g_render_state.compositor.desktop.width) {
            clipped.width = g_render_state.compositor.desktop.width - clipped.x;
        }
        if (clipped.bottom() > g_render_state.compositor.desktop.height) {
            clipped.height = g_render_state.compositor.desktop.height - clipped.y;
        }
        if (clipped.is_empty()) {
            return;
        }

        gwes_mark_damage(Rect{ clipped.x, clipped.y, clipped.width, thickness });
        if (clipped.height > thickness) {
            gwes_mark_damage(Rect{ clipped.x, clipped.bottom() - thickness, clipped.width, thickness });
        }

        if (clipped.height <= (thickness * 2U)) {
            return;
        }

        middle_height = clipped.height - (thickness * 2U);
        gwes_mark_damage(Rect{ clipped.x, clipped.y + thickness, thickness, middle_height });
        if (clipped.width > thickness) {
            gwes_mark_damage(Rect{ clipped.right() - thickness, clipped.y + thickness, thickness, middle_height });
        }
    }

    /*
     * Clip one rectangle to the current desktop bounds.
     *
     * @param rect Rectangle to clip.
     * @return Clipped rectangle or an empty rectangle when fully outside.
     */
    static Rect gwes_clip_to_desktop(const Rect& rect) {
        return rect.intersection(gwes_desktop_rect());
    }

    /*
     * Report whether one desktop point falls inside the supplied rectangle.
     *
     * @param rect Rectangle to inspect.
     * @param x Desktop X coordinate.
     * @param y Desktop Y coordinate.
     * @return True when the point lies inside the rectangle bounds.
     */
    static bool gwes_rect_contains_point(const Rect& rect, unsigned long x, unsigned long y) {
        return !rect.is_empty()
            && (x >= rect.x)
            && (x < rect.right())
            && (y >= rect.y)
            && (y < rect.bottom());
    }

    /*
     * Clip one rectangle to the full client-area chain of its parent windows.
     *
     * @param window Window whose ancestry defines the clip chain.
     * @param rect Rectangle to clip.
     * @return Rectangle clipped to the desktop and every ancestor client area.
     */
    static Rect gwes_clip_to_ancestors(const Window& window, const Rect& rect) {
        Rect clipped = gwes_clip_to_desktop(rect);
        unsigned long parent_id = window.parent_id;

        while (!clipped.is_empty() && parent_id != 0UL) {
            GwesWindowRecord* parent = gwes_find_window_record(parent_id);

            if (!parent) {
                return Rect{ 0U, 0U, 0U, 0U };
            }

            clipped = clipped.intersection(parent->window.surface_frame);
            parent_id = parent->window.parent_id;
        }

        return clipped;
    }

    /*
     * Report whether one retained window belongs to the requested subtree.
     *
     * @param window Candidate window.
     * @param ancestor_hwnd Top-level or parent handle to test against.
     * @return True when the window is the ancestor itself or one of its descendants.
     */
    static bool gwes_window_belongs_to_subtree(const Window& window, unsigned long ancestor_hwnd) {
        unsigned long current_hwnd = static_cast<unsigned long>(window.id);
        unsigned long parent_hwnd = window.parent_id;

        if (current_hwnd == ancestor_hwnd) {
            return true;
        }

        while (parent_hwnd != 0UL) {
            GwesWindowRecord* parent_record;

            if (parent_hwnd == ancestor_hwnd) {
                return true;
            }

            parent_record = gwes_find_window_record(parent_hwnd);
            if (parent_record == nullptr) {
                break;
            }
            parent_hwnd = parent_record->window.parent_id;
        }

        return false;
    }

    /*
     * Rebuild the compositor pointer table after any record-order mutation.
     *
     * @return Nothing.
     */
    static void gwes_refresh_compositor_links(void) {
        int index;

        for (index = 0; index < g_render_state.compositor.window_count; ++index) {
            g_render_state.compositor.windows[index] = &g_render_state.window_records[index].window;
        }
    }

    /*
     * Compute the client surface rectangle for one outer dialog frame.
     *
     * @param outer_frame Full outer window frame.
     * @param client_offset_x Receives the client X inset inside the outer frame.
     * @param client_offset_y Receives the client Y inset inside the outer frame.
     * @return Client surface rectangle in desktop coordinates.
     */
    static Rect gwes_compute_decorated_surface_frame(const Rect& outer_frame, U32* client_offset_x, U32* client_offset_y) {
        const U32 client_width = outer_frame.width > (kDialogBorderThickness * 2U) ? (outer_frame.width - (kDialogBorderThickness * 2U)) : 1U;
        const U32 client_height = outer_frame.height > (kDialogTitleHeight + kDialogBorderThickness) ? (outer_frame.height - kDialogTitleHeight - kDialogBorderThickness) : 1U;

        if (client_offset_x) {
            *client_offset_x = kDialogBorderThickness;
        }
        if (client_offset_y) {
            *client_offset_y = kDialogTitleHeight;
        }

        return Rect{
            static_cast<U32>(outer_frame.x + kDialogBorderThickness),
            static_cast<U32>(outer_frame.y + kDialogTitleHeight),
            client_width,
            client_height
        };
    }

    /*
     * Create the on-screen geometry for one new window.
     *
     * @param parent_hwnd Optional parent handle.
     * @param x Requested X position.
     * @param y Requested Y position.
     * @param width Requested outer width.
     * @param height Requested outer height.
     * @param style Client-supplied style flags.
     * @param frame Receives the outer frame.
     * @param surface_frame Receives the client surface frame.
     * @param client_offset_x Receives the client X offset inside the outer frame.
     * @param client_offset_y Receives the client Y offset inside the outer frame.
     * @return Zero on success, or a negative status code on failure.
     */
    static long gwes_compute_window_layout(unsigned long parent_hwnd,
        long x,
        long y,
        unsigned long width,
        unsigned long height,
        unsigned long style,
        Rect* frame,
        Rect* surface_frame,
        U32* client_offset_x,
        U32* client_offset_y) {
        const bool is_child = (style & ROS_WINDOW_STYLE_CHILD) != 0UL;
        const bool is_decorated = (style & ROS_WINDOW_STYLE_DECORATED) != 0UL;
        unsigned long outer_width = width;
        unsigned long outer_height = height;
        Rect computed_frame;

        if (!frame || !surface_frame || !client_offset_x || !client_offset_y) {
            return -1L;
        }

        if (outer_width == 0UL) {
            outer_width = is_child ? kChildDefaultWidth : kWindowDefaultWidth;
        }
        if (outer_height == 0UL) {
            outer_height = is_child ? kChildDefaultHeight : kWindowDefaultHeight;
        }
        if (is_decorated) {
            outer_width = gwes_max_ul(outer_width, (kDialogBorderThickness * 2U) + 96U);
            outer_height = gwes_max_ul(outer_height, kDialogTitleHeight + kDialogBorderThickness + 48U);
        }

        outer_width = gwes_min_ul(outer_width, g_render_state.compositor.desktop.width);
        outer_height = gwes_min_ul(outer_height, g_render_state.compositor.desktop.height);

        if (is_child) {
            GwesWindowRecord* parent = gwes_find_window_record(parent_hwnd);

            if (!parent) {
                return -1L;
            }

            computed_frame = Rect{
                static_cast<U32>(parent->window.surface_frame.x + gwes_clamp_non_negative(x)),
                static_cast<U32>(parent->window.surface_frame.y + gwes_clamp_non_negative(y)),
                static_cast<U32>(outer_width),
                static_cast<U32>(outer_height)
            };
        }
        else {
            if (width == 0UL && height == 0UL && x == 0L && y == 0L) {
                computed_frame = gwes_default_frame(g_render_state.compositor.desktop.width, g_render_state.compositor.desktop.height);
                computed_frame.width = static_cast<U32>(outer_width);
                computed_frame.height = static_cast<U32>(outer_height);
            }
            else {
                const U32 requested_x = gwes_clamp_non_negative(x);
                const U32 requested_y = gwes_clamp_non_negative(y);
                const U32 clamped_x = (requested_x + outer_width > g_render_state.compositor.desktop.width)
                    ? (g_render_state.compositor.desktop.width > outer_width ? g_render_state.compositor.desktop.width - static_cast<U32>(outer_width) : 0U)
                    : requested_x;
                const U32 clamped_y = (requested_y + outer_height > g_render_state.compositor.desktop.height)
                    ? (g_render_state.compositor.desktop.height > outer_height ? g_render_state.compositor.desktop.height - static_cast<U32>(outer_height) : 0U)
                    : requested_y;

                computed_frame = Rect{ clamped_x, clamped_y, static_cast<U32>(outer_width), static_cast<U32>(outer_height) };
            }
        }

        *frame = computed_frame;
        if (is_decorated) {
            *surface_frame = gwes_compute_decorated_surface_frame(computed_frame, client_offset_x, client_offset_y);
        }
        else {
            *client_offset_x = 0U;
            *client_offset_y = 0U;
            *surface_frame = computed_frame;
        }

        return 0;
    }

    /*
     * Initialize one kernel GUI surface request for a specific window.
     *
     * @param view Request structure to initialize.
     * @param hwnd Stable GWES window handle.
     * @param owner_pid Owning client PID for create requests.
     * @param width Surface width for create requests.
     * @param height Surface height for create requests.
     * @param pixel_format Surface pixel format for create requests.
     * @return Nothing.
     */
    static void gwes_initialize_surface_view(GuiWindowSurfaceView* view, unsigned long hwnd, long owner_pid, unsigned long width, unsigned long height, unsigned long pixel_format) {
        if (!view) {
            return;
        }

        gwes_zero_memory(view, sizeof(*view));
        view->version = ROS_KERNEL_GUI_WINDOW_SURFACE_VIEW_VERSION;
        view->hwnd = hwnd;
        view->owner_pid = owner_pid;
        view->width = static_cast<U32>(width);
        view->height = static_cast<U32>(height);
        view->pixel_format = static_cast<U32>(pixel_format);
    }

    /*
     * Create one shared client surface and map it into GWES.
     *
     * @param hwnd Stable GWES window handle.
     * @param owner_pid Owning client process.
     * @param width Surface width in pixels.
     * @param height Surface height in pixels.
     * @param pixel_format Surface pixel format.
     * @param view Receives the mapped surface view.
     * @return Zero on success, or a negative status code on failure.
     */
    static long gwes_create_shared_surface(unsigned long hwnd, long owner_pid, unsigned long width, unsigned long height, unsigned long pixel_format, GuiWindowSurfaceView* view) {
        long status;

        if (!view) {
            return -1L;
        }

        gwes_initialize_surface_view(view, hwnd, owner_pid, width, height, pixel_format);
        status = gwes_control_gui(ROS_KERNEL_GUI_CONTROL_WINDOW_SURFACE_CREATE, static_cast<unsigned long>(reinterpret_cast<uintptr_t>(view)));
        gwes_render_tracef(
            "gwes.exe: render trace surface-create hwnd=%lu owner=%ld size=%lux%lu fmt=%lu status=%ld view=%08lx",
            hwnd,
            owner_pid,
            width,
            height,
            pixel_format,
            status,
            static_cast<unsigned long>(view->view_address));
        return status;
    }

    /*
     * Destroy one shared window surface owned by GWES.
     *
     * @param hwnd Stable GWES window handle.
     * @return Nothing.
     */
    static void gwes_destroy_shared_surface(unsigned long hwnd) {
        GuiWindowSurfaceView view;

        gwes_initialize_surface_view(&view, hwnd, 0L, 0UL, 0UL, 0UL);
        (void)gwes_control_gui(ROS_KERNEL_GUI_CONTROL_WINDOW_SURFACE_DESTROY, static_cast<unsigned long>(reinterpret_cast<uintptr_t>(&view)));
    }

    /*
     * Remove one retained window record from the compositor tables.
     *
     * @param index Window-table slot to remove.
     * @return Nothing.
     */
    static void gwes_remove_window_at(int index) {
        const int last_index = g_render_state.compositor.window_count - 1;

        if (index < 0 || index >= g_render_state.compositor.window_count) {
            return;
        }

        if (g_render_state.window_records[index].surface_view.hwnd != 0ULL) {
            gwes_destroy_shared_surface(static_cast<unsigned long>(g_render_state.window_records[index].surface_view.hwnd));
        }

        if (index != last_index) {
            g_render_state.window_records[index] = g_render_state.window_records[last_index];
            g_render_state.compositor.windows[index] = &g_render_state.window_records[index].window;
        }

        g_render_state.compositor.windows[last_index] = nullptr;
        gwes_zero_memory(&g_render_state.window_records[last_index], sizeof(g_render_state.window_records[last_index]));
        --g_render_state.compositor.window_count;
    }

    /*
     * Shift one moved parent's descendants by the same desktop delta.
     *
     * @param parent_hwnd Parent window handle that moved.
     * @param delta_x Signed desktop X delta.
     * @param delta_y Signed desktop Y delta.
     * @return Nothing.
     */
    static void gwes_shift_descendants(unsigned long parent_hwnd, long delta_x, long delta_y) {
        int index;

        for (index = 0; index < g_render_state.compositor.window_count; ++index) {
            Window* window = g_render_state.compositor.windows[index];

            if (!window || window->parent_id != parent_hwnd) {
                continue;
            }

            gwes_mark_damage(window->frame);
            window->frame.x = static_cast<U32>(static_cast<long>(window->frame.x) + delta_x);
            window->frame.y = static_cast<U32>(static_cast<long>(window->frame.y) + delta_y);
            window->surface_frame.x = static_cast<U32>(static_cast<long>(window->surface_frame.x) + delta_x);
            window->surface_frame.y = static_cast<U32>(static_cast<long>(window->surface_frame.y) + delta_y);
            gwes_mark_damage(window->frame);
            gwes_shift_descendants(static_cast<unsigned long>(window->id), delta_x, delta_y);
        }
    }

    /*
     * Overlay one solid server-drawn span onto the current composition row.
     *
     * @param chunk_x Desktop X position for the first pixel in the row buffer.
     * @param chunk_width Number of pixels in the row buffer.
     * @param start_x First desktop pixel to update.
     * @param end_x One-past-the-end desktop pixel to update.
     * @param color RGB color to write.
     * @return Nothing.
     */
    static void gwes_fill_row_span(unsigned long chunk_x, unsigned long chunk_width, unsigned long start_x, unsigned long end_x, U32 color) {
        const unsigned long clipped_start = gwes_max_ul(start_x, chunk_x);
        const unsigned long clipped_end = gwes_min_ul(end_x, chunk_x + chunk_width);
        const U32 encoded = gwes_encode_desktop_color(color);
        unsigned long x;

        if (clipped_end <= clipped_start) {
            return;
        }

        for (x = clipped_start; x < clipped_end; ++x) {
            g_gwes_composite_row[x - chunk_x] = encoded;
        }
    }

    /*
     * Copy one visible client-surface span into the current composition row.
     *
     * @param window Window whose client pixels should be copied.
     * @param row_y Desktop Y coordinate for the current row.
     * @param chunk_x Desktop X coordinate of the row buffer start.
     * @param chunk_width Number of pixels in the row buffer.
     * @return Nothing.
     */
    static void gwes_blit_surface_span(const Window& window, unsigned long row_y, unsigned long chunk_x, unsigned long chunk_width) {
        Rect visible_surface;
        unsigned long start_x;
        unsigned long end_x;
        const U32* source_row;
        unsigned long x;

        if (!window.surface.pixels) {
            return;
        }

        visible_surface = gwes_clip_to_ancestors(window, window.surface_frame);
        if (visible_surface.is_empty() || row_y < visible_surface.y || row_y >= visible_surface.bottom()) {
            return;
        }

        start_x = gwes_max_ul(chunk_x, visible_surface.x);
        end_x = gwes_min_ul(chunk_x + chunk_width, visible_surface.right());
        if (end_x <= start_x) {
            return;
        }

        source_row = reinterpret_cast<const U32*>(reinterpret_cast<const U8*>(window.surface.pixels) + ((row_y - window.surface_frame.y) * window.surface.pitch));
        for (x = start_x; x < end_x; ++x) {
            g_gwes_composite_row[x - chunk_x] = source_row[x - window.surface_frame.x];
        }
    }

    /*
     * Return the close-button rectangle for one decorated dialog.
     *
     * @param window Decorated top-level window.
     * @return Close-button rectangle in desktop coordinates.
     */
    static Rect gwes_close_button_rect(const Window& window) {
        if (!gwes_window_is_decorated(window) || window.frame.width <= (kDialogCloseSize + (kDialogCloseMargin * 2U))) {
            return Rect{ 0U, 0U, 0U, 0U };
        }

        return Rect{
            static_cast<U32>(window.frame.x + window.frame.width - kDialogCloseMargin - kDialogCloseSize),
            static_cast<U32>(window.frame.y + ((kDialogTitleHeight > kDialogCloseSize) ? ((kDialogTitleHeight - kDialogCloseSize) / 2U) : 0U)),
            kDialogCloseSize,
            kDialogCloseSize
        };
    }

    static void gwes_draw_row_text(unsigned long chunk_x, unsigned long chunk_width, unsigned long row_y, unsigned long x, unsigned long y, const char* text, U32 color);

    /*
     * Return the maximize-button rectangle for one decorated dialog.
     *
     * @param window Decorated top-level window.
     * @return Maximize-button rectangle in desktop coordinates.
     */
    static Rect gwes_maximize_button_rect(const Window& window) {
        if (!gwes_window_is_decorated(window) || window.frame.width <= ((kDialogCloseSize * 2U) + (kDialogCloseMargin * 2U) + kDialogControlButtonGap)) {
            return Rect{ 0U, 0U, 0U, 0U };
        }

        return Rect{
            static_cast<U32>(window.frame.x + window.frame.width - kDialogCloseMargin - kDialogCloseSize - kDialogControlButtonGap - kDialogCloseSize),
            static_cast<U32>(window.frame.y + ((kDialogTitleHeight > kDialogCloseSize) ? ((kDialogTitleHeight - kDialogCloseSize) / 2U) : 0U)),
            kDialogCloseSize,
            kDialogCloseSize
        };
    }

    /*
     * Return the minimize-button rectangle for one decorated dialog.
     *
     * @param window Decorated top-level window.
     * @return Minimize-button rectangle in desktop coordinates.
     */
    static Rect gwes_minimize_button_rect(const Window& window) {
        if (!gwes_window_is_decorated(window) || window.frame.width <= ((kDialogCloseSize * 3U) + (kDialogCloseMargin * 2U) + (kDialogControlButtonGap * 2U))) {
            return Rect{ 0U, 0U, 0U, 0U };
        }

        return Rect{
            static_cast<U32>(window.frame.x + window.frame.width - kDialogCloseMargin - kDialogCloseSize - kDialogControlButtonGap - kDialogCloseSize - kDialogControlButtonGap - kDialogCloseSize),
            static_cast<U32>(window.frame.y + ((kDialogTitleHeight > kDialogCloseSize) ? ((kDialogTitleHeight - kDialogCloseSize) / 2U) : 0U)),
            kDialogCloseSize,
            kDialogCloseSize
        };
    }

    /*
     * Paint one caption-button rectangle with a simple gradient and centered glyph.
     *
     * @param button Visible button bounds.
     * @param chunk_x Desktop X coordinate of the current row buffer.
     * @param chunk_width Number of pixels in the row buffer.
     * @param row_y Desktop Y coordinate being painted.
     * @param top Top gradient color.
     * @param bottom Bottom gradient color.
     * @param border Border color.
     * @param glyph ASCII glyph string.
     * @param glyph_color Glyph color.
     * @return Nothing.
     */
    static void gwes_paint_caption_button_span(const Rect& button,
        unsigned long chunk_x,
        unsigned long chunk_width,
        unsigned long row_y,
        U32 top,
        U32 bottom,
        U32 border,
        const char* glyph,
        U32 glyph_color) {
        const unsigned long local_y = row_y - button.y;
        const unsigned long factor = (button.height <= 1U) ? 255UL : ((local_y * 255UL) / (button.height - 1U));
        const U32 fill_color = gwes_blend_color(top, bottom, factor);
        unsigned long absolute_x;

        for (absolute_x = gwes_max_ul(chunk_x, button.x); absolute_x < gwes_min_ul(chunk_x + chunk_width, button.right()); ++absolute_x) {
            const unsigned long local_x = absolute_x - button.x;
            const bool is_border = local_x == 0U
                || (local_x + 1U) == button.width
                || local_y == 0U
                || (local_y + 1U) == button.height;

            g_gwes_composite_row[absolute_x - chunk_x] = gwes_encode_desktop_color(is_border ? border : fill_color);
        }

        gwes_draw_row_text(
            chunk_x,
            chunk_width,
            row_y,
            button.x + ((button.width > gwes_measure_text_width(glyph)) ? ((button.width - gwes_measure_text_width(glyph)) / 2U) : 1U),
            button.y + ((button.height > gwes_measure_text_height()) ? ((button.height - gwes_measure_text_height()) / 2U) : 1U),
            glyph,
            glyph_color);
    }

    /*
     * Draw one line of bitmap text into the current composition row.
     *
     * @param chunk_x Desktop X coordinate of the row buffer start.
     * @param chunk_width Number of pixels in the row buffer.
     * @param row_y Desktop Y coordinate being composed.
     * @param x Text origin X coordinate.
     * @param y Text origin Y coordinate.
     * @param text Null-terminated string to draw.
     * @param color RGB text color.
     * @return Nothing.
     */
    static void gwes_draw_row_text(unsigned long chunk_x, unsigned long chunk_width, unsigned long row_y, unsigned long x, unsigned long y, const char* text, U32 color) {
        const GwesRasterFont* font = gwes_system_ui_font();

        if (font) {
            unsigned long cursor_x = x;
            unsigned long index = 0UL;
            const unsigned long baseline_y = y + static_cast<unsigned long>(font->ascent);

            if (!text) {
                return;
            }

            while (text[index] != '\0') {
                const unsigned char ch = static_cast<unsigned char>(text[index]);
                const GwesRasterGlyph& glyph = font->glyphs[ch];

                if (glyph.coverage && glyph.width != 0UL && glyph.height != 0UL) {
                    unsigned long glyph_row;

                    for (glyph_row = 0UL; glyph_row < glyph.height; ++glyph_row) {
                        const long draw_y = static_cast<long>(baseline_y) - glyph.bitmap_top + static_cast<long>(glyph_row);

                        if (draw_y != static_cast<long>(row_y)) {
                            continue;
                        }

                        for (unsigned long glyph_col = 0UL; glyph_col < glyph.width; ++glyph_col) {
                            const unsigned char alpha = glyph.coverage[(glyph_row * glyph.width) + glyph_col];
                            const long draw_x = static_cast<long>(cursor_x) + glyph.bitmap_left + static_cast<long>(glyph_col);

                            if (alpha == 0U || draw_x < static_cast<long>(chunk_x) || draw_x >= static_cast<long>(chunk_x + chunk_width)) {
                                continue;
                            }

                            gwes_blend_row_pixel(&g_gwes_composite_row[static_cast<unsigned long>(draw_x) - chunk_x], color, static_cast<unsigned long>(alpha));
                        }
                    }
                }

                cursor_x += glyph.advance;
                ++index;
            }

            return;
        }

        unsigned long cursor_x = x;
        unsigned long cursor_y = y;
        unsigned long index = 0UL;

        if (!text) {
            return;
        }

        while (text[index] != '\0') {
            const unsigned char ch = static_cast<unsigned char>(text[index]);
            const RosMiniFontGlyph* glyph;
            const uint8_t* coverage;
            const unsigned long baseline_y = cursor_y + static_cast<unsigned long>(ROS_MINI_FONT_ASCENT);
            unsigned long glyph_row;

            if (ch == '\r') {
                ++index;
                continue;
            }
            if (ch == '\n') {
                cursor_x = x;
                cursor_y += ROS_MINI_FONT_LINE_HEIGHT;
                ++index;
                continue;
            }

            glyph = rosMiniFontGlyph(static_cast<unsigned long>(ch));
            coverage = rosMiniFontGlyphCoverage(glyph);
            if (coverage && glyph->width != 0U && glyph->height != 0U) {
                for (glyph_row = 0UL; glyph_row < glyph->height; ++glyph_row) {
                    const long draw_y = static_cast<long>(baseline_y) - glyph->bitmap_top + static_cast<long>(glyph_row);

                    if (draw_y != static_cast<long>(row_y)) {
                        continue;
                    }

                    for (unsigned long glyph_col = 0UL; glyph_col < glyph->width; ++glyph_col) {
                        const unsigned long alpha = coverage[(glyph_row * glyph->width) + glyph_col];
                        const long draw_x = static_cast<long>(cursor_x) + glyph->bitmap_left + static_cast<long>(glyph_col);

                        if (alpha == 0UL || draw_x < static_cast<long>(chunk_x) || draw_x >= static_cast<long>(chunk_x + chunk_width)) {
                            continue;
                        }

                        gwes_blend_row_pixel(&g_gwes_composite_row[static_cast<unsigned long>(draw_x) - chunk_x], color, alpha);
                    }
                }
            }

            cursor_x += glyph->advance;
            ++index;
        }
    }

    /*
     * Draw the server-owned non-client chrome for one decorated top-level window.
     *
     * @param window Decorated window to paint.
     * @param row_y Desktop Y coordinate for the current row.
     * @param chunk_x Desktop X coordinate of the row buffer start.
     * @param chunk_width Number of pixels in the row buffer.
     * @return Nothing.
     */
    static void gwes_paint_window_chrome_span(const Window& window, unsigned long row_y, unsigned long chunk_x, unsigned long chunk_width) {
        Rect visible_outer;
        Rect close_button;
        Rect maximize_button;
        Rect minimize_button;
        unsigned long start_x;
        unsigned long end_x;
        unsigned long absolute_x;

        if (!gwes_window_is_decorated(window)) {
            return;
        }

        visible_outer = gwes_clip_to_ancestors(window, window.frame);
        if (visible_outer.is_empty() || row_y < visible_outer.y || row_y >= visible_outer.bottom()) {
            return;
        }

        start_x = gwes_max_ul(chunk_x, visible_outer.x);
        end_x = gwes_min_ul(chunk_x + chunk_width, visible_outer.right());
        if (end_x <= start_x) {
            return;
        }

        for (absolute_x = start_x; absolute_x < end_x; ++absolute_x) {
            const unsigned long local_x = absolute_x - window.frame.x;
            const unsigned long local_y = row_y - window.frame.y;
            const bool inside_client = absolute_x >= window.surface_frame.x
                && absolute_x < window.surface_frame.right()
                && row_y >= window.surface_frame.y
                && row_y < window.surface_frame.bottom();
            U32 color = 0U;
            bool write_pixel = true;

            if (inside_client) {
                continue;
            }

            if (local_y < kDialogTitleHeight) {
                const unsigned long factor = (kDialogTitleHeight <= 1U) ? 255UL : ((local_y * 255UL) / (kDialogTitleHeight - 1U));

                color = gwes_blend_color(kDialogTitleTop, kDialogTitleBottom, factor);
                if (local_y == 0U || local_x == 0U || (local_x + 1U) == window.frame.width) {
                    color = kDialogFrameOuter;
                }
                else if (local_y == 1U || local_x == 1U || (local_x + 2U) == window.frame.width) {
                    color = kDialogFrameInner;
                }
            }
            else if (local_x < kDialogBorderThickness || local_x >= (window.frame.width - kDialogBorderThickness) || local_y >= (window.frame.height - kDialogBorderThickness)) {
                if (local_x == 0U || (local_x + 1U) == window.frame.width || (local_y + 1U) == window.frame.height) {
                    color = kDialogFrameOuter;
                }
                else {
                    color = kDialogFrameInner;
                }
            }
            else {
                write_pixel = false;
            }

            if (write_pixel) {
                g_gwes_composite_row[absolute_x - chunk_x] = gwes_encode_desktop_color(color);
            }
        }

        close_button = gwes_close_button_rect(window).intersection(visible_outer);
        maximize_button = gwes_maximize_button_rect(window).intersection(visible_outer);
        minimize_button = gwes_minimize_button_rect(window).intersection(visible_outer);
        if (!close_button.is_empty() && row_y >= close_button.y && row_y < close_button.bottom()) {
            gwes_paint_caption_button_span(close_button, chunk_x, chunk_width, row_y, kDialogCloseTop, kDialogCloseBottom, kDialogCloseBorder, "X", kDialogCloseGlyph);
        }
        if (!maximize_button.is_empty() && row_y >= maximize_button.y && row_y < maximize_button.bottom()) {
            gwes_paint_caption_button_span(maximize_button, chunk_x, chunk_width, row_y, kDialogMaxTop, kDialogMaxBottom, kDialogMaxBorder, "+", kDialogMaxGlyph);
        }
        if (!minimize_button.is_empty() && row_y >= minimize_button.y && row_y < minimize_button.bottom()) {
            gwes_paint_caption_button_span(minimize_button, chunk_x, chunk_width, row_y, kDialogMinTop, kDialogMinBottom, kDialogMinBorder, "-", kDialogMinGlyph);
        }

        gwes_draw_row_text(
            chunk_x,
            chunk_width,
            row_y,
            window.frame.x + kDialogCaptionPaddingX + 1U,
            window.frame.y + kDialogCaptionPaddingY + 1U,
            window.title,
            kDialogTitleShadow);
        gwes_draw_row_text(
            chunk_x,
            chunk_width,
            row_y,
            window.frame.x + kDialogCaptionPaddingX,
            window.frame.y + kDialogCaptionPaddingY,
            window.title,
            kDialogTitleText);
    }

    /*
     * Composite one damaged desktop rectangle from the retained window set.
     *
     * @param rect Screen-space damage rectangle.
     * @return Nothing.
     */
    static void gwes_composite_damage_rect(const Rect& rect) {
        const Rect clipped = gwes_clip_to_desktop(rect);
        U32 row;

        if (clipped.is_empty()) {
            return;
        }

        for (row = clipped.y; row < clipped.bottom();) {
            const unsigned long batch_height = gwes_min_ul(static_cast<unsigned long>(clipped.bottom() - row), kCompositeTileRows);
            unsigned long remaining_width = clipped.width;
            unsigned long offset_x = 0UL;

            while (remaining_width != 0UL) {
                const unsigned long chunk_width = gwes_min_ul(remaining_width, kCompositeRowCapacity);
                unsigned long tile_row;

                for (tile_row = 0UL; tile_row < batch_height; ++tile_row) {
                    const unsigned long row_y = row + tile_row;
                    int index;

                    gwes_bind_composite_row(tile_row, chunk_width);
                    gwes_fill_desktop_background_row(clipped.x + offset_x, chunk_width, row_y);
                    for (index = 0; index < g_render_state.compositor.window_count; ++index) {
                        Window* window = g_render_state.compositor.windows[index];

                        if (!window || !gwes_window_is_visible(*window)) {
                            continue;
                        }

                        gwes_paint_window_chrome_span(*window, row_y, clipped.x + offset_x, chunk_width);
                        gwes_blit_surface_span(*window, row_y, clipped.x + offset_x, chunk_width);
                    }

                    gwes_overlay_interaction_preview_span(clipped.x + offset_x, chunk_width, row_y);
                    gwes_overlay_cursor_span(clipped.x + offset_x, chunk_width, row_y);
                }

                gwes_present_tile(clipped.x + offset_x, row, chunk_width, batch_height);
                remaining_width -= chunk_width;
                offset_x += chunk_width;
            }

            row += static_cast<U32>(batch_height);
        }
    }

} // namespace

extern "C" long gwes_render_init(void) {
    const GwesCursorAsset* default_cursor;
    long status;

    if (g_render_state.ready) {
        return 0;
    }

    gwes_zero_memory(&g_render_state, sizeof(g_render_state));
    if (gwes_control_gui(ROS_KERNEL_GUI_CONTROL_DISPLAY_INFO, static_cast<unsigned long>(reinterpret_cast<uintptr_t>(&g_render_state.display))) != 0) {
        return -1;
    }
    if (g_render_state.display.version != ROS_KERNEL_GUI_DISPLAY_INFO_VERSION
        || g_render_state.display.width == 0U
        || g_render_state.display.height == 0U
        || g_render_state.display.pitch < g_render_state.display.width * sizeof(U32)) {
        return -1;
    }

    g_render_state.compositor.desktop.width = g_render_state.display.width;
    g_render_state.compositor.desktop.height = g_render_state.display.height;
    g_render_state.compositor.desktop.pitch = 0U;
    g_render_state.compositor.desktop.pixels = nullptr;
    g_render_state.compositor.window_count = 0;
    g_render_state.compositor.damage.clear();
    g_render_state.cascade_index = 0UL;
    g_render_state.desktop_signature_font_loaded = 0;
    status = gwes_allocate_desktop_surface();
    if (status < 0L) {
        return status;
    }
    if (gwes_gdi_load_font(0, 12UL, &g_render_state.desktop_signature_font) >= 0L) {
        g_render_state.desktop_signature_font_loaded = 1;
    }
    default_cursor = gwes_resolve_cursor_asset(kGwesDefaultCursorPath);
    g_render_state.pointer.visible = default_cursor != nullptr;
    g_render_state.pointer.x = g_render_state.compositor.desktop.width / 2U;
    g_render_state.pointer.y = g_render_state.compositor.desktop.height / 2U;
    g_render_state.pointer.active_cursor = default_cursor;
    (void)gwes_load_system_ui_raster_font();
    gwes_render_desktop_surface();
    g_render_state.ready = 1;
    gwes_render_request_full_redraw();
    return 0;
}

extern "C" void gwes_render_shutdown(void) {
    while (g_render_state.compositor.window_count > 0) {
        gwes_remove_window_at(g_render_state.compositor.window_count - 1);
    }

    gwes_release_font_cache();
    gwes_release_cursor_cache();
    if (g_render_state.desktop_signature_font_loaded) {
        (void)gwes_gdi_unload_font(&g_render_state.desktop_signature_font);
        g_render_state.desktop_signature_font_loaded = 0;
    }
    gwes_release_desktop_surface();
    g_render_state.ready = 0;
    g_render_state.compositor.damage.clear();
    gwes_zero_memory(&g_render_state.display, sizeof(g_render_state.display));
}

extern "C" long gwes_render_create_window(unsigned long hwnd,
    long owner_pid,
    unsigned long parent_hwnd,
    long x,
    long y,
    unsigned long width,
    unsigned long height,
    unsigned long style,
    const char* class_name,
    const char* title) {
    GwesWindowRecord* record;
    Rect frame;
    Rect surface_frame;
    U32 client_offset_x;
    U32 client_offset_y;
    WindowFlags flags = WindowFlags::Opaque;
    long layout_status;
    long surface_status;

    if (!g_render_state.ready || g_render_state.compositor.window_count >= static_cast<int>(kMaxWindowCount)) {
        gwes_render_tracef(
            "gwes.exe: render trace create rejected ready=%d window_count=%d max=%lu",
            g_render_state.ready,
            g_render_state.compositor.window_count,
            kMaxWindowCount);
        return -1L;
    }
    layout_status = gwes_compute_window_layout(parent_hwnd, x, y, width, height, style, &frame, &surface_frame, &client_offset_x, &client_offset_y);
    if (layout_status != 0) {
        gwes_render_tracef(
            "gwes.exe: render trace layout failed hwnd=%lu parent=%lu style=%lu req=%ld,%ld %lux%lu status=%ld",
            hwnd,
            parent_hwnd,
            style,
            x,
            y,
            width,
            height,
            layout_status);
        return layout_status;
    }
    gwes_render_tracef(
        "gwes.exe: render trace layout hwnd=%lu frame=%lu,%lu %lux%lu surface=%lu,%lu %lux%lu style=%lu",
        hwnd,
        static_cast<unsigned long>(frame.x),
        static_cast<unsigned long>(frame.y),
        static_cast<unsigned long>(frame.width),
        static_cast<unsigned long>(frame.height),
        static_cast<unsigned long>(surface_frame.x),
        static_cast<unsigned long>(surface_frame.y),
        static_cast<unsigned long>(surface_frame.width),
        static_cast<unsigned long>(surface_frame.height),
        style);

    record = &g_render_state.window_records[g_render_state.compositor.window_count];
    gwes_zero_memory(record, sizeof(*record));
    record->window.id = static_cast<int>(hwnd);
    record->window.frame = frame;
    record->window.surface_frame = surface_frame;
    record->window.parent_id = parent_hwnd;
    record->window.client_offset_x = client_offset_x;
    record->window.client_offset_y = client_offset_y;
    gwes_copy_text(record->window.class_name, sizeof(record->window.class_name), class_name);
    gwes_copy_text(record->window.title, sizeof(record->window.title), title);

    if ((style & ROS_WINDOW_STYLE_VISIBLE) != 0UL) {
        flags |= WindowFlags::Visible;
    }
    if ((style & ROS_WINDOW_STYLE_DECORATED) != 0UL) {
        flags |= WindowFlags::Decorated;
    }
    if ((style & ROS_WINDOW_STYLE_CHILD) != 0UL) {
        flags |= WindowFlags::Child;
    }
    record->window.flags = flags;

    surface_status = gwes_create_shared_surface(hwnd, owner_pid, surface_frame.width, surface_frame.height, g_render_state.display.pixel_format, &record->surface_view);
    if (surface_status != 0) {
        gwes_render_tracef(
            "gwes.exe: render trace surface-create failed hwnd=%lu status=%ld",
            hwnd,
            surface_status);
        gwes_zero_memory(record, sizeof(*record));
        return surface_status;
    }

    record->window.surface.width = record->surface_view.width;
    record->window.surface.height = record->surface_view.height;
    record->window.surface.pitch = record->surface_view.pitch;
    record->window.surface.pixels = reinterpret_cast<U32*>(static_cast<uintptr_t>(record->surface_view.view_address));
    gwes_zero_memory(record->window.surface.pixels, record->surface_view.view_size);

    g_render_state.compositor.windows[g_render_state.compositor.window_count] = &record->window;
    ++g_render_state.compositor.window_count;
    gwes_mark_damage(frame);
    return 0;
}

extern "C" void gwes_render_destroy_window(unsigned long hwnd) {
    int index;

    for (index = 0; index < g_render_state.compositor.window_count; ++index) {
        Window* window = g_render_state.compositor.windows[index];

        if (!window || static_cast<unsigned long>(window->id) != hwnd) {
            continue;
        }

        gwes_mark_damage(window->frame);
        gwes_remove_window_at(index);
        return;
    }
}

extern "C" long gwes_render_move_window(unsigned long hwnd, long x, long y) {
    GwesWindowRecord* record = gwes_find_window_record(hwnd);
    Rect old_frame;
    Rect old_surface_frame;
    long delta_x;
    long delta_y;

    if (!record) {
        return -1;
    }

    old_frame = record->window.frame;
    old_surface_frame = record->window.surface_frame;
    record->window.frame.x = gwes_clamp_non_negative(x);
    record->window.frame.y = gwes_clamp_non_negative(y);
    record->window.surface_frame.x = record->window.frame.x + record->window.client_offset_x;
    record->window.surface_frame.y = record->window.frame.y + record->window.client_offset_y;
    delta_x = static_cast<long>(record->window.frame.x) - static_cast<long>(old_frame.x);
    delta_y = static_cast<long>(record->window.frame.y) - static_cast<long>(old_frame.y);

    gwes_mark_damage(old_frame);
    gwes_mark_damage(old_surface_frame);
    gwes_mark_damage(record->window.frame);
    gwes_mark_damage(record->window.surface_frame);
    if (delta_x != 0L || delta_y != 0L) {
        gwes_shift_descendants(hwnd, delta_x, delta_y);
    }
    return 0;
}

extern "C" long gwes_render_resize_window(unsigned long hwnd, long x, long y, unsigned long width, unsigned long height) {
    GwesWindowRecord* record = gwes_find_window_record(hwnd);
    const unsigned long style = record ? gwes_window_style_bits(record->window) : 0UL;
    Rect old_frame;
    Rect old_surface_frame;
    GuiWindowSurfaceView old_surface_view;
    U32 old_client_offset_x;
    U32 old_client_offset_y;
    Rect new_frame;
    Rect new_surface_frame;
    U32 new_client_offset_x;
    U32 new_client_offset_y;
    GuiWindowSurfaceView resized_view;
    long layout_status;
    long surface_status;
    long delta_x;
    long delta_y;

    if (!record) {
        return -1L;
    }

    layout_status = gwes_compute_window_layout(
        record->window.parent_id,
        x,
        y,
        width,
        height,
        style,
        &new_frame,
        &new_surface_frame,
        &new_client_offset_x,
        &new_client_offset_y);
    if (layout_status != 0L) {
        return layout_status;
    }

    old_frame = record->window.frame;
    old_surface_frame = record->window.surface_frame;
    old_surface_view = record->surface_view;
    old_client_offset_x = record->window.client_offset_x;
    old_client_offset_y = record->window.client_offset_y;

    gwes_mark_damage(old_frame);
    gwes_mark_damage(old_surface_frame);

    if (record->surface_view.hwnd != 0ULL) {
        gwes_destroy_shared_surface(hwnd);
    }
    gwes_zero_memory(&record->surface_view, sizeof(record->surface_view));
    gwes_zero_memory(&resized_view, sizeof(resized_view));

    surface_status = gwes_create_shared_surface(
        hwnd,
        record->surface_view.owner_pid != 0L ? record->surface_view.owner_pid : old_surface_view.owner_pid,
        new_surface_frame.width,
        new_surface_frame.height,
        g_render_state.display.pixel_format,
        &resized_view);
    if (surface_status != 0L) {
        if (old_surface_view.hwnd != 0ULL) {
            (void)gwes_create_shared_surface(
                hwnd,
                old_surface_view.owner_pid,
                old_surface_view.width,
                old_surface_view.height,
                old_surface_view.pixel_format,
                &record->surface_view);
            record->window.frame = old_frame;
            record->window.surface_frame = old_surface_frame;
            record->window.client_offset_x = old_client_offset_x;
            record->window.client_offset_y = old_client_offset_y;
            record->window.surface.width = old_surface_view.width;
            record->window.surface.height = old_surface_view.height;
            record->window.surface.pitch = old_surface_view.pitch;
            record->window.surface.pixels = reinterpret_cast<U32*>(static_cast<uintptr_t>(record->surface_view.view_address));
        }
        return surface_status;
    }

    record->surface_view = resized_view;
    record->window.frame = new_frame;
    record->window.surface_frame = new_surface_frame;
    record->window.client_offset_x = new_client_offset_x;
    record->window.client_offset_y = new_client_offset_y;
    record->window.surface.width = record->surface_view.width;
    record->window.surface.height = record->surface_view.height;
    record->window.surface.pitch = record->surface_view.pitch;
    record->window.surface.pixels = reinterpret_cast<U32*>(static_cast<uintptr_t>(record->surface_view.view_address));
    gwes_zero_memory(record->window.surface.pixels, record->surface_view.view_size);

    gwes_mark_damage(record->window.frame);
    gwes_mark_damage(record->window.surface_frame);
    delta_x = static_cast<long>(record->window.frame.x) - static_cast<long>(old_frame.x);
    delta_y = static_cast<long>(record->window.frame.y) - static_cast<long>(old_frame.y);
    if (delta_x != 0L || delta_y != 0L) {
        gwes_shift_descendants(hwnd, delta_x, delta_y);
    }

    return 0L;
}

extern "C" long gwes_render_raise_window(unsigned long hwnd) {
    GwesWindowRecord* reordered;
    int subtree_count = 0;
    int write_index = 0;
    int index;

    if (!g_render_state.ready || gwes_find_window_record(hwnd) == nullptr) {
        return -1L;
    }

    reordered = static_cast<GwesWindowRecord*>(malloc(sizeof(GwesWindowRecord) * static_cast<unsigned long>(g_render_state.compositor.window_count)));
    if (reordered == nullptr) {
        return -1L;
    }

    for (index = 0; index < g_render_state.compositor.window_count; ++index) {
        if (gwes_window_belongs_to_subtree(g_render_state.window_records[index].window, hwnd)) {
            ++subtree_count;
            continue;
        }

        reordered[write_index++] = g_render_state.window_records[index];
    }

    for (index = 0; index < g_render_state.compositor.window_count; ++index) {
        if (!gwes_window_belongs_to_subtree(g_render_state.window_records[index].window, hwnd)) {
            continue;
        }

        reordered[write_index++] = g_render_state.window_records[index];
    }

    if (subtree_count == 0 || write_index != g_render_state.compositor.window_count) {
        free(reordered);
        return -1L;
    }

    for (index = 0; index < g_render_state.compositor.window_count; ++index) {
        g_render_state.window_records[index] = reordered[index];
    }

    free(reordered);
    gwes_refresh_compositor_links();
    gwes_render_request_full_redraw();
    return 0L;
}

extern "C" long gwes_render_hit_test(unsigned long x, unsigned long y, unsigned long* hwnd, long* local_x, long* local_y) {
    int index;

    if (hwnd == nullptr || local_x == nullptr || local_y == nullptr) {
        return -1L;
    }

    for (index = g_render_state.compositor.window_count - 1; index >= 0; --index) {
        const Window* window = &g_render_state.window_records[index].window;
        const Rect visible_surface = gwes_clip_to_ancestors(*window, window->surface_frame);
        const Rect visible_outer = gwes_window_is_decorated(*window)
            ? gwes_clip_to_ancestors(*window, window->frame)
            : Rect{ 0U, 0U, 0U, 0U };

        if (!gwes_window_is_visible(*window)) {
            continue;
        }
        if (!gwes_rect_contains_point(visible_surface, x, y)
            && !(gwes_window_is_decorated(*window) && gwes_rect_contains_point(visible_outer, x, y))) {
            continue;
        }

        *hwnd = static_cast<unsigned long>(window->id);
        *local_x = static_cast<long>(x) - static_cast<long>(window->surface_frame.x);
        *local_y = static_cast<long>(y) - static_cast<long>(window->surface_frame.y);
        return 0L;
    }

    return -1L;
}

extern "C" long gwes_render_query_desktop_size(unsigned long* width, unsigned long* height) {
    if (!g_render_state.ready || width == nullptr || height == nullptr) {
        return -1L;
    }

    *width = g_render_state.compositor.desktop.width;
    *height = g_render_state.compositor.desktop.height;
    return 0L;
}

/*
 * Show or update one interactive move/resize placeholder rectangle.
 *
 * @param x Preview outer-frame X coordinate.
 * @param y Preview outer-frame Y coordinate.
 * @param width Preview outer-frame width.
 * @param height Preview outer-frame height.
 * @return Nothing.
 */
extern "C" void gwes_render_set_interaction_placeholder(long x, long y, unsigned long width, unsigned long height) {
    const Rect next_frame = gwes_make_interaction_preview_rect(x, y, width, height);

    if (!g_render_state.ready || next_frame.is_empty()) {
        return;
    }

    if (g_render_state.interaction_preview.active) {
        if (gwes_rect_equals(g_render_state.interaction_preview.frame, next_frame)) {
            return;
        }

        gwes_mark_preview_damage(g_render_state.interaction_preview.frame);
    }

    g_render_state.interaction_preview.active = 1;
    g_render_state.interaction_preview.frame = next_frame;
    gwes_mark_preview_damage(next_frame);
}

/*
 * Hide the current interactive move/resize placeholder rectangle.
 *
 * @return Nothing.
 */
extern "C" void gwes_render_clear_interaction_placeholder(void) {
    if (!g_render_state.ready || !g_render_state.interaction_preview.active) {
        return;
    }

    gwes_mark_preview_damage(g_render_state.interaction_preview.frame);
    g_render_state.interaction_preview.active = 0;
    g_render_state.interaction_preview.frame = Rect{ 0U, 0U, 0U, 0U };
}

extern "C" long gwes_render_translate_pointer(unsigned long hwnd, unsigned long x, unsigned long y, long* local_x, long* local_y) {
    GwesWindowRecord* record = gwes_find_window_record(hwnd);

    if ((record == nullptr) || (local_x == nullptr) || (local_y == nullptr)) {
        return -1L;
    }

    *local_x = static_cast<long>(x) - static_cast<long>(record->window.surface_frame.x);
    *local_y = static_cast<long>(y) - static_cast<long>(record->window.surface_frame.y);
    return 0L;
}

extern "C" void gwes_render_update_pointer(unsigned long x, unsigned long y, int visible, const char* cursor_path) {
    const GwesCursorAsset* old_cursor = g_render_state.pointer.active_cursor;
    const int old_visible = g_render_state.pointer.visible;
    const unsigned long old_x = g_render_state.pointer.x;
    const unsigned long old_y = g_render_state.pointer.y;
    const GwesCursorAsset* new_cursor = nullptr;
    unsigned long clamped_x = x;
    unsigned long clamped_y = y;
    const int new_visible = visible != 0;

    if (g_render_state.ready && g_render_state.display.width != 0U && clamped_x >= g_render_state.display.width) {
        clamped_x = g_render_state.display.width - 1U;
    }
    if (g_render_state.ready && g_render_state.display.height != 0U && clamped_y >= g_render_state.display.height) {
        clamped_y = g_render_state.display.height - 1U;
    }
    if (new_visible) {
        new_cursor = gwes_resolve_cursor_asset(cursor_path);
    }

    if (old_visible == new_visible
        && old_x == clamped_x
        && old_y == clamped_y
        && old_cursor == new_cursor) {
        return;
    }

    if (old_visible && old_cursor) {
        gwes_mark_damage(gwes_cursor_rect(old_cursor, old_x, old_y));
    }

    g_render_state.pointer.visible = new_visible;
    g_render_state.pointer.x = clamped_x;
    g_render_state.pointer.y = clamped_y;
    g_render_state.pointer.active_cursor = new_cursor;

    if (new_visible && new_cursor) {
        gwes_mark_damage(gwes_cursor_rect(new_cursor, clamped_x, clamped_y));
    }
}

extern "C" void gwes_render_mark_window_dirty(unsigned long hwnd, unsigned long x, unsigned long y, unsigned long width, unsigned long height) {
    GwesWindowRecord* record = gwes_find_window_record(hwnd);

    if (!record || width == 0UL || height == 0UL) {
        return;
    }

    gwes_mark_damage(Rect{
        static_cast<U32>(record->window.surface_frame.x + x),
        static_cast<U32>(record->window.surface_frame.y + y),
        static_cast<U32>(width),
        static_cast<U32>(height)
        });
}

extern "C" void gwes_render_request_full_redraw(void) {
    gwes_mark_damage(gwes_desktop_rect());
}

extern "C" void gwes_render_present_if_needed(void) {
    int index;

    if (!g_render_state.ready || g_render_state.compositor.damage.count == 0) {
        return;
    }

    for (index = 0; index < g_render_state.compositor.damage.count; ++index) {
        gwes_composite_damage_rect(g_render_state.compositor.damage.rects[index]);
    }

    g_render_state.compositor.damage.clear();
}
