#pragma once

#include "pz_builtin.h"

#ifdef _WIN32
    #define _WIN32_LEAN_AND_MEAN
    #include <Windows.h>
    typedef ULONGLONG pz_time;
#else
    #include <time.h>
    typedef struct timespec pz_time;
#endif

pz_nonnull(1) static void pz_time_get(pz_time* timestamp);

pz_nonnull(1) static void pz_time_after(pz_time* timestamp, uint64_t nanos);

pz_nonnull(1, 2) static uint64_t pz_time_since(pz_time* restrict timestamp, pz_time* restrict earlier);
