//
// Created by Jeff on 5/16/2024.
//

#ifndef RASPI3_OS_KPRINT_H
#define RASPI3_OS_KPRINT_H

void kprint(char *fmt, ...);
void kdebug(char *fmt, ...);
void kinfo(char *fmt, ...);
void kerror(char *fmt, ...);
void kpanic(char *fmt, ...);

#endif //RASPI3_OS_KPRINT_H
