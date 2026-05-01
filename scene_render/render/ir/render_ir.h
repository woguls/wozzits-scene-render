#pragma once

// wz/render/render_ir.h
//
// Backend-agnostic intermediate representation.
// Consumes CompiledScene. Produces sorted, culled draw references.
// No GPU types. No backend dependencies.

#include <scene/compile/compiled_scene.h>
#include <graph/static_dag.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>

namespace wz::render {

    using namespace wz::scene;

    // ─── DrawRef ──────────────────────────────────────────────────────────────────
    //
    // Lightweight reference into a CompiledScene pipeline span.
    // Sorting operates on DrawRefs — no data is moved.

    struct DrawRef {
        uint32_t index;    // into the relevant CompiledScene span
        uint64_t sort_key; // retained for debugging and secondary sorts
    };

    // ─── RenderIR ─────────────────────────────────────────────────────────────────
    //
    // Sorted draw reference lists, one per pipeline.
    // Holds a non-owning pointer to the CompiledScene it was built from —
    // the CompiledScene must outlive the RenderIR.
    //
    // Buffer layout (single allocation):
    //   [ DrawRef * opaque_count      ]
    //   [ DrawRef * transparent_count ]
    //   [ DrawRef * splat_count       ]
    //   [ DrawRef * particle_count    ]

    struct RenderIR {
        // Sorted draw reference lists
        std::span<const DrawRef> opaque;       // front-to-back by material
        std::span<const DrawRef> transparent;  // back-to-front by depth
        std::span<const DrawRef> splats;       // back-to-front by depth
        std::span<const DrawRef> particles;    // back-to-front by depth

        // Non-owning reference to source data
        const CompiledScene* source{ nullptr };
    };

    // ─── RenderIRStorage ──────────────────────────────────────────────────────────

    struct RenderIRStorage {
        std::unique_ptr<std::byte[]> buffer;
        RenderIR                     ir;
    };


    // ─── build_render_ir() ────────────────────────────────────────────────────────
    //
    // Builds sorted DrawRef lists from a CompiledScene.
    // The CompiledScene must already have update_view() applied for the current frame.
    // O((N log N) per pipeline) for sorting.

    RenderIRStorage build_render_ir(const CompiledScene& cs);

    // ─── update_render_ir() ───────────────────────────────────────────────────────
    //
    // Re-sorts view-dependent pipelines after update_view() has been called.
    // Opaque sort is stable — skipped unless forced.
    // Call this every frame the camera moves, after update_view().

    void update_render_ir(RenderIRStorage& storage);

} // namespace wz::render