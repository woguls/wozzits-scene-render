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
        const CompiledScene& cs);

} // namespace wz::render