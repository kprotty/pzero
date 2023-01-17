#ifndef _PZ_SCHEDULER_H
#define _PZ_SCHEDULER_H

#include "pz_runnable.h"

typedef struct {
    alignas(PZ_ATOMIC_CACHE_LINE) _Atomic(uint32_t) state;
    alignas(PZ_ATOMIC_CACHE_LINE) pz_injector injector;
    alignas(PZ_ATOMIC_CACHE_LINE) pz_idle idle;
    alignas(PZ_ATOMIC_CACHE_LINE) _Atomic(pz_worker*) workers[256];

    unsigned int event_poll_interval;
    unsigned int task_poll_interval;
    pz_trace_callback trace_callback;
    size_t stack_size;
    void* context;
} pz_scheduler;



#endif // _PZ_SCHEDULER_H