#define ROS_GDI_EXPORTS 1
#define ROS_WINDOW_SERVER_ONLY 1
#include "app/gdi.h"

DLL_EXPORT(GdiGetWindowSurface);
DLL_EXPORT(GdiReleaseWindowSurface);
DLL_EXPORT(GdiInvalidateRect);
DLL_EXPORT(GdiFillSurfaceRect);
DLL_EXPORT(GdiFillRect);
DLL_EXPORT(GdiLoadFont);
DLL_EXPORT(GdiUnloadFont);
DLL_EXPORT(GdiMeasureText);
DLL_EXPORT(GdiDrawTextSurfaceEx);
DLL_EXPORT(GdiDrawTextSurface);
DLL_EXPORT(GdiDrawTextEx);
DLL_EXPORT(GdiDrawText);

#define GDI_WAIT_MSEC 100UL
#define GDI_WAIT_RETRIES 50UL
#define GDI_STATUS_INVALID_ARGUMENT (-1L)
#define GDI_STATUS_ERROR (-4L)
#define GDI_SURFACE_CACHE_CAPACITY ROS_KERNEL_GUI_WINDOW_SURFACE_MAX_SLOTS

typedef struct GdiSurfaceCacheEntry {
    int in_use;
    HWND hwnd;
    GuiWindowSurfaceView view;
} GdiSurfaceCacheEntry;

typedef struct GdiBackend {
    long (*fill_rect)(const RosGdiSurface* surface, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long color);
} GdiBackend;

static long gdi_gwes_pid = ROS_USER_IPC_STATUS_NOT_FOUND;
static GdiSurfaceCacheEntry gdi_surface_cache[GDI_SURFACE_CACHE_CAPACITY];

/*
 * Clear one caller-owned byte range without depending on hosted libc.
 *
 * @param destination Buffer to clear.
 * @param size Number of bytes to clear.
 * @return Nothing.
 */
static void gdi_zero_memory(void* destination, unsigned long size) {
    unsigned char* bytes = (unsigned char*)destination;
    unsigned long index;

    if (!bytes) {
        return;
    }

    for (index = 0UL; index < size; ++index) {
        bytes[index] = 0U;
    }
}

/*
 * Pack two 32-bit values into one 64-bit IPC scalar.
 *
 * The GWES invalidate path only needs coordinates and dimensions, so packing
 * them keeps the protocol compact while window pixels stay in the shared
 * surface mapping owned by the kernel GUI service.
 *
 * @param first Upper 32-bit value.
 * @param second Lower 32-bit value.
 * @return Packed scalar used by the invalidate request.
 */
static unsigned long gdi_pack_pair(unsigned long first, unsigned long second) {
    return ((first & 0xFFFFFFFFUL) << 32) | (second & 0xFFFFFFFFUL);
}

/*
 * Translate one surface color into the active shared-surface pixel format.
 *
 * @param pixel_format Target `ROS_KERNEL_GUI_PIXEL_FORMAT_*` value.
 * @param color Caller-supplied RGB color value.
 * @return Encoded 32-bit surface pixel.
 */
static unsigned long gdi_encode_color(unsigned long pixel_format, unsigned long color) {
    if (pixel_format != ROS_KERNEL_GUI_PIXEL_FORMAT_XBGR8888) {
        return color;
    }

    return ((color & 0x000000FFUL) << 16) |
        (color & 0x0000FF00UL) |
        ((color & 0x00FF0000UL) >> 16);
}

/*
 * Check whether one cached PID still resolves to a live process.
 *
 * @param pid Process identifier to validate.
 * @return Non-zero when the PID is still live.
 */
static int gdi_pid_is_live(long pid) {
    UserTaskInfo info;

    if (pid < 0) {
        return 0;
    }
    if (getTaskInfo(pid, &info) < 0) {
        return 0;
    }

    return info.main_thread_state != USER_TASK_STATE_TERMINATED;
}

/*
 * Resolve and cache the GWES PID.
 *
 * @return GWES PID on success, or `ROS_USER_IPC_STATUS_NOT_FOUND`.
 */
static long gdi_resolve_gwes_pid(void) {
    long published_pid;

    if (gdi_gwes_pid >= 0) {
        return gdi_gwes_pid;
    }

    published_pid = WindowServerPublishedPid();
    if (published_pid >= 0) {
        gdi_gwes_pid = published_pid;
        return gdi_gwes_pid;
    }

    gdi_gwes_pid = findUserTaskPidByName(ROS_WINDOW_SERVER_NAME);
    return gdi_gwes_pid;
}

/*
 * Wait for the boot supervisor to launch GWES.
 *
 * @return GWES PID on success, or `ROS_USER_IPC_STATUS_NOT_FOUND`.
 */
