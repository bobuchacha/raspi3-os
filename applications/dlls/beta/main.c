#include "app/beta-shared.h"
#include "app/kernel.h"
#include "stdio.h"

#define SHARED_LIBRARY_ENTRY __attribute__((section(".entrypoint")))

SHARED_LIBRARY_ENTRY const BetaSharedApi* _shared_library_entry(void);

unsigned long beta_page_base(void) {
    return ((unsigned long)(void*)&_shared_library_entry) & ~0xFFFUL;
}

unsigned long beta_entry_address(void) {
    return (unsigned long)(void*)&_shared_library_entry;
}

static void beta_print_message(const char* caller, const char* phase) {
    char buffer[192];
    const char* tag = caller ? caller : "beta";

    snprintf(buffer,
        sizeof(buffer),
        "[%s][beta.dll] %s entry=0x%lX page-base=0x%lX\n",
        tag,
        phase,
        beta_entry_address(),
        beta_page_base());
    user_kernel_write(buffer);
}

void beta_announce(const char* caller) {
    beta_print_message(caller, "hello from beta");
}

long beta_multiply_beta_bias(long value) {
    return value * 7;
}

static const BetaSharedApi beta_api = {
    BETA_SHARED_ABI_VERSION,
    beta_announce,
    beta_entry_address,
    beta_page_base,
    beta_multiply_beta_bias,
};

SHARED_LIBRARY_ENTRY const BetaSharedApi* _shared_library_entry(void) {
    beta_print_message("beta.dll", "loaded");
    return &beta_api;
}