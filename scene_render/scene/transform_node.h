#pragma once

// wz/scene/transform_node.h

#include <math/math_types.h>
#include <math/mat4.h>
#include <cstdint>

namespace wz::scene {

    using namespace wz::math;

    // ─── Flags ────────────────────────────────────────────────────────────────────

    namespace TransformNodeFlag {
        constexpr uint16_t None = 0;
        constexpr uint16_t RenderDomain = 0x000F;
        constexpr uint16_t SceneDomain = 0x00F0;
        constexpr uint16_t UpdateDomain = 0x0F00;
    }

    // ─── TransformNode ────────────────────────────────────────────────────────────
    //
    // Payload for Polytree<TransformNode, NoEdge>.
    // Topology (parent, children) lives entirely in the polytree — not here.
    // This struct is purely data.

    struct TransformNode {
        enum class MotionType : uint8_t {
            Static,
            Animated
        };

        Mat4       local{ mat4_identity() };                             // local space transform
        Mat4       world{ mat4_identity() };                             // world space — computed, not set directly
        uint32_t   last_updated_frame{ INVALID_FRAME }; // frame counter for dirty tracking
        uint16_t   flags{ 0 };
        MotionType motion_type{ MotionType::Static };

        static constexpr uint32_t INVALID_FRAME = 0xFFFF'FFFFu;
    };

} // namespace wz::scene