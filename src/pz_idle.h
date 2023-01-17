#ifndef _PZ_IDLE_H
#define _PZ_IDLE_H

#include "pz_atomic.h"
#include "pz_sync.h"

typedef struct {
    pz_mutex mutex;
    pz_cond cond;
} pz_idle;

#endif // _PZ_IDLE_H