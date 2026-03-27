#include "app/kernel.h"
#include "logger.h"
#include "stdio.h"

long main(void) {
    char buffer[192];

    app_log_info("extmem", "kernel module exports are available through user_kernel_extension_invoke");
    snprintf(
        buffer,
        sizeof(buffer),
        "extmem: run /bin/samplequery.exe to query the sample_sys kernel module export\n");
    user_kernel_write(buffer);
    return 0;
}
