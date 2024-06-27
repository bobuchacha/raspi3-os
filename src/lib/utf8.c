#include <ros.h>
#include "memory.h"
#include "log.h"

// Converts UTF-16 string into UTF-8 string.
// If destination string is NULL returns total number of symbols that would've
// been written (without null terminator). However, when actually writing into
// destination string, it does include it. So, be sure to allocate extra byte
// for destination string.
// Params:
// u16_str      - source UTF-16 string
// u16_str_len  - length of source UTF-16 string
// u8_str       - destination UTF-8 string
// u8_str_size  - size of destination UTF-8 string in bytes
// Return value:
// 0 on success, -1 if encountered invalid surrogate pair, -2 if
// encountered buffer overflow or length of destination UTF-8 string in bytes
// (without including the null terminator).
long int utf16_to_utf8(const UWord *u16_str, int u16_str_len,
                       UByte *u8_str, int u8_str_size)
{
	int i = 0, j = 0;

	if (!u8_str) {
		u8_str_size = u16_str_len * 4;
	}

	while (i < u16_str_len) {
		UInt codepoint = u16_str[i++];

		// check for surrogate pair
		if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
			UWord high_surr = codepoint;
			UWord low_surr  = u16_str[i++];

			if (low_surr < 0xDC00 || low_surr > 0xDFFF)
				return -1;

			codepoint = ((high_surr - 0xD800) << 10) +
			            (low_surr - 0xDC00) + 0x10000;
		}

		if (codepoint < 0x80) {
			if (j + 1 > u8_str_size) return -2;

			if (u8_str) u8_str[j] = (char)codepoint;

			j++;
		} else if (codepoint < 0x800) {
			if (j + 2 > u8_str_size) return -2;

			if (u8_str) {
				u8_str[j + 0] = 0xC0 | (codepoint >> 6);
				u8_str[j + 1] = 0x80 | (codepoint & 0x3F);
			}

			j += 2;
		} else if (codepoint < 0x10000) {
			if (j + 3 > u8_str_size) return -2;

			if (u8_str) {
				u8_str[j + 0] = 0xE0 | (codepoint >> 12);
				u8_str[j + 1] = 0x80 | ((codepoint >> 6) & 0x3F);
				u8_str[j + 2] = 0x80 | (codepoint & 0x3F);
			}

			j += 3;
		} else {
			if (j + 4 > u8_str_size) return -2;

			if (u8_str) {
				u8_str[j + 0] = 0xF0 | (codepoint >> 18);
				u8_str[j + 1] = 0x80 | ((codepoint >> 12) & 0x3F);
				u8_str[j + 2] = 0x80 | ((codepoint >> 6) & 0x3F);
				u8_str[j + 3] = 0x80 | (codepoint & 0x3F);
			}

			j += 4;
		}
	}

	if (u8_str) {
		if (j >= u8_str_size) return -2;
		u8_str[j] = '\0';
	}

	return (long int)j;
}


/**
 * convert utf-16 to multi byte with pre alloc
*/
int str_from_utf16(UWord *u16_str, int length, UByte **target)
{
	int u8_size = -1, rc = -1;

	// printf("UTF-16 length: %d\n", length);

	u8_size = utf16_to_utf8(u16_str, length, NULL, 0);
	if (u8_size < 0) {
		// kprint("exit code (u8_size): %d\n", u8_size);
		return -1;
	}

    // printf("UTF-8 size:    %d\n", u8_size);

	*target = (UByte*)kmalloc(u8_size + 1); // include extra byte for null terminator
	rc = utf16_to_utf8(u16_str, length, *target, u8_size + 1);
	if (rc < 0) {
		kfree((Address)*target);
		// kprint("exit code (rc): %d\n", rc);
		return -2;
	}

	return 0;
}

void print_utf16(UWord *utf16, int length) {
	UByte *buf;
	if (!str_from_utf16(utf16, length, &buf)) {
		kprint(buf);
	}else {
		log_error("Can not print wide char string");
	}
	kfree((Address)buf);
}

