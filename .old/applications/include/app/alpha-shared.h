#ifndef ROS_APP_ALPHA_SHARED_H
#define ROS_APP_ALPHA_SHARED_H

#define ALPHA_SHARED_ABI_VERSION 1UL
#define ALPHA_SHARED_PATH "/lib/alpha.dll"

typedef struct AlphaSharedApi {
    unsigned long abi_version;
    void (*announce)(const char* caller);
    unsigned long (*entry_address)(void);
    unsigned long (*page_base)(void);
    long (*add_alpha_bias)(long value);
} AlphaSharedApi;

typedef const AlphaSharedApi* (*AlphaSharedEntry)(void);


#endif