const std = @import("std");
const queue = @import("queue.zig");
const futex = @import("futex.zig");
const atomic = @import("atomic.zig");
const scheduler = @import("scheduler.zig");

pub fn submit(noalias task: *Task) void {

}

const Ready = extern struct {
    injector: queue.Injector = .{},
    buffer: queue.Buffer = .{},
};

