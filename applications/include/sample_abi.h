#ifndef APPLICATIONS_SAMPLE_ABI_H
#define APPLICATIONS_SAMPLE_ABI_H

#define SAMPLE_INIT_PATH "/bin/init.exe"
#define SAMPLE_LIB_PATH "/lib/sample_lib.dll"
#define SAMPLE_DRIVER_PATH "/system/sample_driver.sys"

enum {
    SAMPLE_DRIVER_OP_STATUS = 1,
    SAMPLE_DRIVER_OP_PULSE = 2,
};

typedef long (*sample_lib_calculate_total_fn)(long subtotal, long tax, long shipping);
typedef unsigned long (*sample_lib_invocation_count_fn)(void);
typedef const char* (*sample_lib_profile_fn)(void);

typedef unsigned long (*sample_driver_dispatch_fn)(unsigned long op, unsigned long value);
typedef unsigned long (*sample_driver_last_value_fn)(void);

#endif