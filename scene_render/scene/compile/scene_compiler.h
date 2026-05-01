#pragma once

// wz/scene/scene_compiler.h

#include <scene/compile/compiled_scene.h>
#include <scene/scene_graph.h>
#include <graph/static_polytree.h>
#include <cfloat>

namespace wz::scene {

    using namespace wz::core::graph;

    // ─── update_view() ───────────────────────────────────────────────────────────
    //
    // Rebuilds only the view-dependent portion of CompiledScene.
    // Rewrites view_buffer — splat_depths updated in-place,
    // transparent and particle primitives rebuilt with new depth values.
    // stable_buffer is never touched.
    //
    // Call every frame the camera moves. O(transparent + particles + splats).

    void update_view(CompiledSceneStorage& storage, const ViewData& view);

    // ─── compile() ───────────────────────────────────────────────────────────────
    //
    // Builds stable_buffer: opaque, splats (no depth), lights.
    // Builds view_buffer:   transparent, particles, splat_depths.
    // Calls update_view() to populate all depth values for the initial view.

    CompiledSceneStorage compile(
        const SceneGraph& g,
        std::span<const RenderableDescriptor> descs,
        std::span<const LightRecord>          lights,
        const ViewData& view);
 
} // namespace wz::scene