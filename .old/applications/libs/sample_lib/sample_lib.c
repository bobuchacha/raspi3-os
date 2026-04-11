#include "user_runtime.h"

DLL_EXPORT(Init);
DLL_EXPORT(Deinit);
DLL_EXPORT(sample_lib_calculate_total);
DLL_EXPORT(sample_lib_invocation_count);
DLL_EXPORT(sample_lib_profile);

static unsigned long g_invoice_count;
static const char g_profile_name[] = "regional-pricing-v1";

int Init(void* base) {
    (void)base;
    g_invoice_count = 0;
    writeLine("sample_lib: pricing profile ready");
    return 0;
}

int Deinit(void* base) {
    (void)base;
    writeLine("sample_lib: shutting down");
    return 0;
}

long sample_lib_calculate_total(long subtotal, long tax, long shipping) {
    g_invoice_count++;
    return subtotal + tax + shipping + (long)(g_invoice_count * 3);
}

unsigned long sample_lib_invocation_count(void) {
    return g_invoice_count;
}

const char* sample_lib_profile(void) {
    return g_profile_name;
}
