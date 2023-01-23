#ifndef _PZ_TIME_H
#define _PZ_TIME_H

#include "pz_builtin.h"

#ifdef _WIN32
    #include <Windows.h>
    typedef ULONGLONG pz_time;
#else
    #include <time.h>
    typedef struct timespec pz_time;
#endif

PZ_NONNULL(1)
static void pz_time_get(pz_time* timestamp);

PZ_NONNULL(1)
static void pz_time_after(pz_time* timestamp, uint64_t nanos);

PZ_NONNULL(1, 2)
static uint64_t pz_time_since(const pz_time* restrict now, const pz_time* restrict earlier);

#endif // _PZ_TIME_H
