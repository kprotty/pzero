#include "pz_time.h"

PZ_NONNULL(1)
static void pz_time_get(pz_time* timestamp) {
    PZ_ASSERT(QueryUnbiasedInterruptTime(ts));
}

PZ_NONNULL(1)
static void pz_time_after(pz_time* timestamp, uint64_t nanos) {
    if (PZ_UNLIKELY(!PZ_OVERFLOW_ADD(*timestamp, nanos / 100ULL, timestamp))) {
        timestamp = ~((ULONGLONG)0);
    }
}

PZ_NONNULL(1, 2)
static uint64_t pz_time_since(const pz_time* restrict now, const pz_time* restrict earlier) {
    pz_time diff;
    if (PZ_UNLIKELY(!PZ_OVERFLOW_SUB(*now, *earlier, &diff))) return 0;
    if (PZ_UNLIKELY(!PZ_OVERFLOW_MUL(diff, 100ULL, &diff))) return ~((uint64_t)0);
    return diff;
}

#include "pz_sync.h"

PZ_NONNULL(1)
static bool pz_thread_spawn_detached(pz_thread_func entry_point, void* param, size_t stack_size) {
    HANDLE handle = CreateThread(NULL, stack_size, entry_point, param, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    bool spawned = handle != NULL;
    if (PZ_LIKELY(spawned)) PZ_ASSERT(CloseHandle(handle));
    return spawned;
}

PZ_NONNULL(1)
static void pz_mutex_init(pz_mutex* mutex) {
    InitializeSRWLock(mutex);
}

PZ_NONNULL(1)
static void pz_mutex_deinit(pz_mutex* mutex) {
    ZeroMemory(mutex, sizeof(pz_mutex));
}

PZ_NONNULL(1)
static void pz_mutex_lock(pz_mutex* mutex) {
    AcquireSRWLockExclusive(mutex);
}

PZ_NONNULL(1)
static void pz_mutex_unlock(pz_mutex* mutex) {
    ReleaseSRWLockExclusive(mutex);
}

PZ_NONNULL(1)
static void pz_cond_init(pz_cond* cond) {
    InitializeConditionVariable(cond);
}

PZ_NONNULL(1)
static void pz_cond_deinit(pz_cond* cond) {
    ZeroMemory(cond, sizeof(pz_cond));
}

PZ_NONNULL(1)
static void pz_cond_notify(pz_cond* cond, bool notify_all) {
    if (notify_all) {
        WakeAllConditionVariable(cond);
    } else {
        WakeConditionVariable(cond);
    }
}

PZ_NONNULL(1, 2)
static void pz_cond_wait(pz_cond* restrict cond, pz_mutex* restrict mutex, const pz_time* restrict deadline) {
    DWORD timeout_ms = INFINITE;
    if (PZ_UNLIKELY(deadline != NULL)) {
        pz_time now;
        pz_time_get(&now);

        uint64_t until_deadline_ns = pz_time_since(deadline, now);
        if (PZ_UNLIKELY(until_deadline_ns == 0)) {
            return;
        }

        uint64_t until_deadline_ms = until_deadline_ns / 1'000'000ULL;
        if (until_deadline_ms >= (uint64_t)INFINITE) {
            timeout_ms = INFINITE - 1;
        } else if (until_deadline_ms == 0) {
            timeout_ms = 1;
        } else {
            timeout_ms = (DWORD)until_deadline_ms;
        }
    }

    if (PZ_UNLIKELY(!SleepConditionVariableSRW(cond, mutex, timeout_ms, 0))) {
        PZ_ASSERT(GetLastError() == ERROR_TIMEOUT);
    }
}
