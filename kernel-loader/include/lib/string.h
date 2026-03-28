#ifndef ROS_LOADER_STRING_H
#define ROS_LOADER_STRING_H

void* memset(void* s, int c, int n);
void* memmove(void* dst, const void* src, unsigned int n);
int memcmp(const void* lhs, const void* rhs, unsigned int n);
int strlen(const char* s);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, int length);
char* strncpy(char* dest, const char* src, int length);
int k_toupper(int c);

#endif
