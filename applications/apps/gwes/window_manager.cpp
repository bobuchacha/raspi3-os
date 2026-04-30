#include "gwes_server.h"
#include "render.h"

#define ROS_GDI_NO_IMPORTS 1
#include "app/gdi.h"
#undef ROS_GDI_NO_IMPORTS
#include "app/mini_font.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

#ifndef DEBUG_ENABLE_GWES_MENU_TRACE
#define DEBUG_ENABLE_GWES_MENU_TRACE 1
#endif

#ifndef DEBUG_ENABLE_GWES_MENU_TIMING_TRACE
#define DEBUG_ENABLE_GWES_MENU_TIMING_TRACE 0
#endif

    constexpr unsigned long kGwesMenuNoHover = 0xFFFFFFFFUL;
    constexpr unsigned long kGwesMenuSubmenuOpenDelayMsec = 150UL;
    constexpr unsigned long kGwesMenuSubmenuCloseDelayMsec = 220UL;
    constexpr unsigned long kGwesMenuOuterBorder = 1UL;
    constexpr unsigned long kGwesMenuInnerBorder = 1UL;
    constexpr unsigned long kGwesMenuRowHeight = 24UL;
    constexpr unsigned long kGwesMenuSeparatorHeight = 8UL;
    constexpr unsigned long kGwesMenuTextInsetX = 12UL;
    constexpr unsigned long kGwesMenuTextInsetY = 7UL;
    constexpr unsigned long kGwesMenuMinWidth = 120UL;
    constexpr unsigned long kGwesMenuBackground = 0x00F4F4F2UL;
    constexpr unsigned long kGwesMenuInnerBackground = 0x00FFFFFFUL;
    constexpr unsigned long kGwesMenuFrame = 0x007C7C7CUL;
    constexpr unsigned long kGwesMenuInnerFrame = 0x00D5D5D5UL;
    constexpr unsigned long kGwesMenuHoverFill = 0x00D7E8F8UL;
    constexpr unsigned long kGwesMenuHoverFrame = 0x0077A9D9UL;
    constexpr unsigned long kGwesMenuText = 0x00171717UL;
    constexpr unsigned long kGwesMenuDisabledText = 0x00888888UL;
    constexpr unsigned long kGwesMenuSeparator = 0x00C7CDD4UL;
    constexpr unsigned long kGwesMenuArrowTextInset = 12UL;
    constexpr char kGwesMenuClassName[] = "builtin.menu";

    /*
     * Emit one narrow popup-menu lifecycle trace line.
     *
     * Menu faults are currently easiest to diagnose from the serial log during
     * interactive QEMU runs, so these traces stay scoped to popup open, paint,
     * close, and completion transitions rather than broad GWES message flow.
     *
     * @param fmt `printf`-style format string.
     * @param ... Format arguments.
     * @return Nothing.
     */
    void gwes_menu_tracef(const char* fmt, ...) {
#if DEBUG_ENABLE_GWES_MENU_TRACE
        char line[256];
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
     * Capture one start timestamp for optional menu timing traces.
     *
     * @return Millisecond timestamp when timing traces are enabled, else zero.
     */
    unsigned long gwes_menu_timing_begin(void) {
#if DEBUG_ENABLE_GWES_MENU_TIMING_TRACE
    return getUptimeMs();
#else
    return 0UL;
#endif
    }

    /*
     * Emit one optional timing trace tagged with elapsed milliseconds.
     *
     * @param start Timestamp returned by `gwes_menu_timing_begin`.
     * @param fmt `printf`-style suffix format string.
     * @param ... Format arguments.
     * @return Nothing.
     */
    void gwes_menu_timing_endf(unsigned long start, const char* fmt, ...) {
#if DEBUG_ENABLE_GWES_MENU_TIMING_TRACE
    char suffix[160];
    char line[256];
    va_list args;
    unsigned long elapsed = getUptimeMs() - start;

    va_start(args, fmt);
    vsnprintf(suffix, sizeof(suffix), fmt, args);
    va_end(args);
    snprintf(line, sizeof(line), "gwes.exe: menu timing dt=%lums %s", elapsed, suffix);
    writeLine(line);
#else
    (void)start;
    (void)fmt;
#endif
    }

    /*
     * Build one encoded surface pixel for the popup-menu software painter.
     *
     * @param pixel_format Target surface pixel format.
     * @param color RGB color in `0x00RRGGBB` order.
     * @return Encoded 32-bit surface pixel.
     */
    unsigned long gwes_menu_encode_color(unsigned long pixel_format, unsigned long color) {
        const unsigned long rgb = color & 0x00FFFFFFUL;

        if (pixel_format != ROS_KERNEL_GUI_PIXEL_FORMAT_XBGR8888) {
            return 0xFF000000UL | rgb;
        }

        return 0xFF000000UL
            | ((rgb & 0x000000FFUL) << 16)
            | (rgb & 0x0000FF00UL)
            | ((rgb & 0x00FF0000UL) >> 16);
    }

    /*
     * Decode one stored surface pixel back into RGB order for alpha blending.
     *
     * @param pixel_format Surface pixel format.
     * @param encoded Encoded pixel already stored in the surface.
     * @return RGB color in `0x00RRGGBB` order.
     */
    unsigned long gwes_menu_decode_color(unsigned long pixel_format, unsigned long encoded) {
        if (pixel_format != ROS_KERNEL_GUI_PIXEL_FORMAT_XBGR8888) {
            return encoded & 0x00FFFFFFUL;
        }

        return ((encoded & 0x000000FFUL) << 16)
            | (encoded & 0x0000FF00UL)
            | ((encoded & 0x00FF0000UL) >> 16);
    }

    /*
     * Blend two RGB colors with one eight-bit interpolation factor.
     *
     * @param first Start color.
     * @param second End color.
     * @param factor Blend factor in the inclusive range [0, 255].
     * @return Interpolated RGB color.
     */
    unsigned long gwes_menu_blend_color(unsigned long first, unsigned long second, unsigned long factor) {
        const unsigned long inv = 255UL - factor;
        const unsigned long red = ((((first >> 16) & 0xFFUL) * inv) + (((second >> 16) & 0xFFUL) * factor)) / 255UL;
        const unsigned long green = ((((first >> 8) & 0xFFUL) * inv) + (((second >> 8) & 0xFFUL) * factor)) / 255UL;
        const unsigned long blue = (((first & 0xFFUL) * inv) + ((second & 0xFFUL) * factor)) / 255UL;

        return (red << 16) | (green << 8) | blue;
    }

    /*
     * Blend one glyph pixel directly into the mapped popup surface.
     *
     * @param surface Target mapped surface.
     * @param x Pixel X coordinate.
     * @param y Pixel Y coordinate.
     * @param color RGB glyph color.
     * @param alpha Coverage alpha in the range [0, 255].
     * @return Nothing.
     */
    void gwes_menu_blend_surface_pixel(const RosGdiSurface* surface, unsigned long x, unsigned long y, unsigned long color, unsigned long alpha) {
        uint32_t* pixel;

        if (surface == NULL || surface->pixels == NULL || alpha == 0UL) {
            return;
        }
        if (x >= surface->width || y >= surface->height) {
            return;
        }

        pixel = (uint32_t*)((uint8_t*)surface->pixels + (y * surface->pitch)) + x;
        if (alpha >= 255UL) {
            *pixel = (uint32_t)gwes_menu_encode_color(surface->pixel_format, color);
            return;
        }

        *pixel = (uint32_t)gwes_menu_encode_color(
            surface->pixel_format,
            gwes_menu_blend_color(gwes_menu_decode_color(surface->pixel_format, *pixel), color, alpha));
    }

    /*
     * Fill one rectangle directly in the mapped popup surface.
     *
     * @param surface Writable mapped surface.
     * @param x Rectangle X coordinate.
     * @param y Rectangle Y coordinate.
     * @param width Rectangle width.
     * @param height Rectangle height.
     * @param color RGB fill color.
     * @return Nothing.
     */
    void gwes_menu_fill_surface_rect(const RosGdiSurface* surface, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long color) {
        unsigned long row;
        unsigned long col;
        unsigned long end_x;
        unsigned long end_y;
        unsigned long encoded_color;

        if (surface == NULL || surface->pixels == NULL || width == 0UL || height == 0UL) {
            return;
        }
        if (x >= surface->width || y >= surface->height) {
            return;
        }

        end_x = x + width;
        end_y = y + height;
        if (end_x > surface->width) {
            end_x = surface->width;
        }
        if (end_y > surface->height) {
            end_y = surface->height;
        }

        encoded_color = gwes_menu_encode_color(surface->pixel_format, color);
        for (row = y; row < end_y; ++row) {
            uint32_t* pixels = (uint32_t*)((uint8_t*)surface->pixels + (row * surface->pitch));

            for (col = x; col < end_x; ++col) {
                pixels[col] = (uint32_t)encoded_color;
            }
        }
    }

    /*
     * Draw one one-pixel frame rectangle into the popup surface.
     *
     * @param surface Writable mapped surface.
     * @param x Rectangle X coordinate.
     * @param y Rectangle Y coordinate.
     * @param width Rectangle width.
     * @param height Rectangle height.
     * @param color RGB frame color.
     * @return Nothing.
     */
    void gwes_menu_frame_surface_rect(const RosGdiSurface* surface, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long color) {
        if (width < 2UL || height < 2UL) {
            return;
        }

        gwes_menu_fill_surface_rect(surface, x, y, width, 1UL, color);
        gwes_menu_fill_surface_rect(surface, x, y + height - 1UL, width, 1UL, color);
        gwes_menu_fill_surface_rect(surface, x, y, 1UL, height, color);
        gwes_menu_fill_surface_rect(surface, x + width - 1UL, y, 1UL, height, color);
    }

    /*
     * Draw one ASCII text run into the popup surface with the built-in mini font.
     *
     * @param surface Target mapped surface.
     * @param x Baseline-left X coordinate.
     * @param y Baseline-top Y coordinate.
     * @param text Null-terminated caption.
     * @param color RGB text color.
     * @return Nothing.
     */
    void gwes_menu_draw_text(const RosGdiSurface* surface, unsigned long x, unsigned long y, const char* text, unsigned long color) {
        unsigned long cursor_x = x;
        unsigned long cursor_y = y;
        unsigned long index;

        if (surface == NULL || text == NULL) {
            return;
        }

        for (index = 0UL; text[index] != '\0'; ++index) {
            const unsigned char ch = (unsigned char)text[index];
            const RosMiniFontGlyph* glyph;
            const uint8_t* coverage;
            unsigned long row;
            unsigned long baseline_y;

            if (ch == '\r') {
                continue;
            }
            if (ch == '\n') {
                cursor_x = x;
                cursor_y += ROS_MINI_FONT_LINE_HEIGHT;
                continue;
            }

            glyph = rosMiniFontGlyph((unsigned long)ch);
            coverage = rosMiniFontGlyphCoverage(glyph);
            baseline_y = cursor_y + (unsigned long)ROS_MINI_FONT_ASCENT;
            if (coverage != NULL && glyph->width != 0U && glyph->height != 0U) {
                for (row = 0UL; row < glyph->height; ++row) {
                    unsigned long col;
                    const long draw_y = (long)baseline_y - glyph->bitmap_top + (long)row;

                    if (draw_y < 0L) {
                        continue;
                    }

                    for (col = 0UL; col < glyph->width; ++col) {
                        const unsigned long alpha = coverage[(row * glyph->width) + col];
                        const long draw_x = (long)cursor_x + glyph->bitmap_left + (long)col;

                        if (alpha == 0UL || draw_x < 0L) {
                            continue;
                        }

                        gwes_menu_blend_surface_pixel(surface, (unsigned long)draw_x, (unsigned long)draw_y, color, alpha);
                    }
                }
            }

            cursor_x += glyph->advance;
        }
    }

    /* Forward declarations for the popup-row layout helpers used below. */
    unsigned long gwes_menu_item_height(const RosWindowMenuItem* item);
    unsigned long gwes_menu_item_top(const RosWindowMenuModel* model, unsigned long menu_index, unsigned long item_index);

    /*
     * Paint one logical popup-menu row into the mapped popup surface.
     *
     * Pointer hover used to repaint the whole popup and rerender every caption
     * on each move. Popup geometry is stable after creation, so hover changes
     * only need to redraw the rows whose highlight state changed.
     *
     * @param surface Writable popup surface.
     * @param model Rooted menu tree backing the popup.
     * @param menu_index Menu index shown by the popup.
     * @param item_index Row index inside that menu.
     * @param hovered Non-zero when the row should render hovered.
     * @return Nothing.
     */
    void gwes_menu_paint_item(const RosGdiSurface* surface,
        const RosWindowMenuModel* model,
        unsigned long menu_index,
        unsigned long item_index,
        int hovered) {
        unsigned long first_item;
        const RosWindowMenuItem* item;
        unsigned long top;
        unsigned long height;
        const unsigned long row_x = surface != NULL && surface->width > 2UL ? 1UL : 0UL;
        const unsigned long row_width = surface != NULL && surface->width > 2UL ? surface->width - 2UL : (surface != NULL ? surface->width : 0UL);

        if (surface == NULL || model == NULL || menu_index >= model->menu_count) {
            return;
        }
        if (item_index >= model->menus[menu_index].item_count) {
            return;
        }

        first_item = model->menus[menu_index].first_item;
        item = &model->items[first_item + item_index];
        top = gwes_menu_item_top(model, menu_index, item_index);
        height = gwes_menu_item_height(item);

        /*
         * Restore the popup interior background for the entire row before
         * adding hover chrome or separator strokes so old highlights do not
         * leave stale pixels behind.
         */
        gwes_menu_fill_surface_rect(surface, row_x, top, row_width, height, kGwesMenuInnerBackground);

        if ((item->flags & ROS_MENU_ITEM_FLAG_SEPARATOR) != 0UL) {
            const unsigned long separator_y = top + (height / 2UL);

            if (separator_y < surface->height) {
                gwes_menu_fill_surface_rect(surface,
                    kGwesMenuTextInsetX,
                    separator_y,
                    surface->width > (kGwesMenuTextInsetX * 2UL) ? surface->width - (kGwesMenuTextInsetX * 2UL) : 1UL,
                    1UL,
                    kGwesMenuSeparator);
            }
            return;
        }

        if (hovered && (item->flags & ROS_MENU_ITEM_FLAG_DISABLED) == 0UL) {
            gwes_menu_fill_surface_rect(surface,
                3UL,
                top + 1UL,
                surface->width > 6UL ? surface->width - 6UL : 1UL,
                height > 2UL ? height - 2UL : 1UL,
                kGwesMenuHoverFill);
            gwes_menu_frame_surface_rect(surface,
                3UL,
                top + 1UL,
                surface->width > 6UL ? surface->width - 6UL : 2UL,
                height > 2UL ? height - 2UL : 2UL,
                kGwesMenuHoverFrame);
        }

        gwes_menu_draw_text(surface,
            kGwesMenuTextInsetX,
            top + kGwesMenuTextInsetY,
            item->text,
            (item->flags & ROS_MENU_ITEM_FLAG_DISABLED) != 0UL ? kGwesMenuDisabledText : kGwesMenuText);
        if (item->submenu_index != ROS_WINDOW_MENU_INVALID_INDEX) {
            gwes_menu_draw_text(surface,
                surface->width > kGwesMenuArrowTextInset ? surface->width - kGwesMenuArrowTextInset : 0UL,
                top + kGwesMenuTextInsetY,
                ">",
                kGwesMenuText);
        }
    }

    /*
     * Repaint one popup row and dirty only that strip.
     *
     * Hover tracking normally changes at most two rows, so restricting damage
     * to the affected bands cuts the dominant menu-overhead hot path.
     *
     * @param popup_slot Popup slot to update.
     * @param item_index Row index inside that popup menu.
     * @return Nothing.
     */
    void gwes_menu_repaint_popup_item(unsigned long popup_slot, unsigned long item_index) {
        const unsigned long trace_start = gwes_menu_timing_begin();
        RosGdiSurface surface;
        const RosWindowMenuModel* model = &g_gwes_menu_session.model;
        unsigned long menu_index;
        unsigned long first_item;
        unsigned long top;
        unsigned long height;

        if (!g_gwes_menu_session.active || popup_slot >= g_gwes_menu_session.open_popup_count) {
            return;
        }

        menu_index = g_gwes_menu_session.popups[popup_slot].menu_index;
        if (menu_index >= model->menu_count || item_index >= model->menus[menu_index].item_count) {
            return;
        }
        if (gwes_render_get_window_surface(g_gwes_menu_session.popups[popup_slot].hwnd, &surface) < 0L) {
            return;
        }

        first_item = model->menus[menu_index].first_item;
        top = gwes_menu_item_top(model, menu_index, item_index);
        height = gwes_menu_item_height(&model->items[first_item + item_index]);
        gwes_menu_paint_item(&surface,
            model,
            menu_index,
            item_index,
            g_gwes_menu_session.popups[popup_slot].hover_index == item_index);
        gwes_render_mark_window_dirty(g_gwes_menu_session.popups[popup_slot].hwnd,
            surface.width > 2UL ? 1UL : 0UL,
            top,
            surface.width > 2UL ? surface.width - 2UL : surface.width,
            height);
        gwes_menu_timing_endf(trace_start,
            "repaint-row slot=%lu item=%lu height=%lu",
            popup_slot,
            item_index,
            height);
    }

    /*
     * Repaint just the rows affected by one hover-state transition.
     *
     * The previously hovered row must lose its highlight and the newly hovered
     * row must gain one. Everything else in the popup remains unchanged.
     *
     * @param popup_slot Popup slot whose hover changed.
     * @param previous_hover Previously hovered row, or `kGwesMenuNoHover`.
     * @param next_hover Newly hovered row, or `kGwesMenuNoHover`.
     * @return Nothing.
     */
    void gwes_menu_repaint_hover_transition(unsigned long popup_slot, unsigned long previous_hover, unsigned long next_hover) {
        if (previous_hover == next_hover) {
            return;
        }

        if (previous_hover != kGwesMenuNoHover) {
            gwes_menu_repaint_popup_item(popup_slot, previous_hover);
        }
        if (next_hover != kGwesMenuNoHover) {
            gwes_menu_repaint_popup_item(popup_slot, next_hover);
        }
    }

    /*
     * Validate one rooted popup-menu tree received from `window.dll`.
     *
     * @param model Shared popup-menu tree copied from the client process.
     * @return Non-zero when the tree is structurally valid.
     */
    int gwes_menu_model_is_valid(const RosWindowMenuModel* model) {
        unsigned long index;

        if (model == NULL
            || model->version != ROS_WINDOW_MENU_MODEL_VERSION
            || model->menu_count == 0UL
            || model->menu_count > ROS_WINDOW_MENU_MAX_MENUS
            || model->item_count == 0UL
            || model->item_count > ROS_WINDOW_MENU_MAX_ITEMS
            || model->root_menu_index >= model->menu_count) {
            return 0;
        }

        for (index = 0UL; index < model->menu_count; ++index) {
            if (model->menus[index].first_item > model->item_count) {
                return 0;
            }
            if (model->menus[index].item_count > (model->item_count - model->menus[index].first_item)) {
                return 0;
            }
        }
        for (index = 0UL; index < model->item_count; ++index) {
            if (model->items[index].submenu_index != ROS_WINDOW_MENU_INVALID_INDEX
                && model->items[index].submenu_index >= model->menu_count) {
                return 0;
            }
        }

        return 1;
    }

    /*
     * Return the pixel width required for one visible menu row caption.
     *
     * @param item Popup-menu item to measure.
     * @return Caption width in pixels.
     */
    unsigned long gwes_menu_measure_item_text(const RosWindowMenuItem* item) {
        if (item == NULL || (item->flags & ROS_MENU_ITEM_FLAG_SEPARATOR) != 0UL) {
            return 0UL;
        }

        return rosMiniFontMeasureText(item->text);
    }

    /*
     * Return the vertical extent of one popup-menu row.
     *
     * @param item Popup-menu item whose row height is needed.
     * @return Row height in pixels.
     */
    unsigned long gwes_menu_item_height(const RosWindowMenuItem* item) {
        return (item != NULL && (item->flags & ROS_MENU_ITEM_FLAG_SEPARATOR) != 0UL)
            ? kGwesMenuSeparatorHeight
            : kGwesMenuRowHeight;
    }

    /*
     * Compute the popup width and height for one menu inside the rooted tree.
     *
     * @param model Rooted menu tree copied from the client process.
     * @param menu_index Target menu index inside the rooted tree.
     * @param width Receives the popup width.
     * @param height Receives the popup height.
     * @return Nothing.
     */
    void gwes_menu_compute_popup_extent(const RosWindowMenuModel* model, unsigned long menu_index, unsigned long* width, unsigned long* height) {
        unsigned long max_text_width = 0UL;
        unsigned long total_height = kGwesMenuOuterBorder + kGwesMenuInnerBorder + kGwesMenuInnerBorder + kGwesMenuOuterBorder;
        unsigned long first_item;
        unsigned long item_count;
        unsigned long index;

        if (width != NULL) {
            *width = kGwesMenuMinWidth;
        }
        if (height != NULL) {
            *height = total_height;
        }
        if (model == NULL || menu_index >= model->menu_count) {
            return;
        }

        first_item = model->menus[menu_index].first_item;
        item_count = model->menus[menu_index].item_count;
        for (index = 0UL; index < item_count; ++index) {
            const RosWindowMenuItem* item = &model->items[first_item + index];
            unsigned long item_width = gwes_menu_measure_item_text(item);

            if (item->submenu_index != ROS_WINDOW_MENU_INVALID_INDEX) {
                item_width += 16UL;
            }
            if (item_width > max_text_width) {
                max_text_width = item_width;
            }
            total_height += gwes_menu_item_height(item);
        }

        if (width != NULL) {
            const unsigned long desired_width = max_text_width + (kGwesMenuTextInsetX * 2UL) + 28UL;

            *width = desired_width > kGwesMenuMinWidth ? desired_width : kGwesMenuMinWidth;
        }
        if (height != NULL) {
            *height = total_height;
        }
    }

    /*
     * Return the Y origin for one popup-menu row inside the painted popup.
     *
     * @param model Rooted popup-menu tree.
     * @param menu_index Target menu index inside the tree.
     * @param item_index Row index within the target menu.
     * @return Row top Y coordinate in popup-local pixels.
     */
    unsigned long gwes_menu_item_top(const RosWindowMenuModel* model, unsigned long menu_index, unsigned long item_index) {
        unsigned long top = kGwesMenuOuterBorder + kGwesMenuInnerBorder;
        unsigned long first_item;
        unsigned long item_count;
        unsigned long index;

        if (model == NULL || menu_index >= model->menu_count) {
            return top;
        }

        first_item = model->menus[menu_index].first_item;
        item_count = model->menus[menu_index].item_count;
        for (index = 0UL; index < item_index && index < item_count; ++index) {
            top += gwes_menu_item_height(&model->items[first_item + index]);
        }

        return top;
    }

    /*
     * Resolve the hovered popup-menu row for one popup-local pointer position.
     *
     * @param model Rooted popup-menu tree.
     * @param menu_index Target menu index.
     * @param y Popup-local pointer Y coordinate.
     * @return Matching row index, or `kGwesMenuNoHover` when no row is hit.
     */
    unsigned long gwes_menu_hit_test_y(const RosWindowMenuModel* model, unsigned long menu_index, long y) {
        unsigned long first_item;
        unsigned long item_count;
        unsigned long index;

        if (model == NULL || menu_index >= model->menu_count || y < 0L) {
            return kGwesMenuNoHover;
        }

        first_item = model->menus[menu_index].first_item;
        item_count = model->menus[menu_index].item_count;
        for (index = 0UL; index < item_count; ++index) {
            const unsigned long top = gwes_menu_item_top(model, menu_index, index);
            const unsigned long height = gwes_menu_item_height(&model->items[first_item + index]);

            if ((unsigned long)y >= top && (unsigned long)y < (top + height)) {
                return index;
            }
        }

        return kGwesMenuNoHover;
    }

    /*
     * Return whether one popup-menu row can produce a command selection.
     *
     * @param item Candidate popup-menu item.
     * @return Non-zero when the item is selectable.
     */
    int gwes_menu_item_is_selectable(const RosWindowMenuItem* item) {
        return item != NULL
            && (item->flags & ROS_MENU_ITEM_FLAG_SEPARATOR) == 0UL
            && (item->flags & ROS_MENU_ITEM_FLAG_DISABLED) == 0UL
            && item->command_id != 0UL;
    }

    /*
     * Fold one ASCII character to lower case for mnemonic comparisons.
     *
     * @param ch Input ASCII code point.
     * @return Lower-case ASCII code point when applicable.
     */
    unsigned long gwes_menu_fold_ascii(unsigned long ch) {
        if (ch >= 'A' && ch <= 'Z') {
            return ch + ('a' - 'A');
        }

        return ch;
    }

    /*
     * Find the first selectable or submenu row whose mnemonic matches the supplied key.
     *
     * @param model Rooted popup-menu tree.
     * @param menu_index Target menu index.
     * @param key ASCII key code.
     * @return Matching row index, or `kGwesMenuNoHover` when none match.
     */
    unsigned long gwes_menu_find_hotkey_match(const RosWindowMenuModel* model, unsigned long menu_index, unsigned long key) {
        const unsigned long folded_key = gwes_menu_fold_ascii(key);
        unsigned long first_item;
        unsigned long item_count;
        unsigned long index;

        if (model == NULL || menu_index >= model->menu_count || folded_key == 0UL) {
            return kGwesMenuNoHover;
        }

        first_item = model->menus[menu_index].first_item;
        item_count = model->menus[menu_index].item_count;
        for (index = 0UL; index < item_count; ++index) {
            const RosWindowMenuItem* item = &model->items[first_item + index];

            if (((item->flags & ROS_MENU_ITEM_FLAG_SEPARATOR) != 0UL)
                || ((item->flags & ROS_MENU_ITEM_FLAG_DISABLED) != 0UL)
                || item->hotkey == 0UL) {
                continue;
            }
            if (gwes_menu_fold_ascii(item->hotkey) == folded_key) {
                return index;
            }
        }

        return kGwesMenuNoHover;
    }

    /*
     * Find the first enabled row inside one popup menu.
     *
     * @param model Rooted popup-menu tree.
     * @param menu_index Target menu index.
     * @return Row index within the target menu, or `kGwesMenuNoHover` when none match.
     */
    unsigned long gwes_menu_first_selectable_item(const RosWindowMenuModel* model, unsigned long menu_index) {
        unsigned long first_item;
        unsigned long item_count;
        unsigned long index;

        if (model == NULL || menu_index >= model->menu_count) {
            return kGwesMenuNoHover;
        }

        first_item = model->menus[menu_index].first_item;
        item_count = model->menus[menu_index].item_count;
        for (index = 0UL; index < item_count; ++index) {
            const RosWindowMenuItem* item = &model->items[first_item + index];

            if ((item->flags & ROS_MENU_ITEM_FLAG_SEPARATOR) == 0UL && (item->flags & ROS_MENU_ITEM_FLAG_DISABLED) == 0UL) {
                return index;
            }
        }

        return kGwesMenuNoHover;
    }

    /*
     * Find the popup slot that owns one specific popup window.
     *
     * @param session Live popup-menu session.
     * @param hwnd Popup window handle.
     * @return Popup slot index, or `ROS_WINDOW_MENU_INVALID_INDEX` when none match.
     */
    unsigned long gwes_menu_popup_slot_for_hwnd(const GwesMenuSession* session, unsigned long hwnd) {
        unsigned long index;

        if (session == NULL || hwnd == 0UL) {
            return ROS_WINDOW_MENU_INVALID_INDEX;
        }

        for (index = 0UL; index < session->open_popup_count; ++index) {
            if (session->popups[index].hwnd == hwnd) {
                return index;
            }
        }

        return ROS_WINDOW_MENU_INVALID_INDEX;
    }

    /*
     * Find the popup slot that already shows one specific menu index.
     *
     * @param session Live popup-menu session.
     * @param menu_index Menu index inside the rooted tree.
     * @return Popup slot index, or `ROS_WINDOW_MENU_INVALID_INDEX` when none match.
     */
    unsigned long gwes_menu_popup_slot_for_menu(const GwesMenuSession* session, unsigned long menu_index) {
        unsigned long index;

        if (session == NULL) {
            return ROS_WINDOW_MENU_INVALID_INDEX;
        }

        for (index = 0UL; index < session->open_popup_count; ++index) {
            if (session->popups[index].menu_index == menu_index) {
                return index;
            }
        }

        return ROS_WINDOW_MENU_INVALID_INDEX;
    }

    /*
     * Destroy the popup windows at or deeper than one cascade slot.
     *
     * @param first_slot First popup slot to close.
     * @return Nothing.
     */
    void gwes_menu_close_popups_from(unsigned long first_slot) {
        const unsigned long trace_start = gwes_menu_timing_begin();
        unsigned long closed_count = 0UL;

        while (g_gwes_menu_session.open_popup_count > first_slot) {
            const unsigned long closing_slot = g_gwes_menu_session.open_popup_count - 1UL;
            const unsigned long hwnd = g_gwes_menu_session.popups[closing_slot].hwnd;
            const unsigned long menu_index = g_gwes_menu_session.popups[closing_slot].menu_index;
            GwesWindowRecord* popup = gwes_find_window_any(hwnd);

            gwes_menu_tracef(
                "gwes.exe: menu trace popup-close slot=%lu hwnd=%lu menu=%lu remaining=%lu",
                closing_slot,
                hwnd,
                menu_index,
                g_gwes_menu_session.open_popup_count);

            g_gwes_menu_session.popups[closing_slot].hwnd = 0UL;
            g_gwes_menu_session.popups[closing_slot].menu_index = ROS_WINDOW_MENU_INVALID_INDEX;
            g_gwes_menu_session.popups[closing_slot].hover_index = kGwesMenuNoHover;
            g_gwes_menu_session.popups[closing_slot].popup_width = 0UL;
            g_gwes_menu_session.popups[closing_slot].popup_height = 0UL;
            --g_gwes_menu_session.open_popup_count;
            ++closed_count;
            if (popup != NULL) {
                gwes_release_window_record(popup, 1);
            }
        }

        gwes_render_refresh_window_groups();
        gwes_menu_timing_endf(trace_start,
            "close-popups first-slot=%lu closed=%lu remaining=%lu",
            first_slot,
            closed_count,
            g_gwes_menu_session.open_popup_count);
    }

    /*
     * Create one popup window for one menu node inside the rooted tree.
     *
     * @param menu_index Menu index to show.
     * @param popup_x Desktop X coordinate for the popup origin.
     * @param popup_y Desktop Y coordinate for the popup origin.
     * @param popup_slot Receives the newly assigned popup slot.
     * @return Zero on success, or a negative status code on failure.
     */
    long gwes_menu_create_popup(unsigned long menu_index, long popup_x, long popup_y, unsigned long* popup_slot) {
        GwesWindowRecord* popup_window;
        unsigned long popup_width;
        unsigned long popup_height;
        long status;

        if (!g_gwes_menu_session.active || popup_slot == NULL || g_gwes_menu_session.open_popup_count >= ROS_WINDOW_MENU_MAX_MENUS) {
            return ROS_USER_IPC_STATUS_NO_SPACE;
        }

        gwes_menu_compute_popup_extent(&g_gwes_menu_session.model, menu_index, &popup_width, &popup_height);
        popup_window = gwes_allocate_window();
        if (popup_window == NULL) {
            return ROS_USER_IPC_STATUS_NO_SPACE;
        }

        popup_window->in_use = 1;
        popup_window->owner_pid = g_gwes_menu_session.owner_pid;
        popup_window->hwnd = g_gwes_next_hwnd++;
        popup_window->parent = 0UL;
        popup_window->x = popup_x;
        popup_window->y = popup_y;
        popup_window->width = popup_width;
        popup_window->height = popup_height;
        popup_window->style = ROS_WINDOW_STYLE_VISIBLE | ROS_WINDOW_STYLE_TOPMOST | ROS_WINDOW_STYLE_SYSTEM_UI | ROS_WINDOW_STYLE_BORDER;
        popup_window->show_state = GWES_WINDOW_SHOW_NORMAL;
        popup_window->minimized_restore_state = GWES_WINDOW_SHOW_NORMAL;
        gwes_copy_text(popup_window->class_name, sizeof(popup_window->class_name), kGwesMenuClassName);
        gwes_copy_text(popup_window->title, sizeof(popup_window->title), "menu");

        status = gwes_render_create_window(
            popup_window->hwnd,
            popup_window->owner_pid,
            popup_window->parent,
            popup_window->x,
            popup_window->y,
            popup_window->width,
            popup_window->height,
            popup_window->style,
            popup_window->class_name,
            popup_window->title);
        if (status < 0L) {
            gwes_release_window_record(popup_window, 0);
            return status;
        }

        *popup_slot = g_gwes_menu_session.open_popup_count++;
        g_gwes_menu_session.popups[*popup_slot].hwnd = popup_window->hwnd;
        g_gwes_menu_session.popups[*popup_slot].menu_index = menu_index;
        g_gwes_menu_session.popups[*popup_slot].hover_index = kGwesMenuNoHover;
        g_gwes_menu_session.popups[*popup_slot].popup_width = popup_width;
        g_gwes_menu_session.popups[*popup_slot].popup_height = popup_height;
        gwes_menu_tracef(
            "gwes.exe: menu trace popup-create slot=%lu hwnd=%lu menu=%lu origin=%ld,%ld size=%lux%lu",
            *popup_slot,
            popup_window->hwnd,
            menu_index,
            popup_x,
            popup_y,
            popup_width,
            popup_height);
        (void)gwes_render_raise_window(popup_window->hwnd);
        gwes_render_refresh_window_groups();
        return ROS_USER_IPC_STATUS_OK;
    }

    /*
     * Repaint one popup window owned by the current menu session.
     *
     * @param popup_slot Popup slot to repaint.
     * @return Nothing.
     */
    void gwes_menu_repaint_popup_slot(unsigned long popup_slot) {
        RosGdiSurface surface;
        unsigned long menu_index;
        unsigned long item_count;
        unsigned long index;

        if (!g_gwes_menu_session.active || popup_slot >= g_gwes_menu_session.open_popup_count) {
            return;
        }
        if (gwes_render_get_window_surface(g_gwes_menu_session.popups[popup_slot].hwnd, &surface) < 0L) {
            gwes_menu_tracef(
                "gwes.exe: menu trace popup-repaint-miss slot=%lu hwnd=%lu",
                popup_slot,
                g_gwes_menu_session.popups[popup_slot].hwnd);
            return;
        }

        menu_index = g_gwes_menu_session.popups[popup_slot].menu_index;
        item_count = g_gwes_menu_session.model.menus[menu_index].item_count;
        gwes_menu_tracef(
            "gwes.exe: menu trace popup-repaint slot=%lu hwnd=%lu menu=%lu hover=%lu surface=%p size=%lux%lu",
            popup_slot,
            g_gwes_menu_session.popups[popup_slot].hwnd,
            menu_index,
            g_gwes_menu_session.popups[popup_slot].hover_index,
            surface.pixels,
            surface.width,
            surface.height);
        gwes_menu_fill_surface_rect(&surface, 0UL, 0UL, surface.width, surface.height, kGwesMenuBackground);
        if (surface.width > 2UL && surface.height > 2UL) {
            gwes_menu_fill_surface_rect(&surface, 1UL, 1UL, surface.width - 2UL, surface.height - 2UL, kGwesMenuInnerBackground);
        }
        gwes_menu_frame_surface_rect(&surface, 0UL, 0UL, surface.width, surface.height, kGwesMenuFrame);
        if (surface.width > 2UL && surface.height > 2UL) {
            gwes_menu_frame_surface_rect(&surface, 1UL, 1UL, surface.width - 2UL, surface.height - 2UL, kGwesMenuInnerFrame);
        }

        for (index = 0UL; index < item_count; ++index) {
            gwes_menu_paint_item(&surface,
                &g_gwes_menu_session.model,
                menu_index,
                index,
                g_gwes_menu_session.popups[popup_slot].hover_index == index);
        }

        gwes_render_mark_window_dirty(g_gwes_menu_session.popups[popup_slot].hwnd, 0UL, 0UL, surface.width, surface.height);
    }

    /*
     * Repaint every popup window currently opened by the menu cascade.
     *
     * @return Nothing.
     */
    void gwes_menu_repaint_all_popups(void) {
        unsigned long index;

        for (index = 0UL; index < g_gwes_menu_session.open_popup_count; ++index) {
            gwes_menu_repaint_popup_slot(index);
        }
    }

    /*
     * Compute the desktop anchor for one child submenu popup.
     *
     * @param popup_slot Parent popup slot.
     * @param item_index Parent row that owns the submenu.
     * @param popup_x Receives the child popup X coordinate.
     * @param popup_y Receives the child popup Y coordinate.
     * @return Nothing.
     */
    void gwes_menu_submenu_anchor(unsigned long popup_slot, unsigned long item_index, long* popup_x, long* popup_y) {
        GwesWindowRecord* popup_window;
        const unsigned long top = gwes_menu_item_top(&g_gwes_menu_session.model, g_gwes_menu_session.popups[popup_slot].menu_index, item_index);

        if (popup_x == NULL || popup_y == NULL) {
            return;
        }

        popup_window = gwes_find_window_any(g_gwes_menu_session.popups[popup_slot].hwnd);
        if (popup_window == NULL) {
            *popup_x = 0L;
            *popup_y = 0L;
            return;
        }

        *popup_x = popup_window->x + (long)popup_window->width - 3L;
        *popup_y = popup_window->y + (long)top;
    }

    /*
     * Open or refresh the child submenu popup that belongs to one hovered row.
     *
     * @param popup_slot Parent popup slot.
     * @param item_index Parent row index inside that popup.
     * @param focus_first_item Non-zero when the child popup should preselect its first enabled row.
     * @return Zero on success, or a negative status code on failure.
     */
    long gwes_menu_open_submenu(unsigned long popup_slot, unsigned long item_index, int focus_first_item) {
        const unsigned long trace_start = gwes_menu_timing_begin();
        const unsigned long parent_menu_index = g_gwes_menu_session.popups[popup_slot].menu_index;
        const RosWindowMenuItem* item;
        unsigned long existing_slot;
        unsigned long child_slot;
        unsigned long popup_width = 0UL;
        unsigned long popup_height = 0UL;
        long popup_x;
        long popup_y;
        long work_x = 0L;
        long work_y = 0L;
        unsigned long work_width = 0UL;
        unsigned long work_height = 0UL;
        long status;

        if (!g_gwes_menu_session.active || popup_slot >= g_gwes_menu_session.open_popup_count) {
            gwes_menu_timing_endf(trace_start,
                "submenu-open parent-slot=%lu item=%lu focus=%d status=%ld child-popups=%lu",
                popup_slot,
                item_index,
                focus_first_item,
                (long)ROS_USER_IPC_STATUS_NOT_FOUND,
                g_gwes_menu_session.open_popup_count);
            return ROS_USER_IPC_STATUS_NOT_FOUND;
        }
        if (item_index >= g_gwes_menu_session.model.menus[parent_menu_index].item_count) {
            gwes_menu_timing_endf(trace_start,
                "submenu-open parent-slot=%lu item=%lu focus=%d status=%ld child-popups=%lu",
                popup_slot,
                item_index,
                focus_first_item,
                (long)ROS_USER_IPC_STATUS_NOT_FOUND,
                g_gwes_menu_session.open_popup_count);
            return ROS_USER_IPC_STATUS_NOT_FOUND;
        }

        item = &g_gwes_menu_session.model.items[g_gwes_menu_session.model.menus[parent_menu_index].first_item + item_index];
        if (item->submenu_index == ROS_WINDOW_MENU_INVALID_INDEX || (item->flags & ROS_MENU_ITEM_FLAG_DISABLED) != 0UL) {
            gwes_menu_close_popups_from(popup_slot + 1UL);
            gwes_menu_timing_endf(trace_start,
                "submenu-open parent-slot=%lu item=%lu focus=%d status=%ld child-popups=%lu",
                popup_slot,
                item_index,
                focus_first_item,
                (long)ROS_USER_IPC_STATUS_OK,
                g_gwes_menu_session.open_popup_count);
            return ROS_USER_IPC_STATUS_OK;
        }

        existing_slot = gwes_menu_popup_slot_for_menu(&g_gwes_menu_session, item->submenu_index);
        if (existing_slot != ROS_WINDOW_MENU_INVALID_INDEX && existing_slot == (popup_slot + 1UL)) {
            gwes_menu_close_popups_from(existing_slot + 1UL);
            if (focus_first_item) {
                g_gwes_menu_session.popups[existing_slot].hover_index = gwes_menu_first_selectable_item(&g_gwes_menu_session.model, item->submenu_index);
                gwes_menu_repaint_popup_slot(existing_slot);
            }
            gwes_menu_timing_endf(trace_start,
                "submenu-open parent-slot=%lu item=%lu focus=%d status=%ld child-popups=%lu",
                popup_slot,
                item_index,
                focus_first_item,
                (long)ROS_USER_IPC_STATUS_OK,
                g_gwes_menu_session.open_popup_count);
            return ROS_USER_IPC_STATUS_OK;
        }

        gwes_menu_close_popups_from(popup_slot + 1UL);
        gwes_menu_submenu_anchor(popup_slot, item_index, &popup_x, &popup_y);
        gwes_menu_compute_popup_extent(&g_gwes_menu_session.model, item->submenu_index, &popup_width, &popup_height);
        if (gwes_query_shell_work_area(&work_x, &work_y, &work_width, &work_height) >= 0L) {
            if ((unsigned long)(popup_x - work_x) + popup_width > work_width) {
                GwesWindowRecord* parent_popup = gwes_find_window_any(g_gwes_menu_session.popups[popup_slot].hwnd);

                popup_x = parent_popup != NULL ? (parent_popup->x - (long)popup_width + 3L) : work_x;
            }
            if (popup_x < work_x) {
                popup_x = work_x;
            }
            if (popup_y < work_y) {
                popup_y = work_y;
            }
            if ((unsigned long)(popup_y - work_y) + popup_height > work_height) {
                popup_y = work_y + (long)(work_height - popup_height);
            }
        }

        status = gwes_menu_create_popup(item->submenu_index, popup_x, popup_y, &child_slot);
        if (status < 0L) {
            gwes_menu_timing_endf(trace_start,
                "submenu-open parent-slot=%lu item=%lu focus=%d status=%ld child-popups=%lu",
                popup_slot,
                item_index,
                focus_first_item,
                status,
                g_gwes_menu_session.open_popup_count);
            return status;
        }
        if (focus_first_item) {
            g_gwes_menu_session.popups[child_slot].hover_index = gwes_menu_first_selectable_item(&g_gwes_menu_session.model, item->submenu_index);
        }
        gwes_menu_tracef(
            "gwes.exe: menu trace submenu-open parent-slot=%lu child-slot=%lu child-menu=%lu focus-first=%d",
            popup_slot,
            child_slot,
            item->submenu_index,
            focus_first_item);
        gwes_menu_repaint_popup_slot(child_slot);
        gwes_menu_timing_endf(trace_start,
            "submenu-open parent-slot=%lu item=%lu focus=%d status=%ld child-popups=%lu",
            popup_slot,
            item_index,
            focus_first_item,
            (long)ROS_USER_IPC_STATUS_OK,
            g_gwes_menu_session.open_popup_count);
        return ROS_USER_IPC_STATUS_OK;
    }

    /*
     * Cancel any delayed submenu-open request.
     *
     * @return Nothing.
     */
    void gwes_menu_cancel_open_timer(void) {
        g_gwes_menu_session.submenu_open_armed = 0;
        g_gwes_menu_session.submenu_open_due_msec = 0UL;
        g_gwes_menu_session.submenu_open_popup_slot = 0UL;
        g_gwes_menu_session.submenu_open_item_index = 0UL;
        g_gwes_menu_session.submenu_open_focus_first_item = 0;
    }

    /*
     * Cancel any delayed submenu-close request.
     *
     * @return Nothing.
     */
    void gwes_menu_cancel_close_timer(void) {
        g_gwes_menu_session.submenu_close_armed = 0;
        g_gwes_menu_session.submenu_close_due_msec = 0UL;
        g_gwes_menu_session.submenu_close_first_slot = 0UL;
    }

    /*
     * Cancel every hover-driven submenu timer owned by the active session.
     *
     * @return Nothing.
     */
    void gwes_menu_cancel_hover_timers(void) {
        gwes_menu_cancel_open_timer();
        gwes_menu_cancel_close_timer();
    }

    /*
     * Report whether the direct child popup already matches one submenu row.
     *
     * @param popup_slot Parent popup slot.
     * @param item Candidate row inside that popup.
     * @return Non-zero when the matching submenu is already visible.
     */
    int gwes_menu_is_submenu_visible_for_item(unsigned long popup_slot, const RosWindowMenuItem* item) {
        unsigned long existing_slot;

        if (item == NULL || item->submenu_index == ROS_WINDOW_MENU_INVALID_INDEX) {
            return 0;
        }

        existing_slot = gwes_menu_popup_slot_for_menu(&g_gwes_menu_session, item->submenu_index);
        return existing_slot != ROS_WINDOW_MENU_INVALID_INDEX && existing_slot == (popup_slot + 1UL);
    }

    /*
     * Arm one delayed submenu-open request for pointer-hover tracking.
     *
     * @param popup_slot Parent popup slot that owns the hovered row.
     * @param item_index Hovered row index.
     * @param focus_first_item Non-zero when the eventual child popup should preselect.
     * @return Nothing.
     */
    void gwes_menu_arm_open_timer(unsigned long popup_slot, unsigned long item_index, int focus_first_item) {
        g_gwes_menu_session.submenu_open_armed = 1;
        g_gwes_menu_session.submenu_open_due_msec = getUptimeMs() + kGwesMenuSubmenuOpenDelayMsec;
        g_gwes_menu_session.submenu_open_popup_slot = popup_slot;
        g_gwes_menu_session.submenu_open_item_index = item_index;
        g_gwes_menu_session.submenu_open_focus_first_item = focus_first_item;
    }

    /*
     * Arm one delayed close request for deeper submenu popups.
     *
     * @param first_slot First popup slot to close when the timer fires.
     * @return Nothing.
     */
    void gwes_menu_arm_close_timer(unsigned long first_slot) {
        if (first_slot >= g_gwes_menu_session.open_popup_count) {
            gwes_menu_cancel_close_timer();
            return;
        }

        g_gwes_menu_session.submenu_close_armed = 1;
        g_gwes_menu_session.submenu_close_due_msec = getUptimeMs() + kGwesMenuSubmenuCloseDelayMsec;
        g_gwes_menu_session.submenu_close_first_slot = first_slot;
    }

    /*
     * Compute how long the active menu session may wait before a hover timer fires.
     *
     * @param now Current uptime in milliseconds.
     * @return Milliseconds until the next due menu timer, or one second.
     */
    unsigned long gwes_menu_timer_wait_budget(unsigned long now) {
        unsigned long best_wait = 1000UL;

        if (!g_gwes_menu_session.active) {
            return best_wait;
        }
        if (g_gwes_menu_session.submenu_open_armed) {
            if (g_gwes_menu_session.submenu_open_due_msec <= now) {
                return 0UL;
            }
            best_wait = g_gwes_menu_session.submenu_open_due_msec - now;
        }
        if (g_gwes_menu_session.submenu_close_armed) {
            unsigned long close_wait;

            if (g_gwes_menu_session.submenu_close_due_msec <= now) {
                return 0UL;
            }
            close_wait = g_gwes_menu_session.submenu_close_due_msec - now;
            if (close_wait < best_wait) {
                best_wait = close_wait;
            }
        }

        return best_wait;
    }

    /*
     * Fire any delayed submenu open or close actions that reached their deadline.
     *
     * @param now Current uptime in milliseconds.
     * @return Count of menu-timer actions that ran.
     */
    unsigned long gwes_menu_fire_due_timers(unsigned long now) {
        unsigned long action_count = 0UL;

        if (!g_gwes_menu_session.active) {
            gwes_menu_cancel_hover_timers();
            return 0UL;
        }

        if (g_gwes_menu_session.submenu_close_armed
            && g_gwes_menu_session.submenu_close_due_msec <= now
            && g_gwes_menu_session.submenu_close_first_slot < g_gwes_menu_session.open_popup_count) {
            const unsigned long first_slot = g_gwes_menu_session.submenu_close_first_slot;

            gwes_menu_cancel_close_timer();
            gwes_menu_close_popups_from(first_slot);
            ++action_count;
        }

        if (g_gwes_menu_session.submenu_open_armed && g_gwes_menu_session.submenu_open_due_msec <= now) {
            const unsigned long popup_slot = g_gwes_menu_session.submenu_open_popup_slot;
            const unsigned long item_index = g_gwes_menu_session.submenu_open_item_index;
            const int focus_first_item = g_gwes_menu_session.submenu_open_focus_first_item;

            gwes_menu_cancel_open_timer();
            if (g_gwes_menu_session.active) {
                (void)gwes_menu_open_submenu(popup_slot, item_index, focus_first_item);
                ++action_count;
            }
        }

        return action_count;
    }

    /*
     * Send the deferred `TrackPopupMenu` reply and tear down every open popup.
     *
     * @param status Status code returned to `window.dll`.
     * @param command_id Selected command identifier, or zero on cancel.
     * @return Nothing.
     */
    void gwes_menu_finish_session(long status, unsigned long command_id) {
        GwesMenuSession session = g_gwes_menu_session;

        if (!session.active) {
            return;
        }

        gwes_menu_cancel_hover_timers();

        gwes_menu_tracef(
            "gwes.exe: menu trace finish owner=%lu status=%ld command=%lu popups=%lu",
            session.owner_hwnd,
            status,
            command_id,
            session.open_popup_count);

        gwes_menu_close_popups_from(0UL);
        memset(&g_gwes_menu_session, 0, sizeof(g_gwes_menu_session));
        (void)gwes_send_packet(session.owner_pid, ROS_WINDOW_SERVER_KIND_TRACK_POPUP_MENU_REPLY, (unsigned long)status, command_id, 0UL, 0UL, NULL, NULL);
        gwes_refresh_pointer_cursor();
    }

    /*
     * Return whether one published top-level window should appear on the shell taskbar.
     *
     * @param window Candidate server window.
     * @return Non-zero when explorer should publish the window as one task.
     */
    int gwes_window_is_task_candidate(const GwesWindowRecord* window) {
        return window != NULL
            && window->in_use
            && window->parent == 0UL
            && (window->style & ROS_WINDOW_STYLE_VISIBLE) != 0UL
            && (window->style & ROS_WINDOW_STYLE_SYSTEM_UI) == 0UL;
    }

    /*
     * Resolve the top-level task window that explorer should currently highlight.
     *
     * @return Foreground top-level task window handle, or zero when none exists.
     */
    unsigned long gwes_task_foreground_hwnd(void) {
        GwesWindowRecord* focused = gwes_find_window_any(g_gwes_focus_hwnd);
        GwesWindowRecord* focused_root = gwes_find_root_window(focused);

        if (gwes_window_is_task_candidate(focused_root)) {
            return focused_root->hwnd;
        }

        focused = gwes_find_window_any(g_gwes_last_task_focus_hwnd);
        focused_root = gwes_find_root_window(focused);
        return gwes_window_is_task_candidate(focused_root) ? focused_root->hwnd : 0UL;
    }

    /*
     * Resolve the currently focused top-level window, including shell-owned windows.
     *
     * @return Focused top-level window handle, or zero when focus is unset.
     */
    unsigned long gwes_focused_root_hwnd(void) {
        GwesWindowRecord* focused = gwes_find_window_any(g_gwes_focus_hwnd);
        GwesWindowRecord* focused_root = gwes_find_root_window(focused);

        if (focused_root != NULL) {
            return focused_root->hwnd;
        }

        return focused != NULL ? focused->hwnd : 0UL;
    }

    /*
     * Report whether one published task window currently requests fullscreen behavior.
     *
     * @param window Candidate top-level window.
     * @return Non-zero when the fullscreen style bit is set.
     */
    int gwes_window_requests_fullscreen(const GwesWindowRecord* window) {
        return gwes_window_is_task_candidate(window)
            && (window->style & ROS_WINDOW_STYLE_FULLSCREEN) != 0UL;
    }

    /*
     * Snapshot one decorated top-level window's restorable geometry.
     *
     * @param window Target window record.
     * @return Nothing.
     */
    void gwes_store_restore_geometry_impl(GwesWindowRecord* window) {
        if (!gwes_window_is_decorated_root(window)) {
            return;
        }

        window->restore_valid = 1;
        window->restore_x = window->x;
        window->restore_y = window->y;
        window->restore_width = window->width;
        window->restore_height = window->height;
    }

    /*
     * Compute one dialog client width for Win32-style `WM_SIZE` delivery.
     *
     * @param window Window whose client width is needed.
     * @return Client width in pixels.
     */
    unsigned long gwes_window_client_width(const GwesWindowRecord* window) {
        if (!gwes_window_is_decorated_root(window)) {
            return window != NULL ? window->width : 0UL;
        }

        return window->width > (GWES_DIALOG_BORDER_THICKNESS * 2UL)
            ? (window->width - (GWES_DIALOG_BORDER_THICKNESS * 2UL))
            : 1UL;
    }

    /*
     * Compute one dialog client height for Win32-style `WM_SIZE` delivery.
     *
     * @param window Window whose client height is needed.
     * @return Client height in pixels.
     */
    unsigned long gwes_window_client_height(const GwesWindowRecord* window) {
        if (!gwes_window_is_decorated_root(window)) {
            return window != NULL ? window->height : 0UL;
        }

        return window->height > (GWES_DIALOG_TITLE_HEIGHT + GWES_DIALOG_BORDER_THICKNESS)
            ? (window->height - GWES_DIALOG_TITLE_HEIGHT - GWES_DIALOG_BORDER_THICKNESS)
            : 1UL;
    }

    /*
     * Send one `WM_MOVE` notification using client-origin coordinates.
     *
     * @param window Target window that moved.
     * @return Nothing.
     */
    void gwes_send_move_message(GwesWindowRecord* window) {
        long move_x;
        long move_y;

        if (window == NULL) {
            return;
        }

        move_x = window->x;
        move_y = window->y;
        if (gwes_window_is_decorated_root(window)) {
            move_x += (long)GWES_DIALOG_BORDER_THICKNESS;
            move_y += (long)GWES_DIALOG_TITLE_HEIGHT;
        }

        gwes_send_window_message(window, WM_MOVE, 0UL, gwes_pack_signed_pair(move_x, move_y));
    }

    /*
     * Send one `WM_SIZE` notification using client-area dimensions.
     *
     * @param window Target window that resized.
     * @return Nothing.
     */
    void gwes_send_size_message(GwesWindowRecord* window) {
        if (window == NULL) {
            return;
        }

        gwes_send_window_message(
            window,
            WM_SIZE,
            0UL,
            gwes_pack_signed_pair((long)gwes_window_client_width(window), (long)gwes_window_client_height(window)));
    }

    /*
     * Return the close button rectangle in desktop coordinates.
     *
     * @param window Decorated top-level window.
     * @param x Receives the button X coordinate.
     * @param y Receives the button Y coordinate.
     * @return Non-zero when the rectangle is valid.
     */
    int gwes_close_button_origin(const GwesWindowRecord* window, unsigned long* x, unsigned long* y) {
        if (!gwes_window_is_decorated_root(window) || window->width <= (GWES_DIALOG_CLOSE_SIZE + (GWES_DIALOG_CLOSE_MARGIN * 2UL))) {
            return 0;
        }

        if (x != NULL) {
            *x = (unsigned long)window->x + window->width - GWES_DIALOG_CLOSE_MARGIN - GWES_DIALOG_CLOSE_SIZE;
        }
        if (y != NULL) {
            *y = (unsigned long)window->y + ((GWES_DIALOG_TITLE_HEIGHT > GWES_DIALOG_CLOSE_SIZE)
                ? ((GWES_DIALOG_TITLE_HEIGHT - GWES_DIALOG_CLOSE_SIZE) / 2UL)
                : 0UL);
        }
        return 1;
    }

    /*
     * Return the maximize button rectangle in desktop coordinates.
     *
     * @param window Decorated top-level window.
     * @param x Receives the button X coordinate.
     * @param y Receives the button Y coordinate.
     * @return Non-zero when the rectangle is valid.
     */
    int gwes_maximize_button_origin(const GwesWindowRecord* window, unsigned long* x, unsigned long* y) {
        unsigned long close_x;
        unsigned long close_y;

        if (!gwes_close_button_origin(window, &close_x, &close_y)) {
            return 0;
        }

        if (close_x < (unsigned long)window->x + GWES_DIALOG_CONTROL_BUTTON_GAP + GWES_DIALOG_CLOSE_SIZE) {
            return 0;
        }

        if (x != NULL) {
            *x = close_x - GWES_DIALOG_CONTROL_BUTTON_GAP - GWES_DIALOG_CLOSE_SIZE;
        }
        if (y != NULL) {
            *y = close_y;
        }
        return 1;
    }

    /*
     * Return the minimize button rectangle in desktop coordinates.
     *
     * @param window Decorated top-level window.
     * @param x Receives the button X coordinate.
     * @param y Receives the button Y coordinate.
     * @return Non-zero when the rectangle is valid.
     */
    int gwes_minimize_button_origin(const GwesWindowRecord* window, unsigned long* x, unsigned long* y) {
        unsigned long max_x;
        unsigned long max_y;

        if (!gwes_maximize_button_origin(window, &max_x, &max_y)) {
            return 0;
        }

        if (max_x < (unsigned long)window->x + GWES_DIALOG_CONTROL_BUTTON_GAP + GWES_DIALOG_CLOSE_SIZE) {
            return 0;
        }

        if (x != NULL) {
            *x = max_x - GWES_DIALOG_CONTROL_BUTTON_GAP - GWES_DIALOG_CLOSE_SIZE;
        }
        if (y != NULL) {
            *y = max_y;
        }
        return 1;
    }

} // namespace

