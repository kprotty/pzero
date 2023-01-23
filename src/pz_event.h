#ifndef _PZ_EVENT_H
#define _PZ_EVENT_H

#include "pz_time.h"
#include "pz_atomic.h"

typedef struct {
    _Atomic(void*) state;
} pz_event;

static const pz_event PZ_EVENT_INIT = {NULL};

PZ_NONNULL(1)
static bool pz_event_wait(pz_event* restrict event, const pz_time* restrict deadline);

PZ_NONNULL(1)
static void pz_event_set(pz_event* event);

#endif // _PZ_EVENT_H
