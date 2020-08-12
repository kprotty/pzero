#ifndef PZ_TASK_WORKER_H
#define PZ_TASK_WORKER_H

    #include "pz.h"

    typedef enum PZ_TASK_WORKER_TYPE {
        PZ_TASK_WORKER_IDLE = 0,
        PZ_TASK_WORKER_SPAWNING = 1,
        PZ_TASK_WORKER_RUNNING = 2,
        PZ_TASK_WORKER_SHUTDOWN = 3,
    } PZ_TASK_WORKER_TYPE;

    static PZ_INLINE uintptr_t PzTaskWorkerInit(PZ_TASK_WORKER_TYPE ptr_type, uintptr_t ptr) {
        PzDebugAssert((ptr & 3) == 0, "misaligned worker ptr");
        return ptr | ptr_type;
    }

    static PZ_INLINE PZ_TASK_WORKER_TYPE PzTaskWorkerGetType(uintptr_t ptr) {
        return (PZ_TASK_WORKER_TYPE)(ptr & 3);
    }

    static PZ_INLINE uintptr_t PzTaskWorkerGetPtr(uintptr_t ptr) {
        return ptr & ~((uintptr_t)3);
    }

#endif
