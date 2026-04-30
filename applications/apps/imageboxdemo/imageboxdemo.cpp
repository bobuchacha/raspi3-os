#define ROS_APP_WITH_CRT 1
#define ROS_APP_USE_WINDOW 1
#define ROS_APP_USE_GDI 1
#define ROS_APP_USE_PNG 1
#include "app/app.h"
#include "app/jpeg.h"

#include <stdarg.h>
#include <stdint.h>

#define IMAGEBOXDEMO_WINDOW_CLASS "imageboxdemo.main"
#define IMAGEBOXDEMO_WINDOW_TITLE "imageboxdemo"
#define IMAGEBOXDEMO_WINDOW_X 112L
#define IMAGEBOXDEMO_WINDOW_Y 88L
#define IMAGEBOXDEMO_WINDOW_WIDTH 780UL
#define IMAGEBOXDEMO_WINDOW_HEIGHT 420UL
#define IMAGEBOXDEMO_WM_ASSETS_READY (WM_USER + 1UL)

#define IMAGEBOXDEMO_JPEG_PATH "C:\\wallpapers\\bliss.jpg"
#define IMAGEBOXDEMO_PNG_PATH "C:\\icons\\start_menu.png"

#define IMAGEBOXDEMO_MARGIN 24UL
#define IMAGEBOXDEMO_PANEL_GAP 20UL
#define IMAGEBOXDEMO_PANEL_TITLE_HEIGHT 32UL
#define IMAGEBOXDEMO_PANEL_PADDING 12UL

typedef struct ImageboxdemoRect {
    unsigned long x;
    unsigned long y;
    unsigned long width;
    unsigned long height;
} ImageboxdemoRect;

typedef struct ImageboxdemoArgbImage {
    unsigned long width;
    unsigned long height;
    uint32_t* pixels;
    long load_status;
} ImageboxdemoArgbImage;

typedef enum ImageboxdemoAssetKind {
    IMAGEBOXDEMO_ASSET_KIND_NONE = 0,
    IMAGEBOXDEMO_ASSET_KIND_JPEG = 1,
    IMAGEBOXDEMO_ASSET_KIND_PNG = 2
} ImageboxdemoAssetKind;

static HWND g_imageboxdemo_window = 0UL;
static RosGdiFont g_imageboxdemo_font;
static int g_imageboxdemo_font_ready = 0;
static long g_imageboxdemo_font_status = -1L;
static int g_imageboxdemo_font_prepare_attempted = 0;
static int g_imageboxdemo_background_load_started = 0;
static int g_imageboxdemo_background_load_running = 0;
static int g_imageboxdemo_background_load_complete = 0;
static int g_imageboxdemo_close_requested = 0;
static U8* g_imageboxdemo_jpeg_bytes = 0;
static int g_imageboxdemo_jpeg_bytes_from_window_loader = 0;
static U32 g_imageboxdemo_jpeg_size = 0U;
static long g_imageboxdemo_jpeg_load_status = -1L;
static long g_imageboxdemo_jpeg_draw_status = -1L;
static ImageboxdemoArgbImage g_imageboxdemo_png_image = { 0UL, 0UL, 0, -1L };

static long imageboxdemo_paint_window(HWND hwnd);

/*
 * Write one formatted debug line for the demo.
 *
 * @param fmt Printf-style format string.
 * @return Nothing.
 */
static void imageboxdemo_log(const char* fmt, ...) {
    char buffer[224];
    va_list args;

    if (!fmt) {
        return;
    }

    va_start(args, fmt);
    crt_vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    writeLine(buffer);
}

/*
 * Finish one deferred close request on the UI thread.
 *
 * Clearing the cached handle first prevents late worker notifications from
 * posting into a window that is already being torn down.
 *
 * @param hwnd Window being closed.
 * @return Nothing.
 */
static void imageboxdemo_complete_close(HWND hwnd) {
    if (g_imageboxdemo_window != 0UL) {
        g_imageboxdemo_window = 0UL;
    }
    if (hwnd != 0UL) {
        (void)DestroyWindow(hwnd);
    }
    (void)PostQuitMessage(0L);
}

/*
 * Translate one RGB color into the active shared-surface pixel format.
 *
 * The PNG helper renders into an ARGB canvas, while the shared GWES surface
 * can publish either XRGB8888 or XBGR8888. Mirroring GDI's own conversion here
 * lets the demo blit cached PNG pixels without depending on extra exported APIs.
 *
 * @param pixel_format Target `ROS_KERNEL_GUI_PIXEL_FORMAT_*` value.
 * @param color RGB color in `0x00RRGGBB` form.
 * @return Encoded 32-bit surface pixel value.
 */
static unsigned long imageboxdemo_encode_color(unsigned long pixel_format, unsigned long color) {
    unsigned long rgb;

    rgb = color & 0x00FFFFFFUL;
    if (pixel_format != ROS_KERNEL_GUI_PIXEL_FORMAT_XBGR8888) {
        return 0xFF000000UL | rgb;
    }

    return 0xFF000000UL
        | ((rgb & 0x000000FFUL) << 16)
        | (rgb & 0x0000FF00UL)
        | ((rgb & 0x00FF0000UL) >> 16);
}

