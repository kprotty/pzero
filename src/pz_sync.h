#ifndef _PZ_SYNC_H
#define _PZ_SYNC_H

#include "pz_builtin.h"
#include "pz_time.h"

#ifdef _WIN32
    #include <Windows.h>

    typedef SRWLOCK pz_mutex;
    typedef CONDITION_VARIABLE pz_cond;
    typedef LPTHREAD_START_ROUNTINE pz_thread_func;

    #define PZ_THREAD_FUNC_BEGIN(name, param) DWORD WINAPI ##name(LPVOID ##param) {
    #define PZ_THREAD_FUNC_END return 0; }
#else
    #include <pthread.h>

    typedef pthread_mutex_t pz_mutex;
    typedef pthread_cond_t pz_cond;
    typedef void* (*pz_thread_func)(void*);

    #define PZ_THREAD_FUNC_BEGIN(name, param) void* ##name(void* ##param) {
    #define PZ_THREAD_FUNC_END return NULL; }
#endif

PZ_NONNULL(1)
static bool pz_thread_spawn_detached(pz_thread_func entry_point, void* param, size_t stack_size);

PZ_NONNULL(1)
static void pz_mutex_init(pz_mutex* mutex);

PZ_NONNULL(1)
static void pz_mutex_deinit(pz_mutex* mutex);

PZ_NONNULL(1)
static void pz_mutex_lock(pz_mutex* mutex);

PZ_NONNULL(1)
static void pz_mutex_unlock(pz_mutex* mutex);

PZ_NONNULL(1)
static void pz_cond_init(pz_cond* cond);

PZ_NONNULL(1)
static void pz_cond_deinit(pz_cond* cond);

PZ_NONNULL(1)
static void pz_cond_notify(pz_cond* cond, bool notify_all);

PZ_NONNULL(1, 2)
static void pz_cond_wait(pz_cond* restrict cond, pz_mutex* restrict mutex, const pz_time* restrict deadline);

#endif // _PZ_SYNC_H
