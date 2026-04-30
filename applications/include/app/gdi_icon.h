#ifndef ROS_APP_GDI_ICON_H
#define ROS_APP_GDI_ICON_H

#include "app/gdi_handle.h"

typedef HGDIHANDLE HICON;

typedef struct RosGdiIconHandle {
    RosGdiHandleHeader header;
    unsigned long width;
    unsigned long height;
    unsigned long hotspot_x;
    unsigned long hotspot_y;
    unsigned long pixel_format;
    void* pixels;
} RosGdiIconHandle;

#endif