const std = @import("std");
const builtin = @import("builtin");
const assert = std.debug.assert;

const platform = switch (builtin.target.os) {
    .windows => @import("platform/windows.zig"),
    .linux => @import("platform/linux.zig"),
    else => @import("platform/posix.zig"),
};

pub const Task = extern struct {
    next: ?*Task = null,
    callback: fn (*Task, *Worker) callconv(.C) void,
};

pub const Scheduler = extern struct {
    sync: u32,
    _sync_padding: [std.atomic.cache_line - @sizeOf(u32)]u8 = undefined,

    idle: Idle = .{},
    polling: Polling = .{},

    workers: [capacity]?*Worker = [_]?*Worker{ null } ** capacity,
    reactor: platform.Reactor,

    const Sync = packed struct {
        shutdown: bool = false,
        joining: bool = false,
        idle: u10 = 0,
        spawned: u10 = 0,
        max: u10 = 0,
    };
};

const Worker = extern struct {
    injector: Injector = .{},
    buffer: Buffer = .{},

    scheduler: *Scheduler,
    random

    fn submit(noalias worker: *Worker, noalias task: *Task) void {
        worker.buffer.push(task, &worker.injector);
        worker.scheduler.notify();
    }

    fn inject(noalias worker: *Worker, noalias task: *Task) void {

    }

    fn steal(noalias worker: *Worker)
};

const Idle = extern struct {

};

const Polling = extern struct {
    state: u32,

    const State = packed struct {
        active: bool = false,
        acquired: bool = false,
        pending: u30 = 0,
    };

    fn init(active: bool) Polling {
        return .{ .state = @bitCast(u32, State{ .active = active }) };
    }

    fn ref(polling: *Polling) void {
        const one_pending = @bitCast(u32, State{ .pending = 1 });
        const state = @bitCast(State, @atomicRmw(u32, &polling.state, .Add, one_pending, .Acquire));

        assert(state.pending < std.math.maxInt(u30));
        assert(state.active);
    }

    fn unref(polling: *Polling) void {
        const one_pending = @bitCast(u32, State{ .pending = 1 });
        const state = @bitCast(State, @atomicRmw(u32, &polling.state, .Sub, one_pending, .Release));

        assert(state.pending > 0);
        assert(state.active);
    }

    fn acquire(noalias polling: *Polling, noalias random: *Random) bool {
        while (true) : (random.backoff()) {
            const state = @bitCast(State, @atomicLoad(u32, &polling.state, .Monotonic));
            if (!state.active or state.acquired or state.pending == 0) {
                return false;
            }

            const one_acquired = @bitCast(u32, State{ .acquired = true });
            state = @bitCast(State, @atomicRmw(u32, &polling.state, .Or, one_acquired, .Acquire));
            if (!state.acquired) {
                return true;
            }
        }
    }

    fn release(polling: *Polling, unreffed: u32) void {
        const remove = @bitCast(u32, State{ .acquired = true, .pending = @intCast(u30, unreffed) });
        const state = @bitCast(State, @atomicRmw(u32, &polling.state, .Sub, remove, .Release));

        assert(state.pending >= unreffed);
        assert(state.acquired);
        assert(state.active);
    }
};

const Injector = extern struct {
    head: usize = 0,
    _head_padding: [std.atomic.cache_line - @sizeOf(usize)]u8 = undefined,

    tail: ?*Task = null,
    _tail_padding: [std.atomic.cache_line - @sizeOf(?*Task)]u8 = undefined,
};

const Buffer = extern struct {
    head: u32 = 0,
    _cache_padding: [std.atomic.cache_line - @sizeOf(u32)]u8 = undefined,

    tail: u32 = 0,
    buffer: [256]?*Task = [_]?*Task{ null } ** 256,

    fn push(noalias buffer: *Buffer, noalias task: *Task, noalias injector: *Injector) void {

    }

    fn pop(buffer: *Buffer) ?*align(1) Task {

    }

    fn steal(noalias buffer: *Buffer, noalias target: *Buffer) ?*align(1) Task {

    }

    fn inject(noalias buffer: *Buffer, noalias injector: *Injector) ?*align(1) Task {

    }
};

const Random = extern struct {
    state: u32,

    fn init(seed: u32) Random {
        assert(seed != 0);
        return .{ .state = seed };
    }

    fn next(random: *Random) u32 {
        const state = random.state;
        random.state = (state *% 1103515245) +% 12345;
        return state;
    }

    fn backoff(random: *Random) void {
        if (comptime builtin.target.isDarwin() and builtin.target.cpu.arch == .aarch64) {
            asm volatile("wfe" ::: "memory");
            return;
        }

        var spins = ((random.next() >> 24) & (128 - 1)) | (32 - 1);
        while (spins > 0) : (spins -= 1) {
            std.atomic.spinLoopHint();
        }
    }

    fn sequence(random: *Random, range: u16) Sequence {
        assert(range != 0);
        return .{
            .iter = range,
            .range = range,
            .index = (random.next() >> 16) % @as(u32, range),
        };
    }

    const Sequence = extern struct {
        iter: u32,
        range: u32,
        index: u32,

        fn next(seq: *Sequence) ?u32 {
            seq.iter = std.math.sub(u32, seq.iter, 1) catch return null;
            
            defer {
                seq.index += seq.range - 1;
                if (seq.index >= seq.range) {
                    seq.index -= seq.range;
                }
            }

            assert(seq.index < seq.range);
            return seq.index;
        }
    };
};
