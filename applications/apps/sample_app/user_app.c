/*
 * user_app.c
 * Example EXE-style artifact exports and lifecycle callbacks.
 */

#include <stdint.h>

/*
 * Init
 *
 * Entry callback called by loader with module base as first arg.
 *
 * Args:
 *   base - module runtime base address.
 *
 * Returns:
 *   0 for success.
 */
int
Init (void *base)
{
    (void)base;
    return 0;
}

/*
 * Deinit
 *
 * Optional teardown callback called before unload.
 *
 * Args:
 *   base - module runtime base address.
 *
 * Returns:
 *   0 for success.
 */
int
Deinit (void *base)
{
    (void)base;
    return 0;
}

int
AppMain (int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 0;
}