/*
 * Find one previously registered client class.
 *
 * @param owner_pid Owning client PID.
 * @param class_name Registered class name.
 * @return Matching class record, or NULL when none exists.
 */
GwesClassRecord* gwes_find_class(long owner_pid, const char* class_name) {
    GwesClassRecord* record;

    for (record = g_gwes_classes; record != NULL; record = record->next) {
        if (!record->in_use) {
            continue;
        }
        if (record->owner_pid != owner_pid) {
            continue;
        }
        if (userIpcTextEquals(record->class_name, class_name)) {
            return record;
        }
    }

    return NULL;
}

/*
 * Allocate one class record on the process heap and link it into GWES.
 *
 * @return New class record, or NULL when allocation fails.
 */
GwesClassRecord* gwes_allocate_class(void) {
    GwesClassRecord* record = (GwesClassRecord*)malloc(sizeof(*record));

    if (record == NULL) {
        gwes_log_registry_counts("class-alloc-failed");
        return NULL;
    }

    memset(record, 0, sizeof(*record));
    record->next = g_gwes_classes;
    g_gwes_classes = record;
    gwes_note_class_alloc();
    return record;
}

/*
 * Release one class record and unlink it from the GWES registry.
 *
 * @param record Class record to unlink.
 * @return Nothing.
 */
