/**
 * @file UIContext.h
 * @brief Cross-panel UI refresh context carrying Editor/Profiler state.
 *
 * UIContext is a lightweight data struct assembled by the Application each frame
 * and consumed by UIPanel subclasses via their Refresh() method. It embeds the
 * shared EditorContext (scene + render config) produced by Editor::GetContext()
 * and adds the per-frame render profile returned by DeferredRenderer::DrawFrame().
 *
 * Architecture:
 * - No Qt dependency, no Vulkan dependency.
 * - Lives in src/ui/ but is populated by the Application layer, not the Editor.
 * - Panels cast opaque pointers to their known types; no dynamic dispatch needed.
 */

#pragma once

#include <vector>

#include "scene/EditorContext.h"

namespace neurus
{

// Forward declaration — no scene headers needed in UIContext.h.
class ObjectID;

/**
 * @brief Read-only context passed to UIPanel::Refresh() each frame.
 *
 * Assembled by the Application from Editor::GetContext() plus the profile
 * returned by DeferredRenderer::DrawFrame(). Panels extract the data they need
 * by casting opaque pointers to their concrete types
 * (e.g. static_cast<const RenderConfig*>(ctx.editor.config)).
 */
struct UIContext
{
	/** @brief Editor-owned scene + render config snapshot (shared with RenderContext). */
	EditorContext editor;

	/**
	 * @brief Opaque pointer to the latest FrameProfile (const FrameProfile*).
	 *        Set by the Application from DeferredRenderer::DrawFrame(); the
	 *        ProfilingPanel casts it to const FrameProfile*.
	 */
	const void* profile = nullptr;

	/**
	 * @brief Returns all scene objects as const ObjectID* pointers.
	 *
	 * Casts editor.scene to const Scene* and collects every ObjectID from the
	 * scene's master obj_list. Returns an empty vector if no scene is set.
	 *
	 * @return Vector of const ObjectID* pointers (one per scene object).
	 */
	std::vector<const ObjectID*> GetObjectIDs() const;
};

} // namespace neurus
