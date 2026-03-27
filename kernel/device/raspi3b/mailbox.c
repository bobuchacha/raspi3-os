//
// Created by Thang Cao on 5/18/24.
//

#include <ros.h>
#include "device/raspi3b/mailbox.h"
#include "device/raspi3b/peripherals/base.h"
#include "printf.h"

#define VIDEOCORE_MBOX  (PBASE+0x0000B880)
#define MBOX_READ       ((UInt*)(VIDEOCORE_MBOX+0x0))
#define MBOX_POLL       ((volatile unsigned int*)(VIDEOCORE_MBOX+0x10))
#define MBOX_SENDER     ((volatile unsigned int*)(VIDEOCORE_MBOX+0x14))
#define MBOX_STATUS     ((UInt*)(VIDEOCORE_MBOX+0x18))
#define MBOX_CONFIG     ((volatile unsigned int*)(VIDEOCORE_MBOX+0x1C))
#define MBOX_WRITE      ((UInt*)(VIDEOCORE_MBOX+0x20))

#define MAIL0_ADDR (PBASE + 0xB880)
#define MAIL1_ADDR (PBASE + 0xB8A0) // which is MBOX_WRITE

#define MBOX_RESPONSE   0x80000000
#define MBOX_FULL       0x80000000
#define MBOX_EMPTY      0x40000000


/* a properly aligned buffer */
volatile unsigned int __attribute((aligned(16))) mailbox_buffer[36];

UInt mailbox_read(Int channel) {
    MailStatus status;
    MailMessage response;

    // Make sure that the message is from the right channel
    do {
        // Make sure there is mail to recieve
        do {
            status.status = *MBOX_STATUS;
        } while (status.is_empty);

        // Get the message
        response.message = *MBOX_READ;
    } while (response.channel != channel);

    return response.message;
}

void mailbox_send(UInt msg, Int channel) {
    MailStatus status;
    msg |= (channel & 0xF);     // add channel to low 4 bit

    // Make sure you can send mail
    do {
        status.status = *MBOX_STATUS;
    } while (status.is_full);

    // send the message
    *MBOX_WRITE = msg;
}

/**
 * Make a mailbox call. Returns 0 on failure, non-zero on success
 */
int mailbox_call(UInt* data, unsigned char channel)
{

    // send the request
    mailbox_send(((ULong)data) & (0xFFFFFFF0), channel);

    // retrieve response
    MailMessage ret;
    ret.message = mailbox_read(channel);
    return ret.data;

}

#define MBOX_STATUS2 ((int*)(VIDEOCORE_MBOX+0x18))
#define MBOX_WRITE2      ((int*)(VIDEOCORE_MBOX+0x20))

int mbox_call(unsigned char ch)
{
    unsigned int r = (((unsigned int)((unsigned long)mailbox_buffer)&~0xF) | (ch&0xF));
    /* wait until we can write to the mailbox */
    do{asm volatile("nop");}while(*MBOX_STATUS2 & MBOX_FULL);
    /* write the address of our message to the mailbox with channel identifier */
    *MBOX_WRITE2 = r;
    /* now wait for the response */
    while(1) {
        /* is there a response? */
        do{asm volatile("nop");}while(*MBOX_STATUS2 & MBOX_EMPTY);
        /* is it a response to our message? */
        if(r == *(int*)(VIDEOCORE_MBOX + 0x0))
            /* is it a valid successful response? */
            return mailbox_buffer[1]==MBOX_RESPONSE;
    }
    return 0;
}