void gwes_release_class_record(GwesClassRecord* record) {
    GwesClassRecord** link;

    if (record == NULL) {
        return;
    }

    for (link = &g_gwes_classes; *link != NULL; link = &((*link)->next)) {
        if (*link == record) {
            *link = record->next;
            free(record);
            gwes_note_class_release();
            return;
        }
    }
}

/*
 * Allocate one server window record on the process heap and link it into the live registry.
 *
 * @return New window record, or NULL when allocation fails.
 */
GwesWindowRecord* gwes_allocate_window(void) {
    GwesWindowRecord* window = (GwesWindowRecord*)malloc(sizeof(*window));

    if (window == NULL) {
        gwes_log_registry_counts("window-alloc-failed");
        return NULL;
    }

    memset(window, 0, sizeof(*window));
    window->next = g_gwes_windows;
    g_gwes_windows = window;
    gwes_note_window_alloc();
    return window;
}

/*
 * Release one server window record and optionally tear down its render state.
 *
 * @param window Window record to unlink.
 * @param destroy_render_state Non-zero when compositor resources exist.
 * @return Nothing.
 */
void gwes_release_window_record(GwesWindowRecord* window, int destroy_render_state) {
    GwesWindowRecord** link;

    if (window == NULL) {
        return;
    }

    for (link = &g_gwes_windows; *link != NULL; link = &((*link)->next)) {
        if (*link == window) {
            *link = window->next;
            gwes_release_window_timers(window->hwnd);
            if (destroy_render_state) {
                gwes_forget_window_state(window->hwnd);
                gwes_render_destroy_window(window->hwnd);
            }
            free(window);
            gwes_note_window_release();
            gwes_publish_shell_state();
            return;
        }
    }
}

