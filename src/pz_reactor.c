#include "pz_reactor.h"

PZ_NONNULL(1)
static int pz_reactor_init(pz_reactor* reactor) {
    PZ_UNUSED(reactor);
    #error TODO
}

PZ_NONNULL(1)
static void pz_reactor_deinit(pz_reactor* reactor) {
    PZ_UNUSED(reactor);
    #error TODO
}

PZ_NONNULL(1, 2)
static bool pz_reactor_poll(pz_reactor* restrict reactor, pz_batch* restrict ready, const pz_time* restrict deadline) {
    PZ_UNUSED(reactor);
    PZ_UNUSED(ready);
    PZ_UNUSED(deadline);
    #error TODO
}

PZ_NONNULL(1)
static void pz_reactor_notify(pz_reactor* reactor, bool shutdown) {
    PZ_UNUSED(reactor);
    PZ_UNUSED(shutdown);
    #error TODO
}
