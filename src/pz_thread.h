#ifndef _PZ_THREAD_H
#define _PZ_THREAD_H

#include "pz_builtin.h"

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
    typedef DWORD WINAPI (*pz_thread_func)(PVOID);
    #define PZ_THREAD_FUNC_BEGIN(name, param) DWORD WINAPI ##name(LPVOID ##param) {
    #define PZ_THREAD_FUNC_END return 0; }
#else
    #include <pthread.h>
    typedef void* (*pz_thread_func)(void*);
    #define PZ_THREAD_FUNC_BEGIN(name, param) void* ##name(void* ##param) {
    #define PZ_THREAD_FUNC_END return NULL; }
#endif

PZ_NONNULL(1)
static bool pz_thread_spawn_detached(pz_thread_func entry_point, void* param, size_t stack_size);

static bool pz_thread_local_init(void);

static void pz_thread_local_set(void* value);

static void* pz_thread_local_get(void);

#endif // _PZ_THREAD_H
