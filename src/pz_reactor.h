#ifndef _PZ_REACTOR_H
#define _PZ_REACTOR_H

#include "pz_thread.h"
#include "pz_runnable.h"

typedef struct {
    pz_mutex mutex;
    pz_cond cond;
} pz_reactor;

PZ_NONNULL(1)
static int pz_reactor_init(pz_reactor* reactor);

PZ_NONNULL(1)
static void pz_reactor_deinit(pz_reactor* reactor);

PZ_NONNULL(1, 2)
static bool pz_reactor_poll(pz_reactor* restrict reactor, pz_batch* restrict ready, const pz_time* restrict deadline);

PZ_NONNULL(1)
static void pz_reactor_notify(pz_reactor* reactor, bool shutdown);

#endif // _PZ_REACTOR_H