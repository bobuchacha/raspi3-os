#include "app/dll_import.h"
#include "app/alpha-shared.h"
#include "app/bridge-shared.h"
#include "app/kernel.h"
#include "stdio.h"

#define SHARED_LIBRARY_ENTRY __attribute__((section(".entrypoint")))

SHARED_LIBRARY_ENTRY const BridgeSharedApi* _shared_library_entry(void);

DLL_IMPORT_VOID(ALPHA_SHARED_PATH, "alpha_announce", alpha_announce, (const char* caller), (caller))
DLL_IMPORT_DECL(unsigned long, alpha_entry_address, (void), (), ALPHA_SHARED_PATH, "alpha_entry_address")
DLL_IMPORT_DECL(long, alpha_add_alpha_bias, (long value), (value), ALPHA_SHARED_PATH, "alpha_add_alpha_bias")

unsigned long bridge_page_base(void) {
    return ((unsigned long)(void*)&_shared_library_entry) & ~0xFFFUL;
}

unsigned long bridge_entry_address(void) {
    return (unsigned long)(void*)&_shared_library_entry;
}

static void bridge_print_message(const char* caller, const char* phase) {
    char buffer[192];
    const char* tag = caller ? caller : "bridge";

    snprintf(buffer,
        sizeof(buffer),
        "[%s][bridge.dll] %s entry=0x%lX page-base=0x%lX\n",
        tag,
        phase,
        bridge_entry_address(),
        bridge_page_base());
    user_kernel_write(buffer);
}

void bridge_announce(const char* caller) {
    bridge_print_message(caller, "hello from bridge");
}

long bridge_call_alpha_from_bridge(const char* caller, long value) {
    char buffer[192];
    const char* tag = caller ? caller : "bridge";

    if (!alpha_entry_address()) {
        snprintf(buffer, sizeof(buffer), "[%s][bridge.dll] failed to open alpha.dll\n", tag);
        user_kernel_write(buffer);
        return -1;
    }

    alpha_announce("bridge.dll");
    snprintf(buffer,
        sizeof(buffer),
        "[%s][bridge.dll] alpha.add_alpha_bias(%ld) = %ld\n",
        tag,
        value,
        alpha_add_alpha_bias(value));
    user_kernel_write(buffer);
    return alpha_add_alpha_bias(value);
}

static const BridgeSharedApi bridge_api = {
    BRIDGE_SHARED_ABI_VERSION,
    bridge_announce,
    bridge_entry_address,
    bridge_page_base,
    bridge_call_alpha_from_bridge,
};

SHARED_LIBRARY_ENTRY const BridgeSharedApi* _shared_library_entry(void) {
    bridge_print_message("bridge.dll", "loaded");
    return &bridge_api;
}