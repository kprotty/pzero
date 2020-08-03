/* Copyright (c) 2020 kprotty
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef PZ_H
#define PZ_H
    #ifdef __cplusplus
    extern "C" {
    #endif

    // if defined(PZ_LINKED_STATIC) then using/building pz from a statically linked object
    // if defined(PZ_LINKED_SHARED) then using pz from a dynamically linked object
    // if neither defined, then building pz as a dynamically linked object

    #if defined(PZ_LINKED_STATIC) && defined(PZ_LINKED_SHARED)
        #error "Define either PZ_LINKED_STATIC or PZ_LINKED_SHARED, not both"
    #endif

    #if defined(_WIN32)
        #if defined(PZ_LINKED_STATIC)
        #define PZ_EXTERN /* nothing */
        #elif defined(PZ_LINKED_SHARED)
        #define PZ_EXTERN __declspec(dllimport)
        #else
        #define PZ_EXTERN __declspec(dllexport)
        #endif
    #elif __GNUC__ >= 4
        #define PZ_EXTERN __attribute__((visibility("default")))
    #else
        #define PZ_EXTERN /* nothing */
    #endif

    #if defined(__cplusplus)
        #include <cstddef>
        #include <cstdint>
    #else
        #include <stddef.h>
        #include <stdint.h>
        #include <stdbool.h>
    #endif

    #if !defined(__GNUC__) || (__GNUC__ < 2) || (__GNUC == 2 && __GNUC_MINOR__ < 96)
        #define __builtin_expect(x, expected) (x)
    #endif
    #define PZ_INLINE __inline__
    #define PZ_LIKELY(x) __builtin_expect((x), true)
    #define PZ_UNLIKELY(x) __builtin_expect((x), false)

    ////////////////////////////////////////////////////////////////////

    typedef struct PzTask PzTask;
    typedef struct PzTaskBatch PzTaskBatch;
    typedef struct PzTaskBatchIter PzTaskBatchIter;
    typedef struct PzTaskThread PzTaskThread;
    typedef struct PzTaskWorker PzTaskWorker;
    typedef struct PzTaskNode PzTaskNode;
    typedef struct PzTaskNodeIter PzTaskNodeIter;
    typedef struct PzTaskCluster PzTaskCluster;
    typedef struct PzTaskScheduler PzTaskScheduler;
    typedef void (*PzTaskCallback)(PzTask*, PzTaskThread*);

    struct PzTask {
        PzTask* next;
        PzTaskCallback callback;
    };

    #define PZ_TASK_INIT(callback_fn)   \
        ((PzTask)({                     \
            .next = NULL,               \
            .callback = callback_fn     \
        }))

    static PZ_INLINE void PzTaskInit(PzTask* self, PzTaskCallback callback) {
        *self = PZ_TASK_INIT(callback);
    }

    struct PzTaskBatchIter {
        PzTask* task;
    }

    static PZ_INLINE PzTask* PzTaskBatchIterNext(PzTaskBatch* self) {
        PzTask* task = self->task;
        if (PZ_LIKELY(task != NULL))
            self->task = task->next;
        return task;
    }

    struct PzTaskBatch {
        PzTask* head;
        PzTask* tail;
    };

    #define PZ_TASK_BATCH_INIT      \
        ((PzTaskBatch)({            \
            .head = NULL,           \
            .tail = NULL            \
        }))

    static PZ_INLINE void PzTaskBatchInit(PzTaskBatch* self) {
        *self = PZ_TASK_BATCH_INIT;
    }

    static PZ_INLINE PzTaskBatch PzTaskBatchFromTask(PzTask* task) {
        task->next = NULL;
        return ((PzTaskBatch)({
            .head = task,
            .tail = task
        }));
    }

    static PZ_INLINE void PzTaskBatchPushFrontMany(PzTaskBatch* self, PzTaskBatch batch) {
        if (batch.head == NULL) {
            return;
        } else if (self->head == NULL) {
            *self = batch;
        } else {
            batch.tail->next = self->head;
            self->head = batch->head;
        }
    }

    static PZ_INLINE void PzTaskBatchPushBackMany(PzTaskBatch* self, PzTaskBatch batch) {
        if (batch.head == NULL) {
            return;
        } else if (self->head == NULL) {
            *self = batch;
        } else {
            self->tail->next = batch.head;
            self->tail = batch.tail;
        }
    }

    static PZ_INLINE PzTask* PzTaskBatchPopFront(PzTaskBatch* self) {
        PzTask* task = self->head;
        if (PZ_LIKELY(task != NULL))
            self->head = task->next;
        return task;
    }

    static PZ_INLINE void PzTaskBatchPushFront(PzTaskBatch* self, PzTask* task) {
        PzTaskBatchPushFrontMany(self, PzTaskBatchFromTask(task));
    }

    static PZ_INLINE void PzTaskBatchPushBack(PzTaskBatch* self, PzTask* task) {
        PzTaskBatchPushBackMany(self, PzTaskBatchFromTask(task));
    }

    static PZ_INLINE void PzTaskBatchPush(PzTaskBatch* self, PzTask* task) {
        PzTaskBatchPushBack(self, task);
    }

    static PZ_INLINE PzTask* PzTaskBatchPop(PzTaskBatch* self) {
        PzTaskBatchPopFront(self);
    }

    static PZ_INLINE PzTaskBatchIter PzTaskBatchGetIter(PzTaskBatch* self) {
        return ((PzTaskBatchIter)({ .task = self->head }));   
    

    #define PZ_TASK_BUFFER_SIZE 256

    struct PzTaskThread {
        uintptr_t ptr;
        size_t runq_head;
        size_t runq_tail;
        uintptr_t runq_next;
        PzTask* runq_buffer[PZ_TASK_BUFFER_SIZE];
    };

    typedef struct PzTaskSchedulePtr {
        uintptr_t ptr;
    } PzTaskSchedulePtr;
    
    typedef enum PZ_TASK_SCHED_PTR_TYPE {
        PZ_TASK_SCHED_PTR_NONE = 0,
        PZ_TASK_SCHED_PTR_SPAWN = 1,
        PZ_TASK_SCHED_PTR_RESUME = 2,
    } PZ_TASK_SCHED_PTR_TYPE;

    static PZ_INLINE PZ_TASK_SCHED_PTR_TYPE PzTaskSchedulePtrGetType(PzTaskSchedulePtr self) {
        return self.ptr & 0b11;
    }

    static PZ_INLINE PzTaskWorker* PzTaskSchedulePtrGetWorker(PzTaskSchedulePtr self) {
        if (PZ_LIKELY(PzTaskSchedulePtrGetType(self) == PZ_TASK_SCHED_PTR_SPAWN))
            return ((PzTaskWorker*)(self.ptr & ~((uintptr_t) 0b11)));
        return NULL;
    }

    static PZ_INLINE PzTaskThread* PzTaskSchedulePtrGetThread(PzTaskSchedulePtr self) {
        if (PZ_LIKELY(PzTaskSchedulePtrGetType(self) == PZ_TASK_SCHED_PTR_RESUME))
            return ((PzTaskThread*)(self.ptr & ~((uintptr_t) 0b11)));
        return NULL;
    }

    typedef enum PZ_TASK_SCHED_TYPE {
        PZ_TASK_SCHED_FIFO = 0,
        PZ_TASK_SCHED_LIFO = 1,
        PZ_TASK_SCHED_UNBUFFERED = 2,
    } PZ_TASK_SCHED_TYPE;

    PZ_EXTERN PzTaskSchedulePtr PzTaskThreadSchedule(
        PzTaskThread* self,
        PzTaskBatch batch,
        PZ_TASK_SCHED_TYPE sched_type,
        uintptr_t sched_ptr
    );

    typedef enum PZ_TASK_POLL_TYPE {
        PZ_TASK_POLL_SELF = (1 << 0),
        PZ_TASK_POLL_NODE = (1 << 1),
        PZ_TASK_POLL_THREAD = (1 << 2),
    } PZ_TASK_POLL_TYPE;

    PZ_EXTERN PzTask* PzTaskThreadPoll(
        PzTaskThread* self,
        PZ_TASK_POLL_TYPE poll_type,
        uintptr_t poll_ptr
    );

    typedef enum PZ_TASK_SUSPEND_STATUS {
        PZ_TASK_SUSPEND_NOTIFIED = (1 << 0),
        PZ_TASK_SUSPEND_LAST_IN_NODE = (1 << 0),
        PZ_TASK_SUSPEND_LAST_IN_SCHED = (1 << 1)
    } PZ_TASK_SUSPEND_STATUS;

    PZ_EXTERN PZ_TASK_SUSPEND_STATUS PzTaskThreadSuspend(
        PzTaskThread* self
    );

    struct PzTaskWorker {
        uintptr_t ptr;
    };

    struct PzTaskNodeIter {
        PzTaskNode* start;
        PzTaskNode* current;
    };

    static PZ_INLINE PzTaskNode* PzTaskNodeIterNext(PzTaskNodeIter* self) {
        PzTaskNode* node = self->current;
        if (PZ_LIKELY(node != NULL)) {
            self->current = node->next;
            if (self->current == self->start)
                self->current = NULL;
        }
        return node;
    }

    struct PzTaskNode {
        PzTaskNode* next;
        size_t num_workers;
        PzTaskWorker[] workers;
        uintptr_t idle_queue;
        size_t active_workers;
    };

    static PZ_INLINE size_t PzTaskNodeGetWorkersLen(PzTaskNode* self) {
        return self->num_workers;
    }

    static PZ_INLINE PzTaskWorker[] PzTaskNodeGetWorkersPtr(PzTaskNode* self) {
        return self->workers;
    }

    static PZ_INLINE PzTaskNodeIter PzTaskNodeGetClusterIter(PzTaskNode* self) {
        return ((PzTaskNodeIter)({
            .start = self,
            .current = self
        }));
    }

    typedef enum PZ_TASK_RESUME_TYPE {
        PZ_TASK_RESUME_WAKING = (1 << 0),
        PZ_TASK_RESUME_ON_NODE = (1 << 1),
        PZ_TASK_RESUME_ON_SCHED = (1 << 2)

    } PZ_TASK_RESUME_TYPE;

    typedef struct PzTaskResumeResult {
        PZ_TASK_RESUME_TYPE type;
        PzTaskNode* node;
        PzTaskSchedulePtr ptr;
    } PzTaskResumeResult;

    PZ_EXTERN void PzTaskNodeResume(
        PzTaskNode* self,
        PzTaskResumeResult* result_ptr
    );

    PZ_EXTERN void PzTaskNodeUndoResume(
        PzTaskNode* self,
        PzTaskResumeResult* result_ptr
    );

    struct PzTaskCluster {
        PzNode* head;
        PzNode* tail;
    };

    #define PZ_TASK_CLUSTER_INIT    \
        ((PzTaskCluster)({          \
            .head = NULL,           \
            .tail = NULL            \
        }))

    static PZ_INLINE void PzTaskClusterInit(PzTaskCluster* self) {
        *self = PZ_TASK_CLUSTER_INIT;
    }

    static PZ_INLINE PzTaskCluster PzTaskClusterFromNode(PzTaskNode* node) {
        node->next = node;
        return ((PzTaskCluster)({
            .head = node,
            .tail = node
        }));
    }

    static PZ_INLINE void PzTaskClusterPushFrontMany(PzTaskCluster* self, PzTaskCluster cluster) {
        if (cluster.head == NULL) {
            return;
        } else if (self->head == NULL) {
            *self = cluster;
        } else {
            cluster.tail->next = self->head;
            self->head = cluster->head;
            self->tail->next = self->head;
        }
    }

    static PZ_INLINE void PzTaskClusterPushBackMany(PzTaskCluster* self, PzTaskCluster cluster) {
        if (cluster.head == NULL) {
            return;
        } else if (self->head == NULL) {
            *self = cluster;
        } else {
            self->tail->next = cluster.head;
            self->tail = cluster.tail;
            self->tail->next = self->head;
        }
    }

    static PZ_INLINE PzTaskNode* PzTaskClusterPopFront(PzTaskCluster* self) {
        PzTaskNode* node = self->head;
        if (PZ_LIKELY(node != NULL)) {
            self->head = node->next;
            node->next = node;
            if (self->head == node)
                self->head = NULL;
        }
        return node;
    }

    static PZ_INLINE void PzTaskClusterPushFront(PzTaskCluster* self, PzTaskNode* node) {
        PzTaskClusterPushFrontMany(self, PzTaskClusterFromNode(node));
    }

    static PZ_INLINE void PzTaskClusterPushBack(PzTaskCluster* self, PzTaskNode* node) {
        PzTaskClusterPushBackMany(self, PzTaskClusterFromNode(node));
    }

    static PZ_INLINE void PzTaskClusterPush(PzTaskCluster* self, PzTaskNode* node) {
        PzTaskClusterPushBack(self, node);
    }

    static PZ_INLINE PzTaskNode* PzTaskClusterPop(PzTaskCluster* self) {
        PzTaskClusterPopFront(self);
    }

    static PZ_INLINE PzTaskNodeIter PzTaskClusterGetIter(PzTaskCluster* self) {
        return ((PzTaskNodeIter)({
            .start = self->head,
            .current = self->head
        }));   
    }

    struct PzTaskScheduler {
        size_t active_nodes;
        PzTaskNode* start_node;
    };

    ////////////////////////////////////////////////////////////////////

    #ifdef __cplusplus
    }
    #endif
#endif // PZ_H