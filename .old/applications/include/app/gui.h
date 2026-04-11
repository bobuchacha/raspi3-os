#ifndef ROS_APP_GUI_H
#define ROS_APP_GUI_H

#include "import.h"
#include "kernel_gui.h"

#define ROS_GUI_PATH "/lib/gui.dll"

IMPORT_DLL_DECL(long, guiInit, (void), (), ROS_GUI_PATH, "gui_init")
IMPORT_DLL_DECL(long, guiShowTaskbar, (unsigned long visible), (visible), ROS_GUI_PATH, "gui_taskbar_show")
IMPORT_DLL_DECL(long, guiShowStartMenu, (unsigned long visible), (visible), ROS_GUI_PATH, "gui_start_menu_show")
IMPORT_DLL_DECL(long, guiShowSoftKeyboard, (unsigned long visible), (visible), ROS_GUI_PATH, "gui_soft_keyboard_show")
IMPORT_DLL_DECL(long, guiSetTaskbarClockDate, (const char* clock_text, const char* date_text), (clock_text, date_text), ROS_GUI_PATH, "gui_taskbar_set_clock_date")
IMPORT_DLL_DECL(long, guiTaskbarTick, (void), (), ROS_GUI_PATH, "gui_taskbar_tick")
IMPORT_DLL_DECL(long, guiQueryDisplayInfo, (RosKernelGuiDisplayInfo* info), (info), ROS_GUI_PATH, "gui_display_info_query")
IMPORT_DLL_DECL(long, guiPresentPixels, (const RosKernelGuiPresentBuffer* buffer), (buffer), ROS_GUI_PATH, "gui_pixels_present")
IMPORT_DLL_DECL(long, guiPollInputEvent, (RosKernelGuiInputEvent* event), (event), ROS_GUI_PATH, "gui_input_event_poll")
IMPORT_DLL_DECL(long, guiQueryPointer, (RosKernelGuiPointerState* state), (state), ROS_GUI_PATH, "gui_pointer_query")
IMPORT_DLL_DECL(long, guiPresentWidgetFrame, (const RosKernelGuiWidgetFrame* frame), (frame), ROS_GUI_PATH, "gui_widget_frame_present")
IMPORT_DLL_DECL(long, guiPollWidgetEvent, (RosKernelGuiWidgetEvent* event), (event), ROS_GUI_PATH, "gui_widget_event_poll")
IMPORT_DLL_DECL(long, guiSetWidgetFocus, (unsigned long control_id), (control_id), ROS_GUI_PATH, "gui_widget_focus_set")
IMPORT_DLL_DECL(long, guiSetWidgetPointerCapture, (unsigned long control_id), (control_id), ROS_GUI_PATH, "gui_widget_pointer_capture")
IMPORT_DLL_DECL(long, guiInvalidateWidgets, (unsigned long reserved), (reserved), ROS_GUI_PATH, "gui_widget_invalidate")

#endif