#ifndef _PZ_WORKER_H
#define _PZ_WORKER_H

#include "pz_runnable.h"

struct pz_worker {
    _Alignas(PZ_ATOMIC_CACHE_LINE) _Atomic(pz_runnable*) run_next;
    _Alignas(PZ_ATOMIC_CACHE_LINE) pz_buffer run_queue;
};



#endif // _PZ_WORKER_H
