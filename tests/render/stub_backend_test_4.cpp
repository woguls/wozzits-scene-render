#include <gtest/gtest.h>
#include <math/mat4.h>
#include <render/backend/stub_backend.h>
#include <render/frame/render_frame.h>
#include <render/ir/render_ir.h>
#include <scene/compile/scene_compiler.h>

using namespace wz::render;
using namespace wz::render::backend;
using namespace wz::scene;
using namespace wz::core::graph;
using namespace wz::math;

// ─── Helpers ──────────────────────────────────────────────────────────────────

namespace {

    Mat4 translation_z(float z)
    {
        Mat4 m = mat4_identity();
        m.m[14] = z;
        return m;
    }

    AABB unit_box()
    {
        return { Vec3{-0.5f,-0.5f,-0.5f}, Vec3{0.5f,0.5f,0.5f} };
    }

    ViewData camera_at_z(float z)
    {
        ViewData v{};
        v.view = mat4_identity();
        v.view.m[14] = -z;
        v.projection = mat4_identity();
        v.view_projection = mat4_identity();
        v.camera_position = Vec3{ 0.f, 0.f, z };
        return v;
    }

    // Full pipeline helper — builds scene, compiles, builds IR, builds frame
    struct Pipeline {
        SceneStorage          scene;
        CompiledSceneStorage  compiled;
        RenderIRStorage       ir;
        RenderFrameStorage    frame;
        SubmitResult          result;
    };

    Pipeline make_pipeline(ViewData view = camera_at_z(0.f))
    {
        SceneBuilder b;

        auto make_node = [](float z, uint16_t flags = TransformNodeFlag::RenderDomain) {
            TransformNode n{};
            n.local = translation_z(z);
            n.flags = flags;
            return n;
            };

        auto root = add_node(b, make_node(0.f, 0));
        auto op0 = add_node(b, make_node(1.f)); // opaque   mat=0
        auto op1 = add_node(b, make_node(2.f)); // opaque   mat=2
        auto op2 = add_node(b, make_node(3.f)); // opaque   mat=1
        auto tr0 = add_node(b, make_node(5.f)); // transparent depth=5
        auto tr1 = add_node(b, make_node(2.f)); // transparent depth=2
        auto sp0 = add_node(b, make_node(4.f)); // splat depth=4
        auto sp1 = add_node(b, make_node(1.f)); // splat depth=1
        auto pa0 = add_node(b, make_node(3.f)); // particle depth=3

        for (auto n : { op0,op1,op2,tr0,tr1,sp0,sp1,pa0 })
            add_edge(b, root, n);

        auto storage = build(std::move(b));
        assert(storage.has_value());
        propagate_all(storage->polytree);

        std::vector<RenderableDescriptor> descs(9);
        descs[root] = { RenderPipeline::None };
        descs[op0] = { RenderPipeline::OpaqueGeometry,      0u, 0u, unit_box() };
        descs[op1] = { RenderPipeline::OpaqueGeometry,      2u, 2u, unit_box() };
        descs[op2] = { RenderPipeline::OpaqueGeometry,      1u, 1u, unit_box() };
        descs[tr0] = { RenderPipeline::TransparentGeometry, 0u, 0u, unit_box() };
        descs[tr1] = { RenderPipeline::TransparentGeometry, 1u, 1u, unit_box() };
        descs[sp0] = { RenderPipeline::Splat, INVALID_MESH, INVALID_MATERIAL,
                        {}, SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},0.9f} };
        descs[sp1] = { RenderPipeline::Splat, INVALID_MESH, INVALID_MATERIAL,
                        {}, SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},0.5f} };
        descs[pa0] = { RenderPipeline::Particle, 0u, 0u, unit_box() };

        auto compiled = compile(storage->polytree, descs, {}, view);
        auto ir = build_render_ir(compiled.scene);
        auto frame = build_frame(ir, compiled.scene);
        auto result = submit(frame.frame);

        return Pipeline{
            std::move(*storage),
            std::move(compiled),
            std::move(ir),
            std::move(frame),
            std::move(result)
        };
    }

} // namespace


// ─── Total command count ──────────────────────────────────────────────────────

TEST(StubBackendSpec, TotalCommandCountIsCorrect)
{
    auto p = make_pipeline();
    // 3 opaque + 2 splats + 2 transparent + 1 particle = 8
    EXPECT_EQ(p.result.total(), 8u);
}

TEST(StubBackendSpec, PerPipelineCountsAreCorrect)
{
    auto p = make_pipeline();
    EXPECT_EQ(p.result.opaque_count(), 3u);
    EXPECT_EQ(p.result.splat_count(), 2u);
    EXPECT_EQ(p.result.transparent_count(), 2u);
    EXPECT_EQ(p.result.particle_count(), 1u);
}


// ─── Submission order — pipeline stages ───────────────────────────────────────

TEST(StubBackendSpec, OpaqueCommandsSubmittedFirst)
{
    auto p = make_pipeline();
    // First 3 commands must be opaque
    for (uint32_t i = 0; i < 3; ++i)
        EXPECT_EQ(p.frame.frame.commands[i].stage,
            PipelineStage::OpaqueGeometry)
        << "command " << i << " should be opaque";
}

