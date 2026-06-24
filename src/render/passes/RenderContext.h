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
 * - Forward-declares GeometryRenderItem and Scene to avoid coupling to
 *   GeometryPass.h or Scene.h.
 * - Nullable pointer fields (`renderItems`, `scene`) let passes that don't
 *   need them (e.g. LightingPass, SSAOPass) ignore the data without
 *   special-case overloads.
 *
 * @note This is NOT a GPU push-constant block — it's CPU-side metadata
 *       carried through the pass execution chain.
 */

#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <vector>

namespace neurus
{

// Forward declarations — avoid including GeometryPass.h or Scene.h here.
struct GeometryRenderItem;
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

	/// @brief Combined view-projection matrix (clip-space transform).
	glm::mat4 viewProj{1.0f};

	/// @brief View matrix (world-to-view, used for normal transforms).
	glm::mat4 view{1.0f};

	/// @brief Camera position in world-space (for specular, falloff, etc.).
	glm::vec3 cameraPos{0.0f};

	/// @brief Inverse of (projection * view) for reconstructing world rays from depth.
	glm::mat4 invProjView{1.0f};

	/// @brief Geometry draw batches (nullable). Only GeometryPass reads this.
	const std::vector<GeometryRenderItem>* renderItems = nullptr;

	/// @brief Scene data for light SSBO uploads (nullable). Used by LightingPass.
	const Scene* scene = nullptr;

	/// @brief Optional colour attachment for multiview shadow depth passes.
	///        When set and the pass is in Multiview mode, records colour+depth
	///        instead of depth-only (used for verification readback in tests).
	vk::ImageView optionalColorView = nullptr;
	vk::Format optionalColorFormat = vk::Format::eUndefined;

	/// @brief Active shadow-casting light UID (-1 = no shadow-casting light).
	int32_t lightUID = -1;
};

} // namespace neurus
