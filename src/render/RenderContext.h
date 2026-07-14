/**
 * @file RenderContext.h
 * @brief Per-frame context data passed to all render passes.
 *
 * RenderContext aggregates the per-frame state that multiple passes (Geometry,
 * Lighting, SSAO, IBL) need to record their commands: render extent for
 * viewport/scissor, frame index for descriptor ring-buffer slots, camera
 * matrices for shading, and optional scene pointers for draw batches and
 * light data.
 *
 * Architecture:
 * - Pure data struct with no methods — owned and populated by the caller
 *   (DeferredRenderer or test fixture) each frame.
 * - Passes iterate scene->mesh_list for meshes and scene->light_list for lights.
 * - Nullable pointer field (`scene`) lets passes that don't need it
 *   (e.g. standalone tests) work without a scene.
 *
 * @note This is NOT a GPU push-constant block — it's CPU-side metadata
 *       carried through the pass execution chain.
 */

#pragma once

#include <cstdint>

#include "scene/UID.h"

namespace neurus
{

/**
 * @brief Per-frame context for all render passes.
 *
 * Populated once per frame by the renderer and passed (typically as const&)
 * to each pass's RecordCommandBuffer / Draw / Dispatch call.
 */
struct RenderContext
{
	/// @brief Render area width in pixels (used for viewport, scissor, dispatch groups).
	uint32_t width = 0;

	/// @brief Render area height in pixels (used for viewport, scissor, dispatch groups).
	uint32_t height = 0;

	/// @brief Ring-buffer slot index for per-frame descriptor pools / UBOs.
	uint32_t frameIndex = 0;

	/// @brief Scene reference for mesh, light, and camera access (nullable).
	/// Passes cast to Scene* via static_cast<const Scene*>(ctx.scene) to access
	/// GetActiveCamera(), mesh_list, light_list, and env_list.
	const UID* scene = nullptr;

	/// @brief Opaque pointer to RenderConfig (lifetime managed by Editor/UI layer).
	/// Passes cast to RenderConfig* to read quality/feature flags set by the user.
	void* config = nullptr;

};

} // namespace neurus
