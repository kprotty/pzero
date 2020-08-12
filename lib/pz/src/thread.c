#include "pz.h"
#include "worker.h"

void PzTaskThreadInit(PzTaskThread* self, PzTaskWorker* worker) {
    PzDebugAssert(self != NULL, "invalid PzTaskThread ptr");
    PzDebugAssert(worker != NULL, "invalid PzTaskWorker ptr");

    // get the PzTaskNode* from the worker ptr
    PzTaskNode* node;
    uintptr_t worker_ptr = PzAtomicLoadAcquire(&worker->ptr);
    switch (PzTaskWorkerGetType(worker_ptr)) {
        case PZ_TASK_WORKER_SPAWNING: {
            node = (PzTaskNode*) PzTaskWorkerGetPtr(worker_ptr);
            break;
        }
        case PZ_TASK_WORKER_IDLE: {
            PzPanic("initialized with an idle worker");
            return;
        }
        case PZ_TASK_WORKER_RUNNING: {
            PzPanic("initialized with a worker already associated with another thread");
            return;
        }
        case PZ_TASK_WORKER_SHUTDOWN: {
            PzPanic("initialized with a worker which was already shutdown");
            return;
        }
    }

    // initialize the PzTaskThread*
    self->runq_head = 0;
    self->runq_tail = 0;
    self->runq_overflow = (uintptr_t)NULL;
    self->ptr = (uintptr_t)worker;
    self->node = node;

    // update the PzTaskWorker ptr with the new initialized PzTaskThread*.
    // release barrier so that other threads see the PzTaskThread writes above.
    worker_ptr = PzTaskWorkerInit(PZ_TASK_WORKER_RUNNING, (uintptr_t)self);
    PzAtomicStoreRelease(&worker->ptr, worker_ptr);
}

void PzTaskThreadDestroy(PzTaskThread* self) {
    // make sure the local runq buffer is empty
    uintptr_t tail = self->runq_tail;
    uintptr_t head = PzAtomicLoad(&self->runq_head);
    PzAssert(tail == head, "non empty run queue size of %zu", PZ_WRAPPING_SUB(tail, head));

    // make sure the local runq overflow stack is empty
    uintptr_t overflow = PzAtomicLoad(&self->runq_overflow);
    PzAssert(overflow == (uintptr_t)NULL, "non empty run queue overflow");
}

bool PzTaskThreadIsEmpty(PzTaskThread* self) {
    PzDebugAssert(self != NULL, "invalid PzTaskThread ptr");

    while (true) {
        uintptr_t head = PzAtomicLoad(&self->runq_head);
        uintptr_t tail = PzAtomicLoad(&self->runq_tail);
        uintptr_t overflow = PzAtomicLoad(&self->runq_overflow);

        // need to recheck the tail to retry it all if it changed during the overflow load above.
        // no need to recheck the head as it only checks for emptiness rather than computing the size.
        uintptr_t current_tail = PzAtomicLoad(&self->runq_tail);
        if (tail != current_tail) {
            PzYieldHardwareThread();
            continue;
        }

        // returns true if the buffer and overflow queues are both empty
        return (tail == head) || (overflow == 0);
    }
}

/// Write a task pointer to the thread's run queue buffer by wrapping the index.
/// Uses atomic operations as there could be potential racy reads from stealer threads.
static PZ_INLINE void PzTaskThreadBufferWrite(PzTaskThread* self, uintptr_t index, PzTask* task) {
    PzDebugAssert(task != NULL, "null task pointer being written to the runq_buffer");
    PzAtomicPtr* ptr = &self->runq_buffer[index % PZ_TASK_BUFFER_SIZE];
    PzAtomicStoreUnordered(ptr, (uintptr_t) task);
}

/// Reads a task pointer from the thread's run queue buffer by wrapping the index.
/// Uses atomic operations as the caller may be a stealing thread which races with the producer thread.
static PZ_INLINE PzTask* PzTaskThreadBufferRead(PzTaskThread* self, uintptr_t index) {
    PzAtomicPtr* ptr = &self->runq_buffer[index % PZ_TASK_BUFFER_SIZE];
    PzTask* task = (PzTask*) PzAtomicLoadUnordered(ptr);
    PzDebugAssert(task != NULL, "null task pointer read from runq_buffer");
    return task;
}

