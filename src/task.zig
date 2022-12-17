const std = @import("std");
const queue = @import("queue.zig");
const assert = std.debug.assert;

pub const Task = extern struct {
    node: queue.Node = .{},
    callback: Callback,

    pub const Context = *Worker;

    pub const Callback = fn (*Task, Context) callconv(.C) void;

    pub fn init(callback: Callback) Task {
        return .{ .callback = callback };
    }

    pub const Info = extern struct {
        current: u16,
        idle: u16,
        active: u16,
        max: u16,
    };

    pub fn info(context: Context) Info {

    }

    pub const Scope = union(enum) {
        to_any,
        to_current,
        to_specific: u16,
    };

    pub fn submit(context: Context, task: *Task, scope: Scope) void {

    }

    pub const Batch = extern struct {
        list: queue.List,
        worker: *Worker,
        producer: u32,
        submit_fn: fn (*const Batch) callconv(.C) bool,

        pub fn init(context: Context, scope: Scope) error{InvalidScope}!Batch {
            var batch: Batch = undefined;
            batch.list = .{};
            batch.worker = context;
            batch.producer = 0;
            batch.submit_fn = Batch.submit_to_current;
            
            switch (scope) {
                .to_any => {
                    batch.producer = context.runnable.prepare();
                    batch.submit_fn = Batch.submit_to_any;
                },
                .to_current => {},
                .to_specific => |id| {
                    if (context.id != id) {
                        batch.worker = context.scheduler.get_worker(id) orelse return error.InvalidScope;
                        batch.submit_fn = Batch.submit_to_specific;
                    }
                }
            }

            return batch;
        }

        pub fn push(noalias batch: *Batch, noalias task: *Task) void {
            const node = &task.node;
            if (batch.producer != 0) {
                if (batch.worker.runnable.enqueue(&batch.producer, node)) {
                    return;
                }
            }

            assert(batch.list.push(queue.List.from(node)));
        }

        pub fn pop(batch: *Batch) ?*Task {
            const node = batch.list.pop() orelse blk: {
                if (batch.producer == 0) return null;
                break :blk batch.worker.runnable.dequeue(&batch.producer) orelse return null;
            };

            return @fieldParentPtr(Task, "node", node);
        }

        pub fn submit(batch: *const Batch) bool {
            return batch.submit_fn(batch);
        }

        fn submit_to_any(batch: *const Batch) callconv(.C) bool {
            var submitted = batch.worker.runnable.commit(batch.producer);
            if (batch.worker.overflowed.push(batch.list)) {
                submitted = true;
            }

            if (submitted) {
                const sync = @atomicLoad(u32, &batch.worker.scheduler.sync, .Monotonic);
                batch.worker.scheduler.notify_with(sync);
            }

            return submitted;
        }

        fn submit_to_current(batch: *const Batch) callconv(.C) bool {
            return batch.worker.local.push(batch.list);
        }

        fn submit_to_specific(batch: *const Batch) callconv(.C) bool {
            const submitted = batch.worker.injected.push(batch.list);
            if (submitted) {
                batch.worker.
            }
        }
    };
};

