#pragma once

// wz/render/frame_builder.h
//
// Stub — translates RenderIR into RenderFrame.
// RenderFrame is backend-specific and not yet defined.
// This header establishes the interface that will be filled in
// once the backend representation is decided.

#include <render/render_ir.h>

namespace wz::render {

	// Forward declaration — defined by the backend layer.
	struct RenderFrame;

	// ─── FrameBuilder ─────────────────────────────────────────────────────────────
	//
	// Consumes a RenderIR and produces a RenderFrame.
	// The exact contents of RenderFrame depend on the backend.
	// This stub preserves the interface without committing to an implementation.

	// RenderFrame build_frame(const RenderIRStorage& ir);

} // namespace wz::render