/*
 * Find one live timer owned by the specified client and window.
 *
 * @param owner_pid Client process identifier.
 * @param hwnd Target window handle.
 * @param timer_id Timer identifier.
 * @return Matching timer record, or NULL when none exists.
 */
GwesTimerRecord* gwes_find_timer(long owner_pid, unsigned long hwnd, unsigned long timer_id) {
    GwesTimerRecord* timer;

    for (timer = g_gwes_timers; timer != NULL; timer = timer->next) {
        if (!timer->in_use) {
            continue;
        }
        if (timer->owner_pid != owner_pid || timer->hwnd != hwnd || timer->timer_id != timer_id) {
            continue;
        }
        return timer;
    }

    return NULL;
}

/*
 * Allocate one heap-backed timer record and link it into the live registry.
 *
 * @return New timer record, or NULL when allocation fails.
 */
GwesTimerRecord* gwes_allocate_timer(void) {
    GwesTimerRecord* timer = (GwesTimerRecord*)malloc(sizeof(*timer));

    if (timer == NULL) {
        return NULL;
    }

    memset(timer, 0, sizeof(*timer));
    timer->next = g_gwes_timers;
    g_gwes_timers = timer;
    return timer;
}

/*
 * Release one timer record and unlink it from the live registry.
 *
 * @param timer Timer record to release.
 * @return Nothing.
 */
