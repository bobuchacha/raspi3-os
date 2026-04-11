#ifndef KERNEL_INCLUDE_KERNEL_TIME_H
#define KERNEL_INCLUDE_KERNEL_TIME_H

#include "types.h"

#if !defined(__cplusplus)
#error "kernel_time.h requires C++"
#endif

namespace KernelTime {

    /*
     * One scheduler tick now targets 0.1 ms so message waits and GUI wakeups
     * can resolve substantially faster than the previous 10 ms quantum.
     */
    inline constexpr U64 TickMicroseconds = 100ULL;
    inline constexpr U64 MicrosecondsPerMillisecond = 1000ULL;

    /*
     * Convert scheduler ticks into microseconds.
     *
     * @param tick_count Number of elapsed scheduler ticks.
     * @return Elapsed time in microseconds.
     */
    inline constexpr U64 ticks_to_microseconds(U64 tick_count) {
        return tick_count * TickMicroseconds;
    }

    /*
     * Convert scheduler ticks into whole milliseconds.
     *
     * Public uptime APIs are still millisecond-based, so sub-millisecond tick
     * precision is rounded down at this ABI boundary.
     *
     * @param tick_count Number of elapsed scheduler ticks.
     * @return Whole elapsed milliseconds.
     */
    inline constexpr U64 ticks_to_milliseconds(U64 tick_count) {
        return ticks_to_microseconds(tick_count) / MicrosecondsPerMillisecond;
    }

    /*
     * Convert one millisecond duration into scheduler ticks, rounding up.
     *
     * Sleeps and timed waits must not wake earlier than requested, so the
     * conversion deliberately preserves the requested minimum delay.
     *
     * @param milliseconds Requested wall-clock delay in milliseconds.
     * @return Number of scheduler ticks needed to cover that delay.
     */
    inline constexpr U64 milliseconds_to_ticks_ceil(U64 milliseconds) {
        if (milliseconds == 0ULL) {
            return 0ULL;
        }

        return ((milliseconds * MicrosecondsPerMillisecond) + (TickMicroseconds - 1ULL)) / TickMicroseconds;
    }

} // namespace KernelTime

#endif // KERNEL_INCLUDE_KERNEL_TIME_H