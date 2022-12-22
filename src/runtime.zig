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
    run_queue: Queue = .{},
    scheduler: *Scheduler,
    random: Random,
    event: u32 = 0,
};

const Idle = extern struct {
    state: usize = 0,
    unpark_next: usize = 0,
    _cache_padding: [std.atomic.cache_line - @sizeOf(usize) - @sizeOf(usize)]u8 = undefined,

    const Tag = enum(u2) {
        pending,
        unpark,
        insert,
        cancel,
    };

    fn park(noalias idle: *Idle, noalias event: *Event, noalias producer: *Queue.Producer, deadline_ns: ?u64) bool {
        var waiter: Waiter = undefined;
        waiter.prev = @enumToInt(Tag.pending);
        waiter.producer = producer;
        idle.enqueue(event, &waiter.next, .insert);

        if (!event.wait(producer, deadline_ns)) {
            idle.enqueue(event, &waiter.cancel_next, .cancel);
            assert(event.wait(producer, null));
        }

        return waiter.get_tag() != .cancel;
    }

    fn unpark(noalias idle: *Idle, noalias event: *Event) void {
        idle.enqueue(event, &idle.unpark_next, .unpark);
    }

    fn enqueue(noalias idle: *Idle, noalias event: *Event, noalias link: *usize, new_tag: Tag) void {
        new_link.* = 0;
        assert(@ptrToInt(new_link) & 0b11 == 0);
        var new_state = @ptrToInt(new_link) | @enumToInt(new_tag);

        const old_state = @atomicRmw(usize, &idle.state, .Xchg, new_state, .AcqRel);
        const old_link = @intToPtr(?*usize, old_state & ~@as(usize, 0b11));
        const old_tag = @intToEnum(Tag, @truncate(u2, old_state));
        
        if (old_tag != .pending) {
            const link = old_link orelse unreachable;
            mstore(link, new_state);
            return;
        }

        var unpark_list: ?*Waiter = null;
        defer while (unpark_list) |waiter| {
            assert(@intToEnum(Tag, @truncate(u2, waiter.prev)) != .insert);
            remove(&unpark_list, waiter, .unpark);
            event.set(waiter.producer);
        };

        var unpark_one = false;
        var last_state: usize = 0;
        var wait_list = blk: {
            const link = old_link orelse break :blk null;
            break :blk @fieldParentPtr(Waiter, "next", link);
        };

        while (true) {
            var current = new_state;
            while (current != last_state) {
                const link = @intToPtr(*usize, current & ~@as(usize, 0b11));
                const tag = @intToEnum(Tag, @truncate(u2, current));
                current = mwait(link);

                switch (tag) {
                    .pending => unreachable,
                    .unpark => {
                        assert(link == &idle.unpark_next);
                        assert(!unpark_one);
                        unpark_one = true;
                    },
                    .insert => {
                        const waiter = @fieldParentPtr(Waiter, "next", link);
                        assert(@intToEnum(Tag, @truncate(u2, waiter.prev)) == .pending);

                        waiter.prev = (waiter.prev & ~@as(usize, 0b11)) | @enumToInt(Tag.insert);
                        insert(&wait_list, waiter);
                    },
                    .cancel => {
                        const waiter = @fieldParentPtr(Waiter, "cancel_next", link);
                        const old_tag = @intToEnum(Tag, @truncate(u2, waiter.prev));

                        waiter.prev = (waiter.prev & ~@as(usize, 0b11)) | @enumToInt(Tag.cancel);
                        if (old_tag == .insert) {
                            remove(&wait_list, waiter);
                        }
                    },
                }
            }

            var next_state = @enumToInt(Tag.pending);
            if (wait_list) |waiter| {
                if (unpark_one) {
                    assert(@intToEnum(Tag, @truncate(u2, waiter.prev)) == .insert);
                    remove(&wait_list, waiter);
                    
                    waiter.prev = (waiter.prev & ~@as(usize, 0b11)) | @enumToInt(Tag.unpark);
                    insert(&unpark_list, waiter);
                }
                if (wait_list) |w| {
                    next_state = @ptrToInt(w) | @enumToInt(Tag.pending);
                }
            } else if (unpark_one) {
                next_state = @ptrToInt(&idle.unpark_next) | @enumToInt(Tag.unpark);
            }

            last_state = new_state;
            new_state = @atomicRmw(usize, &idle.state, last_state, next_state, .Release, .Acquire) orelse return;

            if (unpark_list) |waiter| {
                if (unpark_one) {
                    assert(@intToEnum(Tag, @truncate(u2, waiter.prev)) == .unpark);
                    remove(&unpark_list, waiter);

                    waiter.prev = (waiter.prev & ~@as(usize, 0b11)) | @enumToInt(Tag.insert);
                    insert(&wait_list, waiter);
                }
            }
        }
    }

    fn insert(noalias list: *?*Waiter, noalias waiter: *Waiter) void {
        const list_head = list.*;
        waiter.prev = waiter.prev & 0b11;
        waiter.next = @ptrToInt(list_head);

        const head = list_head orelse {
            list.* = waiter;
            return;
        };
        
        assert(head.prev & ~@as(usize, 0b11) == 0);
        head.prev = @ptrToInt(waiter) | (head.prev & 0b11);
    }

    fn remove(noalias list: *?*Waiter, noalias waiter: *Waiter) void {
        const head = list.* orelse unreachable;
        assert(head.prev & ~@as(usize, 0b11) == 0);

        const prev = waiter.prev & ~@as(usize, 0b11);
        const next = waiter.next;

        waiter.prev = waiter.prev & 0b11;
        waiter.next = 0;

        if (@intToPtr(?*Waiter, next)) |n| {
            assert((n.prev & ~@as(usize, 0b11)) == @ptrToInt(waiter));
            n.prev = @ptrToInt(prev) | (n.prev & 0b11);
        }

        if (@intToPtr(?*Waiter, prev)) |p| {
            assert(p.next == @ptrToInt(waiter));
            p.next = next;
        } else {
            assert(head == waiter);
            link.* = next;
        }
    }

    fn mwait(link: *usize) usize {
        var spin: usize = 32;
        while (spin > 0) : (spin -= 1) {
            switch (@atomicLoad(usize, link, .Acquire)) {
                0 => std.atomic.spinLoopHint(),
                else => |value| return value,
            }
        }

        var futex: u32 = 0;
        var value = @atomicRmw(usize, link, .Xchg, @ptrToInt(&futex), .AcqRel);
        if (value != 0) {
            return value;
        }
        
        while (true) {
            _ = platform.futex_wait(&futex, 0, null);
            if (@atomicLoad(u32, &futex, .Acquire) == 0) {
                continue;
            }

            value = @atomicLoad(usize, link, .Acquire);
            assert(value != 0);
            return value;
        }
    }

    fn mstore(link: *usize, new_value: usize) void {
        assert(new_value != 0);
        const old_value = @atomicRmw(usize, link, .Xchg, new_value, .AcqRel);

        if (@intToPtr(?*u32, old_value)) |futex| {
            @atomicStore(u32, futex, 1, .Release);
            platform.futex_wake(futex);
        }
    }
};

