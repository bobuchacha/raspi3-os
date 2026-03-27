#include "app/dll_import.h"
#include "app/alpha-shared.h"
#include "app/beta-shared.h"
#include "app/bridge-shared.h"
#include "app/kernel.h"
#include "logger.h"
#include "stdio.h"

#define ALPHA_SHARED_PATH "/lib/alpha.dll"
#define BETA_SHARED_PATH "/lib/beta.dll"
#define BRIDGE_SHARED_PATH "/lib/bridge.dll"

DLL_IMPORT_DECL_VOID(alpha_announce, (const char* caller), (caller), ALPHA_SHARED_PATH, "alpha_announce")
DLL_IMPORT_DECL(unsigned long, alpha_entry_address, (void), (), ALPHA_SHARED_PATH, "alpha_entry_address")
DLL_IMPORT_DECL(unsigned long, alpha_page_base, (void), (), ALPHA_SHARED_PATH, "alpha_page_base")
DLL_IMPORT_DECL(long, alpha_add_alpha_bias, (long value), (value), ALPHA_SHARED_PATH, "alpha_add_alpha_bias")

DLL_IMPORT_DECL_VOID(beta_announce, (const char* caller), (caller), BETA_SHARED_PATH, "beta_announce")
DLL_IMPORT_DECL(unsigned long, beta_entry_address, (void), (), BETA_SHARED_PATH, "beta_entry_address")
DLL_IMPORT_DECL(unsigned long, beta_page_base, (void), (), BETA_SHARED_PATH, "beta_page_base")
DLL_IMPORT_DECL(long, beta_multiply_beta_bias, (long value), (value), BETA_SHARED_PATH, "beta_multiply_beta_bias")

DLL_IMPORT_DECL_VOID(bridge_announce, (const char* caller), (caller), BRIDGE_SHARED_PATH, "bridge_announce")
DLL_IMPORT_DECL(unsigned long, bridge_entry_address, (void), (), BRIDGE_SHARED_PATH, "bridge_entry_address")
DLL_IMPORT_DECL(unsigned long, bridge_page_base, (void), (), BRIDGE_SHARED_PATH, "bridge_page_base")
DLL_IMPORT_DECL(long, bridge_call_alpha_from_bridge, (const char* caller, long value), (caller, value), BRIDGE_SHARED_PATH, "bridge_call_alpha_from_bridge")

long main(void) {
    char buffer[256];

    if (!alpha_entry_address() || !beta_entry_address() || !bridge_entry_address()) {
        app_log_error("dllsamples", "failed to resolve direct DLL exports");
        return 1;
    }

    alpha_announce("dllsamples");
    beta_announce("dllsamples");
    bridge_announce("dllsamples");

    snprintf(buffer,
        sizeof(buffer),
        "[dllsamples] alpha entry=0x%lX page-base=0x%lX bias(9)=%ld\n",
        alpha_entry_address(),
        alpha_page_base(),
        alpha_add_alpha_bias(9));
    user_kernel_write(buffer);

    snprintf(buffer,
        sizeof(buffer),
        "[dllsamples] beta entry=0x%lX page-base=0x%lX scale(6)=%ld\n",
        beta_entry_address(),
        beta_page_base(),
        beta_multiply_beta_bias(6));
    user_kernel_write(buffer);

    snprintf(buffer,
        sizeof(buffer),
        "[dllsamples] bridge entry=0x%lX page-base=0x%lX\n",
        bridge_entry_address(),
        bridge_page_base());
    user_kernel_write(buffer);

    snprintf(buffer,
        sizeof(buffer),
        "[dllsamples] bridge -> alpha result=%ld\n",
        bridge_call_alpha_from_bridge("dllsamples", 77));
    user_kernel_write(buffer);

    while (1) {
        user_delay(5000);
    }

    return 0;
}