static long gdi_wait_for_gwes_pid(void) {
    unsigned long attempt;

    for (attempt = 0UL; attempt < GDI_WAIT_RETRIES; ++attempt) {
        long pid = gdi_resolve_gwes_pid();

        if (pid >= 0) {
            return pid;
        }

        (void)sleepMs(GDI_WAIT_MSEC);
    }

    return ROS_USER_IPC_STATUS_NOT_FOUND;
}

/*
 * Send one invalidate packet to GWES.
 *
 * @param packet Fully populated request packet.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_send_request(UserIpcMessage* packet) {
    long gwes_pid;

    if (!packet) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    gwes_pid = gdi_wait_for_gwes_pid();
    if (gwes_pid < 0) {
        return gwes_pid;
    }

    packet->receiver_pid = gwes_pid;
    return sendUserIpcMessage(gwes_pid, packet);
}

/*
 * Find one cached shared-surface mapping by window handle.
 *
 * @param hwnd Window handle associated with the mapping.
 * @return Matching cache entry, or NULL when no view is cached.
 */
static GdiSurfaceCacheEntry* gdi_find_surface_entry(HWND hwnd) {
    unsigned long index;

    for (index = 0UL; index < GDI_SURFACE_CACHE_CAPACITY; ++index) {
        if (gdi_surface_cache[index].in_use && gdi_surface_cache[index].hwnd == hwnd) {
            return &gdi_surface_cache[index];
        }
    }

    return NULL;
}

/*
 * Reserve one empty surface-cache entry.
 *
 * @return Empty slot, or NULL when the cache is full.
 */
static GdiSurfaceCacheEntry* gdi_reserve_surface_entry(void) {
    unsigned long index;

    for (index = 0UL; index < GDI_SURFACE_CACHE_CAPACITY; ++index) {
        if (!gdi_surface_cache[index].in_use) {
            return &gdi_surface_cache[index];
        }
    }

    return NULL;
}

/*
 * Build one in-out kernel GUI request for a window surface.
 *
 * @param view Request structure to initialize.
 * @param hwnd Stable GWES window handle.
 * @return Nothing.
 */
static void gdi_initialize_surface_view(GuiWindowSurfaceView* view, HWND hwnd) {
    if (!view) {
        return;
    }

    gdi_zero_memory(view, sizeof(*view));
    view->version = ROS_KERNEL_GUI_WINDOW_SURFACE_VIEW_VERSION;
    view->hwnd = (unsigned long)hwnd;
}

/*
 * Convert one kernel GUI view into the public GDI surface wrapper.
 *
 * @param entry Cached surface entry.
 * @param surface Receives the caller-visible surface fields.
 * @return Nothing.
 */
static void gdi_build_surface(const GdiSurfaceCacheEntry* entry, RosGdiSurface* surface) {
    if (!entry || !surface) {
        return;
    }

    surface->hwnd = entry->hwnd;
    surface->width = entry->view.width;
    surface->height = entry->view.height;
    surface->pitch = entry->view.pitch;
    surface->pixel_format = entry->view.pixel_format;
    surface->pixels = (void*)(unsigned long)entry->view.view_address;
}

/*
 * Acquire and cache one shared window surface from the kernel GUI service.
 *
 * @param hwnd Target window handle.
 * @param entry Receives the populated cache entry.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_acquire_surface(HWND hwnd, GdiSurfaceCacheEntry** entry) {
    GdiSurfaceCacheEntry* cache_entry;
    GuiWindowSurfaceView view;
    long status;

    if (!entry || hwnd == 0UL) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    cache_entry = gdi_find_surface_entry(hwnd);
    if (cache_entry) {
        *entry = cache_entry;
        return ROS_USER_IPC_STATUS_OK;
    }

    cache_entry = gdi_reserve_surface_entry();
    if (!cache_entry) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    gdi_initialize_surface_view(&view, hwnd);
    status = controlGui(ROS_KERNEL_GUI_CONTROL_WINDOW_SURFACE_ACQUIRE, (unsigned long)&view);
    if (status < 0) {
        return status;
    }
    if (view.view_address == 0ULL || view.width == 0U || view.height == 0U || view.pitch < (view.width * 4UL)) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    gdi_zero_memory(cache_entry, sizeof(*cache_entry));
    cache_entry->in_use = 1;
    cache_entry->hwnd = hwnd;
    cache_entry->view = view;
    *entry = cache_entry;
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Fill one rectangle through the software backend.
 *
 * @param surface Caller-visible surface wrapper.
 * @param x Rectangle X coordinate.
 * @param y Rectangle Y coordinate.
 * @param width Rectangle width.
 * @param height Rectangle height.
 * @param color Fill color in RGB order.
 * @return Zero on success, or a negative status code on failure.
 */
