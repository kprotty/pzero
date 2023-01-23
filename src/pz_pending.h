#ifndef _PZ_PENDING_H
#define _PZ_PENDING_H

#include "pz_atomic.h"

typedef struct {
    _Atomic(void*) next;
} pz_pending_node;

typedef struct {
    _Atomic(void*) tail;
} pz_pending_list;

static const pz_pending_list PZ_PENDING_LIST_INIT = {NULL};

#endif // _PZ_PENDING_H
