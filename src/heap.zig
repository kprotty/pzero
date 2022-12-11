const std = @import("std");
const assert = std.debug.assert;

pub const MinHeap = extern struct {
    min: ?*Node = null,
    len: usize = 0,

    pub const Node = extern struct {
        key: u64,
        parent: ?*Node = null,
        children: [2]?*Node = [_]?*Node{ null, null },
    };

    pub fn push(noalias heap: *Heap, noalias node: *Node, key: u64) void {
        assert(key != 0);
        assert(heap.len < std.math.maxInt(usize));
        node.* = Node{ .key = key };

        // Insert node at the end of the eytzinger array.
        {
            var link = heap.lookup_link(heap.len, &node.parent);
            heap.len += 1;
            
            assert(link.* == null);
            link.* = node;
        }

        // Sift up the node to main the min-heap property.
        while (node.parent) |parent| {
            if (!(node.key < parent.key)) break;
            heap.swap(parent, node);
        }
    }

    pub inline fn peek(noalias heap: *const Heap) ?*Node {
        return heap.min;
    }

    pub fn pop(noalias heap: *Heap) ?*Node {
        var node = heap.min orelse return null;
        assert(node.parent == null);
        assert(heap.len > 0);

        // Move the last node at the end of the eytzinger array to min/root.
        {
            heap.len -= 1;
            var link = heap.lookup_link(heap.len, null);

            const last_node = link.* orelse unreachable;
            assert(last_node.children[0] == null);
            assert(last_node.children[1] == null);

            link = heap.parent_link(last_node.parent, last_node, null);
            assert(link.* == last_node);
            link.* = null;

            last_node.children = node.children;
            last_node.parent = node.parent;
            node = last_node;
        }

        // Sift down the last node to main the min-heap property.
        var current = node;
        while (true) {
            var smallest = current;
            for (current.children) |c| {
                const child = c orelse continue;
                assert(child.parent == current);
                if (child.key < smallest.key) smallest = child;
            }

            if (current == smallest) break;
            heap.swap(current, smallest);
            current = smallest;
        }
    }

    fn swap(
        noalias heap: *Heap,
        noalias parent: *Node,
        noalias child: *Node,
    ) void {
        assert(parent.key != 0);
        assert(child.key != 0);
        assert(child.parent == parent);

        // Swap the grand parent's link from the parent to the child.
        {
            const grand_parent = parent.parent;
            const link = heap.parent_link(grand_parent, parent, null);

            assert(link.* == parent);
            link.* = child;

            child.parent = grand_parent;
            parent.parent = child;
        }

        // Swap the child and sibling's parent links from the parent to the child.
        blk: {
            var sibling: ?*Node = undefined;
            const link = heap.parent_link(parent, child, &sibling);

            assert(link.* == child);
            link.* = parent;

            const s = sibling orelse break :blk;
            assert(s.parent == parent);
            s.parent = child;
        }

        // Swap the grand children's parent links from the parent to the child.
        {
            const grand_children = child.children;
            for (grand_children) |c| {
                const grand_child = c orelse continue;
                assert(grand_child.parent == child);
                grand_child.parent = parent;
            }

            child.children = parent.children;
            parent.children = grand_children;
        }
    }

    fn lookup_link(
        noalias heap: *Heap,
        array_index: usize,
        noalias out_parent: ?*?*Node,
    ) *?*Node {
        var link = &heap.min;
        if (array_index == 0) return link;

        // Find the path through tree to follow from the eytzinger array index.
        var path = @bitReverse(array_index) >> 1;
        var traverse: usize = @bitSizeOf(usize) - @clz(array_index) - 1;

        // Walk through the tree using the path, recording the current link and parent if requested.
        while (true) : (path >>= 1) {
            traverse = std.math.sub(usize, traverse, 1) catch return link;
            const node = link.* orelse unreachable;
            if (out_parent) |parent| parent.* = node;
            link = &node.children[path & 1];
        }
    }

    fn parent_link(
        noalias heap: *Heap,
        noalias parent: ?*Node,
        noalias child: *Node,
        noalias out_sibling: ?*?*Node,
    ) *?*Node {
        assert(child.parent == parent);
        assert(child.key != 0);

        const p = parent orelse return &heap.min;
        const is_right = p.children[1] == child;
        
        // Find the parent's link to the child.
        const link = &p.children[@boolToInt(is_right)];
        assert(link.* == child);

        // Record the sibling of the child if requested.
        if (out_sibling) |sibling| sibling.* = p.children[@boolToInt(!is_right)];
        return link;
    }
};