/// Push a batch of tasks onto the current thread's run queue.
/// This assumes that the caller is the producer/owner thread (there can only be one).
/// This should ideally be followed by a call to PzTaskNodeResume() to wake up a thread to handle the newly submitted tasks.
void PzTaskThreadPush(PzTaskThread* self, PzTaskBatch batch) {
    PzDebugAssert(self != NULL, "invalid PzTaskThread ptr");

    // Load the head and tail of the queue in order to add to its ring runq_buffer.
    // The tail can be loaded without synchronization as we should be the only producer thread.
    // The head needs to be synchronized with other stealer threads from PzTaskThreadPollSteal().
    uintptr_t tail = self->runq_tail;
    uintptr_t head = PzAtomicLoad(&self->runq_head);

    // Try to push all the tasks in the batch to this thread's run queue
    while (!PzTaskBatchIsEmpty(&batch)) {
        uintptr_t size = PZ_WRAPPING_SUB(tail, head);
        PzAssert(size <= PZ_TASK_BUFFER_SIZE, "invalid run queue size of %zu", size);

        uintptr_t remaining = PZ_TASK_BUFFER_SIZE - size;
        if (PZ_LIKELY(remaining != 0)) {

            // write as many tasks as possible from the batch into the runq_buffer
            uintptr_t new_tail = tail;
            for (PzTask* task = PzTaskBatchPop(&batch); task != NULL && remaining > 0; --remaining) {
                PzTaskThreadBufferWrite(self, new_tail, task);
                new_tail = PZ_WRAPPING_ADD(new_tail, 1);
            }

            // update the tail pointer if we wrote anything to the buffer.
            // release barrier to ensure that stealer threads see:
            //  - valid PzTask pointers in the runq_buffer when stealing
            //  - valid writes to the PzTask's in the batch when polling
            if (new_tail != tail) {
                tail = new_tail;
                PzAtomicStoreRelease(&self->runq_tail, new_tail);
            }

            // reload the head to try and write more in case stealer threads made more room
            PzYieldHardwareThread();
            head = PzAtomicLoad(&self->runq_head);
            continue;
        }

        // the runq_buffer is currently full!
        // try to migrate half of it so that future calls only have to do the cheap atomic store above.
        // the "buffer full" condition amortizes the cost of this synchronization with stealer threads. 
        // acquire barrier to ensure that the task writes below to create the `overflow_batch` arent re-ordered before the steal.
        uintptr_t steal = PZ_TASK_BUFFER_SIZE / 2;
        uintptr_t new_head = PZ_WRAPPING_ADD(head, steal);
        if (!PzAtomicCasAcquire(&self->runq_head, &head, new_head))
            continue;
        
        // Add the stolen tasks to the front of the batch 
        // given they were scheduled into the runq first.
        PzTaskBatch overflow_batch = PzTaskBatchInit(NULL);
        for (; head != new_head; head = PZ_WRAPPING_ADD(head, 1)) {
            PzTask* task = PzTaskThreadBufferRead(self, head);
            PzTaskBatchPush(&overflow_batch, task);
        }
        PzTaskBatchPushFrontMany(&batch, overflow_batch);
        
        // finally, push the batch of remaining tasks to the runq_overflow stack
        uintptr_t overflow = PzAtomicLoad(&self->runq_overflow);
        while (true) {
            batch.tail->next = (PzTask*)overflow;
            uintptr_t new_overflow = (uintptr_t)batch.head;

            // use a cheaper atomic store instead of atomic cmpxchg if the stack is empty as we're its only producers.
            // release barrier to ensure that stealer threads see valid tasks (see the release barrier note above). 
            bool overflowed = batch.tail->next == NULL;
            if (overflowed) {
                PzAtomicStoreRelease(&self->runq_overflow, new_overflow);
            } else {
                overflowed = PzAtomicCasRelease(&self->runq_overflow, &overflow, new_overflow);
            }

            // empty out the batch after pushing it to the overflow stack, ending the parent loop
            if (overflowed) {
                batch = PzTaskBatchInit(NULL);
                break;
            }
        }
    }
}

