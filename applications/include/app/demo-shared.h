#ifndef ROS_APP_DEMO_SHARED_H
#define ROS_APP_DEMO_SHARED_H

#define DEMO_SHARED_ABI_VERSION 2UL
#define DEMO_SHARED_PATH "/lib/demo.dll"

typedef struct DemoSharedApi {
    unsigned long abi_version;
    long (*add)(long lhs, long rhs);
    void (*print_shared_counter)(const char* program_name);
    void (*print_local_counter)(const char* program_name);
} DemoSharedApi;

typedef const DemoSharedApi* (*DemoSharedEntry)(void);

#endif