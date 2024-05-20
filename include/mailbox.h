//
// Created by Thang Cao on 5/18/24.
//

#ifndef RASPI3_OS_MAILBOX_H
#define RASPI3_OS_MAILBOX_H

/* channels */
#define MBOX_CH_POWER   0
#define MBOX_CH_FB      1
#define MBOX_CH_VUART   2
#define MBOX_CH_VCHIQ   3
#define MBOX_CH_LEDS    4
#define MBOX_CH_BTNS    5
#define MBOX_CH_TOUCH   6
#define MBOX_CH_COUNT   7
#define MBOX_CH_PROP    8

/* tags */
#define MBOX_TAG_SETPOWER       0x28001
#define MBOX_TAG_GETSERIAL      0x10004
#define MBOX_TAG_SETCLKRATE     0x38002
#define MBOX_TAG_LAST           0

#define MBOX_REQUEST    0

typedef struct mail_message_t{
    unsigned char channel: 4;
    unsigned int data: 28;
} MailMessage;

typedef struct {
    unsigned int reserved: 30;
    unsigned char is_empty: 1;
    unsigned char is_full:1;
} MailStatus;

MailMessage mailbox_read(int channel);
void mailbox_send(MailMessage msg, int channel);


extern volatile unsigned int mailbox_buffer[36];
int mailbox_call(unsigned long data[], unsigned char channel);
int mbox_call(unsigned char ch);
#endif //RASPI3_OS_MAILBOX_H
