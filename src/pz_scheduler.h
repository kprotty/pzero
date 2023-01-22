#ifndef _PZ_SCHEDULER_H
#define _PZ_SCHEDULER_H

#include "pz_runnable.h"
#include "pz_reactor.h"

typedef struct {
    alignas(PZ_ATOMIC_CACHE_LINE) _Atomic(uint32_t) state;
    alignas(PZ_ATOMIC_CACHE_LINE) pz_injector injector;
    alignas(PZ_ATOMIC_CACHE_LINE) pz_reactor reactor;
    alignas(PZ_ATOMIC_CACHE_LINE) _Atomic(pz_worker*) workers[0xff];

    unsigned int event_poll_interval;
    unsigned int task_poll_interval;
    pz_trace_callback trace_callback;
    size_t stack_size;
    void* context;
} pz_scheduler;



#endif // _PZ_SCHEDULER_H