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
	 * @brief Opaque pointer to the core LogBuffer (const neurus::LogBuffer*).
	 *        Set by the Application each frame; the LogPanel casts it to
	 *        const LogBuffer* to poll new log entries.
	 */
	const void* log = nullptr;

	/**
	 * @brief Opaque pointer to the current undo/redo snapshot (const HistoryView*).
	 *        Set by the Application from Editor::GetHistory(); the HistoryPanel
	 *        casts it to const HistoryView*. Kept opaque so UIContext.h stays
	 *        free of editor headers.
	 */
	const void* history = nullptr;

	/**
	 * @brief Returns all scene object UIDs in the scene's master obj_list.
	 *
	 * Casts editor.scene to const Scene* and collects every object's integer
	 * UID. Returns an empty vector if no scene is set. Panels resolve per-id
	 * details (name/type/visibility) via the Scene and compare the plain ints
	 * for dirty checks and lazy updates — no cached object pointers.
	 *
	 * @return Vector of int object UIDs (one per scene object).
	 */
	std::vector<int> GetObjectIDs() const;
};

} // namespace neurus