void gwes_release_timer_record(GwesTimerRecord* timer) {
    GwesTimerRecord** link;

    if (timer == NULL) {
        return;
    }

    for (link = &g_gwes_timers; *link != NULL; link = &((*link)->next)) {
        if (*link == timer) {
            *link = timer->next;
            free(timer);
            return;
        }
    }
}

/*
 * Release every timer currently owned by one window.
 *
 * @param hwnd Window whose timers should be removed.
 * @return Nothing.
 */
void gwes_release_window_timers(unsigned long hwnd) {
    GwesTimerRecord* timer = g_gwes_timers;

    while (timer != NULL) {
        GwesTimerRecord* next = timer->next;

        if (timer->hwnd == hwnd) {
            gwes_release_timer_record(timer);
        }

        timer = next;
    }
}

/*
 * Find one live server window by its owner and handle.
 *
 * @param owner_pid Client process identifier that owns the window.
 * @param hwnd Stable window identifier assigned by GWES.
 * @return Matching window record, or NULL when no owned window exists.
 */
GwesWindowRecord* gwes_find_window(long owner_pid, unsigned long hwnd) {
    GwesWindowRecord* window;

    for (window = g_gwes_windows; window != NULL; window = window->next) {
        if (!window->in_use) {
            continue;
        }
        if (window->owner_pid != owner_pid) {
            continue;
        }
        if (window->hwnd == hwnd) {
            return window;
        }
    }

    return NULL;
}

/*
 * Find one live server window by handle regardless of owner PID.
 *
 * @param hwnd Stable window identifier assigned by GWES.
 * @return Matching window record, or NULL when no window exists.
 */
GwesWindowRecord* gwes_find_window_any(unsigned long hwnd) {
    GwesWindowRecord* window;

    for (window = g_gwes_windows; window != NULL; window = window->next) {
        if (window->in_use && window->hwnd == hwnd) {
            return window;
        }
    }

    return NULL;
}

/*
 * Follow parent pointers until one top-level window is reached.
 *
 * @param window Starting child or top-level window.
 * @return Top-level ancestor, or the original window when it has no parent.
 */
GwesWindowRecord* gwes_find_root_window(GwesWindowRecord* window) {
    GwesWindowRecord* current = window;

    while (current != NULL && current->parent != 0UL) {
        current = gwes_find_window_any(current->parent);
    }

    return current;
}

/*
 * Drop focus, capture, and press bookkeeping when a window disappears.
 *
 * @param hwnd Window handle being released.
 * @return Nothing.
 */
void gwes_forget_window_state(unsigned long hwnd) {
    if (g_gwes_menu_session.active) {
        unsigned long popup_slot = gwes_menu_popup_slot_for_hwnd(&g_gwes_menu_session, hwnd);

        if (popup_slot != ROS_WINDOW_MENU_INVALID_INDEX) {
            g_gwes_menu_session.popups[popup_slot].hwnd = 0UL;
            g_gwes_menu_session.popups[popup_slot].hover_index = kGwesMenuNoHover;
        }
    }
    if (g_gwes_focus_hwnd == hwnd) {
        g_gwes_focus_hwnd = 0UL;
    }
    if (g_gwes_last_task_focus_hwnd == hwnd) {
        g_gwes_last_task_focus_hwnd = 0UL;
    }
    if (g_gwes_keyboard_capture_hwnd == hwnd) {
        g_gwes_keyboard_capture_hwnd = 0UL;
    }
    if (g_gwes_pointer_capture_hwnd == hwnd) {
        g_gwes_pointer_capture_hwnd = 0UL;
    }
    if (g_gwes_pointer_down_hwnd == hwnd) {
        g_gwes_pointer_down_hwnd = 0UL;
    }
    if (g_gwes_right_pointer_down_hwnd == hwnd) {
        g_gwes_right_pointer_down_hwnd = 0UL;
    }
    if (g_gwes_hover_hwnd == hwnd) {
        g_gwes_hover_hwnd = 0UL;
    }
    if (g_gwes_pointer_interaction_hwnd == hwnd) {
        g_gwes_pointer_interaction_hwnd = 0UL;
        g_gwes_pointer_capture_hwnd = 0UL;
        g_gwes_pointer_resize_edges = 0UL;
    }
}

/*
 * Record one new keyboard focus target when the handle is still live.
 *
 * @param hwnd Focus target.
 * @return Nothing.
 */
void gwes_set_focus(unsigned long hwnd) {
    GwesWindowRecord* focus_window = gwes_find_window_any(hwnd);
    GwesWindowRecord* root = gwes_find_root_window(focus_window);

    g_gwes_focus_hwnd = focus_window != NULL ? hwnd : 0UL;
    if (root != NULL
        && root->parent == 0UL
        && (root->style & ROS_WINDOW_STYLE_VISIBLE) != 0UL
        && (root->style & ROS_WINDOW_STYLE_SYSTEM_UI) == 0UL) {
        g_gwes_last_task_focus_hwnd = root->hwnd;
    }
    gwes_publish_shell_state();
}

/*
 * Map the shared shell-state block used by GWES and explorer.
 *
 * @return Zero on success, or a negative status code on failure.
 */
