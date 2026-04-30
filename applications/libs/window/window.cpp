/*
 * window.c
 *
 * Build the stateful userspace window client as a dedicated DLL object instead
 * of injecting it into every EXE. The implementation stays in the runtime tree
 * so the API logic remains in one place, while this wrapper gives the packer a
 * non-runtime module object from which it can collect export metadata.
 */

#define ROS_BUILDING_WINDOW_DLL 1

extern "C" {
#include "../../runtime/window_support.c"
}