/*
 * Decode one surface pixel back into canonical RGB order.
 *
 * PNG alpha compositing happens in RGB space, so the demo normalizes the
 * destination pixel before blending transparent icon pixels over the panel
 * background.
 *
 * @param pixel_format Source `ROS_KERNEL_GUI_PIXEL_FORMAT_*` value.
 * @param encoded Surface pixel read from the mapped window buffer.
 * @return RGB color in `0x00RRGGBB` form.
 */
static unsigned long imageboxdemo_decode_color(unsigned long pixel_format, unsigned long encoded) {
    unsigned long rgb;

    rgb = encoded & 0x00FFFFFFUL;
    if (pixel_format != ROS_KERNEL_GUI_PIXEL_FORMAT_XBGR8888) {
        return rgb;
    }

    return ((rgb & 0x000000FFUL) << 16)
        | (rgb & 0x0000FF00UL)
        | ((rgb & 0x00FF0000UL) >> 16);
}

/*
 * Blend one RGB foreground color over one RGB background color.
 *
 * The PNG loader preserves alpha in the ARGB scratch buffer, so the icon demo
 * should keep semi-transparent edges instead of treating the image as opaque.
 * Integer math is enough here because the shared GUI surface is 8 bits per
 * channel and this path only exists to validate rendering, not to do photo
 * editing.
 *
 * @param background Existing RGB background color.
 * @param foreground New RGB foreground color.
 * @param alpha Foreground alpha in the range 0..255.
 * @return Blended RGB color in `0x00RRGGBB` form.
 */
static unsigned long imageboxdemo_blend_rgb(unsigned long background, unsigned long foreground, unsigned int alpha) {
    unsigned int inverse_alpha;
    unsigned int red;
    unsigned int green;
    unsigned int blue;

    if (alpha >= 255U) {
        return foreground;
    }
    if (alpha == 0U) {
        return background;
    }

    inverse_alpha = 255U - alpha;
    red = (unsigned int)(((((background >> 16) & 0xFFUL) * inverse_alpha) + (((foreground >> 16) & 0xFFUL) * alpha) + 127U) / 255U);
    green = (unsigned int)(((((background >> 8) & 0xFFUL) * inverse_alpha) + (((foreground >> 8) & 0xFFUL) * alpha) + 127U) / 255U);
    blue = (unsigned int)((((background & 0xFFUL) * inverse_alpha) + ((foreground & 0xFFUL) * alpha) + 127U) / 255U);
    return ((unsigned long)red << 16) | ((unsigned long)green << 8) | (unsigned long)blue;
}

/*
 * Read one complete file into a caller-owned byte buffer.
 *
 * The JPEG DLL accepts an in-memory byte stream rather than a path, so the demo
 * needs one tiny CRT-backed loader to hand the staged wallpaper bytes to the
 * decoder without adding another userspace helper library.
 *
 * @param path VFS path to the source file.
 * @param bytes_out Receives one heap allocation containing the file contents.
 * @param size_out Receives the byte count stored in `bytes_out`.
 * @return Zero on success, or a negative status code on failure.
 */
static long imageboxdemo_read_file_bytes(const char* path, U8** bytes_out, U32* size_out) {
    U8* bytes;
    size_t capacity;
    size_t size;
    size_t available;
    long read_result;
    void* replacement;

    if (!path || !bytes_out || !size_out) {
        return -1L;
    }

    *bytes_out = 0;
    *size_out = 0U;
    capacity = 4096U;
    bytes = (U8*)crt_malloc(capacity);
    if (bytes == NULL) {
        return -2L;
    }

    size = 0U;
    for (;;) {
        available = capacity - size;
        if (available == 0U) {
            if (capacity > (size_t)0x7FFFFFFFU) {
                crt_free(bytes);
                return -3L;
            }

            replacement = crt_realloc(bytes, capacity * 2U);
            if (replacement == NULL) {
                crt_free(bytes);
                return -4L;
            }

            bytes = (U8*)replacement;
            capacity *= 2U;
            available = capacity - size;
        }

        read_result = readFile(path, (unsigned long)size, (char*)bytes + size, (unsigned long)available);
        if (read_result < 0L) {
            crt_free(bytes);
            return read_result;
        }
        if (read_result == 0L) {
            break;
        }

        size += (size_t)read_result;
        if ((size_t)read_result < available) {
            break;
        }
    }

    if (size == 0U || size > (size_t)0xFFFFFFFFUL) {
        crt_free(bytes);
        return -5L;
    }

    *bytes_out = bytes;
    *size_out = (U32)size;
    return 0L;
}

/*
 * Load the staged JPEG wallpaper once during startup.
 *
 * Caching the file bytes keeps repaint logic simple: every `WM_PAINT` can hand
 * the same immutable buffer to `jpeg_render_to_surface` without reopening the
 * VFS file or managing a second decoded scratch image.
 *
 * @return Zero on success, or a negative status code on failure.
 */