long gwes_shell_state_acquire(void) {
    const unsigned long requested_bytes = ExplorerShellSharedStateBytes(ROS_EXPLORER_SHELL_TASK_CAPACITY_DEFAULT);

    if (g_gwes_shell_state != NULL) {
        return ROS_USER_IPC_STATUS_OK;
    }

    g_gwes_shell_state = ExplorerShellSharedState(requested_bytes);
    if (g_gwes_shell_state == NULL) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    if (g_gwes_shell_state->version != ROS_EXPLORER_SHELL_SHARED_STATE_VERSION
        || ExplorerShellTaskCapacity(g_gwes_shell_state) != ROS_EXPLORER_SHELL_TASK_CAPACITY_DEFAULT) {
        memset(g_gwes_shell_state, 0, requested_bytes);
        g_gwes_shell_state->task_capacity = ROS_EXPLORER_SHELL_TASK_CAPACITY_DEFAULT;
        ExplorerShellInitializeSharedState(g_gwes_shell_state);
    }

    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Query the current desktop work area after shell reservations are applied.
 *
 * @param x Receives the work-area origin X.
 * @param y Receives the work-area origin Y.
 * @param width Receives the usable desktop width.
 * @param height Receives the usable desktop height.
 * @return Zero on success, or a negative status code on failure.
 */
long gwes_query_shell_work_area(long* x, long* y, unsigned long* width, unsigned long* height) {
    unsigned long desktop_width = 0UL;
    unsigned long desktop_height = 0UL;

    if (x == NULL || y == NULL || width == NULL || height == NULL) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    if (gwes_render_query_desktop_size(&desktop_width, &desktop_height) < 0L) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    *x = 0L;
    *y = 0L;
    *width = desktop_width;
    *height = desktop_height;

    if (g_gwes_shell_state == NULL || g_gwes_shell_state->version != ROS_EXPLORER_SHELL_SHARED_STATE_VERSION) {
        return ROS_USER_IPC_STATUS_OK;
    }
    if (g_gwes_shell_state->fullscreen_hwnd != 0ULL) {
        return ROS_USER_IPC_STATUS_OK;
    }
    if (g_gwes_shell_state->taskbar_visible == 0U || g_gwes_shell_state->taskbar_height == 0U) {
        return ROS_USER_IPC_STATUS_OK;
    }
    if (desktop_height <= g_gwes_shell_state->taskbar_height) {
        *height = 0UL;
        return ROS_USER_IPC_STATUS_OK;
    }

    *height = desktop_height - g_gwes_shell_state->taskbar_height;
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Snapshot one decorated top-level window's restorable geometry.
 *
 * @param window Target window record.
 * @return Nothing.
 */
void gwes_store_restore_geometry(GwesWindowRecord* window) {
    gwes_store_restore_geometry_impl(window);
}

/*
 * Publish one fresh task snapshot and work-area view into the shared shell state.
 *
 * @return Nothing.
 */
void gwes_publish_shell_state(void) {
    RosExplorerShellSharedState* state;
    unsigned long task_capacity;
    unsigned long desktop_width = 0UL;
    unsigned long desktop_height = 0UL;
    unsigned long active_hwnd;
    GwesWindowRecord* active_window;
    unsigned long task_count = 0UL;
    unsigned long index;

    if (gwes_shell_state_acquire() < 0L) {
        return;
    }

    state = g_gwes_shell_state;
    if (state == NULL) {
        return;
    }
    task_capacity = ExplorerShellTaskCapacity(state);
    if (task_capacity == 0UL) {
        return;
    }

    if (gwes_render_query_desktop_size(&desktop_width, &desktop_height) < 0L) {
        desktop_width = 0UL;
        desktop_height = 0UL;
    }

    active_hwnd = gwes_task_foreground_hwnd();
    active_window = gwes_find_window_any(active_hwnd);
    state->foreground_hwnd = active_hwnd;
    state->focused_hwnd = gwes_focused_root_hwnd();
    state->fullscreen_hwnd = gwes_window_requests_fullscreen(active_window) ? active_hwnd : 0ULL;
    state->desktop_width = desktop_width;
    state->desktop_height = desktop_height;
    state->work_area_x = 0L;
    state->work_area_y = 0L;
    state->work_area_width = desktop_width;
    state->work_area_height = desktop_height;
    if (state->fullscreen_hwnd == 0ULL
        && state->taskbar_visible != 0U
        && state->taskbar_height != 0U
        && desktop_height > state->taskbar_height) {
        state->work_area_height = desktop_height - state->taskbar_height;
    }

    for (index = 0UL; index < task_capacity; ++index) {
        memset(&state->tasks[index], 0, sizeof(state->tasks[index]));
    }

    for (GwesWindowRecord* window = g_gwes_windows; window != NULL && task_count < task_capacity; window = window->next) {
        RosExplorerShellTaskEntry* entry;

        if (!gwes_window_is_task_candidate(window)) {
            continue;
        }

        entry = &state->tasks[task_count++];
        entry->hwnd = window->hwnd;
        entry->owner_pid = window->owner_pid;
        entry->x = window->x;
        entry->y = window->y;
        entry->width = window->width;
        entry->height = window->height;
        entry->style = window->style;
        entry->flags = ROS_EXPLORER_SHELL_TASK_FLAG_VISIBLE;
        if (window->hwnd == active_hwnd) {
            entry->flags |= ROS_EXPLORER_SHELL_TASK_FLAG_ACTIVE;
        }
        if ((window->style & ROS_WINDOW_STYLE_DECORATED) != 0UL) {
            entry->flags |= ROS_EXPLORER_SHELL_TASK_FLAG_DECORATED;
        }
        if ((window->style & ROS_WINDOW_STYLE_TOPMOST) != 0UL) {
            entry->flags |= ROS_EXPLORER_SHELL_TASK_FLAG_TOPMOST;
        }
        if ((window->style & ROS_WINDOW_STYLE_FULLSCREEN) != 0UL) {
            entry->flags |= ROS_EXPLORER_SHELL_TASK_FLAG_FULLSCREEN;
        }
        gwes_copy_text(entry->class_name, sizeof(entry->class_name), window->class_name);
        gwes_copy_text(entry->title, sizeof(entry->title), window->title);
    }

    state->task_count = task_count;
    ++state->task_generation;
}

/*
 * Report whether one server window is a decorated top-level dialog.
 *
 * @param window Candidate server window.
 * @return Non-zero when the window owns GWES-managed chrome.
 */
int gwes_window_is_decorated_root(const GwesWindowRecord* window) {
    return window != NULL && window->parent == 0UL && (window->style & ROS_WINDOW_STYLE_DECORATED) != 0UL;
}

/*
 * Report whether one server window is Explorer's painted desktop background.
 *
 * @param window Candidate server window.
 * @return Non-zero when the window matches the non-topmost Explorer desktop.
 */
int gwes_window_is_desktop_background(const GwesWindowRecord* window) {
    return window != NULL
        && window->parent == 0UL
        && (window->style & ROS_WINDOW_STYLE_SYSTEM_UI) != 0UL
        && (window->style & ROS_WINDOW_STYLE_TOPMOST) == 0UL
        && (window->style & ROS_WINDOW_STYLE_DECORATED) == 0UL
        && userIpcTextEquals(window->class_name, GWES_EXPLORER_DESKTOP_CLASS);
}

/*
 * Filter one pointer-move target before GWES updates hover or posts messages.
 *
 * @param target Hit-tested pointer target.
 * @return Original target, or NULL when the desktop background should stay idle.
 */
GwesWindowRecord* gwes_filter_pointer_move_target(GwesWindowRecord* target) {
    if (gwes_window_is_desktop_background(target)) {
        return NULL;
    }

    return target;
}

/*
 * Report whether one desktop point hits the server-drawn close button.
 *
 * @param window Decorated top-level window under the pointer.
 * @param x Desktop X coordinate.
 * @param y Desktop Y coordinate.
 * @return Non-zero when the close button was hit.
 */
int gwes_window_hit_close_button(const GwesWindowRecord* window, unsigned long x, unsigned long y) {
    unsigned long button_x = 0UL;
    unsigned long button_y = 0UL;

    if (!gwes_close_button_origin(window, &button_x, &button_y)) {
        return 0;
    }
    return x >= button_x
        && x < (button_x + GWES_DIALOG_CLOSE_SIZE)
        && y >= button_y
        && y < (button_y + GWES_DIALOG_CLOSE_SIZE);
}

/*
 * Report whether one desktop point hits the maximize controller button.
 *
 * @param window Decorated top-level window under the pointer.
 * @param x Desktop X coordinate.
 * @param y Desktop Y coordinate.
 * @return Non-zero when the maximize button was hit.
 */
int gwes_window_hit_maximize_button(const GwesWindowRecord* window, unsigned long x, unsigned long y) {
    unsigned long button_x = 0UL;
    unsigned long button_y = 0UL;

    if (!gwes_maximize_button_origin(window, &button_x, &button_y)) {
        return 0;
    }

    return x >= button_x
        && x < (button_x + GWES_DIALOG_CLOSE_SIZE)
        && y >= button_y
        && y < (button_y + GWES_DIALOG_CLOSE_SIZE);
}

/*
 * Report whether one desktop point hits the minimize controller button.
 *
 * @param window Decorated top-level window under the pointer.
 * @param x Desktop X coordinate.
 * @param y Desktop Y coordinate.
 * @return Non-zero when the minimize button was hit.
 */
int gwes_window_hit_minimize_button(const GwesWindowRecord* window, unsigned long x, unsigned long y) {
    unsigned long button_x = 0UL;
    unsigned long button_y = 0UL;

    if (!gwes_minimize_button_origin(window, &button_x, &button_y)) {
        return 0;
    }

    return x >= button_x
        && x < (button_x + GWES_DIALOG_CLOSE_SIZE)
        && y >= button_y
        && y < (button_y + GWES_DIALOG_CLOSE_SIZE);
}

/*
 * Report whether one desktop point hits the title bar but not the caption buttons.
 *
 * @param window Decorated top-level window under the pointer.
 * @param x Desktop X coordinate.
 * @param y Desktop Y coordinate.
 * @return Non-zero when the title bar should begin a move drag.
 */
int gwes_window_hit_title_bar(const GwesWindowRecord* window, unsigned long x, unsigned long y) {
    if (!gwes_window_is_decorated_root(window)) {
        return 0;
    }
    if (x < (unsigned long)window->x || x >= ((unsigned long)window->x + window->width)) {
        return 0;
    }
    if (y < (unsigned long)window->y || y >= ((unsigned long)window->y + GWES_DIALOG_TITLE_HEIGHT)) {
        return 0;
    }
    return !gwes_window_hit_close_button(window, x, y)
        && !gwes_window_hit_maximize_button(window, x, y)
        && !gwes_window_hit_minimize_button(window, x, y);
}

/*
 * Report which resize edges should react for one desktop point.
 *
 * @param window Decorated top-level window under the pointer.
 * @param x Desktop X coordinate.
 * @param y Desktop Y coordinate.
 * @return Resize-edge mask.
 */
unsigned long gwes_window_resize_edges(const GwesWindowRecord* window, unsigned long x, unsigned long y) {
    unsigned long edges = ROS_KERNEL_GUI_WIDGET_RESIZE_NONE;
    unsigned long left;
    unsigned long top;
    unsigned long right;
    unsigned long bottom;

    if (!gwes_window_is_decorated_root(window) || window->width == 0UL || window->height == 0UL) {
        return edges;
    }

    left = (unsigned long)window->x;
    top = (unsigned long)window->y;
    right = left + window->width;
    bottom = top + window->height;
    if (x < left || x >= right || y < top || y >= bottom) {
        return ROS_KERNEL_GUI_WIDGET_RESIZE_NONE;
    }

    if (x <= left + GWES_DIALOG_RESIZE_GRIP) {
        edges |= ROS_KERNEL_GUI_WIDGET_RESIZE_LEFT;
    }
    else if (x + GWES_DIALOG_RESIZE_GRIP >= right) {
        edges |= ROS_KERNEL_GUI_WIDGET_RESIZE_RIGHT;
    }
    if (y <= top + GWES_DIALOG_RESIZE_GRIP) {
        edges |= ROS_KERNEL_GUI_WIDGET_RESIZE_TOP;
    }
    else if (y + GWES_DIALOG_RESIZE_GRIP >= bottom) {
        edges |= ROS_KERNEL_GUI_WIDGET_RESIZE_BOTTOM;
    }

    return edges;
}

/*
 * Clear the current pointer drag or resize session.
 *
 * @return Nothing.
 */
void gwes_clear_pointer_interaction(void) {
    gwes_render_clear_interaction_placeholder();
    g_gwes_pointer_interaction_hwnd = 0UL;
    g_gwes_pointer_capture_hwnd = 0UL;
    g_gwes_pointer_resize_edges = 0UL;
    g_gwes_pointer_drag_start_x = 0UL;
    g_gwes_pointer_drag_start_y = 0UL;
    g_gwes_pointer_drag_origin_x = 0L;
    g_gwes_pointer_drag_origin_y = 0L;
    g_gwes_pointer_drag_origin_width = 0UL;
    g_gwes_pointer_drag_origin_height = 0UL;
    g_gwes_pointer_drag_offset_x = 0L;
    g_gwes_pointer_drag_offset_y = 0L;
    g_gwes_pointer_preview_active = 0;
    g_gwes_pointer_preview_x = 0L;
    g_gwes_pointer_preview_y = 0L;
    g_gwes_pointer_preview_width = 0UL;
    g_gwes_pointer_preview_height = 0UL;
}

/*
 * Start one title-bar drag or border-resize interaction on a top-level window.
 *
 * @param window Target top-level window.
 * @param pointer_x Desktop X coordinate at interaction start.
 * @param pointer_y Desktop Y coordinate at interaction start.
 * @param resize_edges Non-zero for resize interactions, or zero for moves.
 * @return Nothing.
 */
void gwes_begin_pointer_interaction(GwesWindowRecord* window, unsigned long pointer_x, unsigned long pointer_y, unsigned long resize_edges) {
    if (window == NULL) {
        return;
    }

    g_gwes_pointer_interaction_hwnd = window->hwnd;
    g_gwes_pointer_capture_hwnd = window->hwnd;
    g_gwes_pointer_resize_edges = resize_edges;
    g_gwes_pointer_drag_start_x = pointer_x;
    g_gwes_pointer_drag_start_y = pointer_y;
    g_gwes_pointer_drag_origin_x = window->x;
    g_gwes_pointer_drag_origin_y = window->y;
    g_gwes_pointer_drag_origin_width = window->width;
    g_gwes_pointer_drag_origin_height = window->height;
    g_gwes_pointer_drag_offset_x = (long)pointer_x - window->x;
    g_gwes_pointer_drag_offset_y = (long)pointer_y - window->y;
    gwes_set_pointer_interaction_preview(window->x, window->y, window->width, window->height);
}

/*
 * Deliver one synthetic `WM_MOUSELEAVE` when the hover target changes.
 *
 * @param next_hwnd Window now under the pointer, or zero when none is hovered.
 * @param buttons Pointer button mask.
 * @param x Current desktop X coordinate.
 * @param y Current desktop Y coordinate.
 * @return Nothing.
 */
void gwes_update_hover(unsigned long next_hwnd, unsigned long buttons, unsigned long x, unsigned long y) {
    GwesWindowRecord* previous;
    long local_x = 0L;
    long local_y = 0L;

    if (g_gwes_hover_hwnd == next_hwnd) {
        return;
    }

    previous = gwes_find_window_any(g_gwes_hover_hwnd);
    if (previous != NULL) {
        if (gwes_render_translate_pointer(previous->hwnd, x, y, &local_x, &local_y) != 0) {
            local_x = 0L;
            local_y = 0L;
        }
        gwes_send_window_message(previous, WM_MOUSELEAVE, buttons, gwes_pack_signed_pair(local_x, local_y));
    }

    g_gwes_hover_hwnd = next_hwnd;
}

/*
 * Move one top-level window through the retained renderer and mirror `WM_MOVE`.
 *
 * @param window Target window being dragged.
 * @param x Requested outer-frame X coordinate.
 * @param y Requested outer-frame Y coordinate.
 * @return Zero on success, or a negative status code on failure.
 */
long gwes_move_window_record(GwesWindowRecord* window, long x, long y) {
    long status;

    if (window == NULL) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    status = gwes_render_move_window(window->hwnd, x, y);
    if (status < 0L) {
        return status;
    }

    window->x = x;
    window->y = y;
    if (gwes_window_is_decorated_root(window) && window->show_state == GWES_WINDOW_SHOW_NORMAL) {
        gwes_store_restore_geometry(window);
    }
    else if (gwes_window_is_decorated_root(window) && window->show_state == GWES_WINDOW_SHOW_MINIMIZED && window->restore_valid) {
        window->restore_x = x;
        window->restore_y = y;
    }
    gwes_send_move_message(window);
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Resize one top-level window through the retained renderer and mirror the client notifications.
 *
 * @param window Target window being resized.
 * @param x Requested outer-frame X coordinate.
 * @param y Requested outer-frame Y coordinate.
 * @param width Requested outer-frame width.
 * @param height Requested outer-frame height.
 * @return Zero on success, or a negative status code on failure.
 */
long gwes_resize_window_record(GwesWindowRecord* window, long x, long y, unsigned long width, unsigned long height) {
    long status;
    int moved;
    int resized;

    if (window == NULL) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    moved = window->x != x || window->y != y;
    resized = window->width != width || window->height != height;
    if (!moved && !resized) {
        return ROS_USER_IPC_STATUS_OK;
    }

    if (moved && !resized) {
        return gwes_move_window_record(window, x, y);
    }

    status = gwes_render_resize_window(window->hwnd, x, y, width, height);
    if (status < 0L) {
        return status;
    }

    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    if (moved) {
        gwes_send_move_message(window);
    }
    if (resized) {
        gwes_send_size_message(window);
    }
    if (gwes_window_is_decorated_root(window) && window->show_state == GWES_WINDOW_SHOW_NORMAL) {
        gwes_store_restore_geometry(window);
    }
    else if (gwes_window_is_decorated_root(window) && window->show_state == GWES_WINDOW_SHOW_MINIMIZED && window->restore_valid) {
        window->restore_x = x;
        window->restore_y = y;
    }

    gwes_publish_shell_state();

    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Commit one placeholder move or resize into the live retained-window state.
 *
 * @return Nothing.
 */
void gwes_commit_pointer_interaction(void) {
    GwesWindowRecord* window;

    if (g_gwes_pointer_interaction_hwnd == 0UL || !g_gwes_pointer_preview_active) {
        return;
    }

    window = gwes_find_window_any(g_gwes_pointer_interaction_hwnd);
    if (window == NULL) {
        return;
    }

    if (g_gwes_pointer_resize_edges != ROS_KERNEL_GUI_WIDGET_RESIZE_NONE) {
        (void)gwes_resize_window_record(
            window,
            g_gwes_pointer_preview_x,
            g_gwes_pointer_preview_y,
            g_gwes_pointer_preview_width,
            g_gwes_pointer_preview_height);
    }
    else {
        (void)gwes_move_window_record(window, g_gwes_pointer_preview_x, g_gwes_pointer_preview_y);
    }
}

/*
 * Toggle one top-level window between normal and maximized geometry.
 *
 * @param window Decorated top-level window record.
 * @return Zero on success, or a negative status code on failure.
 */
long gwes_toggle_maximize_window(GwesWindowRecord* window) {
    unsigned long desktop_width = 0UL;
    unsigned long desktop_height = 0UL;
    long desktop_x = 0L;
    long desktop_y = 0L;
    unsigned long previous_state;
    long status;

    if (!gwes_window_is_decorated_root(window)) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    if (window->show_state == GWES_WINDOW_SHOW_MAXIMIZED) {
        if (!window->restore_valid) {
            return ROS_USER_IPC_STATUS_OK;
        }

        window->show_state = GWES_WINDOW_SHOW_NORMAL;
        status = gwes_resize_window_record(window, window->restore_x, window->restore_y, window->restore_width, window->restore_height);
        if (status < 0L) {
            window->show_state = GWES_WINDOW_SHOW_MAXIMIZED;
            return status;
        }
        return ROS_USER_IPC_STATUS_OK;
    }

    if (gwes_window_requests_fullscreen(window)) {
        if (gwes_render_query_desktop_size(&desktop_width, &desktop_height) < 0L) {
            return ROS_USER_IPC_STATUS_NOT_FOUND;
        }
    }
    else if (gwes_query_shell_work_area(&desktop_x, &desktop_y, &desktop_width, &desktop_height) < 0L) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    if (window->show_state == GWES_WINDOW_SHOW_NORMAL || !window->restore_valid) {
        gwes_store_restore_geometry(window);
    }

    previous_state = window->show_state;
    window->show_state = GWES_WINDOW_SHOW_MAXIMIZED;
    status = gwes_resize_window_record(window, desktop_x, desktop_y, desktop_width, desktop_height);
    if (status < 0L) {
        window->show_state = previous_state;
        return status;
    }

    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Toggle one top-level window between normal or maximized geometry and a shaded minimized state.
 *
 * @param window Decorated top-level window record.
 * @return Zero on success, or a negative status code on failure.
 */
long gwes_toggle_minimize_window(GwesWindowRecord* window) {
    long status;
    unsigned long previous_state;
    long target_x;
    long target_y;
    unsigned long target_width;

    if (!gwes_window_is_decorated_root(window)) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    if (window->show_state == GWES_WINDOW_SHOW_MINIMIZED) {
        if (window->minimized_restore_state == GWES_WINDOW_SHOW_MAXIMIZED) {
            return gwes_toggle_maximize_window(window);
        }
        if (!window->restore_valid) {
            window->show_state = GWES_WINDOW_SHOW_NORMAL;
            return ROS_USER_IPC_STATUS_OK;
        }

        window->show_state = GWES_WINDOW_SHOW_NORMAL;
        status = gwes_resize_window_record(window, window->restore_x, window->restore_y, window->restore_width, window->restore_height);
        if (status < 0L) {
            window->show_state = GWES_WINDOW_SHOW_MINIMIZED;
            return status;
        }
        return ROS_USER_IPC_STATUS_OK;
    }

    if (window->show_state == GWES_WINDOW_SHOW_NORMAL || !window->restore_valid) {
        gwes_store_restore_geometry(window);
    }

    target_x = (window->show_state == GWES_WINDOW_SHOW_MAXIMIZED && window->restore_valid) ? window->restore_x : window->x;
    target_y = (window->show_state == GWES_WINDOW_SHOW_MAXIMIZED && window->restore_valid) ? window->restore_y : window->y;
    target_width = (window->show_state == GWES_WINDOW_SHOW_MAXIMIZED && window->restore_valid) ? window->restore_width : window->width;

    previous_state = window->show_state;
    window->minimized_restore_state = previous_state;
    window->show_state = GWES_WINDOW_SHOW_MINIMIZED;
    status = gwes_resize_window_record(window, target_x, target_y, target_width, GWES_DIALOG_TITLE_HEIGHT + GWES_DIALOG_BORDER_THICKNESS);
    if (status < 0L) {
        window->show_state = previous_state;
        return status;
    }

    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Release every class and window record currently owned by one client process.
 *
 * @param owner_pid Client process identifier whose state should be purged.
 * @return Counts of released records for logging and diagnostics.
 */
GwesCleanupSummary gwes_release_client_objects(long owner_pid) {
    GwesCleanupSummary summary = { 0UL, 0UL };
    GwesClassRecord* class_record = g_gwes_classes;
    GwesWindowRecord* window = g_gwes_windows;

    while (class_record != NULL) {
        GwesClassRecord* next = class_record->next;

        if (!class_record->in_use || class_record->owner_pid != owner_pid) {
            class_record = next;
            continue;
        }

        gwes_release_class_record(class_record);
        ++summary.released_classes;
        class_record = next;
    }

    while (window != NULL) {
        GwesWindowRecord* next = window->next;

        if (!window->in_use || window->owner_pid != owner_pid) {
            window = next;
            continue;
        }

        gwes_release_window_record(window, 1);
        ++summary.released_windows;
        window = next;
    }

    if (g_gwes_menu_session.active && g_gwes_menu_session.owner_pid == owner_pid) {
        memset(&g_gwes_menu_session.model, 0, sizeof(g_gwes_menu_session.model));
        memset(&g_gwes_menu_session, 0, sizeof(g_gwes_menu_session));
    }

    gwes_refresh_pointer_cursor();

    return summary;
}

/*
 * Reap any client records that survived a process death without an explicit quit notification.
 *
 * @return Nothing.
 */
void gwes_reap_dead_clients(void) {
    GwesWindowRecord* window = g_gwes_windows;

    while (window != NULL) {
        GwesCleanupSummary summary;
        long owner_pid;
        GwesWindowRecord* next = window->next;

        if (!window->in_use) {
            window = next;
            continue;
        }

        owner_pid = window->owner_pid;
        if (gwes_pid_is_live(owner_pid)) {
            window = next;
            continue;
        }

        summary = gwes_release_client_objects(owner_pid);
        if (summary.released_classes != 0UL || summary.released_windows != 0UL) {
            gwes_log_cleanup(owner_pid, 0UL, "process-dead", summary);
        }

        window = next;
    }
}

/*
 * Reap dead client state from the window and render owner thread.
 *
 * @return Nothing.
 */
void gwes_window_thread_reap_dead_clients(void) {
    gwes_reap_dead_clients();
}

/*
 * Deliver every per-window timer that is due at the supplied time.
 *
 * @param now Current uptime in milliseconds.
 * @return Count of delivered `WM_TIMER` messages.
 */
unsigned long gwes_window_thread_fire_due_timers(unsigned long now) {
    GwesTimerRecord* timer = g_gwes_timers;
    unsigned long delivered_count = 0UL;

    while (timer != NULL) {
        GwesTimerRecord* next = timer->next;
        GwesWindowRecord* window;

        if (!timer->in_use || timer->interval_msec == 0UL || timer->next_fire_msec > now) {
            timer = next;
            continue;
        }
        if (!gwes_pid_is_live(timer->owner_pid)) {
            GwesCleanupSummary summary = gwes_release_client_objects(timer->owner_pid);

            if (summary.released_classes != 0UL || summary.released_windows != 0UL) {
                gwes_log_cleanup(timer->owner_pid, 0UL, "timer-process-dead", summary);
            }
            timer = next;
            continue;
        }

        window = gwes_find_window(timer->owner_pid, timer->hwnd);
        if (window == NULL) {
            gwes_release_timer_record(timer);
            timer = next;
            continue;
        }

        do {
            timer->next_fire_msec += timer->interval_msec;
        } while (timer->next_fire_msec <= now);
        gwes_send_window_message(window, WM_TIMER, timer->timer_id, 0UL);
        ++delivered_count;
        timer = next;
    }

    delivered_count += gwes_menu_fire_due_timers(now);

    return delivered_count;
}

/*
 * Compute the time until the next due window timer.
 *
 * @param now Current uptime in milliseconds.
 * @return Milliseconds until the next due timer, or one second when none exist.
 */
unsigned long gwes_window_thread_timer_wait_budget(unsigned long now) {
    GwesTimerRecord* timer;
    unsigned long best_wait = gwes_menu_timer_wait_budget(now);

    for (timer = g_gwes_timers; timer != NULL; timer = timer->next) {
        unsigned long wait_msec;

        if (!timer->in_use || timer->interval_msec == 0UL) {
            continue;
        }
        if (timer->next_fire_msec <= now) {
            return 0UL;
        }

        wait_msec = timer->next_fire_msec - now;
        if (wait_msec < best_wait) {
            best_wait = wait_msec;
        }
    }

    return best_wait;
}

/*
 * Report whether GWES currently owns one active authoritative popup-menu session.
 *
 * @return Non-zero when one popup menu is active.
 */
int gwes_menu_is_active(void) {
    return g_gwes_menu_session.active != 0;
}

/*
 * Start one authoritative popup-menu tracking session on behalf of one client.
 *
 * @param packet Client request packet that names the owner window and shared model block.
 * @return Zero when setup succeeded, or a negative status code on failure.
 */
long gwes_track_popup_menu(const UserIpcMessage* packet) {
    void* shared_address = NULL;
    RosWindowMenuModel* shared_model;
    GwesWindowRecord* owner_window;
    unsigned long popup_width;
    unsigned long popup_height;
    unsigned long popup_slot;
    long popup_x;
    long popup_y;
    long work_x = 0L;
    long work_y = 0L;
    unsigned long work_width = 0UL;
    unsigned long work_height = 0UL;
    long status;

    if (packet == NULL || packet->arg0 == 0UL || packet->text[0] == '\0') {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    if (g_gwes_menu_session.active) {
        (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_TRACK_POPUP_MENU_REPLY, (unsigned long)ROS_USER_IPC_STATUS_BUSY, 0UL, 0UL, 0UL, NULL, NULL);
        return ROS_USER_IPC_STATUS_BUSY;
    }

    owner_window = gwes_find_window(packet->sender_pid, packet->arg0);
    if (owner_window == NULL) {
        (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_TRACK_POPUP_MENU_REPLY, (unsigned long)ROS_USER_IPC_STATUS_NOT_FOUND, 0UL, 0UL, 0UL, NULL, NULL);
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    status = acquireSharedMemoryRegion(packet->text, 0UL, &shared_address);
    if (status < 0L || shared_address == NULL) {
        (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_TRACK_POPUP_MENU_REPLY, (unsigned long)(status < 0L ? status : ROS_USER_IPC_STATUS_NOT_FOUND), 0UL, 0UL, 0UL, NULL, NULL);
        return status < 0L ? status : ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    shared_model = (RosWindowMenuModel*)shared_address;
    if (!gwes_menu_model_is_valid(shared_model)) {
        (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_TRACK_POPUP_MENU_REPLY, (unsigned long)ROS_USER_IPC_STATUS_NOT_FOUND, 0UL, 0UL, 0UL, NULL, NULL);
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    gwes_menu_compute_popup_extent(shared_model, shared_model->root_menu_index, &popup_width, &popup_height);
    if (gwes_query_shell_work_area(&work_x, &work_y, &work_width, &work_height) < 0L) {
        work_x = 0L;
        work_y = 0L;
        work_width = popup_width;
        work_height = popup_height;
    }
    if (popup_width > work_width || popup_height > work_height) {
        (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_TRACK_POPUP_MENU_REPLY, (unsigned long)ROS_USER_IPC_STATUS_NO_SPACE, 0UL, 0UL, 0UL, NULL, NULL);
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    gwes_unpack_signed_pair(packet->arg1, &popup_x, &popup_y);
    if (popup_x < work_x) {
        popup_x = work_x;
    }
    if (popup_y < work_y) {
        popup_y = work_y;
    }
    if ((unsigned long)(popup_x - work_x) + popup_width > work_width) {
        popup_x = work_x + (long)(work_width - popup_width);
    }
    if ((unsigned long)(popup_y - work_y) + popup_height > work_height) {
        popup_y = work_y + (long)(work_height - popup_height);
    }

    memset(&g_gwes_menu_session, 0, sizeof(g_gwes_menu_session));
    g_gwes_menu_session.active = 1;
    g_gwes_menu_session.owner_pid = packet->sender_pid;
    g_gwes_menu_session.owner_hwnd = (HWND)packet->arg0;
    g_gwes_menu_session.flags = packet->arg2;
    memcpy(&g_gwes_menu_session.model, shared_model, sizeof(g_gwes_menu_session.model));
    gwes_menu_tracef(
        "gwes.exe: menu trace track owner=%lu pid=%ld root-menu=%lu anchor=%ld,%ld flags=0x%lx",
        (unsigned long)packet->arg0,
        packet->sender_pid,
        shared_model->root_menu_index,
        popup_x,
        popup_y,
        packet->arg2);

    status = gwes_menu_create_popup(shared_model->root_menu_index, popup_x, popup_y, &popup_slot);
    if (status < 0L) {
        memset(&g_gwes_menu_session, 0, sizeof(g_gwes_menu_session));
        (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_TRACK_POPUP_MENU_REPLY, (unsigned long)status, 0UL, 0UL, 0UL, NULL, NULL);
        return status;
    }

    g_gwes_menu_session.popups[popup_slot].hover_index = gwes_menu_first_selectable_item(&g_gwes_menu_session.model, shared_model->root_menu_index);
    gwes_menu_repaint_all_popups();
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Consume one keyboard event while the authoritative popup menu is active.
 *
 * @param event Shared-input keyboard event.
 * @return Non-zero when the event was consumed by the popup menu.
 */
int gwes_menu_dispatch_keyboard_event(const RosKernelGuiInputEvent* event) {
    unsigned long popup_slot;
    unsigned long menu_index;
    unsigned long next_index;
    unsigned long first_item;
    const RosWindowMenuItem* item = NULL;

    if (!g_gwes_menu_session.active || event == NULL) {
        return 0;
    }
    if (event->type != ROS_KERNEL_GUI_INPUT_EVENT_KEY_DOWN && event->type != ROS_KERNEL_GUI_INPUT_EVENT_KEY_UP) {
        return 1;
    }
    if (event->type == ROS_KERNEL_GUI_INPUT_EVENT_KEY_UP) {
        return 1;
    }

    if (g_gwes_menu_session.open_popup_count == 0UL) {
        gwes_menu_finish_session(ROS_USER_IPC_STATUS_OK, 0UL);
        return 1;
    }

    popup_slot = g_gwes_menu_session.open_popup_count - 1UL;
    menu_index = g_gwes_menu_session.popups[popup_slot].menu_index;
    first_item = g_gwes_menu_session.model.menus[menu_index].first_item;

    if (event->key == 27UL) {
        gwes_menu_cancel_hover_timers();
        if (g_gwes_menu_session.open_popup_count > 1UL) {
            gwes_menu_close_popups_from(popup_slot);
            gwes_menu_repaint_all_popups();
        }
        else {
            gwes_menu_finish_session(ROS_USER_IPC_STATUS_OK, 0UL);
        }
        return 1;
    }
    if (event->key == '\r' || event->key == '\n') {
        if (g_gwes_menu_session.popups[popup_slot].hover_index != kGwesMenuNoHover) {
            item = &g_gwes_menu_session.model.items[first_item + g_gwes_menu_session.popups[popup_slot].hover_index];

            if (item->submenu_index != ROS_WINDOW_MENU_INVALID_INDEX) {
                gwes_menu_cancel_hover_timers();
                (void)gwes_menu_open_submenu(popup_slot, g_gwes_menu_session.popups[popup_slot].hover_index, 1);
                return 1;
            }
            if (gwes_menu_item_is_selectable(item)) {
                gwes_menu_cancel_hover_timers();
                gwes_menu_finish_session(ROS_USER_IPC_STATUS_OK, item->command_id);
                return 1;
            }
        }
        return 1;
    }

    next_index = gwes_menu_find_hotkey_match(&g_gwes_menu_session.model, menu_index, event->key);
    if (next_index != kGwesMenuNoHover) {
        item = &g_gwes_menu_session.model.items[first_item + next_index];

        g_gwes_menu_session.popups[popup_slot].hover_index = next_index;
        if (item->submenu_index != ROS_WINDOW_MENU_INVALID_INDEX) {
            gwes_menu_cancel_hover_timers();
            (void)gwes_menu_open_submenu(popup_slot, next_index, 1);
        }
        else {
            gwes_menu_cancel_hover_timers();
            gwes_menu_repaint_popup_slot(popup_slot);
            gwes_menu_finish_session(ROS_USER_IPC_STATUS_OK, item->command_id);
        }
    }

    return 1;
}

/*
 * Consume one pointer event while the authoritative popup menu is active.
 *
 * @param event Shared-input pointer event.
 * @return Non-zero when the event was consumed by the popup menu.
 */
int gwes_menu_dispatch_pointer_event(const RosKernelGuiInputEvent* event) {
    GwesWindowRecord* target = NULL;
    long local_x = 0L;
    long local_y = 0L;
    unsigned long popup_slot = ROS_WINDOW_MENU_INVALID_INDEX;
    unsigned long menu_index = ROS_WINDOW_MENU_INVALID_INDEX;
    unsigned long hit_index = kGwesMenuNoHover;
    const RosWindowMenuItem* item = NULL;

    if (!g_gwes_menu_session.active || event == NULL) {
        return 0;
    }

    if (event->type != ROS_KERNEL_GUI_INPUT_EVENT_POINTER_MOVE
        && event->type != ROS_KERNEL_GUI_INPUT_EVENT_POINTER_DOWN
        && event->type != ROS_KERNEL_GUI_INPUT_EVENT_POINTER_UP
        && event->type != ROS_KERNEL_GUI_INPUT_EVENT_POINTER_LEAVE) {
        return 1;
    }

    if (event->type == ROS_KERNEL_GUI_INPUT_EVENT_POINTER_LEAVE) {
        unsigned long index;

        gwes_menu_cancel_hover_timers();

        for (index = 0UL; index < g_gwes_menu_session.open_popup_count; ++index) {
            if (g_gwes_menu_session.popups[index].hover_index != kGwesMenuNoHover) {
                const unsigned long previous_hover = g_gwes_menu_session.popups[index].hover_index;

                g_gwes_menu_session.popups[index].hover_index = kGwesMenuNoHover;
                gwes_menu_repaint_hover_transition(index, previous_hover, kGwesMenuNoHover);
            }
        }
        return 1;
    }

    target = gwes_resolve_pointer_target(event->x, event->y, &local_x, &local_y);
    if (target != NULL) {
        popup_slot = gwes_menu_popup_slot_for_hwnd(&g_gwes_menu_session, target->hwnd);
        if (popup_slot != ROS_WINDOW_MENU_INVALID_INDEX) {
            menu_index = g_gwes_menu_session.popups[popup_slot].menu_index;
            hit_index = gwes_menu_hit_test_y(&g_gwes_menu_session.model, menu_index, local_y);
            if (hit_index != kGwesMenuNoHover) {
                item = &g_gwes_menu_session.model.items[g_gwes_menu_session.model.menus[menu_index].first_item + hit_index];
            }
        }
    }

    if (event->type == ROS_KERNEL_GUI_INPUT_EVENT_POINTER_MOVE) {
        /*
         * Refresh the cursor before repainting menu chrome so pointer motion
         * is not serialized behind the full popup redraw on every hover change.
         */
        gwes_refresh_pointer_cursor_for_target(target);
        if (popup_slot != ROS_WINDOW_MENU_INVALID_INDEX) {
            if (hit_index != g_gwes_menu_session.popups[popup_slot].hover_index) {
                const unsigned long previous_hover = g_gwes_menu_session.popups[popup_slot].hover_index;

                g_gwes_menu_session.popups[popup_slot].hover_index = hit_index;
                gwes_menu_repaint_hover_transition(popup_slot, previous_hover, hit_index);
            }
            if (item != NULL && item->submenu_index != ROS_WINDOW_MENU_INVALID_INDEX) {
                gwes_menu_cancel_close_timer();
                if (!gwes_menu_is_submenu_visible_for_item(popup_slot, item)) {
                    gwes_menu_arm_open_timer(popup_slot, hit_index, 0);
                }
                else {
                    gwes_menu_cancel_open_timer();
                }
            }
            else {
                gwes_menu_cancel_open_timer();
                gwes_menu_arm_close_timer(popup_slot + 1UL);
            }
        }
        else {
            gwes_menu_cancel_open_timer();
        }
        return 1;
    }

    if (event->type == ROS_KERNEL_GUI_INPUT_EVENT_POINTER_DOWN) {
        if (popup_slot == ROS_WINDOW_MENU_INVALID_INDEX) {
            gwes_menu_finish_session(ROS_USER_IPC_STATUS_OK, 0UL);
            return 1;
        }

        gwes_menu_cancel_hover_timers();

        if (g_gwes_menu_session.popups[popup_slot].hover_index != hit_index) {
            const unsigned long previous_hover = g_gwes_menu_session.popups[popup_slot].hover_index;

            g_gwes_menu_session.popups[popup_slot].hover_index = hit_index;
            gwes_menu_repaint_hover_transition(popup_slot, previous_hover, hit_index);
        }
        if (item != NULL && item->submenu_index != ROS_WINDOW_MENU_INVALID_INDEX) {
            (void)gwes_menu_open_submenu(popup_slot, hit_index, 0);
        }
        else {
            gwes_menu_close_popups_from(popup_slot + 1UL);
        }
        return 1;
    }

    if (event->type == ROS_KERNEL_GUI_INPUT_EVENT_POINTER_UP) {
        if (popup_slot == ROS_WINDOW_MENU_INVALID_INDEX || hit_index == kGwesMenuNoHover || item == NULL) {
            gwes_menu_finish_session(ROS_USER_IPC_STATUS_OK, 0UL);
            return 1;
        }

        gwes_menu_cancel_hover_timers();

        if (item->submenu_index != ROS_WINDOW_MENU_INVALID_INDEX) {
            (void)gwes_menu_open_submenu(popup_slot, hit_index, 1);
            return 1;
        }
        if (gwes_menu_item_is_selectable(item)) {
            gwes_menu_finish_session(ROS_USER_IPC_STATUS_OK, item->command_id);
        }

        return 1;
    }

    return 1;
}