void PzTaskThreadInject(PZ_NOALIAS(PzTaskThread*) self, PZ_NOALIAS(PzTask*) runq) {
    PzDebugAssert(self != NULL, "invalid PzTaskThread ptr");

    // nothing to do if there's no runq provided
    if (PZ_UNLIKELY(runq == NULL))
        return;

    uintptr_t tail = self->runq_tail;
    uintptr_t head = PzAtomicLoad(&self->runq_head);
    uintptr_t size = PZ_WRAPPING_SUB(tail, head);
    PzAssert(size <= PZ_TASK_BUFFER_SIZE, "invalid run queue size of %zu", size);
    
    // push tasks from the provided runq into the thread local runq buffer
    uintptr_t new_tail = tail;
    for (
        uintptr_t remaining = PZ_TASK_BUFFER_SIZE - size;
        (remaining != 0) && (runq != NULL);
        --remaining, runq = runq->next
    ) {
        PzTaskThreadBufferWrite(self, new_tail, runq);
        new_tail = PZ_WRAPPING_ADD(new_tail, 1);
    }

    // update the local runq buffer pointer with new tasks if there were any
    // release barrier to ensure that stealer threads see valid PzTask* writes when they try to steal.
    if (new_tail != tail)
        PzAtomicStoreRelease(&self->runq_tail, new_tail);

    // add any remaining tasks to the local runq overflow if there were any.
    // release barrier for the same reasoning as stated above.
    if (runq != NULL)
        PzAtomicStoreRelease(&self->runq_overflow, (uintptr_t)runq);
}

void PzTaskThreadNodePush(PzTaskNode* self, PzTaskBatch batch) {
    PzDebugAssert(self != NULL, "invalid PzTaskNode ptr");

    // nothing to do if there's nothing to push
    if (PZ_UNLIKELY(PzTaskBatchIsEmpty(&batch)))
        return;

    // push the entire batch to the top of the runq stack.
    // release barrier on success so that callers of PzTaskThreadPollGlobal() 
    //  which use an acquire barrier see the correct PzTask writes done to those in the batch.
    uintptr_t runq = PzAtomicLoad(&self->runq);
    do {
        batch.tail->next = (PzTask*)runq;
    } while (!PzAtomicCasRelease(&self->runq, &runq, (uintptr_t)batch.head));
}

PzTask* PzTaskThreadPollGlobal(PZ_NOALIAS(PzTaskThread*) self, PZ_NOALIAS(PzTaskNode*) node) {
    PzDebugAssert(self != NULL, "invalid PzTaskThread ptr");
    PzDebugAssert(node != NULL, "invalid PzTaskNode ptr");

    // If our local queue isnt empty, then we cant inject so we cant steal from the node.
    // If we do try to steal, then we could overflow into runq_overflow with remaining tasks.
    // runq_overflow requires knowing the tail in order to link it together which takes O(runq) to figure out.
    if (!PzTaskThreadIsEmpty(self))
        return NULL;

    // try to grab the entire runq in one go
    // acquire barrier on success to see the correct PzTask writes released by PzTaskNodePush()
    uintptr_t node_runq = PzAtomicLoad(&node->runq);
    do {
        PzTask* runq = (PzTask*) node_runq;
        if (runq == NULL)
            return NULL;
    } while (!PzAtomicCasAcquire(&node->runq, &node_runq, (uintptr_t)NULL));

    // return the first task from the PzTask* stack we stole 
    //  and inject the rest into our thread local runq
    PzTask* first_task = (PzTask*) node_runq;
    PzTaskThreadInject(self, first_task->next);
    return first_task;
}

