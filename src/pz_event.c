#include "pz_event.h"

#define PZ_NS_PER_S 1000000000

#ifdef _WIN32
    #include <Windows.h>

    typedef SRWLOCK pz_mutex;
    #define pz_mutex_init InitializeSRWLock
    #define pz_mutex_destroy(m) ZeroMemory((m), sizeof(pz_mutex))
    #define pz_mutex_lock AcquireSRWLockExclusive
    #define pz_mutex_unlock ReleaseSRWLockExclusive

    typedef CONDITION_VARIABLE pz_cond;
    #define pz_cond_init InitializeConditionVariable
    #define pz_cond_destroy(c) ZeroMemory((c), sizeof(pz_cond))
    #define pz_cond_signal WakeConditionVariable

    PZ_NONNULL(1, 2)
    static void pz_cond_wait(pz_cond* restrict cond, pz_mutex* restrict mutex, uint64_t timeout_ns) {
        DWORD delay_ms = INFINITE;
        if (PZ_UNLIKELY(timeout_ns != ~((uint64_t)0))) {
            uint64_t timeout_ms = timeout_ns / PZ_NS_PER_S;
            if (timeout_ms >= (uint64_t)INFINITE) {
                delay_ms = INFINITE - 1;
            } else if (timeout_ms == 0) {
                delay_ms = 1;
            } else {
                delay_ms = (DWORD)timeout_ms;
            }
        }
        if (!SleepConditionVariableSRW(cond, mutex, delay_ms, 0)) {
            PZ_ASSERT(GetLastError() == ERROR_TIMEOUT);
        }
    }
#else
    #include <pthread.h>

    typedef pthread_mutex_t pz_mutex;
    #define pz_mutex_init(m) PZ_ASSERT(pthread_mutex_init((m), NULL))
    #define pz_mutex_destroy(m) PZ_ASSERT(pthread_mutex_destroy(m))
    #define pz_mutex_lock(m) PZ_ASSERT(pthread_mutex_lock(m))
    #define pz_mutex_unlock PZ_ASSERT(pthread_mutex_unlock(m))

    typedef pthread_cond_t pz_cond;
    #define pz_cond_init(c) PZ_ASSERT(pthread_cond_init((c), NULL))
    #define pz_cond_destroy(c) PZ_ASSERT(pthread_cond_destroy(c))
    #define pz_cond_signal(c) PZ_ASSERT(pthread_cond_signal(c))

    PZ_NONNULL(1, 2)
    static void pz_cond_wait(pz_cond* restrict cond, pz_mutex* restrict mutex, uint64_t timeout_ns) {
        if (PZ_LIKELY(timeout_ns == ~((uint64_t)0))) {
            PZ_ASSERT(pthread_cond_wait(cond, mutex));
            return;
        }
        
        struct timespec ts;
        PZ_ASSERT(clock_getttime(CLOCK_REALTIME, &ts));

        uint64_t sec = timeout_ns / PZ_NS_PER_S;
        uint32_t nsec = (uint32_t)(timeout_ns % PZ_NS_PER_S);

        if (PZ_UNLIKELY(((uint64_t)((time_t)sec)) != sec)) {
            goto ts_overflow;
        }
        if (PZ_UNLIKELY(!PZ_OVERFLOW_ADD(ts.tv_sec, (time_t)sec, &ts.tv_sec))) {
            goto ts_overflow;
        }

        ts.tv_nsec += nsec;
        if (ts.tv_nsec >= PZ_NS_PER_S) {
            ts.tv_nsec -= PZ_NS_PER_S;
            if (PZ_UNLIKELY(!PZ_OVERFLOW_ADD(ts.tv_sec, 1, &ts.tv_sec))) {
                goto ts_overflow;
            }
        }

        goto ts_wait;

    ts_overflow:
        ts.tv_sec = (time_t)(1ULL << ((sizeof(time_t) * CHAR_BITS) - 1));
        ts.tv_nsec = PZ_NS_PER_S - 1;
    ts_wait:
        int rc = pthread_cond_timedwait(cond, mutex, &ts);
        if (PZ_UNLIKELY(rc != 0)) {
            PZ_ASSERT(rc == ETIMEDOUT);
        }
    }
#endif

typedef struct {
    pz_cond cond;
    pz_mutex mutex;
    bool notified;
} pz_os_event;

PZ_NONNULL(1)
static bool pz_event_wait(pz_event* restrict event, const pz_time* restrict deadline) {
    void* ev = atomic_load_explicit(&event->state, memory_order_acquire);
    if (PZ_UNLIKELY(ev != NULL)) {
        return true;
    }

    pz_os_event os_ev;
    os_ev.notified = false;
    pz_cond_init(&os_ev.cond);
    pz_mutex_init(&os_ev.mutex);
    pz_mutex_lock(&os_ev.mutex);

    os_ev.notified = atomic_exchange_explicit(&event->state, (void*)&os_ev, memory_order_acq_rel) != NULL;
    while (PZ_LIKELY(!os_ev.notified)) {
        uint64_t timeout_ns = ~((uint64_t)0);
        if (PZ_UNLIKELY(deadline != NULL)) {
            pz_time now;
            pz_time_get(&now);

            timeout_ns = pz_time_since(deadline, &now);
            if (PZ_UNLIKELY(timeout_ns == 0)) {
                os_ev.notified = atomic_exchange_explicit(&event->state, NULL, memory_order_acquire) != ((void*)&os_ev);
                if (PZ_LIKELY(!os_ev.notified)) {
                    break;
                }

                os_ev.notified = false;
                deadline = NULL;
                continue;
            }
        }
        pz_cond_wait(&os_ev.cond, &os_ev.mutex, timeout_ns);
    }

    pz_mutex_unlock(&os_ev.mutex);
    pz_mutex_destroy(&os_ev.mutex);
    pz_cond_destroy(&os_ev.cond);
    return os_ev.notified;
}

PZ_NONNULL(1)
static void pz_event_set(pz_event* event) {
    void* ev = atomic_exchange_explicit(&event->state, (void*)event, memory_order_acq_rel);
    if (PZ_LIKELY(ev == NULL)) {
        return;
    }

    pz_os_event* os_ev = (pz_os_event*)ev;
    pz_mutex_lock(&os_ev->mutex);
    os_ev->notified = true;
    pz_cond_signal(&os_ev->cond);
    pz_mutex_unlock(&os_ev->mutex);
}