static long imageboxdemo_load_jpeg_asset(void) {
    g_imageboxdemo_jpeg_bytes_from_window_loader = 0;
    g_imageboxdemo_jpeg_load_status = imageboxdemo_read_file_bytes(
        IMAGEBOXDEMO_JPEG_PATH,
        &g_imageboxdemo_jpeg_bytes,
        &g_imageboxdemo_jpeg_size);
    if (g_imageboxdemo_jpeg_load_status >= 0L) {
        imageboxdemo_log(
            "imageboxdemo.exe: loaded jpeg path=%s size=%lu",
            IMAGEBOXDEMO_JPEG_PATH,
            (unsigned long)g_imageboxdemo_jpeg_size);
    }
    else {
        imageboxdemo_log(
            "imageboxdemo.exe: failed to load jpeg path=%s status=%ld",
            IMAGEBOXDEMO_JPEG_PATH,
            g_imageboxdemo_jpeg_load_status);
    }

    return g_imageboxdemo_jpeg_load_status;
}

/*
 * Decode one staged PNG byte stream into the reusable ARGB scratch image.
 *
 * The async file loader only stages raw bytes. The UI thread adopts those
 * bytes, queries the intrinsic PNG size, allocates the scratch canvas, and
 * decodes the pixels without reopening the file path.
 *
 * @param bytes Pointer to the complete PNG byte stream.
 * @param size Byte count stored in `bytes`.
 * @return Zero on success, or a negative status code on failure.
 */
static long imageboxdemo_decode_png_bytes(const void* bytes, U32 size) {
    RosPngMemory memory;
    U32 width = 0U;
    U32 height = 0U;
    size_t pixel_count;

    if (bytes == 0 || size == 0U) {
        return -1L;
    }

    if (g_imageboxdemo_png_image.pixels != 0) {
        crt_free(g_imageboxdemo_png_image.pixels);
        g_imageboxdemo_png_image.pixels = 0;
    }
    g_imageboxdemo_png_image.width = 0UL;
    g_imageboxdemo_png_image.height = 0UL;

    g_imageboxdemo_png_image.load_status = PngGetInfoFromMemory(bytes, size, &width, &height);
    if (g_imageboxdemo_png_image.load_status < 0L) {
        imageboxdemo_log("imageboxdemo.exe: failed to probe png bytes status=%ld", g_imageboxdemo_png_image.load_status);
        return g_imageboxdemo_png_image.load_status;
    }

    pixel_count = (size_t)width * (size_t)height;
    g_imageboxdemo_png_image.pixels = (uint32_t*)crt_calloc(pixel_count, sizeof(uint32_t));
    if (g_imageboxdemo_png_image.pixels == NULL) {
        g_imageboxdemo_png_image.load_status = -2L;
        imageboxdemo_log("imageboxdemo.exe: failed to allocate png buffer pixels=%lu", (unsigned long)pixel_count);
        return g_imageboxdemo_png_image.load_status;
    }

    memory.x = 0U;
    memory.y = 0U;
    memory.width = width;
    memory.height = height;
    memory.buffer = g_imageboxdemo_png_image.pixels;
    g_imageboxdemo_png_image.load_status = PngRenderMemoryToMemory(bytes, size, &memory);
    if (g_imageboxdemo_png_image.load_status < 0L) {
        crt_free(g_imageboxdemo_png_image.pixels);
        g_imageboxdemo_png_image.pixels = 0;
        imageboxdemo_log("imageboxdemo.exe: failed to decode png bytes status=%ld", g_imageboxdemo_png_image.load_status);
        return g_imageboxdemo_png_image.load_status;
    }

    g_imageboxdemo_png_image.width = (unsigned long)width;
    g_imageboxdemo_png_image.height = (unsigned long)height;
    imageboxdemo_log(
        "imageboxdemo.exe: decoded png bytes size=%lux%lu",
        g_imageboxdemo_png_image.width,
        g_imageboxdemo_png_image.height);
    return 0L;
}

/*
 * Decode the staged PNG icon into one reusable ARGB scratch image.
 *
 * The synchronous fallback path still reads the PNG once on the caller thread,
 * but it now reuses the same memory-based decoder that the async completion
 * path uses after adopting worker-staged bytes.
 *
 * @return Zero on success, or a negative status code on failure.
 */
static long imageboxdemo_load_png_asset(void) {
    U8* bytes = 0;
    U32 size = 0U;
    long status;

    status = imageboxdemo_read_file_bytes(IMAGEBOXDEMO_PNG_PATH, &bytes, &size);
    if (status < 0L) {
        g_imageboxdemo_png_image.load_status = status;
        imageboxdemo_log(
            "imageboxdemo.exe: failed to load png path=%s status=%ld",
            IMAGEBOXDEMO_PNG_PATH,
            status);
        return status;
    }

    status = imageboxdemo_decode_png_bytes(bytes, size);
    crt_free(bytes);
    return status;
}

/*
 * Load one fallback UI font for panel labels and status text.
 *
 * The demo should still work when font loading fails, but having a readable
 * caption around each image box makes manual verification much faster than
 * relying on the serial log alone.
 *
 * @return Nothing.
 */
