/*
 * console.cpp
 *
 * Bind the freestanding `console.h` helpers to the ROS userspace console
 * syscalls so every app can use the same formatter without linking hosted
 * stdio or manually redefining the serial macros in each translation unit.
 */

#include "user_runtime.h"

#include <stddef.h>

#define ROS_CONSOLE_WRITE_CHUNK 240U

 /*
  * Copy one bounded text slice into a temporary syscall buffer and always leave
  * room for the trailing terminator expected by `USER_SYS_WRITE`.
  *
  * @param destination Caller-owned temporary buffer.
  * @param capacity Size of the temporary buffer in bytes.
  * @param source Source text slice.
  * @param length Requested slice length.
  * @return Number of bytes copied into `destination`.
  */
static size_t ros_console_copy_chunk(char* destination, size_t capacity, const char* source, size_t length) {
    size_t index = 0U;

    if (destination == 0 || capacity == 0U || source == 0) {
        return 0U;
    }

    while (index < length && (index + 1U) < capacity) {
        destination[index] = source[index];
        ++index;
    }

    destination[index] = '\0';
    return index;
}

/*
 * Write one formatted text slice to the kernel-backed serial console.
 *
 * The kernel `USER_SYS_WRITE` contract accepts one null-terminated string and
 * reports success as a status code rather than a byte count. This wrapper
 * therefore chunks long writes into temporary terminated slices and returns the
 * number of source bytes that were accepted.
 *
 * @param text Source text slice.
 * @param length Requested byte count.
 * @return Number of bytes written successfully.
 */
extern "C" size_t ros_console_platform_write(const char* text, size_t length) {
    char buffer[ROS_CONSOLE_WRITE_CHUNK + 1U];
    size_t total_written = 0U;

    if (text == 0 || length == 0U) {
        return 0U;
    }

    while (total_written < length) {
        size_t remaining = length - total_written;
        size_t chunk_length = remaining;

        if (chunk_length > ROS_CONSOLE_WRITE_CHUNK) {
            chunk_length = ROS_CONSOLE_WRITE_CHUNK;
        }

        chunk_length = ros_console_copy_chunk(buffer, sizeof(buffer), text + total_written, chunk_length);
        if (chunk_length == 0U) {
            break;
        }
        if (writeText(buffer) < 0L) {
            break;
        }

        total_written += chunk_length;
    }

    return total_written;
}

/*
 * Read one byte from the kernel-backed console input stream.
 *
 * @return Unsigned byte value on success, or -1 on failure.
 */
extern "C" int ros_console_platform_getc(void) {
    long value = readConsole();

    if (value < 0L) {
        return -1;
    }

    return (int)(unsigned char)value;
}

/*
 * Write one byte to the kernel-backed console output stream.
 *
 * @param ch Byte value to write.
 * @return Nothing.
 */
extern "C" void ros_console_platform_putc(int ch) {
    char buffer[2];

    buffer[0] = (char)ch;
    buffer[1] = '\0';
    (void)writeText(buffer);
}