TEST(StubBackendSpec, SplatsSubmittedAfterOpaque)
{
    auto p = make_pipeline();
    // Commands 3-4 must be splats
    for (uint32_t i = 3; i < 5; ++i)
        EXPECT_EQ(p.frame.frame.commands[i].stage, PipelineStage::Splat)
        << "command " << i << " should be splat";
}

TEST(StubBackendSpec, TransparentSubmittedAfterSplats)
{
    auto p = make_pipeline();
    // Commands 5-6 must be transparent
    for (uint32_t i = 5; i < 7; ++i)
        EXPECT_EQ(p.frame.frame.commands[i].stage,
            PipelineStage::TransparentGeometry)
        << "command " << i << " should be transparent";
}

TEST(StubBackendSpec, ParticlesSubmittedLast)
{
    auto p = make_pipeline();
    EXPECT_EQ(p.frame.frame.commands[7].stage, PipelineStage::Particle);
}


// ─── Sort order preserved through frame ───────────────────────────────────────

TEST(StubBackendSpec, OpaqueCommandsSortedByMaterial)
{
    auto p = make_pipeline();
    // Keys non-decreasing across opaque commands
    for (uint32_t i = 1; i < 3; ++i)
        EXPECT_LE(p.frame.frame.commands[i - 1].sort_key,
            p.frame.frame.commands[i].sort_key)
        << "opaque sort broken at command " << i;
}

TEST(StubBackendSpec, TransparentCommandsSortedBackToFront)
{
    auto p = make_pipeline(camera_at_z(0.f));
    // Commands 5-6 are transparent — keys non-decreasing = furthest first
    EXPECT_LE(p.frame.frame.commands[5].sort_key,
        p.frame.frame.commands[6].sort_key);
}

TEST(StubBackendSpec, SplatCommandsSortedBackToFront)
{
    auto p = make_pipeline(camera_at_z(0.f));
    // Commands 3-4 are splats — furthest first
    EXPECT_LE(p.frame.frame.commands[3].sort_key,
        p.frame.frame.commands[4].sort_key);
}


// ─── Data integrity through the pipeline ─────────────────────────────────────

TEST(StubBackendSpec, SplatCommandCarriesCorrectData)
{
    auto p = make_pipeline(camera_at_z(0.f));

    // Find splat commands and verify opacity values are preserved
    std::vector<float> opacities;
    for (auto& cmd : p.frame.frame.commands)
        if (cmd.stage == PipelineStage::Splat)
            opacities.push_back(cmd.splat_opacity);

    ASSERT_EQ(opacities.size(), 2u);
    // Both splat opacities should be present (0.9 and 0.5, order may vary)
    bool has_09 = false, has_05 = false;
    for (float op : opacities) {
        if (std::abs(op - 0.9f) < 1e-5f) has_09 = true;
        if (std::abs(op - 0.5f) < 1e-5f) has_05 = true;
    }
    EXPECT_TRUE(has_09);
    EXPECT_TRUE(has_05);
}

TEST(StubBackendSpec, OpaqueCommandCarriesCorrectMesh)
{
    auto p = make_pipeline();

    std::vector<uint32_t> meshes;
    for (auto& cmd : p.frame.frame.commands)
        if (cmd.stage == PipelineStage::OpaqueGeometry)
            meshes.push_back(cmd.mesh);

    ASSERT_EQ(meshes.size(), 3u);
    // All three mesh handles 0,1,2 should be present
    for (uint32_t m : {0u, 1u, 2u})
        EXPECT_NE(std::find(meshes.begin(), meshes.end(), m), meshes.end())
        << "mesh " << m << " missing from opaque commands";
}

TEST(StubBackendSpec, ViewDataReachesFrame)
{
    auto p = make_pipeline(camera_at_z(7.f));
    EXPECT_FLOAT_EQ(p.frame.frame.view.camera_position.z, 7.f);
}


// ─── Log output ───────────────────────────────────────────────────────────────

TEST(StubBackendSpec, LogHasOneEntryPerCommand)
{
    auto p = make_pipeline();
    EXPECT_EQ(p.result.log.size(), p.result.total());
}

TEST(StubBackendSpec, LogEntriesAreNonEmpty)
{
    auto p = make_pipeline();
    for (auto& entry : p.result.log)
        EXPECT_FALSE(entry.empty());
}


// ─── Full pipeline end-to-end ─────────────────────────────────────────────────

TEST(StubBackendSpec, EndToEndEmptyScene)
{
    SceneBuilder b;
    TransformNode root{};
    add_node(b, root);
    auto s = build(std::move(b));
    ASSERT_TRUE(s.has_value());
    propagate_all(s->polytree);

    std::vector<RenderableDescriptor> descs(1);
    descs[0] = { RenderPipeline::None };

    auto compiled = compile(s->polytree, descs, {}, camera_at_z(0.f));
    auto ir = build_render_ir(compiled.scene);
    auto frame = build_frame(ir, compiled.scene);
    auto result = submit(frame.frame);

    EXPECT_EQ(result.total(), 0u);
    EXPECT_TRUE(result.log.empty());
}