static void imageboxdemo_prepare_font(void) {
    g_imageboxdemo_font_status = GdiLoadFont(0, 16UL, &g_imageboxdemo_font);
    if (g_imageboxdemo_font_status >= 0L) {
        g_imageboxdemo_font_ready = 1;
        imageboxdemo_log("imageboxdemo.exe: loaded fallback ui font");
        return;
    }

    imageboxdemo_log("imageboxdemo.exe: font load failed status=%ld", g_imageboxdemo_font_status);
}

/*
 * Ensure the fallback UI font is prepared at most once.
 *
 * JPEG and PNG staging now complete through UI-thread completion messages. The
 * sample only needs to pay the font setup cost once no matter which asset
 * finishes first.
 *
 * @return Nothing.
 */
static void imageboxdemo_prepare_font_once(void) {
    if (g_imageboxdemo_font_prepare_attempted) {
        return;
    }

    g_imageboxdemo_font_prepare_attempted = 1;
    imageboxdemo_prepare_font();
}

/*
 * Complete the background asset pipeline on the UI thread.
 *
 * Once the final asset completion has been claimed, the demo can either honor
 * a deferred close request or keep running with a stable loaded-state flag.
 *
 * @return Nothing.
 */
static void imageboxdemo_finish_background_load(void) {
    g_imageboxdemo_background_load_running = 0;
    g_imageboxdemo_background_load_complete = 1;
}

static void imageboxdemo_queue_png_stage(void);
static void imageboxdemo_consume_async_asset(unsigned long request_id);

/*
 * Release startup caches and font state before process exit.
 *
 * Keeping teardown explicit makes the sample safe to restart repeatedly while
 * iterating on GWES or decoder behavior.
 *
 * @return Nothing.
 */
static void imageboxdemo_release_resources(void) {
    if (g_imageboxdemo_font_ready) {
        (void)GdiUnloadFont(&g_imageboxdemo_font);
        g_imageboxdemo_font_ready = 0;
    }
    if (g_imageboxdemo_jpeg_bytes != 0) {
        if (g_imageboxdemo_jpeg_bytes_from_window_loader) {
            FreeAssetBuffer(g_imageboxdemo_jpeg_bytes);
        }
        else {
            crt_free(g_imageboxdemo_jpeg_bytes);
        }
        g_imageboxdemo_jpeg_bytes = 0;
        g_imageboxdemo_jpeg_bytes_from_window_loader = 0;
        g_imageboxdemo_jpeg_size = 0U;
    }
    if (g_imageboxdemo_png_image.pixels != 0) {
        crt_free(g_imageboxdemo_png_image.pixels);
        g_imageboxdemo_png_image.pixels = 0;
    }
    g_imageboxdemo_png_image.width = 0UL;
    g_imageboxdemo_png_image.height = 0UL;
}

/*
 * Claim one queued asset result from window.dll and adopt it on the UI thread.
 *
 * The worker thread only stages file bytes and posts a wake-up message. The UI
 * thread receives that message, claims the result by request id, updates the
 * demo's owned asset state, and then repaints or closes.
 *
 * @param request_id Request identifier carried in the posted completion message.
 * @return Nothing.
 */
static void imageboxdemo_consume_async_asset(unsigned long request_id) {
    WindowAssetCompletion completion;
    ImageboxdemoAssetKind asset_kind;
    long status;

    status = ReceiveAssetCompletion(request_id, &completion);
    if (status < 0L) {
        imageboxdemo_log("imageboxdemo.exe: missing async completion request=%lu status=%ld", request_id, status);
        return;
    }

    asset_kind = (ImageboxdemoAssetKind)(uintptr_t)completion.context;
    imageboxdemo_prepare_font_once();

    if (asset_kind == IMAGEBOXDEMO_ASSET_KIND_JPEG) {
        if (completion.status >= 0L && !g_imageboxdemo_close_requested) {
            g_imageboxdemo_jpeg_bytes = (U8*)completion.bytes;
            g_imageboxdemo_jpeg_bytes_from_window_loader = 1;
            g_imageboxdemo_jpeg_size = (U32)completion.size;
            g_imageboxdemo_jpeg_load_status = 0L;
            imageboxdemo_log(
                "imageboxdemo.exe: async jpeg ready path=%s size=%lu",
                completion.path[0] != '\0' ? completion.path : IMAGEBOXDEMO_JPEG_PATH,
                (unsigned long)g_imageboxdemo_jpeg_size);
        }
        else {
            if (completion.bytes != 0) {
                FreeAssetBuffer(completion.bytes);
            }
            g_imageboxdemo_jpeg_load_status = completion.status;
            g_imageboxdemo_jpeg_bytes = 0;
            g_imageboxdemo_jpeg_bytes_from_window_loader = 0;
            g_imageboxdemo_jpeg_size = 0U;
            imageboxdemo_log(
                "imageboxdemo.exe: async jpeg failed path=%s status=%ld",
                completion.path[0] != '\0' ? completion.path : IMAGEBOXDEMO_JPEG_PATH,
                completion.status);
        }

        if (g_imageboxdemo_close_requested) {
            imageboxdemo_finish_background_load();
            return;
        }

        imageboxdemo_queue_png_stage();
        return;
    }

    if (asset_kind == IMAGEBOXDEMO_ASSET_KIND_PNG) {
        if (completion.status >= 0L && !g_imageboxdemo_close_requested) {
            g_imageboxdemo_png_image.load_status = imageboxdemo_decode_png_bytes(completion.bytes, (U32)completion.size);
        }
        else {
            g_imageboxdemo_png_image.load_status = completion.status;
            imageboxdemo_log(
                "imageboxdemo.exe: async png failed path=%s status=%ld",
                completion.path[0] != '\0' ? completion.path : IMAGEBOXDEMO_PNG_PATH,
                completion.status);
        }

        if (completion.bytes != 0) {
            FreeAssetBuffer(completion.bytes);
        }
        imageboxdemo_finish_background_load();
    }
}

