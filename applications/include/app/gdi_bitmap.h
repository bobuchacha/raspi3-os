#ifndef ROS_APP_GDI_BITMAP_H
#define ROS_APP_GDI_BITMAP_H

#include "app/gdi_handle.h"

typedef HGDIHANDLE HBITMAP;

typedef struct RosGdiBitmapHandle {
    RosGdiHandleHeader header;
    unsigned long width;
    unsigned long height;
    unsigned long pitch;
    unsigned long pixel_format;
    void* pixels;
} RosGdiBitmapHandle;

#endif