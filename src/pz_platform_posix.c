#include "pz_time.h"

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

#include "pz_sync.h"

PZ_NONNULL(1)
static bool pz_thread_spawn_detached(pz_thread_func entry_point, void* param, size_t stack_size) {
    bool spawned;
    pthread_t handle;
    pthread_attr_t attr;

    spawned = pthread_attr_init(&attr);
    if (PZ_UNLIKELY(!spanwed)) goto finished;

    spawned = pthread_attr_setstacksize(&attr, stack_size);
    if (PZ_UNLIKELY(!spawned)) goto cleanup_attr;

    spawned = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (PZ_UNLIKELY(!spawned)) goto cleanup_attr;

    spawned = pthread_create(&handle, &attr, entry_point, param);
    if (PZ_UNLIKELY(!spawned)) goto cleanup_attr;

cleanup_attr:
    PZ_ASSERT(pthread_attr_destroy(&attr));
finished:
    return spawned;
}

PZ_NONNULL(1)
static void pz_mutex_init(pz_mutex* mutex) {
    PZ_ASSERT(pthread_mutex_init(mutex, NULL));
}

PZ_NONNULL(1)
static void pz_mutex_deinit(pz_mutex* mutex) {
    PZ_ASSERT(pthread_mutex_destroy(mutex));
}

PZ_NONNULL(1)
static void pz_mutex_lock(pz_mutex* mutex) {
    PZ_ASSERT(pthread_mutex_lock(mutex));
}

PZ_NONNULL(1)
static void pz_mutex_unlock(pz_mutex* mutex) {
    PZ_ASSERT(pthread_mutex_unlock(mutex));
}

PZ_NONNULL(1)
static void pz_cond_init(pz_cond* cond) {
    PZ_ASSERT(pthread_cond_init(cond, NULL));
}

PZ_NONNULL(1)
static void pz_cond_deinit(pz_cond* cond) {
    PZ_ASSERT(pthread_cond_destroy(cond));
}

PZ_NONNULL(1)
static void pz_cond_notify(pz_cond* cond, bool notify_all) {
    PZ_ASSERT(notify_all ? pthread_cond_broadcast(cond) : pthread_cond_signal(cond));
}

PZ_NONNULL(1, 2)
static void pz_cond_wait(pz_cond* restrict cond, pz_mutex* restrict mutex, const pz_time* restrict deadline) {
    if (PZ_LIKELY(deadline == NULL)) {
        PZ_ASSERT(pthread_cond_wait(cond, mutex));
        return;
    }

    pz_time ts;
    pz_time_get(&ts);

    uint64_t timeout = pz_time_since(deadline, &ts);
    if (PZ_UNLIKELY(timeout > 0)) {
        PZ_ASSERT(clock_gettime(CLOCK_REALTIME, &ts));
        pz_time_after(&ts, timeout);

        int rc = pthread_cond_timedwait(cond, mutex, &ts);
        if (rc != 0) PZ_ASSERT(rc == ETIMEDOUT);
    } 
}