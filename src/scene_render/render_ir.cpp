#include <render/ir/render_ir.h>

namespace wz::render {

    using namespace wz::scene;


// ─── Sort key generation ──────────────────────────────────────────────────────
//
// Each pipeline defines its own key format.
// Keys are only compared within a pipeline — formats need not be compatible.

    namespace detail {

        // Opaque: sort by material to minimize state changes.
        // Lower material handle = earlier submission.
        inline uint64_t opaque_key(const OpaqueGeometryPrimitive& p)
        {
            return static_cast<uint64_t>(p.material) << 32
                | static_cast<uint64_t>(p.mesh);
        }

        // Depth-based back-to-front key.
        // Converts float depth to uint32 preserving order for positive depths.
        // Higher depth (further from camera) = lower key = sorted first.
        inline uint64_t depth_key_back_to_front(float depth)
        {
            uint32_t bits;
            std::memcpy(&bits, &depth, sizeof(float));
            // Flip all bits to reverse order: larger float = smaller uint
            return static_cast<uint64_t>(~bits);
        }
    } // namespace detail
    
    RenderIRStorage build_render_ir(const CompiledScene& cs)
    {
        const uint32_t opaque_count = static_cast<uint32_t>(cs.opaque.size());
        const uint32_t transparent_count = static_cast<uint32_t>(cs.transparent.size());
        const uint32_t splat_count = static_cast<uint32_t>(cs.splats.size());
        const uint32_t particle_count = static_cast<uint32_t>(cs.particles.size());

        const size_t buf_size =
            sizeof(DrawRef) * opaque_count + alignof(DrawRef)
            + sizeof(DrawRef) * transparent_count + alignof(DrawRef)
            + sizeof(DrawRef) * splat_count + alignof(DrawRef)
            + sizeof(DrawRef) * particle_count + alignof(DrawRef);

        auto buf = std::make_unique<std::byte[]>(buf_size);
        std::byte* ptr = buf.get();
        std::byte* end = ptr + buf_size;

        std::span<DrawRef> opaque_w;
        std::span<DrawRef> transparent_w;
        std::span<DrawRef> splats_w;
        std::span<DrawRef> particles_w;

        ptr = wz::core::graph::detail::carve<DrawRef>(ptr, end, opaque_count, opaque_w);
        ptr = wz::core::graph::detail::carve<DrawRef>(ptr, end, transparent_count, transparent_w);
        ptr = wz::core::graph::detail::carve<DrawRef>(ptr, end, splat_count, splats_w);
        ptr = wz::core::graph::detail::carve<DrawRef>(ptr, end, particle_count, particles_w);

        // ── Opaque: sort by material key, ascending ───────────────────────────────

        for (uint32_t i = 0; i < opaque_count; ++i)
            new (&opaque_w[i]) DrawRef{ i, detail::opaque_key(cs.opaque[i]) };

        std::sort(opaque_w.begin(), opaque_w.end(),
            [](const DrawRef& a, const DrawRef& b) {
                return a.sort_key < b.sort_key;
            });

        // ── Transparent: sort back-to-front by depth ──────────────────────────────

        for (uint32_t i = 0; i < transparent_count; ++i)
            new (&transparent_w[i]) DrawRef{
                i, detail::depth_key_back_to_front(cs.transparent[i].depth) };

        std::sort(transparent_w.begin(), transparent_w.end(),
            [](const DrawRef& a, const DrawRef& b) {
                return a.sort_key < b.sort_key;
            });

        // ── Splats: sort back-to-front by depth ───────────────────────────────────
        //
        // Splat depths live in the parallel splat_depths span.

        for (uint32_t i = 0; i < splat_count; ++i)
            new (&splats_w[i]) DrawRef{
                i, detail::depth_key_back_to_front(cs.splat_depths[i]) };

        std::sort(splats_w.begin(), splats_w.end(),
            [](const DrawRef& a, const DrawRef& b) {
                return a.sort_key < b.sort_key;
            });

        // ── Particles: sort back-to-front by depth ────────────────────────────────

        for (uint32_t i = 0; i < particle_count; ++i)
            new (&particles_w[i]) DrawRef{
                i, detail::depth_key_back_to_front(cs.particles[i].depth) };

        std::sort(particles_w.begin(), particles_w.end(),
            [](const DrawRef& a, const DrawRef& b) {
                return a.sort_key < b.sort_key;
            });

        RenderIR ir{
            .opaque = opaque_w,
            .transparent = transparent_w,
            .splats = splats_w,
            .particles = particles_w,
            .source = &cs,
        };

        return RenderIRStorage{ std::move(buf), ir };
    }

    void update_render_ir(RenderIRStorage& storage)
    {
        RenderIR& ir = storage.ir;
        const CompiledScene& cs = *ir.source;

        // Re-sort transparent
        auto tw = std::span<DrawRef>(
            const_cast<DrawRef*>(ir.transparent.data()), ir.transparent.size());
        for (auto& ref : tw)
            ref.sort_key = detail::depth_key_back_to_front(cs.transparent[ref.index].depth);
        std::sort(tw.begin(), tw.end(),
            [](const DrawRef& a, const DrawRef& b) { return a.sort_key < b.sort_key; });

        // Re-sort splats
        auto sw = std::span<DrawRef>(
            const_cast<DrawRef*>(ir.splats.data()), ir.splats.size());
        for (auto& ref : sw)
            ref.sort_key = detail::depth_key_back_to_front(cs.splat_depths[ref.index]);
        std::sort(sw.begin(), sw.end(),
            [](const DrawRef& a, const DrawRef& b) { return a.sort_key < b.sort_key; });

        // Re-sort particles
        auto pw = std::span<DrawRef>(
            const_cast<DrawRef*>(ir.particles.data()), ir.particles.size());
        for (auto& ref : pw)
            ref.sort_key = detail::depth_key_back_to_front(cs.particles[ref.index].depth);
        std::sort(pw.begin(), pw.end(),
            [](const DrawRef& a, const DrawRef& b) { return a.sort_key < b.sort_key; });
    }

} // namespace wz::scene