PzTask* PzTaskThreadPollLocal(PzTaskThread* self) {
    // The tail can be loaded without synchronization as we should be the only producer thread.
    // The head needs to be synchronized with other stealer threads from PzTaskThreadPoll().
    uintptr_t tail = self->runq_tail;
    uintptr_t head = PzAtomicLoad(&self->runq_head);

    // try to pop a task from our local runq buffer, bailing if its empty
    while (true) {
        uintptr_t size = PZ_WRAPPING_SUB(tail, head);
        PzAssert(size <= PZ_TASK_BUFFER_SIZE, "invalid run queue size of %zu", size);
        if (size == 0)
            break;

        // compare exchange to synchronize with stealer threads who are also trying to pop from runq buffer.
        // acquire barrier to prevent task load below from being re-ordered above this CAS.
        // if re-ordered, could possibly access task memory later on that was stolen by another thread.
        uintptr_t new_head = PZ_WRAPPING_ADD(head, 1);
        if (!PzAtomicCasAcquire(&self->runq_head, &head, new_head))
            continue;
        
        PzTask* task = PzTaskThreadBufferRead(self, head);
        return task;
    }

    // our local runq buffer was empty, look to our overflow stack.
    uintptr_t overflow = PzAtomicLoad(&self->runq_overflow);
    while (true) {
        PzTask* runq = (PzTask*) overflow;
        if (runq == NULL)
            break;

        // grab the entire overflow
        uintptr_t new_overflow = (uintptr_t)NULL;
        if (!PzAtomicCasAcquire(&self->runq_overflow, &overflow, new_overflow))
            continue;

        // will be returning the first task from the runq
        // and pushing the remaining tasks into the local runq buffer + possibly runq overflow again.
        PzTask* first_task = runq;
        PzTaskThreadInject(self, first_task->next);
        return first_task;
    }

    // there were no tasks in the local runq buffer and in the runq overflow stack
    return NULL;
}

PzTask* PzTaskThreadPollSteal(PZ_NOALIAS(PzTaskThread*) self, PZ_NOALIAS(PzTaskThread*) target) {
    // get the size of the current local runq buffer to figure out how to steal from the target
    uintptr_t tail = self->runq_tail;
    uintptr_t head = PzAtomicLoad(&self->runq_head);
    uintptr_t size = PZ_WRAPPING_SUB(head, tail);
    PzDebugAssert(size <= PZ_TASK_BUFFER_SIZE, "invalid run queue size of %zu", size);

    // we shouldnt try to steal if our runq buffer is full, even if our runq overflow stack isnt full.
    // stealing into our overflow stack decreases the visibilty time window of tasks in the scheduler:
    //  when we steal an overflow stack from another thread, we need to add them to our runq buffer then make them available.
    //  the period of time when adding and before theyre made available could be time other threads were searching for tasks.
    //  its then better to keep the tasks stealing by multiple threads in the buffer rather than to serialize thread steals via the overflow stack. 
    if (size == PZ_TASK_BUFFER_SIZE)
        return NULL;

    // acquire barrier on the head & tail loads of the target in order to ensure write visibility to its runq buffer tasks.
    while (true) {
        uintptr_t target_head = PzAtomicLoadAcquire(&target->runq_head);
        uintptr_t target_tail = PzAtomicLoadAcquire(&target->runq_tail);

        // we will try to steal half of the target's runq buffer tasks in order to not saturate other stealer threads.
        uintptr_t target_size = PZ_WRAPPING_SUB(target_tail, target_head);
        uintptr_t steal = target_size - (target_size / 2);

        // retry if the queue size changed drastically between the head & tail loads.
        if (target_size > PZ_TASK_BUFFER_SIZE) {
            PzYieldHardwareThread();
            continue;
        }

        // if the target runq buffer is empty, try to steal its runq oevrflow stack
        if (steal == 0) {
            uintptr_t target_overflow = PzAtomicLoad(&target->runq_overflow);
            if (((PzTask*)target_overflow) == NULL)
                break;

            // dont try to steal the target runq overflow if ours isnt empty as it needs to be for Inject()
            PzTask* overflow = (PzTask*) PzAtomicLoad(&self->runq_overflow);
            if (overflow == NULL)
                break;

            // give time for any other thread running on our physical cpu core to try and steal before we do.
            // if the target thread is running on our physical cpu core and is checking overflow, then it prevents task thread migration.
            PzYieldHardwareThread(); 

            // steal from the target runq overflow stack, return the first task & push the rest to our
            // acquire barrier to ensure that we read valid PzTask* writes from the overflow stack
            if (PzAtomicCasAcquire(&target->runq_overflow, &target_overflow, (uintptr_t)NULL)) {
                PzTask* first_task = (PzTask*) target_overflow;
                PzTaskThreadInject(self, first_task->next);
                return first_task;
            }

            // failed to steal the target's runq overflow stack, retry the entire steal.
            // retrying also catches the case if new tasks were added to the target's runq buffer as well.
            PzYieldHardwareThread();
            continue;
        }

        // we will be returning the first task stolen from the target's runq buffer
        PzTask* first_task = PzTaskThreadBufferRead(target, target_head);
        uintptr_t new_target_head = PZ_WRAPPING_ADD(target_head, 1);
        steal -= 1;
        
        // copy tasks from the target runq buffer into our runq buffer in a racy fashion.
        // "racy" in that the target thread could be writing over the tasks we're reading.  
        uintptr_t new_tail = tail;
        for (; steal != 0; --steal) {
            PzTask* task = PzTaskThreadBufferRead(target, new_target_head);
            new_target_head = PZ_WRAPPING_ADD(new_target_head, 1);

            PzTaskThreadBufferWrite(self, new_tail, task);
            new_tail = PZ_WRAPPING_ADD(new_tail, 1);
        }

        // try to commit the atomic steal from the target runq buffer to our runq buffer.
        // release barrier to ensure that the copying above is not re-ordered after the steal. 
        if (!PzAtomicCasRelease(&target->runq_head, &target_head, new_target_head)) {
            PzYieldHardwareThread();
            continue;
        }

        // publish the tasks written to our runq buffer if we stole any.
        // release barrier so stealer threads see valid PzTask writes in the runq buffer.
        if (new_tail != tail)
            PzAtomicStoreRelease(&self->runq_tail, new_tail);

        // return the first task we stole out of the target's runq buffer.
        return first_task;
    }

    // the target's runq buffer and runq overflow stack appeared to be empty.
    return NULL;
}

