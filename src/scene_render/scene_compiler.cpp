#include <scene/compile/scene_compiler.h>

namespace wz::scene {

    using namespace wz::core::graph;

    namespace detail {

        // ─── Geometry helpers ─────────────────────────────────────────────────────────

        AABB transform_aabb(const AABB& local, const Mat4& world)
        {
            Vec3 corners[8] = {
                { local.min.x, local.min.y, local.min.z },
                { local.max.x, local.min.y, local.min.z },
                { local.min.x, local.max.y, local.min.z },
                { local.max.x, local.max.y, local.min.z },
                { local.min.x, local.min.y, local.max.z },
                { local.max.x, local.min.y, local.max.z },
                { local.min.x, local.max.y, local.max.z },
                { local.max.x, local.max.y, local.max.z },
            };
            AABB out;
            out.min = { FLT_MAX,  FLT_MAX,  FLT_MAX };
            out.max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
            for (auto& c : corners) {
                Vec3 tc = mul_point(world, c);
                out.min.x = std::min(out.min.x, tc.x);
                out.min.y = std::min(out.min.y, tc.y);
                out.min.z = std::min(out.min.z, tc.z);
                out.max.x = std::max(out.max.x, tc.x);
                out.max.y = std::max(out.max.y, tc.y);
                out.max.z = std::max(out.max.z, tc.z);
            }
            return out;
        }

        inline Vec3 aabb_center(const AABB& b)
        {
            return {
                (b.min.x + b.max.x) * 0.5f,
                (b.min.y + b.max.y) * 0.5f,
                (b.min.z + b.max.z) * 0.5f,
            };
        }

        inline float view_depth(const Mat4& view, const Vec3& world_pos)
        {
            Vec3 vs = mul_point(view, world_pos);
            return -vs.z;
        }


        // ─── Primitive counts ─────────────────────────────────────────────────────────

        struct PrimitiveCounts {
            uint32_t opaque{ 0 };
            uint32_t transparent{ 0 };
            uint32_t splats{ 0 };
            uint32_t particles{ 0 };
            uint32_t lights{ 0 };
        };

        PrimitiveCounts count_primitives(
            const SceneGraph& g,
            std::span<const RenderableDescriptor> descs,
            uint32_t                              light_count)
        {
            PrimitiveCounts c;
            c.lights = light_count;
            for (NodeHandle n : topo_order(g)) {
                if (n >= descs.size())   continue;
                const auto& d = descs[n];
                if (!d.visible)          continue;
                switch (d.pipeline) {
                case RenderPipeline::OpaqueGeometry:
                    if (d.mesh != INVALID_MESH) ++c.opaque;      break;
                case RenderPipeline::TransparentGeometry:
                    if (d.mesh != INVALID_MESH) ++c.transparent; break;
                case RenderPipeline::Splat:
                    ++c.splats;                                   break;
                case RenderPipeline::Particle:
                    if (d.mesh != INVALID_MESH) ++c.particles;   break;
                case RenderPipeline::None:
                    break;
                }
            }
            return c;
        }
    } // namespace detail

    void update_view(CompiledSceneStorage& storage, const ViewData& view)
    {
        CompiledScene& cs = storage.scene;

        // Recompute splat depths in-place — no reallocation needed
        for (uint32_t i = 0; i < cs.splats.size(); ++i)
            cs.splat_depths[i] = detail::view_depth(view.view, cs.splats[i].position);

        // Transparent and particle counts are stable — rewrite depth fields only
        for (auto& p : cs.transparent) {
            Vec3 ctr = detail::aabb_center(p.bounds);
            const_cast<TransparentGeometryPrimitive&>(p).depth
                = detail::view_depth(view.view, ctr);
        }

        for (auto& p : cs.particles) {
            Vec3 pos = { p.world.m[12], p.world.m[13], p.world.m[14] };
            const_cast<ParticlePrimitive&>(p).depth
                = detail::view_depth(view.view, pos);
        }

        cs.view = view;
    }

