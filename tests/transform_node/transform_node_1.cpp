#include <gtest/gtest.h>

#include <scene/transform_node.h>

using namespace wz::scene;

namespace
{
    ::testing::AssertionResult Mat4Near(const char* a_expr, const char* b_expr,
        const Mat4& a, const Mat4& b)
    {
        for (int i = 0; i < 16; ++i)
        {
            if (std::abs(a.m[i] - b.m[i]) > 1e-5f)
            {
                return ::testing::AssertionFailure()
                    << "Mismatch at index " << i
                    << ": " << a.m[i] << " vs " << b.m[i];
            }
        }
        return ::testing::AssertionSuccess();
    }
}


TEST(TransformNode_static, AttachChild_SimpleInsert)
{
    TransformNode nodes[3]{};

    constexpr uint32_t parent = 0;
    constexpr uint32_t child = 1;

    nodes[parent].first_child = TransformNode::INVALID_NODE;
    nodes[child].parent = TransformNode::INVALID_NODE;
    nodes[child].next_sibling = TransformNode::INVALID_NODE;

    attach_child(nodes, child, parent);

    EXPECT_EQ(nodes[child].parent, parent);
    EXPECT_EQ(nodes[parent].first_child, child);
    EXPECT_EQ(nodes[child].next_sibling, TransformNode::INVALID_NODE);
}

TEST(TransformNode_static, AttachChild_MultipleChildren_Order)
{
    TransformNode nodes[4]{};

    constexpr uint32_t parent = 0;
    constexpr uint32_t a = 1;
    constexpr uint32_t b = 2;
    constexpr uint32_t c = 3;

    nodes[parent].first_child = TransformNode::INVALID_NODE;

    attach_child(nodes, a, parent);
    attach_child(nodes, b, parent);
    attach_child(nodes, c, parent);

    // Expected order: c -> b -> a

    EXPECT_EQ(nodes[parent].first_child, c);
    EXPECT_EQ(nodes[c].next_sibling, b);
    EXPECT_EQ(nodes[b].next_sibling, a);
    EXPECT_EQ(nodes[a].next_sibling, TransformNode::INVALID_NODE);
}

TEST(TransformNode_static, AttachChild_Reparenting)
{
    TransformNode nodes[3]{};

    constexpr uint32_t parentA = 0;
    constexpr uint32_t parentB = 1;
    constexpr uint32_t child = 2;

    nodes[parentA].first_child = TransformNode::INVALID_NODE;
    nodes[parentB].first_child = TransformNode::INVALID_NODE;

    nodes[child].parent = TransformNode::INVALID_NODE;

    attach_child(nodes, child, parentA);
    attach_child(nodes, child, parentB);

    EXPECT_EQ(nodes[child].parent, parentB);
    EXPECT_EQ(nodes[parentB].first_child, child);
}

TEST(TransformNode_static, DetachNode_RemovesFromChain)
{
    TransformNode nodes[3]{};

    constexpr uint32_t parent = 0;
    constexpr uint32_t a = 1;
    constexpr uint32_t b = 2;

    nodes[parent].first_child = TransformNode::INVALID_NODE;

    attach_child(nodes, a, parent);
    attach_child(nodes, b, parent); // b -> a

    detach_node(nodes, a);

    EXPECT_EQ(nodes[a].parent, TransformNode::INVALID_NODE);
    EXPECT_EQ(nodes[b].next_sibling, TransformNode::INVALID_NODE);
    EXPECT_EQ(nodes[parent].first_child, b);
}

// ============================================================
TEST(TransformNode_static, UpdateStatic_SingleNodeRoot)
{
    TransformNode nodes[1]{};

    constexpr uint32_t node = 0;
    constexpr uint32_t frame = 1;

    nodes[node].parent = TransformNode::INVALID_NODE;
    nodes[node].first_child = TransformNode::INVALID_NODE;

    // explicitly "never updated"
    nodes[node].last_updated_frame = UINT32_MAX;

    nodes[node].local = mat4_identity();
    nodes[node].world = Mat4{}; // irrelevant initial value

    uint32_t roots[1] = { node };
    uint32_t count = 1;

    update_static(nodes, roots, count, frame);

    EXPECT_PRED_FORMAT2(Mat4Near, nodes[node].world, nodes[node].local);
    EXPECT_EQ(nodes[node].last_updated_frame, frame);
}