/*
 * Queue the second-stage PNG asset request.
 *
 * JPEG and font readiness are enough for the first visible repaint, while the
 * PNG icon can continue staging in the same shared callback pipeline.
 *
 * @return Nothing.
 */
static void imageboxdemo_queue_png_stage(void) {
    long request_id;

    if (g_imageboxdemo_close_requested) {
        imageboxdemo_finish_background_load();
        return;
    }

    request_id = LoadFileAssetAsyncNotify(
        IMAGEBOXDEMO_PNG_PATH,
        g_imageboxdemo_window,
        IMAGEBOXDEMO_WM_ASSETS_READY,
        (void*)(uintptr_t)IMAGEBOXDEMO_ASSET_KIND_PNG);
    if (request_id >= 0L) {
        imageboxdemo_log("imageboxdemo.exe: queued async png stage request=%ld", request_id);
        return;
    }

    imageboxdemo_log("imageboxdemo.exe: async png queue failed status=%ld", request_id);
    imageboxdemo_prepare_font_once();
    (void)imageboxdemo_load_png_asset();
    imageboxdemo_finish_background_load();
}

/*
 * Start the background asset pipeline once.
 *
 * The new flow uses window.dll's shared async asset worker instead of an app-
 * specific parked helper thread. If the request cannot be queued, the demo
 * falls back to the old synchronous startup path so functionality still works.
 *
 * @return Nothing.
 */
static void imageboxdemo_start_background_loader(void) {
    long request_id;

    if (g_imageboxdemo_background_load_started) {
        return;
    }

    g_imageboxdemo_background_load_started = 1;
    g_imageboxdemo_background_load_running = 1;
    request_id = LoadFileAssetAsyncNotify(
        IMAGEBOXDEMO_JPEG_PATH,
        g_imageboxdemo_window,
        IMAGEBOXDEMO_WM_ASSETS_READY,
        (void*)(uintptr_t)IMAGEBOXDEMO_ASSET_KIND_JPEG);
    if (request_id >= 0L) {
        imageboxdemo_log("imageboxdemo.exe: queued async jpeg stage request=%ld", request_id);
        return;
    }

    imageboxdemo_log("imageboxdemo.exe: async jpeg queue failed status=%ld", request_id);
    g_imageboxdemo_font_prepare_attempted = 1;
    imageboxdemo_prepare_font();
    (void)imageboxdemo_load_jpeg_asset();
    (void)imageboxdemo_load_png_asset();
    imageboxdemo_finish_background_load();
}

/*
 * Compute the two panel rectangles from the current surface size.
 *
 * The window is resizable, so panel geometry should follow the client surface
 * rather than baking one set of coordinates that only looks correct at startup.
 *
 * @param surface_width Current mapped surface width.
 * @param surface_height Current mapped surface height.
 * @param jpeg_panel Receives the left JPEG panel rectangle.
 * @param png_panel Receives the right PNG panel rectangle.
 * @return Nothing.
 */
static void imageboxdemo_layout_panels(
    unsigned long surface_width,
    unsigned long surface_height,
    ImageboxdemoRect* jpeg_panel,
    ImageboxdemoRect* png_panel
) {
    unsigned long usable_width;
    unsigned long panel_width;
    unsigned long panel_height;

    if (!jpeg_panel || !png_panel) {
        return;
    }

    usable_width = surface_width > (IMAGEBOXDEMO_MARGIN * 2UL + IMAGEBOXDEMO_PANEL_GAP)
        ? (surface_width - (IMAGEBOXDEMO_MARGIN * 2UL) - IMAGEBOXDEMO_PANEL_GAP)
        : 320UL;
    panel_width = usable_width / 2UL;
    panel_height = surface_height > 118UL ? (surface_height - 118UL) : 180UL;

    jpeg_panel->x = IMAGEBOXDEMO_MARGIN;
    jpeg_panel->y = 92UL;
    jpeg_panel->width = panel_width;
    jpeg_panel->height = panel_height;

    png_panel->x = IMAGEBOXDEMO_MARGIN + panel_width + IMAGEBOXDEMO_PANEL_GAP;
    png_panel->y = 92UL;
    png_panel->width = panel_width;
    png_panel->height = panel_height;
}

