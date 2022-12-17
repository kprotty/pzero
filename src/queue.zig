const std = @import("std");
const builtin = @import("builtin");
const assert = std.debug.assert;

pub const Node = extern struct {
    next: ?*Node = null,
};

pub const List = extern struct {
    head: ?*Node = null,
    tail: ?*Node = null,

    pub fn from(node: *Node) List {
        node.next = null;
        return .{ .head = node, .tail = node };
    }

    pub fn push(list: *List, other: List) bool {
        const prev = if (list.tail) |tail| &tail.next else &list.head;
        prev.* = other.head orelse return false;
        list.tail = other.tail orelse unreachable;
        return true;
    }

    pub fn pop(list: *List) ?*Node {
        const node = list.head orelse return null;
        list.head = node.next;
        if (list.head == null) list.tail = null;
        return node;
    }
};

pub const Injector = extern struct {
    head: usize = 0,
    _head_padding: [std.atomic.cache_line - @sizeOf(usize)]u8 = undefined,

    tail: ?*Node = null,
    _head_padding: [std.atomic.cache_line - @sizeOf(?*Node)]u8 = undefined,

    const consuming_bit: usize = 0b1;
    const node_mask: usize = ~consuming_bit;

    pub fn push(noalias injector: *Injector, list: List) bool {
        const front = list.head orelse return false;
        const back = list.tail orelse unreachable;
        assert(back.next == null);

        if (@atomicRmw(?*Node, &injector.tail, .Xchg, back, .Release)) |prev| {
            @atomicStore(?*Node, &prev.next, front, .Release);
            return true;
        }

        const head = @atomicRmw(usize, &injector.head, .Add, @ptrToInt(front), .Release);
        assert(head & node_mask == 0);
        return true;
    }

    pub fn pop(injector: *Injector) ?*Node {
        const head = @atomicLoad(usize, &injector.head, .Acquire);
        assert(head & consuming_bit == 0);

        var consumer = @intToPtr(?*Node, head);
        defer @atomicStore(usize, &injector.head, @ptrToInt(consumer), .Release);

        return injector.consume(&consumer);
    }

    fn acquire(injector: *Injector) ?*Node {
        var head = @atomicLoad(usize, &injector.head, .Monotonic);
        while (true) {
            if (head & node_mask == 0) return null;
            if (head & consuming_bit != 0) return null;

            head = @cmpxchgWeak(usize, &injector.head, head, consuming_bit, .Acquire, .Monotonic) orelse {
                return @intToPtr(*Node, head);
            };
        }
    }

    fn consume(noalias injector: *Injector, noalias consumer: *?*Node) ?*Node {
        const head = consumer.* orelse blk: {
            const head = @atomicLoad(usize, &injector.head, .Acquire);
            assert(head & consuming_bit != 0);

            const node = @intToPtr(?*Node, head & node_mask) orelse return null;
            consumer.* = node;
            break :blk node;
        };

        const next = @atomicLoad(?*Node, &head.next, .Acquire) orelse blk: {
            _ = @cmpxchgStrong(?*Node, &injector.tail, head, null, .AcqRel, .Acquire) orelse {
                return node;
            }

            std.atomic.spinLoopHint();
            break :blk @atomicLoad(?*Node, &head.next, .Acquire) orelse return null;
        };

        consumer.* = next;
        return head;
    }

    fn release(noalias injector: *Injector, consumer: ?*Node) void {
        if (consumer) |head| {
            @atomicStore(usize, &injector.head, @ptrToInt(head), .Release);
            return;
        }

        const head = @atomicRmw(usize, &injector.head, .Sub, consuming_bit, .Release);
        assert(head & consuming_bit != 0);
    }
};

