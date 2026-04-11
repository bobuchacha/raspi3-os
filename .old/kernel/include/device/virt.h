#ifndef ROS_KERNEL_DEVICE_VIRT_H
#define ROS_KERNEL_DEVICE_VIRT_H

#include "memory.h"

#define DEVICE_MEMORY_SIZE 0x10000000UL
#define VIRT_MMIO_BASE 0x09000000UL
#define VIRT_VIRTIO_MMIO_BASE 0x0A000000UL
#define VIRT_VIRTIO_MMIO_STRIDE 0x200UL
#define VIRT_GIC_DIST_BASE (0x08000000UL + VA_START)

void uart0_init(void);
void uart0_send(unsigned int c);
char uart0_getc(void);
int uart0_try_getc(char* out_char);
void uart0_puts(char* s);
void uart0_putc(void* p, char c);
void uart0_lhex(unsigned long d);
int virtio_blk_init(void);
int virtio_blk_read(void* private, unsigned int begin, int count, void* buf);
int virtio_blk_write(void* private, unsigned int begin, int count, const void* buf);
Bool virtio_blk_is_ready(void);

#endif