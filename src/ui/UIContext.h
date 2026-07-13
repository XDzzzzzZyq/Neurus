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
 * - Pure data struct — no methods, no Qt dependency, no Vulkan dependency.
 * - Lives in src/ui/ but is populated by Editor (src/editor/).
 * - Panels cast opaque pointers to their known types; no dynamic dispatch needed.
 */

#pragma once

namespace neurus
{

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
	void* renderConfig = nullptr;
};

} // namespace neurus
