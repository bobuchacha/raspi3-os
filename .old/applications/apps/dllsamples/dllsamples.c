#include "user_runtime.h"

#define DLLSAMPLES_DLL_ONE   "/lib/hello_one.dll"
#define DLLSAMPLES_DLL_TWO   "/lib/hello_two.dll"
#define DLLSAMPLES_DLL_THREE "/lib/hello_three.dll"

typedef void (*dllsamples_print_fn)(unsigned long base);

DLL_CACHE(g_dllsamples_print_one_cache);
DLL_CACHE(g_dllsamples_print_two_cache);
DLL_CACHE(g_dllsamples_print_three_cache);

/*
 * dllsamples_write_status
 *
 * Write one status line that ties the requested DLL path to the loader base
 * returned by `openSharedLibrary`.
 */
static void dllsamples_write_status(const char* label, const char* path, unsigned long base) {
    char line[192];
    char* cursor = line;

    cursor = appendText(cursor, "dllsamples.exe: ");
    cursor = appendText(cursor, label);
    cursor = appendText(cursor, " ");
    cursor = appendText(cursor, path);
    if (base != 0) {
        cursor = appendText(cursor, " base=0x");
        cursor = appendHex(cursor, base);
    }
    *cursor = '\0';
    writeLine(line);
}

/*
 * dllsamples_load_one
 *
 * Load one DLL image and report the mapped base. The DLL's own `Init`
 * callback also prints its greeting, so the console shows both the caller and
 * callee side of the load.
 */
static int dllsamples_load_one(const char* path, unsigned long base, const char* export_name, unsigned long* cached_export) {
    dllsamples_print_fn print_fn;

    if (base == 0) {
        dllsamples_write_status("failed to load", path, 0);
        return -1;
    }

    print_fn = (dllsamples_print_fn)(unsigned long)resolveSharedLibraryCached(path, export_name, cached_export);
    if (!print_fn) {
        dllsamples_write_status("missing export for", path, base);
        return -1;
    }

    print_fn(base);
    dllsamples_write_status("loaded", path, base);
    return 0;
}

/*
 * dllsamples_unload_one
 *
 * Drop one explicit DLL reference so the loader can call `Deinit` and reclaim
 * the mapping when the last owner releases it.
 */
static int dllsamples_unload_one(const char* path, unsigned long base) {
    if (closeSharedLibrary(path, base) != 0) {
        dllsamples_write_status("failed to unload", path, 0);
        return -1;
    }

    dllsamples_write_status("unloaded", path, 0);
    return 0;
}

int main(void) {
    unsigned long base_one;
    unsigned long base_two;
    unsigned long base_three;

    writeLine("dllsamples.exe: loading hello DLL set");

    base_one = openSharedLibrary(DLLSAMPLES_DLL_ONE);
    base_two = openSharedLibrary(DLLSAMPLES_DLL_TWO);
    base_three = openSharedLibrary(DLLSAMPLES_DLL_THREE);
    if (base_one == 0 || base_two == 0 || base_three == 0) {
        writeLine("dllsamples.exe: failed to map one or more DLLs");
        exitProcess(1);
    }

    if (dllsamples_load_one(DLLSAMPLES_DLL_ONE, base_one, "hello_one_print", &g_dllsamples_print_one_cache) != 0 ||
        dllsamples_load_one(DLLSAMPLES_DLL_TWO, base_two, "hello_two_print", &g_dllsamples_print_two_cache) != 0 ||
        dllsamples_load_one(DLLSAMPLES_DLL_THREE, base_three, "hello_three_print", &g_dllsamples_print_three_cache) != 0) {
        exitProcess(1);
    }

    writeLine("dllsamples.exe: all DLLs loaded");
    if (dllsamples_unload_one(DLLSAMPLES_DLL_THREE, base_three) != 0 ||
        dllsamples_unload_one(DLLSAMPLES_DLL_TWO, base_two) != 0 ||
        dllsamples_unload_one(DLLSAMPLES_DLL_ONE, base_one) != 0) {
        exitProcess(2);
    }

    writeLine("dllsamples.exe: all DLLs unloaded");
    return 0xDEADBEEF;
}