TEST(TransformNode_static, UpdateStatic_ParentChildPropagation)
{
    TransformNode nodes[2]{};

    constexpr uint32_t parent = 0;
    constexpr uint32_t child = 1;
    constexpr uint32_t frame = 1;

    nodes[parent].parent = TransformNode::INVALID_NODE;
    nodes[parent].first_child = child;
    nodes[parent].next_sibling = TransformNode::INVALID_NODE;

    nodes[child].parent = parent;
    nodes[child].first_child = TransformNode::INVALID_NODE;
    nodes[child].next_sibling = TransformNode::INVALID_NODE;

    nodes[parent].local = mat4_identity();
    nodes[child].local = mat4_identity();

    nodes[parent].last_updated_frame = UINT32_MAX;
    nodes[child].last_updated_frame = UINT32_MAX;

    uint32_t roots[1] = { parent };
    uint32_t count = 1;

    update_static(nodes, roots, count, frame);

    EXPECT_PRED_FORMAT2(Mat4Near,
        nodes[parent].world,
        nodes[parent].local);

    EXPECT_PRED_FORMAT2(Mat4Near,
        nodes[child].world,
        mul(nodes[parent].world, nodes[child].local));

    EXPECT_EQ(nodes[parent].last_updated_frame, frame);
    EXPECT_EQ(nodes[child].last_updated_frame, frame);
}

TEST(TransformNode_static, UpdateStatic_SkipsAlreadyUpdated)
{
    TransformNode nodes[1]{};

    constexpr uint32_t node = 0;
    constexpr uint32_t frame = 1;

    nodes[node].parent = TransformNode::INVALID_NODE;
    nodes[node].first_child = TransformNode::INVALID_NODE;

    nodes[node].local = mat4_identity();

    // IMPORTANT: set world to something that would change if recomputed
    nodes[node].world = Mat4{}; // NOT identity

    nodes[node].last_updated_frame = frame; // already up-to-date

    uint32_t roots[1] = { node };
    uint32_t count = 1;

    update_static(nodes, roots, count, frame);

    // Must remain unchanged (proves skip actually happened)
    EXPECT_PRED_FORMAT2(Mat4Near, nodes[node].world, Mat4{});
    EXPECT_EQ(nodes[node].last_updated_frame, frame);
}

