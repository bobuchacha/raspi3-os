/*
 * widgets.c
 *
 * Build the reusable userspace widget framework as one dedicated DLL so apps
 * can share the same built-in class registration, paint helpers, and control
 * procedures without duplicating per-process state tables.
 */

#include "../../runtime/widget_support.c"