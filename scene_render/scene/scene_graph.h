#pragma once

// wz/scene/scene_graph.h
//
// Free functions operating on Polytree<TransformNode, NoEdge>.
// No new types — the polytree and pipeline machinery are used directly.

#include <scene/transform_node.h>
#include <graph/static_polytree.h>
#include <graph/static_polytree_algo.h>
#include <algo/pipeline.h>

#include <span>
#include <vector>

namespace wz::scene {

    using namespace wz::math;
    using namespace wz::core::graph;

    using SceneEdge = struct {};
    using SceneGraph = Polytree<TransformNode, SceneEdge>;
    using SceneStorage = PolytreeStorage<TransformNode, SceneEdge>;
    using SceneBuilder = PolytreeBuilder<TransformNode, SceneEdge>;


    // ─── Transform propagation ────────────────────────────────────────────────────
    //
    // Pure function — given parent world transform and node local transform,
    // produces the node world transform.

    inline Mat4 compute_world(const Mat4& parent_world, const Mat4& local)
    {
        return mul(parent_world, local);
    }


    // ─── Full propagation — all nodes in topo order ───────────────────────────────
    //
    // Unconditional: used for animated subgraphs or first-frame initialization.
    // Roots use their local transform as world (no parent).

    void propagate_all(SceneGraph& g)
    {
        for (NodeHandle n : topo_order(g)) {
            TransformNode& node = const_cast<TransformNode&>(node_data(g, n));
            NodeHandle p = parent(g, n);
            node.world = (p == INVALID_NODE)
                ? node.local
                : compute_world(node_data(g, p).world, node.local);
        }
    }


    // ─── Dirty tracking ───────────────────────────────────────────────────────────
    //
    // A node is dirty if last_updated_frame != current_frame.
    // Roots whose subtree needs recomputation are "dirty roots".

    inline bool is_dirty(const TransformNode& node)
    {
        return node.last_updated_frame == TransformNode::INVALID_FRAME;
    }

    inline void mark_dirty(SceneGraph& g, NodeHandle n)
    {
        const_cast<TransformNode&>(node_data(g, n)).last_updated_frame
            = TransformNode::INVALID_FRAME;
    }

    // Mark a node's local transform and flag it dirty.
    inline void set_local(SceneGraph& g, NodeHandle n, const Mat4& local)
    {
        auto& node = const_cast<TransformNode&>(node_data(g, n));
        node.local = local;
        node.last_updated_frame = TransformNode::INVALID_FRAME;
    }


    // ─── Collect dirty roots ──────────────────────────────────────────────────────
    //
    // A dirty root is a node that is dirty and whose parent is either absent
    // (it is a scene root) or clean. This avoids redundant subtree updates —
    // updating a dirty root propagates to all its dirty descendants.
    //
    // Runs in topo order so parents are always evaluated before children.
    // Caller provides scratch buffer.

    std::span<NodeHandle> collect_dirty_roots(
        const SceneGraph& g,
        uint32_t              current_frame,
        std::span<NodeHandle> scratch)
    {
        uint32_t count = 0;
        for (NodeHandle n : topo_order(g)) {
            const auto& node = node_data(g, n);
            if (!is_dirty(node)) continue;

            NodeHandle p = parent(g, n);
            bool parent_clean = (p == INVALID_NODE)
                || !is_dirty(node_data(g, p));

            if (parent_clean) {
                if (count < static_cast<uint32_t>(scratch.size()))
                    scratch[count++] = n;
            }
        }
        return scratch.subspan(0, count);
    }



    // ─── Static update — dirty subtrees only ─────────────────────────────────────
    //
    // For each dirty root, propagates transforms down through its subtree
    // using BFS. Marks each visited node clean by writing current_frame.
    //
    // Uses sink-based BFS for natural subtree containment — BFS from a
    // dirty root visits exactly that subtree.

    void update_static(
        SceneGraph& g,
        std::span<const NodeHandle> dirty_roots,
        uint32_t                    current_frame)
    {
        for (NodeHandle root : dirty_roots) {
            // BFS from this dirty root
            // Use visitor form — we need to write to node data
            std::vector<NodeHandle> queue;
            queue.push_back(root);

            for (uint32_t head = 0; head < queue.size(); ++head) {
                NodeHandle n = queue[head];
                auto& node = const_cast<TransformNode&>(node_data(g, n));
                NodeHandle p = parent(g, n);

                node.world = (p == INVALID_NODE)
                    ? node.local
                    : compute_world(node_data(g, p).world, node.local);

                node.last_updated_frame = current_frame;

                for (NodeHandle child : children(g, n))
                    queue.push_back(child);
            }
        }
    }


    // ─── Build animated update list ───────────────────────────────────────────────
    //
    // Returns all animated nodes in topo order — parents always before children.
    // Caller provides scratch buffer.

    std::span<NodeHandle> build_animated_list(
        const SceneGraph& g,
        std::span<NodeHandle> scratch)
    {
        using namespace wz::core::algo::pipeline;

        uint32_t count = 0;

        struct AnimSink {
            std::span<NodeHandle> buf;
            uint32_t& count;
            bool push(NodeHandle n) {
                if (count >= static_cast<uint32_t>(buf.size())) return false;
                buf[count++] = n;
                return true;
            }
        };

        auto pipe = filter([&](NodeHandle n) {
            return node_data(g, n).motion_type == TransformNode::MotionType::Animated;
            });

        AnimSink sink{ scratch, count };
        auto psink = as_sink(pipe, sink);

        // walk topo order — guaranteed parent before child
        for (NodeHandle n : topo_order(g))
            if (!psink.push(n)) break;

        return scratch.subspan(0, count);
    }


    // ─── Animated update — unconditional, full list ───────────────────────────────
    //
    // Propagates transforms for all animated nodes.
    // List must be in topo order (use build_animated_list).

    void update_animated(
        SceneGraph& g,
        std::span<const NodeHandle> animated_list,
        uint32_t                    current_frame)
    {
        for (NodeHandle n : animated_list) {
            auto& node = const_cast<TransformNode&>(node_data(g, n));
            NodeHandle p = parent(g, n);

            node.world = (p == INVALID_NODE)
                ? node.local
                : compute_world(node_data(g, p).world, node.local);

            node.last_updated_frame = current_frame;
        }
    }

} // namespace wz::scene