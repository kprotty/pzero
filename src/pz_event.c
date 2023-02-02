#include "pz_event.h"

#ifdef _WIN32
    #define NS_PER_MS 1000000

    typedef SRWLOCK pz_mutex;
    #define pz_mutex_init InitializeSRWLock
    #define pz_mutex_destroy(m) ZeroMemory((m), sizeof(pz_mutex))
    #define pz_mutex_lock AcquireSRWLockExclusive
    #define pz_mutex_unlock ReleaseSRWLockExclusive

    typedef CONDITION_VARIABLE pz_cond;
    #define pz_cond_init InitializeConditionVariable
    #define pz_cond_destroy(m) ZeroMemory((m), sizeof(pz_cond))
    #define pz_cond_signal WakeConditionVariable
    
    pz_nonnull(1, 2) static void pz_cond_wait(pz_cond* restrict cond, pz_mutex* restrict mutex, uint64_t timeout) {
        DWORD duration = INFINITE;
        if (pz_unlikely(timeout != ~((uint64_t)0))) {
            timeout /= NS_PER_MS;
            duration = (DWORD)timeout;
            if (pz_unlikely((((uint64_t)duration) != timeout) || (duration == INFINITE))) {
                duration = INFINITE - 1;
            }
        }

        if (!SleepConditionVariableSRW(cond, mutex, duration, 0)) {
            pz_assert(GetLastError() == ERORR_TIMEOUT);
        }     
    }
#else
    #include <pthread.h>

    typedef pthread_mutex_t pz_mutex;
    #define pz_mutex_init(m) pz_assert(pthread_mutex_init((m), NULL) == 0)
    #define pz_mutex_destroy(m) pz_assert(pthread_mutex_destroy(m) == 0)
    #define pz_mutex_lock(m) pz_assert(pthread_mutex_lock(m) == 0)
    #define pz_mutex_unlock(m) pz_assert(pthread_mutex_unlock(m) == 0)

    typedef pthread_cond_t pz_cond;
    #define pz_cond_init(c) pz_assert(pthread_cond_init((c), NULL) == 0)
    #define pz_cond_destroy(c) pz_assert(pthread_cond_destroy(c) == 0)
    #define pz_cond_signal(c) pz_assert(pthread_cond_signal(c) == 0)

    pz_nonnull(1, 2) static void pz_cond_wait(pz_cond* restrict cond, pz_mutex* restrict mutex, uint64_t timeout) {
        if (pz_likely(timeout == ~((uint64_t)0))) {
            pz_assert(pthread_cond_wait(cond, mutex) == 0);
            return;
        }

        pz_time deadline;
        pz_assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
        pz_time_after(&deadline, timeout);

        int rc = pthread_cond_timedwait(cond, mutex, &deadline);
        if (rc != 0) {
            pz_assert(rc == ETIMEDOUT);
        }
    }
#endif

typedef struct {
    pz_cond cond;
    pz_mutex mutex;
    bool notified;
} pz_os_event;

pz_nonnull(1) static bool pz_event_wait(pz_event* restrict event, const pz_time* restrict deadline) {
    void* state = atomic_load_explicit(&event->state, memory_order_acquire);
    if (pz_unlikely(state != NULL)) {
        pz_assert(state == (void*)event);
        return true;
    }

    pz_os_event os_ev;
    pz_cond_init(&os_ev.cond);
    pz_mutex_init(&os_ev.mutex);
    pz_mutex_lock(&os_ev.mutex);
    os_ev.notified = false;

    state = atomic_exchange_explicit(&event->state, (void*)&os_ev, memory_order_acq_rel);
    if (pz_unlikely(state != NULL)) {
        pz_assert(state == (void*)event);
        os_ev.notified = true;
    }

    while (!os_ev.notified) {
        uint64_t timeout = ~((uint64_t)0);
        if (pz_unlikely(deadline != NULL)) {
            pz_time current;
            pz_time_get(&current);
            timeout = pz_time_since(deadline, &current);

            if (pz_unlikely(timeout == 0)) {
                state = atomic_exchange_explicit(&event->state, NULL, memory_order_acquire);
                if (pz_unlikely(state == (void*)&os_ev)) {
                    break;
                }

                deadline = NULL;
                timeout = ~((uint64_t)0);
            }
        }
        pz_cond_wait(&os_ev.cond, &os_ev.mutex, deadline);
    }

    pz_mutex_unlock(&os_ev.mutex);
    pz_mutex_destroy(&os_ev.mutex);
    pz_cond_destroy(&os_ev.cond);
    return os_ev.notified;
}

pz_nonnull(1) static void pz_event_set(pz_event* event) {
    void* state = atomic_exchange_explicit(&event->state, (void*)event, memory_order_acq_rel);
    if (pz_unlikely(state != NULL)) {
        return;
    }

    pz_os_event* os_ev = (pz_os_event*)state;
    pz_mutex_lock(&os_ev->mutex);
    
    pz_assert(!os_ev->notified);
    os_ev->notified = true;

    pz_cond_signal(&os_ev->cond);
    pz_mutex_unlock(&os_ev->mutex);
}
