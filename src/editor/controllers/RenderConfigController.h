/**
 * @file RenderConfigController.h
 * @brief Render-config controller — applies + records whole-config edits.
 *
 * RenderConfigController is the single mutation path for RenderConfig. It
 * subscribes to RenderConfigChangedEvent (emitted by the UI panel and replayed
 * by SetRenderConfigOp) and writes the new config into the Editor-owned
 * RenderConfig, reached through a provider callback so the controller never
 * includes Editor internals.
 *
 * Undo bounding mirrors the camera drag gesture: a slider drag brackets its
 * stream of value changes with ConfigEditBegin / ConfigEditEnd, so the whole
 * drag collapses to a single SetRenderConfigOp recorded on release. Discrete
 * edits (checkbox, combo box) arrive without a gesture and are recorded
 * immediately, one op each.
 *
 * @note Unlike other controllers, this one needs access to the Editor-owned
 *       RenderConfig, so it is constructed manually with a provider rather than
 *       via Editor::RegisterController<T>() (which only forwards bus + ops).
 */

#pragma once

#include <functional>

#include "editor/controllers/Controllers.h"
#include "editor/events/EventBus.h"
#include "render/RenderConfig.h"

namespace neurus {

/**
 * @brief Event-driven controller that applies and records RenderConfig edits.
 */
class RenderConfigController : public Controllers
{
public:
	/** @brief Provider returning the live, Editor-owned RenderConfig to mutate. */
	using ConfigProvider = std::function<RenderConfig*()>;

	/**
	 * @brief Constructs the controller with a live-config provider.
	 * @param provider Returns the Editor-owned RenderConfig pointer (non-owning).
	 */
	explicit RenderConfigController(ConfigProvider provider)
		: m_provider(std::move(provider))
	{}

	/**
	 * @brief Subscribes to config change + edit-gesture events.
	 * @param bus EventQueue to subscribe to.
	 * @param ops Operation sink; records SetRenderConfigOp for undo/redo.
	 */
	void Init(EventQueue& bus, IOperationSink& ops) override;

private:
	ConfigProvider m_provider;   ///< Returns the live RenderConfig to mutate.
	bool m_editing = false;      ///< True between ConfigEditBegin and ConfigEditEnd.
	RenderConfig m_before{};     ///< Config captured at gesture start (undo endpoint).
};

} // namespace neurus
