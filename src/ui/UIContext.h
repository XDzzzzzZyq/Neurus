/**
 * @file UIContext.h
 * @brief Cross-panel UI refresh context carrying Editor/Project state.
 *
 * UIContext is a lightweight data struct populated by the Editor during the
 * newFrame cycle and consumed by UIPanel subclasses via their Refresh() method.
 * It carries opaque pointers to render-layer data (RenderConfig, etc.) that
 * panels cast back to their concrete types for display updates.
 *
 * Architecture:
 * - Pure data struct — no Qt dependency, no Vulkan dependency.
 * - Lives in src/ui/ but is populated by Editor (src/editor/).
 * - Panels cast opaque pointers to their known types; no dynamic dispatch needed.
 */

#pragma once

#include <vector>

namespace neurus
{

// Forward declaration — no scene headers needed in UIContext.h.
class ObjectID;

/**
 * @brief Read-only context passed to UIPanel::Refresh() each frame.
 *
 * Populated by Editor::GetUIContext() from the active Project's state.
 * Panels extract the data they need by casting opaque pointers to their
 * concrete types (e.g. static_cast<const RenderConfig*>(ctx.renderConfig)).
 */
struct UIContext
{
	/** @brief Opaque pointer to Project::proj_config (const RenderConfig*). */
	const void* renderConfig = nullptr;

	/**
	 * @brief Opaque pointer to the Editor's copy of the latest FrameProfile
	 *        (const FrameProfile*). Populated by Editor::SetFrameProfile()
	 *        from the profile returned by DeferredRenderer::DrawFrame().
	 */
	const void* frameProfile = nullptr;

	/** @brief Opaque pointer to Scene*. Cast to const Scene* in cpp. */
	void* scene = nullptr;

	/**
	 * @brief Returns all scene objects as const ObjectID* pointers.
	 *
	 * Casts the opaque scene pointer to const Scene* and collects every
	 * ObjectID from the scene's master obj_list. Returns an empty vector
	 * if no scene is set.
	 *
	 * @return Vector of const ObjectID* pointers (one per scene object).
	 */
	std::vector<const ObjectID*> GetObjectIDs() const;
};

} // namespace neurus