const Event = extern struct {
    polling: Polling,
    reactor: platform.Reactor,

    fn init() Event {

    }

    fn deinit(event: *Event) void {

    }

    const empty = 0;
    const wait_futex = 1;
    const wait_reactor = 2;
    const notified = 3;

    fn wait(noalias event: *Event, noalias producer: *Queue.Producer, deadline_ns: ?u64) bool {
        
    } 

    fn poll(noalias idle: *Idle, noalias context: *anyopaque, wait_state: u32, deadline_ns: ?u64) u32 {
        
    }

    fn set(noalias event: *Event, noalias producer: *Queue.Producer) void {
        
    }
};

const Polling = extern struct {
    state: u32,
    _cache_padding: [std.atomic.cache_line - @sizeOf(u32)]u8 = undefined,

    const State = packed struct {
        active: bool = false,
        acquired: bool = false,
        pending: u30 = 0,
    };

    fn init(active: bool) Polling {
        return .{ .state = @bitCast(u32, State{ .active = active }) };
    }

    fn deinit(polling: *Polling) bool {
        const state = @bitCast(State, polling.state);
        assert(state.pending == 0);
        assert(!state.acquired);
        return state.active;
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

const Queue = extern struct {
    head: u32 = 0,
    inject_head: usize = 0,
    _cache_padding: [std.atomic.cache_line - @sizeOf(usize) - @sizeOf(u32)]u8 = undefined,

    tail: u32 = 0,
    inject_tail: ?*Task = null,
    buffer: [capacity]?*Task = [_]?*Task{ null } ** capacity,

    const capacity = 256;
    const consuming_bit: usize = 0b1;

    const List = extern struct {
        head: ?*Task = null,
        tail: ?*Task = null,

        fn push(noalias list: *List, noalias task: *Task) void {
            const prev = if (list.tail) |tail| &tail.next else &list.head;
            prev.* = task;
            list.tail = task;
            task.next = null;
        }

        fn pop(list: *List) ?*Task {
            const task = list.head orelse return null;
            list.head = task.next;
            if (list.head == null) list.tail = null;
            return task;
        }
    };

    fn push(noalias queue: *Queue, noalias task: *Task) void {
        var head = @atomicLoad(u32, &queue.head, .Monotonic);
        const tail = queue.tail;

        var size = tail -% head;
        assert(size <= capacity);

        if (size == capacity) overflow: {
            var migrate = size / 2;
            assert(migrate > 0);

            if (@cmpxchgStrong(u32, &queue.head, head, head +% migrate, .AcqRel, .Monotonic)) |new_head| {
                size = tail -% new_head;
                break :overflow;
            }

            var overflowed = List{};
            while (migrate > 0) : (migrate -= 1) {
                const stolen = queue.buffer[head % capacity] orelse unreachable;
                overflowed.push(stolen);
                head +%= 1;
            }

            overflowed.push(task);
            assert(queue.inject(overflowed));
            return;
        }

        assert(size < capacity);
        @atomicStore(?*Task, &queue.buffer[tail % capacity], task, .Unordered);
        @atomicStore(u32, &queue.tail, tail +% 1, .Release);
    }

    fn inject(noalias queue: *Queue, list: List) bool {
        const front = list.head orelse return false;
        const back = list.tail orelse unreachable;
        assert(back.next == null);

        if (@atomicRmw(?*Task, &queue.inject_tail, .Xchg, back, .Release)) |tail| {
            @atomicStore(?*Task, &tail.next, front, .Release);
            return true;
        }

        const head = @atomicRmw(usize, &queue.inject_head, .Add, @ptrToInt(front), .Release);
        assert(head & ~consuming_bit == 0);
        return true;
    }

    pub fn pop(noalias queue: *Queue, noalias producer: *Producer) ?*align(1) Task {
        const head = @atomicRmw(u32, &queue.head, .Add, 1, .Acquire);
        const tail = queue.tail;

        const size = tail -% head;
        assert(size <= capacity);

        if (size > 0) {
            const task = queue.buffer[head % capacity] orelse unreachable;
            return @ptrCast(*align(1) Task, task);
        }

        producer.* = .{
            .queue = queue,
            .overflow = .{},
            .position = @bitCast(u64, Producer.Position{
                .available = capacity,
                .pushed = 0,
                .tail = tail,
            }),
        };

        @atomicStore(u32, &queue.head, head, .Monotonic);
        return producer.consume(queue);
    }

    const Producer = extern struct {
        queue: *Queue,
        overflow: List,
        position: u64,

        const Position = packed struct {
            available: u16 = 0,
            pushed: u16 = 0,
            tail: u32 = 0,
        };

        fn enqueue(noalias producer: *Producer, noalias task: *Task) void {
            var position = @bitCast(Position, producer.position);
            position.available = std.math.sub(u16, position.available, 1) catch {
                producer.overflow.push(task);
                return;
            };

            position.pushed += 1;
            producer.position = @bitCast(u64, position);
            @atomicStore(?*Task, &producer.queue.buffer[position.tail % capacity], task, .Unordered);
        }

        fn steal(noalias producer: *Producer, noalias target: *Queue, noalias random: *Random) ?*align(1) Task {
            while (true) {
                if (producer.consume(target)) |task| {
                    return task;
                }

                while (true) {
                    const target_head = @atomicLoad(u32, &target.head, .Acquire);
                    const target_tail = @atomicLoad(u32, &target.tail, .Acquire);

                    const target_size = target_tail -% target_head;
                    if (@bitCast(i32, target_size) <= 0) {
                        return null;
                    }

                    var target_steal = target_size - (target_size / 2);
                    if (target_steal > (capacity / 2)) {
                        std.atomic.spinLoopHint();
                        continue;
                    }
                    
                    var position = @bitCast(Position, producer.position);
                    assert(position.available == capacity);
                    assert(position.pushed == 0);

                    var new_target_head = target_head;
                    while (target_steal > 0) : (target_steal -= 1) {
                        const slot = &producer.queue.buffer[(position.tail +% position.pushed) % capacity];
                        position.pushed += 1;

                        const target_slot = &target.buffer[new_target_head % capacity];
                        new_target_head +%= 1;

                        const task = @atomicLoad(?*Task, target_slot, .Unordered) orelse unreachable;
                        @atomicStore(?*Task, slot, task, .Unordered);
                    }

                    if (@cmpxchgStrong(u32, &target.head, target_head, new_target_head, .AcqRel, .Monotonic)) |_| {
                        random.backoff();
                        break;
                    }

                    producer.position = @bitCast(u64, position);
                    return producer.commit();
                }
            }
        }

        fn consume(noalias producer: *Producer, noalias target: *Queue) ?*align(1) Task {
            var inject_head = @atomicLoad(usize, &target.inject_head, .Monotonic);
            while (true) {
                if (inject_head & ~consuming_bit == 0) return null;
                if (inject_head & consuming_bit != 0) return null;
                inject_head = @cmpxchgWeak(usize, &target.inject_head, inject_head, consuming_bit, .Acquire, .Monotonic) orelse break;
            }

            var position = @bitCast(Position, producer.position);
            while (position.available > 0) : (position.available -= 1) {
                const task = popped: {
                    const head = @intToPtr(?*Task, inject_head) orelse blk: {
                        inject_head = @atomicLoad(usize, &target.inject_head, .Acquire);
                        assert(inject_head & consuming_bit != 0);
                        inject_head &= ~consuming_bit;

                        const new_head = @intToPtr(?*Task, inject_head) orelse break;
                        @atomicStore(usize, &target.inject_head, consuming_bit);
                        break :blk new_head;
                    };

                    const next: ?*Task = @atomicLoad(?*Task, &head.next, .Acquire) orelse blk: {
                        _ = @cmpxchgStrong(?*Task, &target.inject_tail, head, null, .AcqRel, .Monotonic) orelse break :blk null;
                        std.atomic.spinLoopHint();
                        break :blk (@atomicLoad(?*Task, &head.next, .Acquire) orelse break);
                    };

                    inject_head = @ptrToInt(next);
                    break :blk head;
                };

                @atomicStore(?*Task, &producer.queue.buffer[(position.tail +% position.pushed) % capacity], task, .Unordered);
                position.pushed += 1;
            }

            if (inject_head != 0) {
                assert(inject_head & consuming_bit == 0);
                @atomicStore(usize, &target.inject_head, inject_head, .Release);
            } else {
                inject_head = @atomicRmw(usize, &target.inject_head, .Sub, consuming_bit, .Release);
                assert(inject_head & consuming_bit != 0);
            }

            producer.position = @bitCast(u64, position);
            return producer.commit();
        }

        fn commit(producer: *Producer) ?*align(1) Task {
            var position = @bitCast(Position, producer.position);
            position.pushed = std.math.sub(u16, position.pushed, 1) catch return null;

            const new_tail = position.tail +% @as(u32, position.pushed);
            const task = producer.queue.buffer[new_tail % capacity] orelse unreachable;

            const pushed = new_tail != position.tail;
            if (pushed) {
                @atomicStore(u32, &producer.queue.tail, new_tail, .Release);
            }

            const overfowed = producer.queue.inject(producer.overflow);
            return @intToPtr(*align(1) Task, @ptrToInt(task) | @boolToInt(pushed or overflowed));
        }
    };
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
