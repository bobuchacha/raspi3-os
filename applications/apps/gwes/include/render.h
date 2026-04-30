#ifndef ROS_APP_GWES_RENDER_H
#define ROS_APP_GWES_RENDER_H

typedef struct RosGdiSurface RosGdiSurface;

#ifdef __cplusplus
extern "C" {
#endif

    /*
     * Initialize the GWES retained compositor.
     *
     * @return Zero on success, or a negative status code when display setup fails.
     */
    long gwes_render_init(void);

    /*
     * Release compositor resources and any allocated surfaces.
     *
     * @return Nothing.
     */
    void gwes_render_shutdown(void);

    /*
     * Create one retained window record and attach a backing surface.
     *
     * @param hwnd Stable window identifier assigned by GWES.
     * @param owner_pid Owning client process.
    * @param parent_hwnd Optional parent handle for child controls.
    * @param x Requested X position in desktop or parent-client coordinates.
    * @param y Requested Y position in desktop or parent-client coordinates.
    * @param width Requested outer window width.
    * @param height Requested outer window height.
    * @param style Window style flags supplied by the client.
    * @param class_name Window class name used for diagnostics and styling.
    * @param title Window title used for the default chrome.
     * @return Zero on success, or a negative status code on failure.
     */
    long gwes_render_create_window(unsigned long hwnd, long owner_pid, unsigned long parent_hwnd, long x, long y, unsigned long width, unsigned long height, unsigned long style, const char* class_name, const char* title);

    /*
     * Remove one retained window record and mark its screen region dirty.
     *
     * @param hwnd Stable window identifier assigned by GWES.
     * @return Nothing.
     */
    void gwes_render_destroy_window(unsigned long hwnd);

    /*
     * Move one retained window and queue the old and new bounds for repaint.
     *
     * @param hwnd Stable window identifier assigned by GWES.
     * @param x New screen X coordinate.
     * @param y New screen Y coordinate.
     * @return Zero on success, or a negative status code on failure.
     */
    long gwes_render_move_window(unsigned long hwnd, long x, long y);

    /*
     * Resize one retained window and recreate its shared client surface.
     *
     * This keeps the compositor's frame geometry and the client-visible GDI
     * backing store in sync so `WM_SIZE` handlers can immediately repaint into
     * a surface with the new dimensions.
     *
     * @param hwnd Stable window identifier assigned by GWES.
     * @param x New outer-frame X coordinate.
     * @param y New outer-frame Y coordinate.
     * @param width New outer-frame width.
     * @param height New outer-frame height.
     * @return Zero on success, or a negative status code on failure.
     */
    long gwes_render_resize_window(unsigned long hwnd, long x, long y, unsigned long width, unsigned long height);

    /*
     * Query the retained desktop size backing the current compositor.
     *
     * @param width Receives the desktop width.
     * @param height Receives the desktop height.
     * @return Zero on success, or a negative status code when the renderer is not ready.
     */
    long gwes_render_query_desktop_size(unsigned long* width, unsigned long* height);

    /*
     * Expose one retained GWES-owned client surface for direct system-UI painting.
     *
     * Popup menus are rendered by GWES itself, so they must draw into the
     * compositor's long-lived surface mapping rather than round-tripping through
     * the kernel acquire/release controls that exist for external clients.
     * Reusing the retained mapping avoids unmapping the same popup surface out
     * from under the compositor after each repaint.
     *
     * @param hwnd Stable window identifier assigned by GWES.
     * @param surface Receives the retained surface description.
     * @return Zero on success, or a negative status code when the handle is unknown.
     */
    long gwes_render_get_window_surface(unsigned long hwnd, RosGdiSurface* surface);

    /*
     * Show or update one Win9x-style move/resize placeholder rectangle.
     *
     * GWES uses this during interactive drag and resize so the live client
     * surface can remain untouched until the user releases the pointer. That
     * avoids repeated `WM_MOVE` / `WM_SIZE` traffic and the shared-surface
     * recreation cost on every pointer sample.
     *
     * @param x Preview outer-frame X coordinate.
     * @param y Preview outer-frame Y coordinate.
     * @param width Preview outer-frame width.
     * @param height Preview outer-frame height.
     * @return Nothing.
     */
    void gwes_render_set_interaction_placeholder(long x, long y, unsigned long width, unsigned long height);

    /*
     * Hide the current interactive move/resize placeholder rectangle.
     *
     * @return Nothing.
     */
    void gwes_render_clear_interaction_placeholder(void);

    /*
     * Raise one top-level window subtree to the front of the compositor stack.
     *
     * @param hwnd Top-level window handle to move to the foreground.
     * @return Zero on success, or a negative status code when the handle is unknown.
     */
    long gwes_render_raise_window(unsigned long hwnd);

    /*
     * Reapply the stable topmost grouping after shell or app z-order changes.
     *
     * @return Nothing.
     */
    void gwes_render_refresh_window_groups(void);

    /*
     * Hit-test the retained client-surface stack using desktop coordinates.
     *
     * @param x Desktop X coordinate.
     * @param y Desktop Y coordinate.
     * @param hwnd Receives the target window handle.
     * @param local_x Receives the client-relative X coordinate.
     * @param local_y Receives the client-relative Y coordinate.
     * @return Zero on success, or a negative status code when nothing was hit.
     */
    long gwes_render_hit_test(unsigned long x, unsigned long y, unsigned long* hwnd, long* local_x, long* local_y);

    /*
     * Translate one desktop pointer position into one window's client space.
     *
     * @param hwnd Target window handle.
     * @param x Desktop X coordinate.
     * @param y Desktop Y coordinate.
     * @param local_x Receives the client-relative X coordinate.
     * @param local_y Receives the client-relative Y coordinate.
     * @return Zero on success, or a negative status code when the handle is unknown.
     */
    long gwes_render_translate_pointer(unsigned long hwnd, unsigned long x, unsigned long y, long* local_x, long* local_y);

    /*
     * Update the retained software cursor position, visibility, and active asset.
     *
     * GWES resolves the effective cursor path from per-window metadata and then
     * hands the renderer one consolidated pointer state update. That keeps all
     * cursor damage bookkeeping inside the compositor instead of scattering it
     * across input dispatch and window-lifecycle code.
     *
     * @param x Desktop pointer X coordinate.
     * @param y Desktop pointer Y coordinate.
     * @param visible Non-zero when the cursor should be visible.
     * @param cursor_path DOS-style `.cur32` asset path, or null for the default.
     * @return Nothing.
     */
    void gwes_render_update_pointer(unsigned long x, unsigned long y, int visible, const char* cursor_path);

    /*
     * Mark one client window region dirty in screen coordinates.
     *
     * @param hwnd Stable window identifier assigned by GWES.
     * @param x Region X coordinate relative to the window surface.
     * @param y Region Y coordinate relative to the window surface.
     * @param width Dirty region width.
     * @param height Dirty region height.
     * @return Nothing.
     */
    void gwes_render_mark_window_dirty(unsigned long hwnd, unsigned long x, unsigned long y, unsigned long width, unsigned long height);

    /*
     * Queue a full desktop repaint on the next present pass.
     *
     * @return Nothing.
     */
    void gwes_render_request_full_redraw(void);

    /*
     * Report whether the compositor still has queued damage to present.
     *
     * The paint scheduler uses this to keep the window thread from sleeping
     * between damage production and composition when no explicit wake object is
     * available yet.
     *
     * @return Non-zero when at least one damage rectangle is queued.
     */
    int gwes_render_has_pending_damage(void);

    /*
     * Composite any pending damage and submit it to the display backend.
     *
     * @return Nothing.
     */
    void gwes_render_present_if_needed(void);

#ifdef __cplusplus
}
#endif

#endif