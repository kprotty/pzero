#include "pz_time.h"

#ifdef _WIN32
    pz_nonnull(1) static void pz_time_get(pz_time* timestamp) {
        pz_assert(QueryUnbiasedInterruptTime(timestamp));
    }

    pz_nonnull(1) static void pz_time_after(pz_time* timestamp, uint64_t nanos) {
        if (pz_unlikely(!pz_checked_add(*timestamp, nanos / 100, timestamp))) {
            *timestamp = ~((ULONGLONG)0);
        }
    }

    pz_nonnull(1, 2) static uint64_t pz_time_since(pz_time* restrict timestamp, pz_time* restrict earlier) {
        ULONGLONG diff;
        if (pz_likely(pz_checked_sub(*timestamp, *earlier, &diff))) return (uint64_t)(diff / 100);
        return 0;
    }

#else
    pz_nonnull(1) static void pz_time_get(pz_time* timestamp) {
        #if defined(__APPLE__)
            pz_assert(clock_gettime(CLOCK_UPTIME_RAW_APPROX, timestamp) == 0);
        #elif defined(__FreeBSD__) || defined(__DragonFly__)
            pz_assert(clock_gettime(CLOCK_UPTIME_FAST, timestamp) == 0);
        #elif defined(__OpenBSD__)
            pz_assert(clock_gettime(CLOCK_UPTIME, timestamp) == 0);
        #elif defined(__linux__)
            pz_assert(clock_gettime(CLOCK_MONOTONIC_COARSE, timestamp) == 0);
        #else
            pz_assert(clock_gettime(CLOCK_MONOTONIC, timestamp) == 0);
        #endif
    }

    #define NS_PER_S 1000000000

    pz_nonnull(1) static void pz_time_after(pz_time* timestamp, uint64_t nanos) {
        uint64_t sec = nanos / NS_PER_S;
        uint32_t nsec = nanos % NS_PER_S;

        if (pz_likely(sec == ((uint64_t)((time_t)sec)))) {
            if (pz_likely(pz_checked_add(timestamp->tv_sec, (time_t)sec, &timestamp->tv_sec))) {
                timestamp->tv_nsec += (__typeof__(timestamp->tv_nsec))nsec;

                __typeof__(timestamp->tv_nsec) diff;
                if (!pz_checked_sub(timestamp->tv_nsec, NS_PER_S, &diff)) {
                    return;
                }

                timestamp->tv_nsec = diff;
                if (pz_likely(pz_checked_add(timestamp->tv_nsec, 1, &timestamp->tv_nsec))) {
                    return;
                }
            }
        }
        
        memset((void*)timestamp->tv_sec, 0xff, sizeof(time_t));
        timestamp->tv_nsec = NS_PER_S - 1;
    }

    pz_nonnull(1, 2) static uint64_t pz_time_since(pz_time* restrict timestamp, pz_time* restrict earlier) {
        time_t sec;
        uint64_t nsec;

        if (pz_likely(pz_checked_sub(timestamp->tv_sec, earlier->tv_sec, &sec))) {
            if (pz_likely(pz_checked_mul((uint64_t)sec, NS_PER_S, &nsec))) {
                if (pz_likely(pz_checked_add(nsec, (uint32_t)timestamp->tv_sec, &nsec))) {
                    if (pz_likely(pz_checked_sub(nsec, (uint32_t)timestamp->tv_nsec, &nsec))) {
                        return nsec;
                    }
                }
            }
        }

        return 0;
    }
#endif
