#ifndef ROS_BOOT_HANDOFF_H
#define ROS_BOOT_HANDOFF_H

#ifndef __ASSEMBLER__
#include "ros.h"
#endif

#define ROS_BOOT_HANDOFF_MAGIC 0x524F53424F4F5448UL
#define ROS_BOOT_HANDOFF_VERSION 1U
#define ROS_BOOT_HANDOFF_PHYS_ADDR 0x007FF000UL

#define ROS_BOOT_HANDOFF_FLAG_UART0_READY (1U << 0)
#define ROS_BOOT_HANDOFF_FLAG_SD_READY (1U << 1)

#define ROS_BOOT_HANDOFF_OFFSET_MAGIC 0U
#define ROS_BOOT_HANDOFF_OFFSET_VERSION 8U
#define ROS_BOOT_HANDOFF_OFFSET_SIZE 12U
#define ROS_BOOT_HANDOFF_OFFSET_FLAGS 16U
#define ROS_BOOT_HANDOFF_OFFSET_RESERVED0 20U
#define ROS_BOOT_HANDOFF_OFFSET_KERNEL_LOAD_ADDRESS 24U
#define ROS_BOOT_HANDOFF_OFFSET_KERNEL_IMAGE_BASE 32U
#define ROS_BOOT_HANDOFF_OFFSET_KERNEL_ENTRY_POINT 40U
#define ROS_BOOT_HANDOFF_OFFSET_SECONDARY_PROGRAM_BOOT_ENTRY_FN 48U
#define ROS_BOOT_HANDOFF_OFFSET_SECONDARY_PUBLISH_BOOT_ENTRIES_FN 56U
#define ROS_BOOT_HANDOFF_OFFSET_SECONDARY_ENTRY_POINT 64U

#ifndef __ASSEMBLER__
typedef struct RosBootHandoff {
    ULong magic;
    UInt version;
    UInt size;
    UInt flags;
    UInt reserved0;
    ULong kernel_load_address;
    ULong kernel_image_base;
    ULong kernel_entry_point;
    ULong secondary_program_boot_entry_fn;
    ULong secondary_publish_boot_entries_fn;
    ULong secondary_entry_point;
    ULong sd_scr[2];
    ULong sd_ocr;
    ULong sd_rca;
    ULong sd_err;
    ULong sd_hv;
} RosBootHandoff;

static inline Bool ros_boot_handoff_is_valid(const RosBootHandoff* handoff) {
    return handoff &&
        handoff->magic == ROS_BOOT_HANDOFF_MAGIC &&
        handoff->version == ROS_BOOT_HANDOFF_VERSION &&
        handoff->size >= sizeof(RosBootHandoff);
}
#endif

#endif