static long gdi_software_fill_rect(const RosGdiSurface* surface, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long color) {
    unsigned long clipped_width;
    unsigned long clipped_height;
    unsigned long row;
    unsigned long encoded_color;

    if (!surface || !surface->pixels || width == 0UL || height == 0UL) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }
    if (x >= surface->width || y >= surface->height || surface->pitch < (surface->width * 4UL)) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    clipped_width = width;
    clipped_height = height;
    if (clipped_width > (surface->width - x)) {
        clipped_width = surface->width - x;
    }
    if (clipped_height > (surface->height - y)) {
        clipped_height = surface->height - y;
    }
    if (clipped_width == 0UL || clipped_height == 0UL) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    encoded_color = gdi_encode_color(surface->pixel_format, color);
    for (row = 0UL; row < clipped_height; ++row) {
        unsigned int* destination = (unsigned int*)((unsigned char*)surface->pixels + ((y + row) * surface->pitch)) + x;
        unsigned long column;

        for (column = 0UL; column < clipped_width; ++column) {
            destination[column] = (unsigned int)encoded_color;
        }
    }

    return ROS_USER_IPC_STATUS_OK;
}

static const GdiBackend gdi_software_backend = {
    &gdi_software_fill_rect,
};

/*
 * Reset all process-local client state owned by the GDI helper DLL.
 *
 * @return Nothing.
 */
static void gdi_reset_state(void) {
    gdi_gwes_pid = ROS_USER_IPC_STATUS_NOT_FOUND;
    gdi_zero_memory(gdi_surface_cache, sizeof(gdi_surface_cache));
}

long GdiGetWindowSurface(HWND hwnd, RosGdiSurface* surface) {
    GdiSurfaceCacheEntry* entry;
    long status;

    if (hwnd == 0UL || !surface) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    status = gdi_acquire_surface(hwnd, &entry);
    if (status < 0) {
        return status;
    }

    gdi_build_surface(entry, surface);
    return ROS_USER_IPC_STATUS_OK;
}

long GdiReleaseWindowSurface(HWND hwnd) {
    GdiSurfaceCacheEntry* entry;
    GuiWindowSurfaceView view;
    long status;

    if (hwnd == 0UL) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    entry = gdi_find_surface_entry(hwnd);
    if (!entry) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    gdi_initialize_surface_view(&view, hwnd);
    status = controlGui(ROS_KERNEL_GUI_CONTROL_WINDOW_SURFACE_RELEASE, (unsigned long)&view);
    if (status >= 0) {
        gdi_zero_memory(entry, sizeof(*entry));
    }

    return status;
}

long GdiInvalidateRect(HWND hwnd, unsigned long x, unsigned long y, unsigned long width, unsigned long height) {
    UserIpcMessage request;

    if (hwnd == 0UL || width == 0UL || height == 0UL) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    gdi_zero_memory(&request, sizeof(request));
    request.protocol = ROS_WINDOW_SERVER_PROTOCOL;
    request.kind = ROS_WINDOW_SERVER_KIND_INVALIDATE_WINDOW;
    request.arg0 = (unsigned long)hwnd;
    request.arg1 = gdi_pack_pair(x, y);
    request.arg2 = gdi_pack_pair(width, height);
    return gdi_send_request(&request);
}

long GdiFillSurfaceRect(const RosGdiSurface* surface, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long color) {
    if (!surface) {
        return GDI_STATUS_INVALID_ARGUMENT;
    }

    return gdi_software_backend.fill_rect(surface, x, y, width, height, color);
}

long GdiFillRect(HWND hwnd, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long color) {
    RosGdiSurface surface;
    long status;

    status = GdiGetWindowSurface(hwnd, &surface);
    if (status < 0) {
        return status;
    }

    status = GdiFillSurfaceRect(&surface, x, y, width, height, color);
    if (status < 0) {
        return status;
    }

    return GdiInvalidateRect(hwnd, x, y, width, height);
}

/*
 * Build the font loader into the same DLL image so text rendering can reuse
 * the surface helpers and pixel-format conversion routines above.
 */
#include "gdi_font_support.c"

 /*
  * gdi_entry
  *
  * Keep the GDI helper state-local to each process image so the cached GWES PID
  * is discarded whenever the loader attaches or detaches the DLL.
  *
  * @param image_base Base address where the DLL is mapped in the current process.
  * @param reason Loader notification reason.
  * @return Non-zero success code for the loader.
  */
int gdi_entry(void* image_base, U32 reason) {
    (void)image_base;

    if ((reason == DLL_REASON_PROCESS_ATTACH) || (reason == DLL_REASON_PROCESS_DETACH)) {
        gdi_reset_state();
        return 1;
    }

    return 1;
}