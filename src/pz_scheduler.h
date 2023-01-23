#ifndef _PZ_SCHEDULER_H
#define _PZ_SCHEDULER_H

#include "pz_runnable.h"
#include "pz_reactor.h"

typedef struct {
    _Alignas(PZ_ATOMIC_CACHE_LINE) _Atomic(uint32_t) state;
    _Alignas(PZ_ATOMIC_CACHE_LINE) pz_injector injector;
    _Alignas(PZ_ATOMIC_CACHE_LINE) pz_reactor reactor;
    _Alignas(PZ_ATOMIC_CACHE_LINE) _Atomic(pz_worker*) workers[0xff];

    unsigned int event_poll_interval;
    unsigned int task_poll_interval;
    pz_trace_callback trace_callback;
    size_t stack_size;
    void* context;
} pz_scheduler;



#endif // _PZ_SCHEDULER_H
