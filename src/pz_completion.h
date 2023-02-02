#pragma once

#include "pz_builtin.h"

typedef enum {
    PZ_COMPLETION_REQUEST_CANCEL,
    PZ_COMPLETION_REQUEST_RESULT,
} PZ_COMPLETION_REQUEST;

typedef int (*pz_completion_request_callback)(
    pz_completion* restrict completion,
    PZ_COMPLETION_REQUEST request
);

typedef struct {
    pz_completion_request_callback callback;
} pz_completion_impl;

typedef struct {
    pz_completion_impl impl;
    pz_task_callback callback;
} pz_completion_header;