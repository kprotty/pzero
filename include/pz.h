#ifndef _PZ_H
#define _PZ_H

#ifdef __cplusplus
    extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __GNUC__
#define PZ_GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#else
#define PZ_GCC_VERSION 0
#endif

#ifdef __has_attribute
#define PZ_HAS_ATTR(attr) __has_attribute(attr)
#else
#define PZ_HAS_ATTR(attr) 0
#endif

#if PZ_HAS_ATTR(__nonnull__) || PZ_GCC_VERSION
#define PZ_NONNULL(...) __attribute__((__nonnull__(__VA_ARGS__)))
#else
#define PZ_NONNULL(...) /**/
#endif

struct pz_context;
struct pz_task;

typedef void (*pz_task_callback)(
    struct pz_context* restrict context,
    struct pz_task* restrict task);

struct pz_task {
    void* reserved;
    pz_task_callback callback; 
};

PZ_NONNULL(1, 2)
void pz_schedule(struct pz_context* restrict context, struct pz_task* restrict task);

PZ_NONNULL(1)
void pz_shutdown(struct pz_context* context); 

enum PZ_TRACE_EVENT {
    PZ_TRACE_EXECUTE,
};

struct pz_trace {
    enum PZ_TRACE_EVENT event;
    union {
        struct {
            struct pz_context* context;
            struct pz_task* task;
        } execute;
    } data;
};

typedef void (*pz_trace_callback)(
    void* restrict trace_context,
    const struct pz_trace* restrict trace);

struct pz_config {
    unsigned int max_workers;
    void* trace_context;
    pz_trace_callback trace_callback;
};

PZ_NONNULL(1, 2)
void pz_run(const struct pz_config* restrict config, struct pz_task* restrict task);

struct pz_completion {
    void* reserved[64 / sizeof(void*)];
};

PZ_NONNULL(1)
bool pz_cancel(struct pz_completion* completion);


#ifdef __cplusplus
    }
#endif

#endif // _PZ_H
