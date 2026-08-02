/**
 * @file ProfilingPanel.h
 * @brief GPU rendering profiling dock panel (real-time per-pass timings).
 *
 * Replaces the Debug-menu viewport overlay: displays the FrameProfile
 * returned by DeferredRenderer::DrawFrame() as a tree - a bold "Frame"
 * totals row (CPU record ms, GPU frame ms, total draws/dispatches) with one
 * child row per pass (CPU ms, GPU ms, draws, dispatches).
 *
 * Architecture:
 * - UIPanel subclass (matches Outliner / RenderConfigPanel pattern)
 * - QTreeWidget with 5 columns inside a QVBoxLayout
 * - Refresh() copies the latest FrameProfile from UIContext and rebuilds the
 *   tree only when the frame totals actually changed (no per-frame churn)
 * - No Vulkan or Renderer headers - reads the Vulkan-free ProfilingData POD
 *   through the opaque UIContext::frameProfile pointer
 *
 * @note UI Layer - communicates via UIContext, no direct Renderer coupling.
 */

#pragma once

#include "UIPanel.h"

class QTreeWidget;
class QTreeWidgetItem;

namespace neurus
{

struct FrameProfile;

class ProfilingPanel : public UIPanel
{
	Q_OBJECT

public:
	static constexpr PanelType kType = PanelType::Profiling;

	explicit ProfilingPanel(QWidget* parent = nullptr);
	~ProfilingPanel() override = default;

	ProfilingPanel(const ProfilingPanel&) = delete;
	ProfilingPanel& operator=(const ProfilingPanel&) = delete;

	/**
	 * @brief Per-frame refresh from UIContext.
	 *
	 * Reads the opaque frameProfile pointer and rebuilds the tree when the
	 * frame totals change. GPU timestamps lag the CPU data by 1-2 frames by
	 * design (fence-based readback); the panel just renders what it is given.
	 *
	 * @param ctx Read-only UI context carrying Editor/Project state.
	 */
	void Refresh(const UIContext& ctx) override;

private:
	/** @brief Rebuilds the tree from the latest profile. */
	void Rebuild(const FrameProfile& profile);

	QTreeWidget* m_tree = nullptr;

	// --- Change detection: rebuild only when a new profile arrived ---
	bool     m_hasProfile   = false;
	double   m_lastCpuMs    = 0.0;
	double   m_lastGpuMs    = 0.0;
	uint32_t m_lastPassCount = 0;
};

} // namespace neurus
