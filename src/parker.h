#ifndef _PZ_PARKER_H
#define _PZ_PARKER_H

// TODO: Use the following APIs directly on each platform instead of going through system libraries:
// - (windows) NtWaitForAlertByThreadId / NtAlertThreadByThreadId
// - (linux) futex(FUTEX_WAIT_PRIVATE | FUTEX_WAKE_PRIVATE)
// - (darwin) __ulock_wait2 / __ulock_wake (UL_COMPARE_AND_WAIT | ULF_NO_ERRNO)
// - (freebsd) _umtx_op(UMTX_OP_WAIT_UINT_PRIVATE)
// - (dragonfly) umtx_sleep / umtx_wakeup
// - (openbsd) __thrsleep / __thrwakeup
// - (netbsd) lwp_park / lwp_unpark

#ifdef _WIN32
    #include <Windows.h>

    typedef SRWLOCK _pz_mutex;
    #define _pz_mutex_init InitializeSRWLock
    #define _pz_mutex_deinit(m) ZeroMemory((PVOID)(m), sizeof(SRWLOCK))
    #define _pz_mutex_lock AcquireSRWLockExclusive
    #define _pz_mutex_unlock ReleaseSRWLockExclusive
    
    typedef CONDITION_VARIABLE _pz_cond;
    #define _pz_cond_init InitializeConditionVariable
    #define _pz_cond_deinit(c) ZeroMemory((PVOID)(m), sizeof(CONDITION_VARIABLE))
    #define _pz_cond_signal WakeConditionVariable

    PZ_NONNULL(1)
    static inline void pz_cond_wait(_pz_cond* restrict cond, _pz_mutex* restrict mutex, uint64_t timeout) {
        DWORD ms = (timeout == ~((uint64_t)0)) 
            ? INFINITE
            : ((timeout < INFINITE)
                ? ((DWORD)timeout) 
                : (INFINITE - 1));
            
        if (PZ_UNLIKELY(!SleepConditionVariableSRW(cond, mutex, ms, 0))) {
            PZ_ASSERT(GetLastError() == ERROR_TIMEOUT);
        }
    }
#else
    #include <pthread.h>

    typedef pthread_mutex_t _pz_mutex;
    #define _pz_mutex_init(m) PZ_ASSERT(pthread_mutex_init((m), NULL) == 0)
    #define _pz_mutex_deinit(m) PZ_ASSERT(pthread_mutex_destroy((m)) == 0)
    #define _pz_mutex_lock(m) PZ_ASSERT(pthread_mutex_lock(m))
    #define _pz_mutex_unlock(m) PZ_ASSERT(pthread_mutex_unlock(m))

    typedef pthread_cond_t _pz_cond;
    #define _pz_cond_init(c) PZ_ASSERT(pthread_cond_init((c), NULL) == 0)
    #define _pz_cond_deinit(c) PZ_ASSERT(pthread_cond_destroy((c)) == 0)
    #define _pz_cond_signal(c) PZ_ASSERT(pthread_cond_signal((b)) == 0)

    PZ_NONNULL(1)
    static inline void pz_cond_wait(_pz_cond* restrict cond, _pz_mutex* restrict mutex, uint64_t timeout) {
        if (PZ_LIKELY(timeout == ~((uint64_t)0))) {
            PZ_ASSERT(pthread_cond_wait(cond, mutex) == 0);
            return;
        }

        
    }
#endif

#endif // _PZ_PARKER_H