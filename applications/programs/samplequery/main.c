#include "app/sample_sys.h"
#include "logger.h"

long main(void) {
    long abi_version = sample_sys_query_status(SAMPLE_SYS_QUERY_ABI_VERSION);
    long next_heartbeat_ms = sample_sys_query_status(SAMPLE_SYS_QUERY_NEXT_HEARTBEAT_MS);
    long tick_ms = sample_sys_query_status(SAMPLE_SYS_QUERY_TICKS_MS);

    if (abi_version < 0 || next_heartbeat_ms < 0 || tick_ms < 0) {
        app_log_error("samplequery", "sample_sys query failed abi=%ld next=%ld ticks=%ld", abi_version, next_heartbeat_ms, tick_ms);
        return 1;
    }

    app_log_info("samplequery", "sample_sys abi=%ld", abi_version);
    app_log_info("samplequery", "sample_sys next heartbeat at %ld ms", next_heartbeat_ms);
    app_log_info("samplequery", "sample_sys current tick=%ld ms", tick_ms);


    int result = sample_sys_do_add(40, 2);
    app_log_info("samplequery", "sample_sys do_add(40, 2) = %d", result);
    if (result != 42) {
        app_log_error("samplequery", "sample_sys do_add returned unexpected result: %d", result);
        return 1;
    }

    // Optionally, query the loaded address of the sample_sys module.
    long loaded_address = sample_sys_get_loaded_address();
    app_log_info("samplequery", "sample_sys loaded address: 0x%lx", loaded_address);

    kprint("Hello from samplequery application! The answer to 40 + 2 is %d.\n", result);
    return 0xDEADBEEFUL;
}