#ifndef ROS_APP_GDI_HANDLE_H
#define ROS_APP_GDI_HANDLE_H

#include "types.h"

typedef unsigned long HGDIHANDLE;

typedef enum RosGdiHandleKind {
    ROS_GDI_HANDLE_KIND_INVALID = 0UL,
    ROS_GDI_HANDLE_KIND_BITMAP = 1UL,
    ROS_GDI_HANDLE_KIND_ICON = 2UL,
    ROS_GDI_HANDLE_KIND_IMAGE = 3UL,
    ROS_GDI_HANDLE_KIND_FONT = 4UL,
    ROS_GDI_HANDLE_KIND_SURFACE = 5UL,
} RosGdiHandleKind;

typedef struct RosGdiHandleHeader {
    HGDIHANDLE handle;
    RosGdiHandleKind kind;
    unsigned long ref_count;
} RosGdiHandleHeader;

typedef struct RosGdiHandleRecord {
    RosGdiHandleHeader header;
    void* payload;
} RosGdiHandleRecord;

#endif