PzTask* PzTaskThreadPoll(PzTaskThread* self, PzTaskPollPtr poll_ptr, PzTaskResumeResult* result) {
    PzDebugAssert(self != NULL, "invalid PzTaskThread ptr");
    PzDebugAssert(result != NULL, "invalid PzTaskResumeResult ptr");

    PzTask* task = NULL;
    bool is_remote = false;

    // search for a task  using the poll_ptr type
    switch (PzTaskPollPtrGetType(poll_ptr)) {
        case PZ_TASK_POLL_THREAD: {
            PzTaskThread* thread = (PzTaskThread*) PzTaskPollPtrGetPtr(poll_ptr);
            is_remote = thread != self;
            task = is_remote ? PzTaskThreadPollSteal(self, thread) : PzTaskThreadPollLocal(self);
            break;
        }

        case PZ_TASK_POLL_NODE: {
            PzTaskNode* node = (PzTaskNode*) PzTaskPollPtrGetPtr(poll_ptr);
            is_remote = true;
            task = PzTaskThreadPollGlobal(self, node);
            break;
        }
    }

    // if we found a task 
    PzTaskResumeResult none = { 0 };
    *result = none;
    if (task != NULL && is_remote) {
        PzTaskNode* node = PzTaskThreadGetNode(self);
        PzTaskNodeResumeAnyThread(node, result, is_waking);
    }

    return task;
}

PZ_TASK_SUSPEND_STATUS PzTaskNodeSuspend(PzTaskNode* self, PzTaskThread* thread);

PZ_TASK_SUSPEND_STATUS PzTaskThreadSuspend(PzTaskThread* self) {
    PzDebugAssert(self != NULL, "invalid PzTaskThread ptr");
    PzTaskNode* node = PzTaskThreadGetNode(self);
    return PzTaskNodeSuspend(node, self);
}