pub const Buffer = extern struct {
    head: usize = 0,
    _head_padding: [std.atomic.cache_line - @sizeOf(usize)] = undefined,

    tail: usize = 0,
    array: [128]?*Node = [_]?*Node{ null } ** 128,

    pub fn push(noalias buffer: *Buffer, noalias node: *Node, noalias overflowed: *List) void {
        var head = @truncate(u8, @atomicLoad(u32, &buffer.head, .Monotonic));
        const tail = @truncate(u8, buffer.tail);

        var size = tail -% head;
        assert(size <= capacity);

        if (size == capacity) overflow: {
            var migrate = size / 2;
            assert(migrate > 0);

            if (@cmpxchgStrong(u32, &buffer.head, head, head +% 1, .Acquire, .Monotonic)) |new_head| {
                size = tail -% @truncate(u8, new_head);
                break :overflow;
            }

            while (true) {
                migrate = std.math.sub(u8, migrate, 1) catch break;
                const migrated = buffer.array[head % capacity] orelse unreachable;
                overflowed.push(List.from(migrated));
                head +%= 1;
            }

            overflowed.push(List.from(node));
            return;
        }

        assert(size < capacity);
        @atomicStore(?*Node, &buffer.array[tail % capacity], node, .Unordered);
        @atomicStore(u32, &buffer.tail, tail +% 1, .Release);
    }

    pub fn pop(buffer: *Buffer) usize {
        const head = @truncate(u8, @atomicRmw(u32, &buffer.head, .Add, 1, .Acquire));
        const tail = @truncate(u8, buffer.tail);

        const size = tail -% head;
        assert(size <= capacity);

        if (size == 0) {
            @atomicStore(u32, &buffer.head, head, .Monotonic);
            return 0;
        }

        const node = buffer.array[head % capacity] orelse unreachable;
        return @ptrToInt(node);
    }

    pub fn steal(noalias buffer: *Buffer, noalias target: *Buffer, noalias rand: *u32) usize {
        while (true) {
            const target_head = @truncate(u8, @atomicLoad(u32, &target.head, .Acquire));
            const target_tail = @truncate(u8, @atomicLoad(u32, &target.tail, .Acquire));

            const target_size = target_tail -% target_head;
            if (@bitCast(i8, target_size) <= 0) {
                return 0;
            }

            var target_steal = target_size - (target_size / 2);
            if (target_steal > (capacity / 2)) {
                std.atomic.spinLoopHint();
                continue;
            }

            var producer = buffer.prepare();
            var new_target_head = target_head;
            while (true) {
                target_steal = std.math.sub(u8, target_steal, 1) catch break;

                const slot = &target.buffer[new_target_head % capacity];
                new_target_head +%= 1;

                const node = @atomicLoad(?*Node, slot, .Unordered) orelse unreachable;
                assert(buffer.enqueue(&producer, node));
            }

            _ = @cmpxchgWeak(usize, &target.head, target_head, new_target_head, .AcqRel, .Monotonic) orelse {
                return buffer.stolen(producer);
            };

            if (comptime builtin.target.isDarwin() and builtin.target.cpu.arch == .aarch64) {
                asm volatile("wfe" ::: "memory");
                _ = rand;
                continue;
            }

            const rng = rand.*;
            rand.* = (rng *% 1103515245) +% 12345;

            var spins = (@truncate(u8, rng >> 24) & (128 - 1)) | (32 - 1);
            while (true) {
                spins = std.math.sub(u8, spins, 1) catch break;
                std.atomic.spinLoopHint();
            }
        }
    }

    pub fn inject(noalias buffer: *Buffer, noalias injector: *Injector) usize {
        var consumer = injector.acquire() orelse return 0;
        var producer = buffer.prepare();
        var available: u8 = capacity;

        while (true) {
            available = std.math.sub(u8, available, 1) catch break;
            const node = injector.consume(&consumer) orelse break;
            assert(buffer.enqueue(&producer, node));
        }

        injector.release(consumer);
        return buffer.stolen(producer);
    }

    pub fn fill(noalias buffer: *Buffer, noalias list: *List) usize {
        var available: u8 = capacity;
        var producer = buffer.prepare();

        while (true) {
            available = std.math.sub(u8, available, 1) catch break;
            const node = list.pop() orelse break;
            assert(buffer.enqueue(&producer, node));
        }

        return buffer.stolen(producer);
    }

    const available_shift = 0;
    const pushed_shift = 8;
    const tail_shift = 16;
    const head_shift = 24;

    pub fn prepare(buffer: *Buffer) u32 {
        const head = @truncate(u8, @atomicLoad(u32, &buffer.head, .Monotonic));
        const tail = @truncate(u8, buffer.tail);

        const size = tail -% head;
        assert(size <= capacity);

        var producer: u32 = 0;
        producer |= @as(u32, capacity - size) << available_shift;
        producer |= @as(u32, 0) << pushed_shift;
        producer |= @as(u32, tail) << tail_shift;
        producer |= @as(u32, head) << head_shift;
        return producer;
    }

    pub fn enqueue(noalias buffer: *Buffer, noalias producer: *u32, noalias node: *Node) bool {
        const p = producer.*;
        const available = @truncate(u8, p >> available_shift);
        if (available == 0) {
            return false;
        }

        const pushed = @truncate(u8, p >> pushed_shift);
        const tail = @truncate(u8, p >> tail_shift);
        @atomicStore(?*Node, &buffer.array[(tail +% pushed) % capacity], node, .Unordered);

        producer.* +%= @as(u32, 1 << pushed_shift) -% @as(u32, 1 << available_shift);
        return true;
    }

    pub fn dequeue(noalias buffer: *Buffer, noalias producer: *u32) ?*Node {
        const p = producer.*;
        const pushed = @truncate(u8, p >> pushed_shift);
        if (pushed == 0) {
            return null;
        }

        const tail = @truncate(u8, p >> tail_shift);
        const node = buffer.array[(tail +% pushed) % capacity] orelse unreachable;

        producer.* +%= @as(u32, 1 << available_shift) -% @as(u32, 1 << pushed_shift);
        return node;
    }

    pub fn commit(buffer: *Buffer, producer: u32) bool {
        const pushed = @truncate(u8, producer >> pushed_shift);
        if (pushed == 0) {
            return false;
        }

        const tail = @truncate(u8, producer >> tail_shift);
        @atomicStore(u32, &buffer.tail, tail +% pushed, .Release);
        return true;
    }

    fn stolen(buffer: *Buffer, producer: u32) usize {
        var pushed = @truncate(u8, producer >> pushed_shift);
        pushed = std.math.sub(u8, pushed, 1) catch return 0;

        const tail = @truncate(u8, producer >> tail_shift);
        const new_tail = tail +% pushed;

        const commited = pushed > 0;
        if (committed) {
            @atomicStore(u32, &buffer.tail, new_tail, .Release);
        }
        
        const node = buffer[new_tail % capacity] orelse unreachable;
        return @ptrToInt(node) | @boolToInt(committed);
    }
};