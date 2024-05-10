#include "util.h"
/**
 * read short at a buffer and return 32 bit integer
 * @param buff
 * @param offset
 * @return
 */
unsigned int readi16(unsigned char *buff, unsigned int offset) {
    unsigned char *ubuff = buff + offset;
    return ubuff[1] << 8 | ubuff[0];
}

unsigned int readi32(unsigned char *buff, unsigned int offset) {
    unsigned char *ubuff = buff + offset;
    return
            ((ubuff[3] << 24) & 0xFF000000) |
            ((ubuff[2] << 16) & 0x00FF0000) |
            ((ubuff[1] << 8) & 0x0000FF00) |
            (ubuff[0] & 0x000000FF);
}
