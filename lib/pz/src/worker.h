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

    static PZ_INLINE uintptr_t PzTaskWorkerToIndex(
        PZ_NOALIAS(const PzTaskNode*) node,
        PZ_NOALIAS(const PzTaskWorker*) worker
    ) {
        uintptr_t ptr = (uintptr_t) worker;
        uintptr_t base = (uintptr_t) PzTaskNodeGetWorkersPtr(node);

        PzDebugAssert(
            (ptr >= base) && (ptr < (base + (sizeof(PzTaskWorker) * PzTaskNodeGetWorkersLen(node)))),
            "invalid PzTaskWorker ptr"
        );

        return (ptr - base) + 1;
    }

    static PZ_INLINE PzTaskWorker* PzTaskWorkerFromIndex(
        PZ_NOALIAS(const PzTaskNode*) node,
        uintptr_t index
    ) {
        if (index == 0)
            return NULL;

        PzTaskWorker* workers = PzTaskNodeGetWorkersPtr(node);
        return &workers[index - 1];
    }

#endif