//TEST(TransformNode_static, UpdateStatic_PartialDirtySubtree)
//{
//    TransformNode nodes[2]{};
//
//    constexpr uint32_t parent = 0;
//    constexpr uint32_t child = 1;
//    constexpr uint32_t frame = 2;
//
//    nodes[parent].parent = TransformNode::INVALID_NODE;
//    nodes[parent].first_child = child;
//
//    nodes[child].parent = parent;
//    nodes[child].next_sibling = TransformNode::INVALID_NODE;
//
//    nodes[parent].local = mat4_identity();
//    nodes[child].local = mat4_identity();
//
//    // Parent already valid in this frame
//    nodes[parent].world = mat4_identity();
//    nodes[parent].last_updated_frame = frame;
//
//    // Child is dirty
//    nodes[child].last_updated_frame = 0;
//
//    uint32_t roots[1] = { parent };
//    uint32_t count = 1;
//
//    update_static(nodes, roots, count, frame);
//
//    EXPECT_EQ(nodes[child].last_updated_frame, frame);
//    EXPECT_PRED_FORMAT2(
//        Mat4Near,
//        nodes[child].world,
//        mul(nodes[parent].world, nodes[child].local)
//    );
//}
//
//
//// =====================================================
//TEST(TransformNode_static, UpdateStatic_DeepChain)
//{
//    TransformNode nodes[3]{};
//
//    constexpr uint32_t root = 0;
//    constexpr uint32_t mid = 1;
//    constexpr uint32_t leaf = 2;
//    constexpr uint32_t frame = 10;
//
//    nodes[root].parent = TransformNode::INVALID_NODE;
//    nodes[mid].parent = root;
//    nodes[leaf].parent = mid;
//
//    nodes[root].first_child = mid;
//    nodes[mid].first_child = leaf;
//    nodes[leaf].first_child = TransformNode::INVALID_NODE;
//
//    nodes[root].local = mat4_identity();
//    nodes[mid].local = mat4_identity();
//    nodes[leaf].local = mat4_identity();
//
//    nodes[root].world = Mat4{};
//    nodes[mid].world = Mat4{};
//    nodes[leaf].world = Mat4{};
//
//    uint32_t roots[1] = { root };
//    uint32_t count = 1;
//
//    update_static(nodes, roots, count, frame);
//
//    // frame correctness
//    EXPECT_EQ(nodes[root].last_updated_frame, frame);
//    EXPECT_EQ(nodes[mid].last_updated_frame, frame);
//    EXPECT_EQ(nodes[leaf].last_updated_frame, frame);
//
//    // structural correctness (true hierarchy validation)
//    EXPECT_PRED_FORMAT2(
//        Mat4Near,
//        nodes[mid].world,
//        nodes[root].world
//    );
//
//    EXPECT_PRED_FORMAT2(
//        Mat4Near,
//        nodes[leaf].world,
//        nodes[mid].world
//    );
//}
TEST(TransformNode_static, UpdateStatic_MultipleRoots)
{
    TransformNode nodes[4]{};

    constexpr uint32_t a = 0;
    constexpr uint32_t b = 1;
    constexpr uint32_t c = 2;
    constexpr uint32_t d = 3;
    constexpr uint32_t frame = 3;

    // CRITICAL FIX: explicit initialization
    for (auto& n : nodes)
    {
        n.parent = TransformNode::INVALID_NODE;
        n.first_child = TransformNode::INVALID_NODE;
        n.next_sibling = TransformNode::INVALID_NODE;
        n.last_updated_frame = UINT32_MAX;
    }

    nodes[a].first_child = c;
    nodes[b].first_child = d;

    nodes[c].parent = a;
    nodes[d].parent = b;

    nodes[a].local = mat4_identity();
    nodes[b].local = mat4_identity();
    nodes[c].local = mat4_identity();
    nodes[d].local = mat4_identity();

    uint32_t roots[2] = { a, b };
    uint32_t count = 2;

    update_static(nodes, roots, count, frame);

    EXPECT_EQ(nodes[c].last_updated_frame, frame);
    EXPECT_EQ(nodes[d].last_updated_frame, frame);

    EXPECT_PRED_FORMAT2(Mat4Near, nodes[c].world, nodes[a].world);
    EXPECT_PRED_FORMAT2(Mat4Near, nodes[d].world, nodes[b].world);

    EXPECT_PRED_FORMAT2(Mat4Near, nodes[a].world, mat4_identity());
    EXPECT_PRED_FORMAT2(Mat4Near, nodes[b].world, mat4_identity());
}
TEST(TransformNode_static, CollectDirtyRoots_SkipsDirtyChildren)
{
    TransformNode nodes[3]{};

    constexpr uint32_t root = 0;
    constexpr uint32_t child = 1;
    constexpr uint32_t leaf = 2;
    constexpr uint32_t current_frame = 5;

    nodes[root].parent = TransformNode::INVALID_NODE;
    nodes[child].parent = root;
    nodes[leaf].parent = child;

    nodes[root].first_child = child;
    nodes[child].first_child = leaf;
    nodes[leaf].first_child = TransformNode::INVALID_NODE;

    nodes[root].next_sibling = TransformNode::INVALID_NODE;
    nodes[child].next_sibling = TransformNode::INVALID_NODE;
    nodes[leaf].next_sibling = TransformNode::INVALID_NODE;

    // all are stale (dirty relative to frame)
    nodes[root].last_updated_frame = 0;
    nodes[child].last_updated_frame = 0;
    nodes[leaf].last_updated_frame = 0;

    uint32_t roots[3]{};
    uint32_t count = 0;

    collect_dirty_roots(
        nodes,
        3,
        roots,
        count,
        current_frame
    );

    ASSERT_EQ(count, 1u);
    EXPECT_EQ(roots[0], root);
}

