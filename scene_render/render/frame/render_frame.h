#pragma once

// wz/render/frame/render_frame.h

#include <render/ir/render_ir.h>
#include <scene/compile/compiled_scene.h>
#include <cstdint>
#include <memory>
#include <span>

namespace wz::render {

    using namespace wz::scene;


    // ─── Pipeline submission order ────────────────────────────────────────────────
    //
    // Fixed global order. Not negotiable per frame.

    enum class PipelineStage : uint8_t {
        OpaqueGeometry = 0,
        Splat = 1,
        TransparentGeometry = 2,
        Particle = 3,
    };


    // ─── DrawCommand ──────────────────────────────────────────────────────────────
    //
    // A single resolved draw unit. Self-contained — no pointers into
    // CompiledScene or RenderIR. The backend never touches those structures.

    struct DrawCommand {
        PipelineStage  stage{};
        MeshHandle     mesh{ INVALID_MESH };
        MaterialHandle material{ INVALID_MATERIAL };
        Mat4           world{};
        uint64_t       sort_key{ 0 };

        // Splat-specific — valid only when stage == Splat
        Vec3  splat_position{};
        Vec3  splat_scale{ 1.f, 1.f, 1.f };
        Vec4  splat_rotation{ 0.f, 0.f, 0.f, 1.f };
        Vec3  splat_color{ 1.f, 1.f, 1.f };
        float splat_opacity{ 1.f };
        float splat_depth{ 0.f };
    };


    // ─── RenderFrame ──────────────────────────────────────────────────────────────
    //
    // Immutable after construction.
    // All draws in submission order — pipeline stage ordering is baked in.
    // The backend iterates this linearly and submits each command.

    struct RenderFrame {
        std::span<const DrawCommand> commands;
        ViewData                     view{};
    };


    // ─── RenderFrameStorage ───────────────────────────────────────────────────────

    struct RenderFrameStorage {
        std::unique_ptr<std::byte[]> buffer;
        RenderFrame                  frame;
    };


    // ─── build_frame() ───────────────────────────────────────────────────────────
    //
    // Translates RenderIR into RenderFrame.
    // Submission order: opaque → splat → transparent → particle.
    // Each pipeline's draws are already sorted — order is preserved.

    RenderFrameStorage build_frame(
        const RenderIRStorage& ir_storage,
        const CompiledScene& cs)
    {
        const RenderIR& ir = ir_storage.ir;

        const uint32_t total =
            static_cast<uint32_t>(ir.opaque.size())
            + static_cast<uint32_t>(ir.splats.size())
            + static_cast<uint32_t>(ir.transparent.size())
            + static_cast<uint32_t>(ir.particles.size());

        const size_t buf_size =
            sizeof(DrawCommand) * total + alignof(DrawCommand);

        auto buf = std::make_unique<std::byte[]>(buf_size);
        std::byte* ptr = buf.get();
        std::byte* end = ptr + buf_size;

        std::span<DrawCommand> commands_w;
        ptr = wz::core::graph::detail::carve<DrawCommand>(ptr, end, total, commands_w);

        uint32_t out = 0;

        // ── Opaque ────────────────────────────────────────────────────────────────
        for (auto& ref : ir.opaque) {
            const auto& p = cs.opaque[ref.index];
            new (&commands_w[out++]) DrawCommand{
                .stage = PipelineStage::OpaqueGeometry,
                .mesh = p.mesh,
                .material = p.material,
                .world = p.world,
                .sort_key = ref.sort_key,
            };
        }

        // ── Splats ────────────────────────────────────────────────────────────────
        for (auto& ref : ir.splats) {
            const auto& s = cs.splats[ref.index];
            new (&commands_w[out++]) DrawCommand{
                .stage = PipelineStage::Splat,
                .mesh = INVALID_MESH,
                .material = INVALID_MATERIAL,
                .world = {},
                .sort_key = ref.sort_key,
                .splat_position = s.position,
                .splat_scale = s.scale,
                .splat_rotation = s.rotation,
                .splat_color = s.color,
                .splat_opacity = s.opacity,
                .splat_depth = cs.splat_depths[ref.index],
            };
        }

        // ── Transparent ───────────────────────────────────────────────────────────
        for (auto& ref : ir.transparent) {
            const auto& p = cs.transparent[ref.index];
            new (&commands_w[out++]) DrawCommand{
                .stage = PipelineStage::TransparentGeometry,
                .mesh = p.mesh,
                .material = p.material,
                .world = p.world,
                .sort_key = ref.sort_key,
            };
        }

        // ── Particles ─────────────────────────────────────────────────────────────
        for (auto& ref : ir.particles) {
            const auto& p = cs.particles[ref.index];
            new (&commands_w[out++]) DrawCommand{
                .stage = PipelineStage::Particle,
                .mesh = p.mesh,
                .material = p.material,
                .world = p.world,
                .sort_key = ref.sort_key,
            };
        }

        RenderFrame frame{
            .commands = commands_w,
            .view = cs.view,
        };

        return RenderFrameStorage{ std::move(buf), frame };
    }

} // namespace wz::render