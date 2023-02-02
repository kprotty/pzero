#pragma once

#include "pz_atomic.h"
#include "pz_time.h"

typedef struct {
    _Atomic(void*) state;
} pz_event;

static const pz_event PZ_EVENT_INIT = {NULL};

pz_nonnull(1) static bool pz_event_wait(pz_event* restrict event, const pz_time* restrict deadline);

pz_nonnull(1) static void pz_event_set(pz_event* event);
