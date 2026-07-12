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

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <cstdint>

namespace neurus
{

// Forward declarations — avoid including GeometryPass.h or Scene.h here.
class Scene;

/**
 * @brief Per-frame context for all render passes.
 *
 * Populated once per frame by the renderer and passed (typically as const&)
 * to each pass's RecordCommandBuffer / Draw / Dispatch call.
 */
struct RenderContext
{
	/// @brief Render area dimensions (used for viewport, scissor, dispatch groups).
	vk::Extent2D renderExtent{};

	/// @brief Ring-buffer slot index for per-frame descriptor pools / UBOs.
	uint32_t frameIndex = 0;

	/// @brief Scene data for mesh, light, and camera access (nullable).
	/// Passes access camera via scene->GetActiveCamera(), iterate scene->mesh_list
	/// for meshes, and scene->light_list for lights.
	const Scene* scene = nullptr;

};

} // namespace neurus
