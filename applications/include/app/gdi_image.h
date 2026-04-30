#ifndef ROS_APP_GDI_IMAGE_H
#define ROS_APP_GDI_IMAGE_H

#include "app/gdi_handle.h"

typedef HGDIHANDLE HIMAGE;

typedef struct RosGdiImageHandle {
    RosGdiHandleHeader header;
    unsigned long width;
    unsigned long height;
    unsigned long pitch;
    unsigned long pixel_format;
    void* pixels;
} RosGdiImageHandle;

#endif