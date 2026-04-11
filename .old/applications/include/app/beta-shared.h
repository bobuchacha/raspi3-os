#ifndef ROS_APP_BETA_SHARED_H
#define ROS_APP_BETA_SHARED_H

#define BETA_SHARED_ABI_VERSION 1UL
#define BETA_SHARED_PATH "/lib/beta.dll"

typedef struct BetaSharedApi {
    unsigned long abi_version;
    void (*announce)(const char* caller);
    unsigned long (*entry_address)(void);
    unsigned long (*page_base)(void);
    long (*multiply_beta_bias)(long value);
} BetaSharedApi;

typedef const BetaSharedApi* (*BetaSharedEntry)(void);

#endif