/*
 * Derive the inner image rectangle for one framed panel.
 *
 * Separating the title strip from the image area keeps the rendered content off
 * the text and gives both decoders one consistent destination box.
 *
 * @param panel Outer framed panel rectangle.
 * @param image_rect Receives the inner destination rectangle.
 * @return Nothing.
 */
static void imageboxdemo_panel_image_rect(const ImageboxdemoRect* panel, ImageboxdemoRect* image_rect) {
    if (!panel || !image_rect) {
        return;
    }

    image_rect->x = panel->x + IMAGEBOXDEMO_PANEL_PADDING;
    image_rect->y = panel->y + IMAGEBOXDEMO_PANEL_TITLE_HEIGHT + IMAGEBOXDEMO_PANEL_PADDING;
    image_rect->width = panel->width > (IMAGEBOXDEMO_PANEL_PADDING * 2UL)
        ? (panel->width - (IMAGEBOXDEMO_PANEL_PADDING * 2UL))
        : panel->width;
    image_rect->height = panel->height > (IMAGEBOXDEMO_PANEL_TITLE_HEIGHT + (IMAGEBOXDEMO_PANEL_PADDING * 2UL))
        ? (panel->height - IMAGEBOXDEMO_PANEL_TITLE_HEIGHT - (IMAGEBOXDEMO_PANEL_PADDING * 2UL))
        : panel->height;
}

/*
 * Paint the shared frame and title strip for one image panel.
 *
 * The visual styling is intentionally lightweight so the sample highlights the
 * decoder results rather than looking like a new widget toolkit.
 *
 * @param surface Active mapped window surface.
 * @param panel Outer panel rectangle.
 * @param title Panel title text.
 * @return Nothing.
 */
static void imageboxdemo_draw_panel(const RosGdiSurface* surface, const ImageboxdemoRect* panel, const char* title) {
    if (!surface || !panel) {
        return;
    }

    (void)GdiFillSurfaceRect(surface, panel->x, panel->y, panel->width, panel->height, 0x001B2432UL);
    (void)GdiFillSurfaceRect(surface, panel->x, panel->y, panel->width, IMAGEBOXDEMO_PANEL_TITLE_HEIGHT, 0x0023344BUL);
    (void)GdiFillSurfaceRect(surface, panel->x, panel->y, panel->width, 1UL, 0x006C7F99UL);
    (void)GdiFillSurfaceRect(surface, panel->x, panel->y + panel->height - 1UL, panel->width, 1UL, 0x006C7F99UL);
    (void)GdiFillSurfaceRect(surface, panel->x, panel->y, 1UL, panel->height, 0x006C7F99UL);
    (void)GdiFillSurfaceRect(surface, panel->x + panel->width - 1UL, panel->y, 1UL, panel->height, 0x006C7F99UL);
    if (g_imageboxdemo_font_ready && title) {
        (void)GdiDrawTextSurface(surface, &g_imageboxdemo_font, panel->x + 10UL, panel->y + 8UL, title, 0x00E6EEF8UL);
    }
}

/*
 * Draw one placeholder message inside an image rectangle.
 *
 * Load failures should remain visible inside the GUI itself so decoder changes
 * can be triaged without switching to the serial console.
 *
 * @param surface Active mapped window surface.
 * @param rect Destination rectangle.
 * @param message Placeholder text.
 * @return Nothing.
 */
static void imageboxdemo_draw_placeholder(const RosGdiSurface* surface, const ImageboxdemoRect* rect, const char* message) {
    if (!surface || !rect) {
        return;
    }

    (void)GdiFillSurfaceRect(surface, rect->x, rect->y, rect->width, rect->height, 0x000E1620UL);
    if (g_imageboxdemo_font_ready && message) {
        (void)GdiDrawTextSurface(surface, &g_imageboxdemo_font, rect->x + 12UL, rect->y + 12UL, message, 0x00D5DEE8UL);
    }
}

/*
 * Composite the cached ARGB PNG image into the destination image box.
 *
 * The PNG loader produces an ARGB scratch canvas, not a live GWES surface. A
 * small local blitter keeps the existing decoder untouched while still testing
 * alpha edges against the window background.
 *
 * @param surface Active mapped window surface.
 * @param destination Inner image-box rectangle.
 * @param image Cached PNG image.
 * @return Nothing.
 */
