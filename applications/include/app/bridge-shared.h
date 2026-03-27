#ifndef ROS_APP_BRIDGE_SHARED_H
#define ROS_APP_BRIDGE_SHARED_H

#define BRIDGE_SHARED_ABI_VERSION 1UL
#define BRIDGE_SHARED_PATH "/lib/bridge.dll"

typedef struct BridgeSharedApi {
    unsigned long abi_version;
    void (*announce)(const char* caller);
    unsigned long (*entry_address)(void);
    unsigned long (*page_base)(void);
    long (*call_alpha_from_bridge)(const char* caller, long value);
} BridgeSharedApi;

typedef const BridgeSharedApi* (*BridgeSharedEntry)(void);

#endif