void bytes_to_utf16(unsigned char *bytes, int length, UWord* utf16){
	for (; length > 0; length--){
		*utf16++ = (unsigned short)*bytes++;
	}
}

//Pointer arrays must always include the array size, because pointers do not know about the size of the supposed array size.
void utf8_to_utf16(UByte* const utf8_str, int utf8_str_size, UWord* utf16_str_output, int utf16_str_output_size) {
	//First, grab the first byte of the UTF-8 string
	unsigned char* utf8_currentCodeUnit = utf8_str;
	UWord* utf16_currentCodeUnit = utf16_str_output;
	int utf8_str_iterator = 0;
	int utf16_str_iterator = 0;

	//In a while loop, we check if the UTF-16 iterator is less than the max output size. If true, then we check if UTF-8 iterator
	//is less than UTF-8 max string size. This conditional checking based on order of precedence is intentionally done so it
	//prevents the while loop from continuing onwards if the iterators are outside of the intended sizes.
	while (*utf8_currentCodeUnit && (utf16_str_iterator < utf16_str_output_size || utf8_str_iterator < utf8_str_size)) {
		//Figure out the current code unit to determine the range. It is split into 6 main groups, each of which handles the data
		//differently from one another.
		if (*utf8_currentCodeUnit < 0x80) {
			//0..127, the ASCII range.

			//We directly plug in the values to the UTF-16 code unit.
			*utf16_currentCodeUnit = (UWord) (*utf8_currentCodeUnit);
			utf16_currentCodeUnit++;
			utf16_str_iterator++;

			//Increment the current code unit pointer to the next code unit
			utf8_currentCodeUnit++;

			//Increment the iterator to keep track of where we are in the UTF-8 string
			utf8_str_iterator++;
		}
		else if (*utf8_currentCodeUnit < 0xC0) {
			//0x80..0xBF, we ignore. These are reserved for UTF-8 encoding.
			utf8_currentCodeUnit++;
			utf8_str_iterator++;
		}
		else if (*utf8_currentCodeUnit < 0xE0) {
			//128..2047, the extended ASCII range, and into the Basic Multilingual Plane.

			//Work on the first code unit.
			UWord highShort = (UWord) ((*utf8_currentCodeUnit) & 0x1F);

			//Increment the current code unit pointer to the next code unit
			utf8_currentCodeUnit++;

			//Work on the second code unit.
			UWord lowShort = (UWord) ((*utf8_currentCodeUnit) & 0x3F);

			//Increment the current code unit pointer to the next code unit
			utf8_currentCodeUnit++;

			//Create the UTF-16 code unit, then increment the iterator.
			//Credits to @tbeu. 
			//Thanks to @k6l2 for explaining why we need 6 instead of 8.
			//It's because 0x3F is 6 bits of information from the low short. By shifting 8 bits, you are 
			//adding 2 extra zeroes in between the actual data of both shorts.
			int unicode = (highShort << 6) | lowShort;

			//Check to make sure the "unicode" is in the range [0..D7FF] and [E000..FFFF].
			if ((0 <= unicode && unicode <= 0xD7FF) || (0xE000 <= unicode && unicode <= 0xFFFF)) {
				//Directly set the value to the UTF-16 code unit.
				*utf16_currentCodeUnit = (UWord) unicode;
				utf16_currentCodeUnit++;
				utf16_str_iterator++;
			}

			//Increment the iterator to keep track of where we are in the UTF-8 string
			utf8_str_iterator += 2;
		}
		else if (*utf8_currentCodeUnit < 0xF0) {
			//2048..65535, the remaining Basic Multilingual Plane.

			//Work on the UTF-8 code units one by one.
			//If drawn out, it would be 1110aaaa 10bbbbcc 10ccdddd
			//Where a is 4th byte, b is 3rd byte, c is 2nd byte, and d is 1st byte.
			UWord fourthChar = (UWord) ((*utf8_currentCodeUnit) & 0xF);
			utf8_currentCodeUnit++;
			UWord thirdChar = (UWord) ((*utf8_currentCodeUnit) & 0x3C) >> 2;
			UWord secondCharHigh = (UWord) ((*utf8_currentCodeUnit) & 0x3);
			utf8_currentCodeUnit++;
			UWord secondCharLow = (UWord) ((*utf8_currentCodeUnit) & 0x30) >> 4;
			UWord firstChar = (UWord) ((*utf8_currentCodeUnit) & 0xF);
			utf8_currentCodeUnit++;

			//Create the resulting UTF-16 code unit, then increment the iterator.
			int unicode = (fourthChar << 12) | (thirdChar << 8) | (secondCharHigh << 6) | (secondCharLow << 4) | firstChar;

			//Check to make sure the "unicode" is in the range [0..D7FF] and [E000..FFFF].
			//According to math, UTF-8 encoded "unicode" should always fall within these two ranges.
			if ((0 <= unicode && unicode <= 0xD7FF) || (0xE000 <= unicode && unicode <= 0xFFFF)) {
				//Directly set the value to the UTF-16 code unit.
				*utf16_currentCodeUnit = (UWord) unicode;
				utf16_currentCodeUnit++;
				utf16_str_iterator++;
			}

			//Increment the iterator to keep track of where we are in the UTF-8 string
			utf8_str_iterator += 3;
		}
		else if (*utf8_currentCodeUnit < 0xF8) {
			//65536..10FFFF, the Unicode UTF range

			//Work on the UTF-8 code units one by one.
			//If drawn out, it would be 11110abb 10bbcccc 10ddddee 10eeffff
			//Where a is 6th byte, b is 5th byte, c is 4th byte, and so on.
			UWord sixthChar = (UWord) ((*utf8_currentCodeUnit) & 0x4) >> 2;
			UWord fifthCharHigh = (UWord) ((*utf8_currentCodeUnit) & 0x3);
			utf8_currentCodeUnit++;
			UWord fifthCharLow = (UWord) ((*utf8_currentCodeUnit) & 0x30) >> 4;
			UWord fourthChar = (UWord) ((*utf8_currentCodeUnit) & 0xF);
			utf8_currentCodeUnit++;
			UWord thirdChar = (UWord) ((*utf8_currentCodeUnit) & 0x3C) >> 2;
			UWord secondCharHigh = (UWord) ((*utf8_currentCodeUnit) & 0x3);
			utf8_currentCodeUnit++;
			UWord secondCharLow = (UWord) ((*utf8_currentCodeUnit) & 0x30) >> 4;
			UWord firstChar = (UWord) ((*utf8_currentCodeUnit) & 0xF);
			utf8_currentCodeUnit++;

			int unicode = (sixthChar << 4) | (fifthCharHigh << 2) | fifthCharLow | (fourthChar << 12) | (thirdChar << 8) | (secondCharHigh << 6) | (secondCharLow << 4) | firstChar;
			UWord highSurrogate = (unicode - 0x10000) / 0x400 + 0xD800;
			UWord lowSurrogate = (unicode - 0x10000) % 0x400 + 0xDC00;

			//Set the UTF-16 code units
			*utf16_currentCodeUnit = lowSurrogate;
			utf16_currentCodeUnit++;
			utf16_str_iterator++;

			//Check to see if we're still below the output string size before continuing, otherwise, we cut off here.
			if (utf16_str_iterator < utf16_str_output_size) {
				*utf16_currentCodeUnit = highSurrogate;
				utf16_currentCodeUnit++;
				utf16_str_iterator++;
			}

			//Increment the iterator to keep track of where we are in the UTF-8 string
			utf8_str_iterator += 4;
		}
		else {
			//Invalid UTF-8 code unit, we ignore.
			utf8_currentCodeUnit++;
			utf8_str_iterator++;
		}
	}

	//We clean up the output string if the UTF-16 iterator is still less than the output string size.
	while (utf16_str_iterator < utf16_str_output_size) {
		*utf16_currentCodeUnit = '\0';
		utf16_currentCodeUnit++;
		utf16_str_iterator++;
	}
}