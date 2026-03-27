#include "app/alpha-shared.h"
#include "app/kernel.h"
#include "stdio.h"

#define SHARED_LIBRARY_ENTRY __attribute__((section(".entrypoint")))

SHARED_LIBRARY_ENTRY const AlphaSharedApi* _shared_library_entry(void);

unsigned long alpha_page_base(void) {
    return ((unsigned long)(void*)&_shared_library_entry) & ~0xFFFUL;
}

unsigned long alpha_entry_address(void) {
    return (unsigned long)(void*)&_shared_library_entry;
}

static void alpha_print_message(const char* caller, const char* phase) {
    char buffer[192];
    const char* tag = caller ? caller : "alpha";

    snprintf(buffer,
        sizeof(buffer),
        "[%s][alpha.dll] %s entry=0x%lX page-base=0x%lX\n",
        tag,
        phase,
        alpha_entry_address(),
        alpha_page_base());
    user_kernel_write(buffer);
}

void alpha_announce(const char* caller) {
    alpha_print_message(caller, "hello from alpha");
}

long alpha_add_alpha_bias(long value) {
    return value + 111;
}

static const AlphaSharedApi alpha_api = {
    ALPHA_SHARED_ABI_VERSION,
    alpha_announce,
    alpha_entry_address,
    alpha_page_base,
    alpha_add_alpha_bias,
};

SHARED_LIBRARY_ENTRY const AlphaSharedApi* _shared_library_entry(void) {
    alpha_print_message("alpha.dll", "loaded");
    return &alpha_api;
}