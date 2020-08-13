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
    #if defined(PZ_CPP)
    extern "C" {
    #endif

    #include "base.h"
    #include "atomic.h"

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

    typedef void* (*PzTaskCallback)(
        PzTask* self,
        PzTaskThread* caller_thread
    );

    struct PzTask {
        PzTask* next;
        PzTaskCallback callback;
    };

    static PZ_INLINE PzTask PzTaskInit(PzTaskCallback callback) {
        PzTask self;
        self.callback = callback;
        return self;
    }

    struct PzTaskBatchIter {
        PzTask* task;
    };

    static PZ_INLINE PzTask* PzTaskBatchIterNext(PzTaskBatchIter* self) {
        PzTask* task = self->task;
        if (PZ_LIKELY(task != NULL))
            self->task = task->next;
        return task;
    }

    struct PzTaskBatch {
        PzTask* head;
        PzTask* tail;
    };

    static PZ_INLINE PzTaskBatch PzTaskBatchInit(PzTask* task) {
        if (task != NULL)
            task->next = NULL;
        PzTaskBatch self;
        self.head = task;
        self.tail = task;
        return self;
    }

    static PZ_INLINE bool PzTaskBatchIsEmpty(PzTaskBatch* self) {
        return self->head == NULL;
    }

    static PZ_INLINE void PzTaskBatchPushFrontMany(PzTaskBatch* self, PzTaskBatch batch) {
        if (batch.head == NULL) {
            return;
        } else if (self->head == NULL) {
            *self = batch;
        } else {
            batch.tail->next = self->head;
            self->head = batch.head;
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
        PzTaskBatchPushFrontMany(self, PzTaskBatchInit(task));
    }

    static PZ_INLINE void PzTaskBatchPushBack(
        PZ_NOALIAS(PzTaskBatch*) self,
        PZ_NOALIAS(PzTask*) task
    ) {
        PzTaskBatchPushBackMany(self, PzTaskBatchInit(task));
    }

    static PZ_INLINE void PzTaskBatchPush(
        PZ_NOALIAS(PzTaskBatch*) self,
        PZ_NOALIAS(PzTask*) task
    ) {
        PzTaskBatchPushBack(self, task);
    }

    static PZ_INLINE PzTask* PzTaskBatchPop(PzTaskBatch* self) {
        return PzTaskBatchPopFront(self);
    }

    static PZ_INLINE PzTaskBatchIter PzTaskBatchGetIter(PzTaskBatch* self) {
        PzTaskBatchIter iter = { .task = self->head };
        return iter;   
    }

    struct PzTaskWorker {
        PzAtomicPtr ptr;
    };

    #define PZ_TASK_WORKER_MAX ((UINTPTR_MAX >> 8) - 1)

    struct PzTaskNode {
        PzTaskNode* next;
        PzTaskWorker* workers;
        size_t num_workers;
        PzTaskScheduler* scheduler;
        PZ_CACHE_ALIGN PzAtomicPtr runq;
        PZ_CACHE_ALIGN PzAtomicPtr idle_queue;
        PZ_CACHE_ALIGN PzAtomicPtr active_threads;
    };

    PZ_EXTERN void PzTaskNodeInit(
        PZ_NOALIAS(PzTaskNode*) self,
        PZ_NOALIAS(PzTaskWorker*) workers,
        size_t num_workers
    );

    PZ_EXTERN void PzTaskNodeDestroy(
        PzTaskNode* self
    );

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

    static PZ_INLINE PzTaskNodeIter PzTaskNodeGetClusterIter(PzTaskNode* self) {
        PzTaskNodeIter iter;
        iter.start = self;
        iter.current = self;
        return iter;
    }

    static PZ_INLINE size_t PzTaskNodeGetWorkersLen(const PzTaskNode* self) {
        return self->num_workers;
    }

    static PZ_INLINE PzTaskWorker* PzTaskNodeGetWorkersPtr(const PzTaskNode* self) {
        return self->workers;
    }

    static PZ_INLINE PzTaskScheduler* PzTaskNodeGetScheduler(const PzTaskNode* self) {
        return self->scheduler;
    }

    PZ_EXTERN void PzTaskNodePush(
        PzTaskNode* self,
        PzTaskBatch batch
    );

    typedef struct PzTaskSchedulePtr {
        uintptr_t ptr;
    } PzTaskSchedulePtr;
    
    typedef enum PZ_TASK_SCHED_PTR_TYPE {
        PZ_TASK_SCHED_PTR_NONE = 0,
        PZ_TASK_SCHED_PTR_SPAWN = 1,
        PZ_TASK_SCHED_PTR_RESUME = 2,
    } PZ_TASK_SCHED_PTR_TYPE;

    static PZ_INLINE PZ_TASK_SCHED_PTR_TYPE PzTaskSchedulePtrGetType(PzTaskSchedulePtr self) {
        return self.ptr & 3;
    }

    static PZ_INLINE uintptr_t PzTaskSchedulePtrGetPtr(PzTaskSchedulePtr self) {
        return self.ptr & ~((uintptr_t) 3);
    }

    typedef enum PZ_TASK_RESUME_STATUS {
        PZ_TASK_RESUME_NOTIFIED = (1 << 0),
        PZ_TASK_RESUME_FIRST_IN_NODE = (1 << 1),
        PZ_TASK_RESUME_FIRST_IN_SCHEDULER = (1 << 2),
    } PZ_TASK_RESUME_STATUS;

    typedef struct PzTaskResumeResult {
        uintptr_t node_ptr;
        PzTaskSchedulePtr sched_ptr;
    } PzTaskResumeResult;

    static PZ_INLINE PzTaskSchedulePtr PzTaskResumeResultGetSchedulePtr(PzTaskResumeResult* self) {
        return self->sched_ptr;
    }

    static PZ_INLINE PzTaskNode* PzTaskResumeResultGetNode(PzTaskResumeResult* self) {
        return (PzTaskNode*)(self->node_ptr & ~((uintptr_t)3));
    }

    static PZ_INLINE PZ_TASK_RESUME_STATUS PzTaskResumeResultGetStatus(PzTaskResumeResult* self) {
        return self->node_ptr & 3;
    }

    typedef enum PZ_TASK_RESUME_TYPE {
        PZ_TASK_RESUME_ON_NODE = 0,
        PZ_TASK_RESUME_ON_SCHEDULER = 1,
    } PZ_TASK_RESUME_TYPE;

    PZ_EXTERN void PzTaskNodeResume(
        PZ_NOALIAS(PzTaskNode*) self,
        PZ_TASK_RESUME_TYPE resume_type,
        PZ_NOALIAS(PzTaskResumeResult*) resume_result
    );

    PZ_EXTERN void PzTaskNodeUndoResume(
        PZ_NOALIAS(PzTaskNode*) self,
        PZ_NOALIAS(PzTaskResumeResult*) resume_result
    );

    #define PZ_TASK_BUFFER_SIZE 256

    struct PzTaskThread {
        PzAtomicPtr runq_head;
        PzAtomicPtr runq_tail;
        PzAtomicPtr runq_overflow;
        PZ_CACHE_ALIGN PzAtomicPtr runq_buffer[PZ_TASK_BUFFER_SIZE];
        PzAtomicPtr ptr;
        PzTaskNode* node;
    };

    PZ_EXTERN void PzTaskThreadInit(
        PZ_NOALIAS(PzTaskThread*) self,
        PZ_NOALIAS(PzTaskWorker*) worker
    );

    PZ_EXTERN void PzTaskThreadDestroy(
        PzTaskThread* self
    );

    static PZ_INLINE PzTaskNode* PzTaskThreadGetNode(PzTaskThread* self) {
        return self->node;
    }

    PZ_EXTERN bool PzTaskThreadIsEmpty(
        const PzTaskThread* self
    );

    PZ_EXTERN void PzTaskThreadPush(
        PzTaskThread* self,
        PzTaskBatch batch
    );

    typedef struct PzTaskPollPtr {
        uintptr_t ptr;
    } PzTaskPollPtr;

    typedef enum PZ_TASK_POLL_TYPE {
        PZ_TASK_POLL_THREAD = 0,
        PZ_TASK_POLL_NODE = 1
    } PZ_TASK_POLL_TYPE;

    static PZ_INLINE PzTaskPollPtr PzTaskPollPtrInit(PZ_TASK_POLL_TYPE poll_type, uintptr_t ptr) {
        PzTaskPollPtr self = { ptr | poll_type };
        return self;
    }

    static PZ_INLINE PZ_TASK_POLL_TYPE PzTaskPollPtrGetType(PzTaskPollPtr self) {
        return (PZ_TASK_POLL_TYPE)(self.ptr & 1);
    }

    static PZ_INLINE uintptr_t PzTaskPollPtrGetPtr(PzTaskPollPtr self) {
        return self.ptr & ~((uintptr_t)1);
    }

    PZ_EXTERN PzTask* PzTaskThreadPoll(
        PZ_NOALIAS(PzTaskThread*) self,
        PzTaskPollPtr poll_ptr,
        PZ_NOALIAS(PzTaskResumeResult*) resume_result
    );

    typedef enum PZ_TASK_SUSPEND_STATUS {
        PZ_TASK_SUSPEND_WAIT = (1 << 0),
        PZ_TASK_SUSPEND_NOTIFIED = (1 << 1),
        PZ_TASK_SUSPEND_LAST_IN_NODE = (1 << 2),
        PZ_TASK_SUSPEND_LAST_IN_SCHEDULER = (1 << 3)
    } PZ_TASK_SUSPEND_STATUS;

    PZ_EXTERN PZ_TASK_SUSPEND_STATUS PzTaskThreadSuspend(
        PzTaskThread* self
    );

    struct PzTaskCluster {
        PzTaskNode* head;
        PzTaskNode* tail;
    };

    static PZ_INLINE PzTaskCluster PzTaskClusterInit(PzTaskNode* node) {
        if (node != NULL)
            node->next = node;
        PzTaskCluster self;
        self.head = node;
        self.tail = node;
        return self;
    }

    static PZ_INLINE bool PzTaskClusterIsEmpty(PzTaskCluster* self) {
        return self->head == NULL;
    }

    static PZ_INLINE void PzTaskClusterPushFrontMany(PzTaskCluster* self, PzTaskCluster cluster) {
        if (cluster.head == NULL) {
            return;
        } else if (self->head == NULL) {
            *self = cluster;
        } else {
            cluster.tail->next = self->head;
            self->head = cluster.head;
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
        PzTaskClusterPushFrontMany(self, PzTaskClusterInit(node));
    }

    static PZ_INLINE void PzTaskClusterPushBack(PzTaskCluster* self, PzTaskNode* node) {
        PzTaskClusterPushBackMany(self, PzTaskClusterInit(node));
    }

    static PZ_INLINE void PzTaskClusterPush(PzTaskCluster* self, PzTaskNode* node) {
        PzTaskClusterPushBack(self, node);
    }

    static PZ_INLINE PzTaskNode* PzTaskClusterPop(PzTaskCluster* self) {
        return PzTaskClusterPopFront(self);
    }

    static PZ_INLINE PzTaskNodeIter PzTaskClusterGetIter(PzTaskCluster* self) {
        PzTaskNodeIter iter;
        iter.start = self->head;
        iter.current = iter.start;
        return iter;
    }

    struct PzTaskScheduler {
        PzTaskNode* start_node;
        PzAtomicPtr active_nodes;
    };

    ////////////////////////////////////////////////////////////////////

    #if defined(PZ_CPP)
    }
    #endif
#endif // PZ_H