static void imageboxdemo_blit_png_image(
    const RosGdiSurface* surface,
    const ImageboxdemoRect* destination,
    const ImageboxdemoArgbImage* image
) {
    unsigned long draw_width;
    unsigned long draw_height;
    unsigned long source_start_x;
    unsigned long source_start_y;
    unsigned long destination_start_x;
    unsigned long destination_start_y;
    unsigned long row;

    if (!surface || !destination || !image || !image->pixels || image->width == 0UL || image->height == 0UL) {
        return;
    }

    draw_width = image->width < destination->width ? image->width : destination->width;
    draw_height = image->height < destination->height ? image->height : destination->height;
    source_start_x = image->width > destination->width ? (image->width - destination->width) / 2UL : 0UL;
    source_start_y = image->height > destination->height ? (image->height - destination->height) / 2UL : 0UL;
    destination_start_x = destination->x + (destination->width > draw_width ? (destination->width - draw_width) / 2UL : 0UL);
    destination_start_y = destination->y + (destination->height > draw_height ? (destination->height - draw_height) / 2UL : 0UL);

    /*
     * The icon path is the only place where this demo needs alpha compositing,
     * so the blend loop stays local instead of turning into another exported
     * helper in the GDI DLL surface API.
     */
    for (row = 0UL; row < draw_height; ++row) {
        const uint32_t* source_row = image->pixels + ((source_start_y + row) * image->width) + source_start_x;
        uint32_t* destination_row = (uint32_t*)((U8*)surface->pixels + ((destination_start_y + row) * surface->pitch)) + destination_start_x;
        unsigned long column;

        for (column = 0UL; column < draw_width; ++column) {
            unsigned long source_rgb = source_row[column] & 0x00FFFFFFUL;
            unsigned int alpha = (unsigned int)((source_row[column] >> 24) & 0xFFUL);

            if (alpha >= 255U) {
                destination_row[column] = (uint32_t)imageboxdemo_encode_color(surface->pixel_format, source_rgb);
                continue;
            }
            if (alpha == 0U) {
                continue;
            }

            destination_row[column] = (uint32_t)imageboxdemo_encode_color(
                surface->pixel_format,
                imageboxdemo_blend_rgb(
                    imageboxdemo_decode_color(surface->pixel_format, destination_row[column]),
                    source_rgb,
                    alpha));
        }
    }
}

/*
 * Paint the full demo window.
 *
 * The sample redraws both panels from current cached assets on every paint so
 * resize testing exercises the JPEG cover-fit path and the PNG alpha blit path
 * together inside one simple manual test app.
 *
 * @param hwnd Target window handle.
 * @return Zero on success, or a negative status code on failure.
 */
static long imageboxdemo_paint_window(HWND hwnd) {
    RosGdiSurface surface;
    ImageboxdemoRect jpeg_panel;
    ImageboxdemoRect png_panel;
    ImageboxdemoRect jpeg_image_rect;
    ImageboxdemoRect png_image_rect;
    char status_line[224];
    long status;
    long release_status;

    status = GdiGetWindowSurface(hwnd, &surface);
    if (status < 0L) {
        imageboxdemo_log("imageboxdemo.exe: GdiGetWindowSurface failed status=%ld", status);
        return status;
    }

    (void)GdiFillSurfaceRect(&surface, 0UL, 0UL, surface.width, surface.height, 0x0010151CUL);
    (void)GdiFillSurfaceRect(&surface, 0UL, 0UL, surface.width, 60UL, 0x001E2936UL);
    (void)GdiFillSurfaceRect(&surface, 0UL, 60UL, surface.width, 1UL, 0x003F556DUL);

    if (g_imageboxdemo_font_ready) {
        (void)GdiDrawTextSurface(&surface, &g_imageboxdemo_font, 24UL, 14UL, "Image Box Demo", 0x00F5F7FAUL);
        (void)GdiDrawTextSurface(&surface, &g_imageboxdemo_font, 24UL, 36UL, "JPEG uses bliss.jpg cover-fit; PNG uses start_menu.png alpha blit.", 0x00C5D2E0UL);
    }

    imageboxdemo_layout_panels(surface.width, surface.height, &jpeg_panel, &png_panel);
    imageboxdemo_panel_image_rect(&jpeg_panel, &jpeg_image_rect);
    imageboxdemo_panel_image_rect(&png_panel, &png_image_rect);
    imageboxdemo_draw_panel(&surface, &jpeg_panel, "JPEG: C:\\wallpapers\\bliss.jpg");
    imageboxdemo_draw_panel(&surface, &png_panel, "PNG: C:\\icons\\start_menu.png");

    imageboxdemo_draw_placeholder(&surface, &jpeg_image_rect, "JPEG pending");
    imageboxdemo_draw_placeholder(&surface, &png_image_rect, "PNG pending");

    if (g_imageboxdemo_jpeg_bytes != 0 && g_imageboxdemo_jpeg_size != 0U) {
        JpegRenderTarget target;

        target.width = (U32)jpeg_image_rect.width;
        target.height = (U32)jpeg_image_rect.height;
        target.pitch = (U32)surface.pitch;
        target.pixel_format = (U32)surface.pixel_format;
        target.pixels = (void*)((U8*)surface.pixels + (jpeg_image_rect.y * surface.pitch) + (jpeg_image_rect.x * sizeof(uint32_t)));
        g_imageboxdemo_jpeg_draw_status = jpeg_render_to_surface(
            g_imageboxdemo_jpeg_bytes,
            g_imageboxdemo_jpeg_size,
            &target,
            0x00131A24UL);
        if (g_imageboxdemo_jpeg_draw_status < 0L) {
            imageboxdemo_draw_placeholder(&surface, &jpeg_image_rect, "JPEG decode failed");
        }
    }
    else {
        g_imageboxdemo_jpeg_draw_status = g_imageboxdemo_jpeg_load_status;
        imageboxdemo_draw_placeholder(&surface, &jpeg_image_rect, "JPEG file missing");
    }

    if (g_imageboxdemo_png_image.pixels != 0 && g_imageboxdemo_png_image.load_status >= 0L) {
        imageboxdemo_draw_placeholder(&surface, &png_image_rect, "");
        imageboxdemo_blit_png_image(&surface, &png_image_rect, &g_imageboxdemo_png_image);
    }
    else {
        imageboxdemo_draw_placeholder(&surface, &png_image_rect, "PNG decode failed");
    }

    if (g_imageboxdemo_font_ready) {
        crt_snprintf(
            status_line,
            sizeof(status_line),
            "jpeg load=%ld draw=%ld bytes=%lu | png load=%ld size=%lux%lu | surface=%lux%lu fmt=%lu",
            g_imageboxdemo_jpeg_load_status,
            g_imageboxdemo_jpeg_draw_status,
            (unsigned long)g_imageboxdemo_jpeg_size,
            g_imageboxdemo_png_image.load_status,
            g_imageboxdemo_png_image.width,
            g_imageboxdemo_png_image.height,
            surface.width,
            surface.height,
            surface.pixel_format);
        (void)GdiDrawTextSurface(&surface, &g_imageboxdemo_font, 24UL, surface.height > 26UL ? (surface.height - 26UL) : 0UL, status_line, 0x009FB3C8UL);
    }

    status = GdiInvalidateRect(hwnd, 0UL, 0UL, surface.width, surface.height);
    release_status = GdiReleaseWindowSurface(hwnd);
    if (release_status < 0L && status >= 0L) {
        status = release_status;
    }
    return status;
}

