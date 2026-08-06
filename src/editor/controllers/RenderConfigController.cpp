/**
 * @file RenderConfigController.cpp
 * @brief Applies RenderConfig edits and records them as undoable operations.
 *
 * The apply path is shared by live UI edits and operation replay: writing the
 * incoming config into the Editor-owned RenderConfig and requesting a render
 * reset. Recording is gesture-aware — see the header for the bounding rules.
 */

#include "editor/controllers/RenderConfigController.h"

#include <memory>

#include "editor/events/ConfigEvents.h"
#include "editor/events/EditorEvents.h"
#include "editor/operations/ConfigOperations.h"
#include "editor/operations/IOperationSink.h"

namespace neurus {

void RenderConfigController::Init(EventQueue& bus, IOperationSink& ops)
{
	bus.subscribe<ConfigEditBegin>([this](const ConfigEditBegin&) {
		if (RenderConfig* cfg = m_provider())
		{
			m_before = *cfg;
			m_editing = true;
		}
	});

	bus.subscribe<RenderConfigChangedEvent>([this, &bus, &ops](const RenderConfigChangedEvent& e) {
		RenderConfig* cfg = m_provider();
		if (!cfg) return;

		const RenderConfig before = *cfg;
		*cfg = e.config;
		bus.enqueue(RenderResetEvent{});

		// During a slider gesture the drag is recorded once on ConfigEditEnd;
		// discrete edits (no gesture) record immediately. Skip no-op writes.
		if (!m_editing && !(before == e.config))
		{
			ops.Submit(std::make_unique<SetRenderConfigOp>(before, e.config));
		}
	});

	bus.subscribe<ConfigEditEnd>([this, &ops](const ConfigEditEnd&) {
		if (m_editing)
		{
			if (RenderConfig* cfg = m_provider(); cfg && !(m_before == *cfg))
			{
				ops.Submit(std::make_unique<SetRenderConfigOp>(m_before, *cfg));
			}
		}
		m_editing = false;
	});
}

} // namespace neurus
