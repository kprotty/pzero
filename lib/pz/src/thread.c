#include "pz.h"

void PzTaskThreadInit(PzTaskThread* self, PzTaskWorker* worker) {
    PZ_UNREFERENCED_PARAMETER(self);
    PZ_UNREFERENCED_PARAMETER(worker);

    // TODO

    return;
}

void PzTaskThreadDestroy(PzTaskThread* self) {
    PZ_UNREFERENCED_PARAMETER(self);

    // TODO

    return;
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
    PzDebugAssert(resume_ptr != NULL, "invalid PzTaskResumeResult ptr");

    // Load the head and tail of the queue in order to add to its ring runq_buffer.
    // The tail can be loaded without synchronization as we should be the only producer thread.
    // The head needs to be synchronized with other stealer threads from PzTaskThreadPoll().
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

PzTask* PzTaskThreadPoll(PzTaskThread* self, PzTaskPollPtr poll_ptr) {
    PZ_UNREFERENCED_PARAMETER(self);
    PZ_UNREFERENCED_PARAMETER(poll_ptr);

    // TODO

    return NULL;
}

PZ_TASK_SUSPEND_STATUS PzTaskNodeSuspend(PzTaskNode* self, PzTaskThread* thread);

PZ_TASK_SUSPEND_STATUS PzTaskThreadSuspend(PzTaskThread* self) {
    PzTaskNode* node = PzTaskThreadGetNode(self);
    return PzTaskNodeSuspend(node, self);
}
