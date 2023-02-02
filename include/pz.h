#ifndef _PZ_H
#define _PZ_H
#ifdef __cplusplus
    extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct pz_task pz_task;
typedef void (*pz_task_callback)(pz_task* task);

// A task is a schedulable entity within the scheduler. The callback is invoked when executed.
struct pz_task {
    pz_task_callback callback;
    pz_task* next;
};

typedef enum PZ_TRACE_EVENT {
    // Generated on a newly started worker before it starts running.
    PZ_TRACE_ON_START,
    // Generated on a worker before it stops running.
    PZ_TRACE_ON_STOP,
    // Generated just before a worker is put to sleep.
    PZ_TRACE_ON_PARK,
    // Generated just after a worker thread is woken up.
    PZ_TRACE_ON_UNPARK,
    // Generated on a worker before it invokes a task callback.
    PZ_TRACE_ON_EXECUTE,
    // Generated on a worker after it invokes a task callback. (The task may be invalid).
    PZ_TRACE_ON_COMPLETE,
} PZ_TRACE_EVENT;

typedef struct pz_trace {
    PZ_TRACE_EVENT event;
    union {
        struct { unsigned int worker_id; } on_start;
        struct { unsigned int worker_id; } on_stop;
        struct { unsigned int worker_id; } on_park;
        struct { unsigned int worker_id; } on_unpark;
        struct { unsigned int worker_id; pz_task* task; } on_execute;
        struct { unsigned int worker_id; pz_task* task; } on_complete;
    } data;
} pz_trace;

typedef void (*pz_trace_callback)(const pz_trace* trace, void* user_data);

typedef struct pz_config {
    // Runtime-local user data that can be retrieved from any worker and is passed to the trace callback.
    void* user_data;
    // Hint to the stack sizes for spawned workers. If set to 0, defaults to stack size decided by the OS.
    size_t stack_size;
    // Maximum amount of workers that can be spawned/running. If set to 0, defaults to the number of OS detectable CPU concurrency.
    unsigned int max_workers;
    // Runtime tick interval at which to ensure scheduled task fairness.
    unsigned int task_poll_interval;
    // Runtime tick interval at which to enforce event-source (IO/timer/etc.) fairness.
    unsigned int event_poll_interval;
    // Optional callback to invoke when a traceable emit is emitted from a worker in the scheduler.
    pz_trace_callback trace_callback;
} pz_config;

// Runs an instance of a scheduler using the provided config, starting with main_task.
// Blocks until pz_shutdown() is called on a worker from the scheduler.
int pz_run(const pz_config* config, pz_task* main_task);

// Stops the runtime associated on the current runtime, eventually stopping all workers and returning from pz_run().
// Returns 
int pz_shutdown(void);

/// Returns the current worker id if the caller is inside an active runtime.
int pz_current(void);

// Schedules a task (assuming its callback was set) to run on the runtime associated with the worker.
int pz_schedule(pz_task* task);


typedef struct pz_completion {
    void* reserved[8];
} pz_completion;

typedef void (*pz_completion_callback)(pz_completion* completion);

int pz_cancel(pz_completion* completion);

int pz_result(const pz_completion* completion);


typedef bool (*pz_validate_callback)(void* key, void* context, pz_completion* completion);

int pz_park(
    void* key,
    void* context,
    pz_validate_callback validate,
    pz_completion* restrict completion,
    pz_completion_callback restrict callback
);

int pz_unpark(void* key, void* context, pz_validate_callback validate);

#ifdef __cplusplus
    }
#endif
#endif // _PZ_H
