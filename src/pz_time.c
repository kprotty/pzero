#include "pz_time.h"

#ifdef _WIN32
    PZ_NONNULL(1)
    static void pz_time_get(pz_time* timestamp) {
        PZ_ASSERT(QueryUnbiasedInterruptTime(timestamp));
    }

    PZ_NONNULL(1)
    static void pz_time_after(pz_time* timestamp, uint64_t nanos) {
        if (PZ_UNLIKELY(!PZ_OVERFLOW_ADD(*timestamp, nanos / 100ULL, timestamp))) {
            *timestamp = ~((ULONGLONG)0);
        }
    }

    PZ_NONNULL(1, 2)
    static uint64_t pz_time_since(const pz_time* restrict now, const pz_time* restrict earlier) {
        pz_time diff;
        if (PZ_UNLIKELY(!PZ_OVERFLOW_SUB(*now, *earlier, &diff))) return 0;
        if (PZ_UNLIKELY(!PZ_OVERFLOW_MUL(diff, 100ULL, &diff))) return ~((uint64_t)0);
        return diff;
    }

#else
    PZ_NONNULL(1)
    static void pz_time_get(pz_time* timestamp) {
        #if defined(__linux__)
            const clockid_t clock_id = CLOCK_BOOTTIME;
        #elif defined(__APPLE__)
            const clockid_t clock_id = CLOCK_UPTIME_RAW;
        #elif defined(__FreeBSD__) || defined(__DragonFly__)
            const clockid_t clock_id = CLOCK_MONOTONIC_FAST;
        #else
            const clockid_t clock_id = CLOCK_MONOTONIC;
        #endif
        PZ_ASSERT(clock_gettime(clock_id, timestamp) == 0);
    }

    _Static_assert(((time_t)1.5) == ((time_t)1)); // make sure time_t an integer.
    _Static_assert(((time_t)0) > ((time_t)-1)); // make sure time_t is signed.

    PZ_NONNULL(1)
    static void pz_time_after(pz_time* timestamp, uint64_t nanos) {
        uint64_t secs = nanos / 1'000'000'000ULL;
        uint32_t nsecs = (uint32_t)(nanos % 1'000'000'000ULL);

        if (PZ_LIKELY(((uint64_t)((time_t)secs)) == secs)) {
            if (PZ_LIKELY(PZ_OVERFLOW_ADD(timestamp->tv_secs, (time_t)secs, &timestamp->tv_secs))) {
                timestamp->tv_nsecs += (__typeof__(timestamp->tv_nsecs))(nsecs);
                if (timestamp->tv_nsecs >= 1'000'000'000) {
                    timestamp->tv_nsecs -= 1'000'000'000;
                    if (PZ_UNLIKELY(!PZ_OVERFLOW_ADD(timestamp->tv_secs, 1, &timestamp->tv_nsecs))) {
                        goto overflowed;
                    }
                }
                return;
            }
        }
        
    overflowed:
        timestamp->tv_secs = (time_t)(1ULL << ((sizeof(time_t) * CHAR_BITS) - 1));
        timestamp->tv_nsecs = 999'999'999;
    }

    PZ_NONNULL(1, 2)
    static uint64_t pz_time_since(const pz_time* restrict now, const pz_time* restrict earlier) {
        time_t secs;
        if (PZ_UNLIKELY(!PZ_OVERFLOW_SUB(now->tv_secs, earlier->tv_secs, &secs))) {
            return 0;
        }

        __typeof__(now->tv_nsec) nsecs;
        if (!PZ_OVERFLOW_SUB(now->tv_nsecs, earlier->tv_nsecs, &nsecs)) {
            nsecs = (now->tv_nsecs + 1'000'000'000) - earlier->tv_nsecs;
        }

        uint64_t nanos;
        if (PZ_LIKELY(PZ_OVERFLOW_MUL((uint64_t)secs, 1'000'000'000, &nanos))) {
            if (PZ_LIKELY(PZ_OVERFLOW_ADD(nanos, (uint64_t)nsecs, &nanos))) {
                return nanos;
            }
        }

        return ~((uint64_t)0);
    }
#endif
