#pragma once

#include "pz_queue.h"
#include "pz_idle.h"

typedef struct {
    _Alignas(PZ_ATOMIC_CACHE_LINE) _Atomic(uint32_t) state;
    _Alignas(PZ_ATOMIC_CACHE_LINE) pz_injector injector;
    _Alignas(PZ_ATOMIC_CACHE_LINE) pz_idle idle;
    _Alignas(PZ_ATOMIC_CACHE_LINE) _Atomic(pz_worker*) workers[0xff];
} pz_scheduler;
