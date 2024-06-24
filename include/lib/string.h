#ifndef STRING_H
#define STRING_H

/**
 * Fills the first @a n bytes of the memory area pointed to by @a s
 * 	with the constant byte @a c.
 * @return A pointer to the memory area s.
 */
void *memset(void *s, int c, int n);

/**
 * Copies @a n bytes from memory area @a src to memory area @a dest.
 * 	The memory areas must not overlap.
 * @return A pointer to dest.
 */
// void *memcpy(void *dest, const void *src, int n);

/**
 * Calculates the length of the string pointed to by @a s,
 * 	excluding the terminating null byte ('\0').
 * @param s A string pointer.
 * @return The number of bytes in the string pointed to by @a s.
 */
int strlen(const char *s);

/**
 * Compares the two strings @a s1 and @a s2.
 * @param s1 A string pointer.
 * @param s2 A string pointer.
 * @return An integer less than, equal to, or greater than 0 if @a s1 is found,
 * 	respectively, to be less than, to match, or be greater than @a s2.
 */
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, int length);

/**
 * Copies the string pointed to by @a src, including the terminating null
 * 	byte ('\0'), to the buffer pointed to by @a dest.
 * @param dest A string pointer.
 * @param src A string pointer.
 * @return A pointer to the destination string @a dest.
 */
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, int length);

/**
 * Appends the @a src string to the @a dest string, overwriting the
 * 	terminating null byte ('\0') at the end of @a dest,
 * 	and then adds a terminating null byte.
 * @param dest A string pointer.
 * @param src A string pointer.
 * @return A pointer to the resulting string @a dest.
 */
char *strcat(char *dest, const char *src);

/**
 * Reverses a given string in place.
 * @param s A string pointer.
 * @see strlen()
 */
void strrev(char *s);

/** @} */
/** @} */

void bzero(void *b, int length);


int k_toupper(int c);

int k_tolower(int c);

int coerce_int(char *s, unsigned int *val);

unsigned char hex_char(unsigned char byte);

char * itos(unsigned int myint, char buffer[], int bufflen);

char * strtok(char *s, char *delim);
char *strncpy(char *dest, const char *src, int length);
volatile void* memmove_volatile(volatile void* dst, const volatile void* src, unsigned int n) ;
void* memmove(void* dst, const void* src, unsigned int n);
char* safestrcpy(char* s, const char* t, int n);
//-------------------------------------- utf8.c -----------------------------------
long int utf16_to_utf8(const UWord *u16_str, int u16_str_len, UByte *u8_str, int u8_str_size); // convert utf16 to utf8
int str_from_utf16(UWord *u16_str, int length, UByte **target);	// convert to utf8 string from utf16 with pre allocated memory
void print_utf16(UWord *utf16, int length);
void bytes_to_utf16(unsigned char *bytes, int length, UWord* utf16);
void utf8_to_utf16(UByte* const utf8_str, int utf8_str_size, UWord* utf16_str_output, int utf16_str_output_size);
#endif