/*
 * widgets.c
 *
 * Build the reusable userspace widget framework as one dedicated DLL so apps
 * can share the same built-in class registration, paint helpers, and control
 * procedures without duplicating per-process state tables.
 */

#define ROS_BUILDING_WIDGETS_DLL 1

extern "C" {
#include "../../runtime/widget_support.c"
}
