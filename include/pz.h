#ifndef _PZ_H
#define _PZ_H
#ifdef __cplusplus
    extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct pz_task pz_task;
typedef struct pz_worker pz_worker;
typedef void (*pz_task_callback)(pz_task* task, pz_worker* worker);

struct pz_task {
    void* reserved;
    pz_task_callback callback;
};

// Returns the config.context given when the runtime was started.
void* pz_context(const pz_worker* worker);

// Returns the current worker id.
unsigned int pz_id(const pz_worker* worker);

// Schedules a task (assuming its callback was set) to run on the runtime associated with the worker.
void pz_schedule(pz_worker* worker, pz_task* task);

// Stops the runtime associated with the worker, eventually stopping all workers and returning from pz_run().
void pz_shutdown(pz_worker* worker);

enum PZ_TRACE_EVENT {
    PZ_TRACE_ON_WORKER_START, // Generated on a newly started worker before it starts running.
    PZ_TRACE_ON_WORKER_STOP, // Generated on a worker before it stops running.
    PZ_TRACE_ON_WORKER_PARK, // Generated just before a worker is put to sleep.
    PZ_TRACE_ON_WORKER_UNPARK, // Generated just after a worker thread is woken up.
    PZ_TRACE_ON_WORKER_EXECUTE, // Generated on a worker before it invokes a task callback.
};

typedef struct pz_trace {
    enum PZ_TRACE_EVENT event;
    union {
        pz_worker* on_start;
        pz_worker* on_stop;
        pz_worker* on_park;
        pz_worker* on_unpark;
        struct {
            pz_worker* worker;
            pz_task* task;
        } on_execute;
    } data;
} pz_trace;

typedef void (*pz_trace_callback)(void* context, const pz_trace* trace);

typedef struct pz_config {
    void* context;
    size_t stack_size;
    unsigned int max_workers;
    unsigned int task_poll_interval;
    unsigned int event_poll_interval;
    pz_trace_callback trace_callback;
} pz_config;

int pz_run(const pz_config* config, pz_task* main_task);

typedef struct pz_completion {
    void* reserved[64 / sizeof(void*)];
} pz_completion;

bool pz_cancel(pz_completion* completion);

int pz_result(const pz_completion* completion);

#ifdef __cplusplus
    }
#endif
#endif // _PZ_H
