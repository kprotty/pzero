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

// A task is a schedulable entity within the scheduler. The callback is invoked when executed.
struct pz_task {
    void* _reserved;
    pz_task_callback callback;
};

enum PZ_TRACE_EVENT {
    // Generated on a newly started worker before it starts running.
    PZ_TRACE_ON_WORKER_START,
    // Generated on a worker before it stops running.
    PZ_TRACE_ON_WORKER_STOP,
    // Generated just before a worker is put to sleep.
    PZ_TRACE_ON_WORKER_PARK,
    // Generated just after a worker thread is woken up.
    PZ_TRACE_ON_WORKER_UNPARK,
    // Generated on a worker before it invokes a task callback.
    PZ_TRACE_ON_WORKER_EXECUTE,
};

// Trace event emitted to pz_trace_callback.
typedef struct {
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

// Invoked when a trace emit in a scheduler is emitted. 
typedef void (*pz_trace_callback)(void* context, const pz_trace* trace);

typedef struct {
    // Scheduler-local user data that can be retrieved from any worker and is passed to the trace callback.
    void* context;
    // Hint to the stack sizes for spawned workers. If set to 0, defaults to stack size decided by the OS.
    size_t stack_size;
    // Maximum amount of workers that can be spawned/running. If set to 0, defaults to the number of OS detectable CPU concurrency.
    unsigned int max_workers;
    // Scheduler tick interval at which to ensure scheduled task fairness.
    unsigned int task_poll_interval;
    // Scheduler tick interval at which to enforce event-source (IO/timer/etc.) fairness.
    unsigned int event_poll_interval;
    // Optional callback to invoke when a traceable emit is emitted from a worker in the scheduler.
    pz_trace_callback trace_callback;
} pz_config;

// Runs an instance of a scheduler using the provided config, starting with main_task.
// Blocks until pz_shutdown() is called on a worker from the scheduler.
int pz_run(const pz_config* config, pz_task* main_task);

// Returns the current worker id.
unsigned int pz_id(const pz_worker* worker);

// Schedules a task (assuming its callback was set) to run on the runtime associated with the worker.
void pz_schedule(pz_worker* worker, pz_task* task);

// Returns the config.context given when the runtime was started.
void* pz_context(const pz_worker* worker);

// Stops the runtime associated with the worker, eventually stopping all workers and returning from pz_run().
void pz_shutdown(pz_worker* worker);

typedef struct pz_completion {
    void* _reserved[64 / sizeof(void*)];
} pz_completion;

bool pz_cancel(pz_completion* completion);

int pz_result(const pz_completion* completion);

#ifdef __cplusplus
    }
#endif
#endif // _PZ_H
