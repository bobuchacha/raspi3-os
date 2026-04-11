#ifndef KERNEL_INCLUDE_GUI_SERVICE_H
#define KERNEL_INCLUDE_GUI_SERVICE_H

#include "types.h"
#include "app/kernel_gui.h"

#if !defined(__cplusplus)
#error "gui_service.h requires C++"
#endif

struct Process;

/*
 * Kernel-side GUI service for framebuffer presentation and shared window
 * surfaces.
 *
 * The display path gives GWES the minimal present contract it needs today,
 * while the shared-surface path maps one window backing store into both GWES
 * and the owning client process so GDI can render locally without routing draw
 * primitives back through the window server.
 */
class GuiService final {
public:
    static Status init(void);
    static Status query_display_info(RosKernelGuiDisplayInfo* info);
    static Status present_buffer(const RosKernelGuiPresentBuffer* buffer);
    static Status acquire_shared_input(Process* process, RosKernelGuiSharedInputView* view);
    static Status release_shared_input(Process* process);
    static Status query_shared_input(Process* process, RosKernelGuiSharedInputView* view);
    static Status create_window_surface(Process* process, GuiWindowSurfaceView* view);
    static Status destroy_window_surface(Process* process, const GuiWindowSurfaceView* view);
    static Status acquire_window_surface(Process* process, GuiWindowSurfaceView* view);
    static Status release_window_surface(Process* process, const GuiWindowSurfaceView* view);
    static Status release_process_surfaces(Process* process);
    static void publish_input_event(uint32_t type, uint32_t x, uint32_t y, uint32_t key, uint32_t buttons);
};

#endif // KERNEL_INCLUDE_GUI_SERVICE_H