#include "pz_thread.h"

#ifdef _WIN32
    PZ_NONNULL(1)
    static bool pz_thread_spawn_detached(pz_thread_func entry_point, void* param, size_t stack_size) {
        HANDLE handle = CreateThread(NULL, stack_size, entry_point, param, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
        if (PZ_UNLIKELY(handle == NULL)) {
            return false;
        }
        
        PZ_ASSERT(CloseHandle(handle));
        return true;
    }

    static DWORD tls_index;
    static INIT_ONCE tls_once = INIT_ONCE_STATIC_INIT;

    BOOL CALLBACK pz_thread_local_create(PINIT_ONCE _tls_once, PVOID _param, PVOID* _context) {
        PZ_UNUSED(_tls_once);
        PZ_UNUSED(_param);
        PZ_UNUSED(_context);
        tls_index = TlsAlloc();
    }

    static bool pz_thread_local_init(void) {
        PZ_ASSERT(InitOnceExecuteOnce(&tls_once, pz_thread_local_create, NULL, NULL));
        return tls_index != TLS_OUT_OF_INDEXES;
    }

    static void pz_thread_local_set(void* value) {
        PZ_ASSERT(TlsSetValue(tls_index, value));
    }

    static void* pz_thread_local_get(void) {
        return TlsGetValue(tls_index);
    }
    
#else
    PZ_NONNULL(1)
    static bool pz_thread_spawn_detached(pz_thread_func entry_point, void* param, size_t stack_size) {
        bool spawned;
        pthread_t handle;
        pthread_attr_t attr;

        spawned = pthread_attr_init(&attr);
        if (PZ_UNLIKELY(!spanwed)) goto finished;

        spawned = pthread_attr_setstacksize(&attr, stack_size);
        if (PZ_UNLIKELY(!spawned)) goto cleanup;

        spawned = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (PZ_UNLIKELY(!spawned)) goto cleanup;

        spawned = pthread_create(&handle, &attr, entry_point, param);
        if (PZ_UNLIKELY(!spawned)) goto cleanup;

    cleanup:
        PZ_ASSERT(pthread_attr_destroy(&attr));
    finished:
        return spawned;
    }

    static bool pz_thread_local_init(void) {
        #error TODO
    }

    static void pz_thread_local_set(void* value) {
        #error TODO
    }

    static void* pz_thread_local_get(void) {
        #error TODO
    }
#endif
