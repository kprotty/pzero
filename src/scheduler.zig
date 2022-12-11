const std = @import("std");
const builtin = @import("builtin");
const assert = std.debug.assert;

const queue = @import("queue.zig");
const atomic = @import("atomic.zig");
const random = @import("random.zig");
const blocking = @import("blocking.zig");

pub const Task = extern struct {
    node: Node = .{},
    callback: switch (builtin.zig_backend) {
        .stage1 => fn (*Task) callconv(.C) void,
        else => *const fn (*Task) callconv(.C) void,
    },
};

pub const Executor = extern struct {
    comptime { assert(@bitSizeOf(Sync) == @bitSizeOf(u32)); }
    const Sync = packed struct {
        shutdown: bool = false,
        searching: bool = false,
        idle: u10 = 0,
        spawned: u10 = 0,
        max_spawn: u10 = 0,
    };

    const worker_limit = std.math.maxInt(u10);
    comptime {
        const MaxSpawnType = @TypeOf(@as(Sync, undefined).max_spawn);
        assert(worker_limit <= std.math.maxInt(MaxSpawnType));
    }

    sync: u32,
    _sync_padding: atomic.CachePadding(u32) = undefined,

    injector: queue.Injector = .{},
    _injector_padding: atomic.CachePadding(queue.Injector) = undefined,

    _reserved: usize = 0,
    workers: [worker_limit]?*Worker = [_]?*Worker{ null } ** worker_limit,

    pub fn run(noalias executor: *Executor, noalias task: *Task, max_workers: usize) void {
        var max_spawn = std.math.maxInt(u10);
        if (max_workers < max_spawn) {
            max_spawn = @intCast(u10, max_workers);
        }

        executor.* = Executor{ .sync = @bitCast(u32, Sync{ .max_spawn = max_spawn }) };
    }
};

pub const Worker = extern struct {
    buffer: queue.Buffer = .{},
    _buffer_padding: atomic.CachePadding(queue.Buffer) = undefined,

    

    executor: *Executor,

    fn run(executor: *Executor, worker_id: u32) void {
        var worker = 
    }
};

