#pragma once

// wz/scene/compiled_scene.h

#include <math/math_types.h>
#include <math/mat4.h>
#include <cstdint>
#include <span>
#include <memory>

namespace wz::scene {

    using namespace wz::math;

    // ─── Asset handles ────────────────────────────────────────────────────────────

    using MeshHandle = uint32_t;
    using MaterialHandle = uint32_t;

    static constexpr MeshHandle     INVALID_MESH = 0xFFFF'FFFFu;
    static constexpr MaterialHandle INVALID_MATERIAL = 0xFFFF'FFFFu;


    // ─── AABB ─────────────────────────────────────────────────────────────────────

    struct AABB {
        Vec3 min{};
        Vec3 max{};
    };


    // ─── Pipeline classification ──────────────────────────────────────────────────

    enum class RenderPipeline : uint8_t {
        None,
        OpaqueGeometry,
        TransparentGeometry,
        Splat,
        Particle,
    };


    // ─── Pipeline primitive types ─────────────────────────────────────────────────
    //
    // Stable primitives carry no view-dependent data.
    // View-dependent primitives (transparent, particle) carry depth
    // because their counts are small and they are rebuilt entirely by update_view().
    // Splat depth is separate — splat counts can be large, depth is the only
    // thing that changes per frame.

    struct OpaqueGeometryPrimitive {
        Mat4           world{};
        AABB           bounds{};
        MeshHandle     mesh{ INVALID_MESH };
        MaterialHandle material{ INVALID_MATERIAL };
    };

    struct TransparentGeometryPrimitive {
        Mat4           world{};
        AABB           bounds{};
        MeshHandle     mesh{ INVALID_MESH };
        MaterialHandle material{ INVALID_MATERIAL };
        float          depth{ 0.f };
    };

    struct SplatPrimitive {
        Vec3  position{};
        Vec3  scale{ 1.f, 1.f, 1.f };
        Vec4  rotation{ 0.f, 0.f, 0.f, 1.f };
        Vec3  color{ 1.f, 1.f, 1.f };
        float opacity{ 1.f };
        // no depth — lives in a parallel splat_depths span
    };

    struct ParticlePrimitive {
        Mat4           world{};
        Vec4           color{ 1.f, 1.f, 1.f, 1.f };
        MeshHandle     mesh{ INVALID_MESH };
        MaterialHandle material{ INVALID_MATERIAL };
        float          depth{ 0.f };
    };


    // ─── Light record ─────────────────────────────────────────────────────────────

    enum class LightType : uint8_t { Directional, Point, Spot };

    struct LightRecord {
        Vec3      position{};
        Vec3      direction{};
        Vec3      color{ 1.f, 1.f, 1.f };
        float     intensity{ 1.f };
        float     range{ 10.f };
        LightType type{ LightType::Point };
    };


    // ─── ViewData ─────────────────────────────────────────────────────────────────

    struct ViewData {
        Mat4 view{};
        Mat4 projection{};
        Mat4 view_projection{};
        Vec3 camera_position{};
    };


    // ─── CompiledScene ────────────────────────────────────────────────────────────
    //
    // Two categories of data with different update frequencies:
    //
    // Stable (compile() only):
    //   opaque, splats, lights — valid until scene structure changes
    //
    // View-dependent (update_view() every frame camera moves):
    //   transparent, particles — small counts, rebuilt entirely
    //   splat_depths           — parallel to splats, large count, in-place update

    struct CompiledScene {
        // Stable
        std::span<const OpaqueGeometryPrimitive> opaque;
        std::span<const SplatPrimitive>          splats;
        std::span<const LightRecord>             lights;

        // View-dependent
        std::span<const TransparentGeometryPrimitive> transparent;
        std::span<const ParticlePrimitive>            particles;
        std::span<float>                              splat_depths; // mutable, parallel to splats

        ViewData view{};
    };


    // ─── Storage ──────────────────────────────────────────────────────────────────
    //
    // Two separate allocations — stable and view-dependent.
    // update_view() only rewrites view_buffer, never stable_buffer.

    struct CompiledSceneStorage {
        std::unique_ptr<std::byte[]> stable_buffer;
        std::unique_ptr<std::byte[]> view_buffer;
        CompiledScene                scene;
    };


    // ─── RenderableDescriptor ─────────────────────────────────────────────────────

    struct SplatDescriptor {
        Vec3  scale{ 1.f, 1.f, 1.f };
        Vec4  rotation{ 0.f, 0.f, 0.f, 1.f };
        Vec3  color{ 1.f, 1.f, 1.f };
        float opacity{ 1.f };
    };

    struct RenderableDescriptor {
        RenderPipeline  pipeline{ RenderPipeline::None };
        MeshHandle      mesh{ INVALID_MESH };
        MaterialHandle  material{ INVALID_MATERIAL };
        AABB            local_bounds{};
        SplatDescriptor splat_data{};
        bool            visible{ true };
    };

} // namespace wz::scene