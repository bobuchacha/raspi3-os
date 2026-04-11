#include "ros.h"
#include "device/raspi3b/mailbox.h"
#include "device/raspi3b/peripherals/base.h"

#define VIDEOCORE_MBOX (PBASE + 0x0000B880)
#define MBOX_READ ((UInt*)(VIDEOCORE_MBOX + 0x0))
#define MBOX_STATUS ((UInt*)(VIDEOCORE_MBOX + 0x18))
#define MBOX_WRITE ((UInt*)(VIDEOCORE_MBOX + 0x20))

#define MBOX_RESPONSE 0x80000000
#define MBOX_FULL 0x80000000
#define MBOX_EMPTY 0x40000000

volatile unsigned int __attribute__((aligned(16))) mailbox_buffer[36];

UInt mailbox_read(Int channel) {
    MailStatus status;
    MailMessage response;

    do {
        do {
            status.status = *MBOX_STATUS;
        } while (status.is_empty);

        response.message = *MBOX_READ;
    } while (response.channel != channel);

    return response.message;
}

void mailbox_send(UInt msg, Int channel) {
    MailStatus status;

    msg |= (channel & 0xF);
    do {
        status.status = *MBOX_STATUS;
    } while (status.is_full);
    *MBOX_WRITE = msg;
}

int mailbox_call(UInt* data, unsigned char channel) {
    MailMessage ret;

    mailbox_send(((ULong)data) & 0xFFFFFFF0UL, channel);
    ret.message = mailbox_read(channel);
    return ret.data;
}

int mbox_call(unsigned char ch) {
    unsigned int r = (((unsigned int)((unsigned long)mailbox_buffer) & ~0xFU) | (ch & 0xF));

    do {
        asm volatile("nop");
    } while (*(volatile unsigned int*)(VIDEOCORE_MBOX + 0x18) & MBOX_FULL);
    *(UInt*)(VIDEOCORE_MBOX + 0x20) = r;
    while (1) {
        do {
            asm volatile("nop");
        } while (*(volatile unsigned int*)(VIDEOCORE_MBOX + 0x18) & MBOX_EMPTY);
        if (r == *(UInt*)(VIDEOCORE_MBOX + 0x0)) {
            return mailbox_buffer[1] == MBOX_RESPONSE;
        }
    }
}