/*
 * Handle basic paint and close messages for the demo window.
 *
 * The app intentionally stays simple: paints happen on create, resize, and
 * explicit `WM_PAINT`, while async asset completions are claimed on the UI
 * thread before repaint or close decisions are made.
 *
 * @param hwnd Target window handle.
 * @param message Message identifier.
 * @param wParam First message payload word.
 * @param lParam Second message payload word.
 * @return Zero for handled messages.
 */
static LRESULT imageboxdemo_wndproc(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam) {
    (void)lParam;

    if (message == WM_CREATE || message == WM_PAINT || message == WM_SIZE) {
        (void)imageboxdemo_paint_window(hwnd);
        return 0L;
    }
    if (message == IMAGEBOXDEMO_WM_ASSETS_READY) {
        imageboxdemo_consume_async_asset(wParam);
        if (!g_imageboxdemo_background_load_running) {
            if (g_imageboxdemo_close_requested) {
                imageboxdemo_complete_close(hwnd);
            }
            else {
                (void)imageboxdemo_paint_window(hwnd);
            }
        }
        else if (!g_imageboxdemo_close_requested) {
            (void)imageboxdemo_paint_window(hwnd);
        }
        return 0L;
    }
    if (message == WM_CLOSE) {
        g_imageboxdemo_close_requested = 1;
        if (!g_imageboxdemo_background_load_running) {
            imageboxdemo_complete_close(hwnd);
        }
        return 0L;
    }

    return 0L;
}

/*
 * Create the demo window and run the standard GUI message loop.
 *
 * Startup loads the existing staged assets up front so any failures are visible
 * immediately when the window first paints.
 *
 * @return Process exit code.
 */
int main(void) {
    MSG message;
    WindowCreateParams params;

    if (CreateWindowClass(IMAGEBOXDEMO_WINDOW_CLASS, imageboxdemo_wndproc) < 0L) {
        imageboxdemo_log("imageboxdemo.exe: failed to register class");
        imageboxdemo_release_resources();
        return 1;
    }

    params.class_name = IMAGEBOXDEMO_WINDOW_CLASS;
    params.title = IMAGEBOXDEMO_WINDOW_TITLE;
    params.parent = 0UL;
    params.x = IMAGEBOXDEMO_WINDOW_X;
    params.y = IMAGEBOXDEMO_WINDOW_Y;
    params.width = IMAGEBOXDEMO_WINDOW_WIDTH;
    params.height = IMAGEBOXDEMO_WINDOW_HEIGHT;
    params.style = ROS_WINDOW_STYLE_VISIBLE | ROS_WINDOW_STYLE_DECORATED;

    g_imageboxdemo_window = CreateWindowEx(&params);
    if (g_imageboxdemo_window == 0UL) {
        imageboxdemo_log("imageboxdemo.exe: failed to create window");
        imageboxdemo_release_resources();
        return 1;
    }

    imageboxdemo_start_background_loader();
    imageboxdemo_log("imageboxdemo.exe: created hwnd=%lu", (unsigned long)g_imageboxdemo_window);
    while (GetMessage(&message) > 0L) {
        (void)TranslateMessage(&message);
        (void)DispatchMessage(&message);
    }

    while (g_imageboxdemo_background_load_running) {
        (void)sleepMs(1UL);
    }

    imageboxdemo_release_resources();
    imageboxdemo_log("imageboxdemo.exe: message loop exited");
    return 0;
}