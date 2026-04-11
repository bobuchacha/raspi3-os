/*
 * gdi.c
 *
 * Build the stateful userspace drawing helper as a dedicated DLL wrapper
 * instead of injecting it into every EXE. The implementation stays in the
 * runtime tree so the IPC and process-lookup logic remains in one place, while
 * this wrapper gives the packer a non-runtime module object from which it can
 * collect export metadata.
 */

#include "../../runtime/gdi_support.c"