#ifndef ROS_LOADER_MAILBOX_H
#define ROS_LOADER_MAILBOX_H

#include "ros.h"

#define MBOX_CH_PROP 8
#define MBOX_TAG_SETCLKRATE 0x38002
#define MBOX_TAG_LAST 0
#define MBOX_REQUEST 0

typedef union {
    struct {
        unsigned char channel : 4;
        unsigned int data : 28;
    };
    int message;
} MailMessage;

typedef union {
    struct {
        unsigned int reserved : 30;
        unsigned char is_empty : 1;
        unsigned char is_full : 1;
    };
    int status;
} MailStatus;

extern volatile unsigned int mailbox_buffer[36];

UInt mailbox_read(Int channel);
void mailbox_send(UInt msg, int channel);
int mailbox_call(UInt* data, unsigned char channel);
int mbox_call(unsigned char ch);

#endif