    CompiledSceneStorage compile(
        const SceneGraph& g,
        std::span<const RenderableDescriptor> descs,
        std::span<const LightRecord>          lights,
        const ViewData& view)
    {
        auto counts = detail::count_primitives(
            g, descs, static_cast<uint32_t>(lights.size()));

        // ── Stable buffer ─────────────────────────────────────────────────────────

        const size_t stable_size =
            sizeof(OpaqueGeometryPrimitive) * counts.opaque
            + alignof(OpaqueGeometryPrimitive)
            + sizeof(SplatPrimitive) * counts.splats
            + alignof(SplatPrimitive)
            + sizeof(LightRecord) * counts.lights
            + alignof(LightRecord);

        auto stable_buf = std::make_unique<std::byte[]>(stable_size);
        std::byte* sp = stable_buf.get();
        std::byte* se = sp + stable_size;

        std::span<OpaqueGeometryPrimitive> opaque_w;
        std::span<SplatPrimitive>          splats_w;
        std::span<LightRecord>             lights_w;

        sp = wz::core::graph::detail::carve<OpaqueGeometryPrimitive>(
            sp, se, counts.opaque, opaque_w);
        sp = wz::core::graph::detail::carve<SplatPrimitive>(
            sp, se, counts.splats, splats_w);
        sp = wz::core::graph::detail::carve<LightRecord>(
            sp, se, counts.lights, lights_w);

        // ── View buffer ───────────────────────────────────────────────────────────

        const size_t view_size =
            sizeof(TransparentGeometryPrimitive) * counts.transparent
            + alignof(TransparentGeometryPrimitive)
            + sizeof(ParticlePrimitive) * counts.particles
            + alignof(ParticlePrimitive)
            + sizeof(float) * counts.splats
            + alignof(float);

        auto view_buf = std::make_unique<std::byte[]>(view_size);
        std::byte* vp = view_buf.get();
        std::byte* ve = vp + view_size;

        std::span<TransparentGeometryPrimitive> transparent_w;
        std::span<ParticlePrimitive>            particles_w;
        std::span<float>                        splat_depths_w;

        vp = wz::core::graph::detail::carve<TransparentGeometryPrimitive>(
            vp, ve, counts.transparent, transparent_w);
        vp = wz::core::graph::detail::carve<ParticlePrimitive>(
            vp, ve, counts.particles, particles_w);
        vp = wz::core::graph::detail::carve<float>(
            vp, ve, counts.splats, splat_depths_w);

        // ── Fill stable data ──────────────────────────────────────────────────────

        uint32_t oi = 0, si = 0, ti = 0, pi = 0;

        for (NodeHandle n : topo_order(g)) {
            if (n >= descs.size()) continue;
            const auto& d = descs[n];
            const auto& node = node_data(g, n);
            if (!d.visible)        continue;

            switch (d.pipeline) {

            case RenderPipeline::OpaqueGeometry: {
                if (d.mesh == INVALID_MESH) break;
                AABB wb = detail::transform_aabb(d.local_bounds, node.world);
                new (&opaque_w[oi++]) OpaqueGeometryPrimitive{
                    .world = node.world,
                    .bounds = wb,
                    .mesh = d.mesh,
                    .material = d.material,
                };
                break;
            }

            case RenderPipeline::Splat: {
                Vec3 pos = { node.world.m[12],
                             node.world.m[13],
                             node.world.m[14] };
                new (&splats_w[si++]) SplatPrimitive{
                    .position = pos,
                    .scale = d.splat_data.scale,
                    .rotation = d.splat_data.rotation,
                    .color = d.splat_data.color,
                    .opacity = d.splat_data.opacity,
                };
                break;
            }

            case RenderPipeline::TransparentGeometry: {
                if (d.mesh == INVALID_MESH) break;
                AABB wb = detail::transform_aabb(d.local_bounds, node.world);
                new (&transparent_w[ti++]) TransparentGeometryPrimitive{
                    .world = node.world,
                    .bounds = wb,
                    .mesh = d.mesh,
                    .material = d.material,
                    .depth = 0.f, // set by update_view()
                };
                break;
            }

            case RenderPipeline::Particle: {
                if (d.mesh == INVALID_MESH) break;
                new (&particles_w[pi++]) ParticlePrimitive{
                    .world = node.world,
                    .color = { 1.f, 1.f, 1.f, 1.f },
                    .mesh = d.mesh,
                    .material = d.material,
                    .depth = 0.f, // set by update_view()
                };
                break;
            }

            case RenderPipeline::None: break;
            }
        }

        // Lights
        for (uint32_t i = 0; i < counts.lights; ++i)
            new (&lights_w[i]) LightRecord(lights[i]);

        // Initialise splat_depths to zero — update_view() will fill them
        for (uint32_t i = 0; i < counts.splats; ++i)
            splat_depths_w[i] = 0.f;

        CompiledSceneStorage storage{
            .stable_buffer = std::move(stable_buf),
            .view_buffer = std::move(view_buf),
            .scene = CompiledScene {
                .opaque = opaque_w,
                .splats = splats_w,
                .lights = lights_w,
                .transparent = transparent_w,
                .particles = particles_w,
                .splat_depths = splat_depths_w,
                .view = {},
            }
        };

        // Populate all depth values for the initial view
        update_view(storage, view);

        return storage;
    }
}// namespace wz::scene