#pragma once

#include "editor/events/EventBus.h"

namespace neurus {

class IOperationSink;

class Controllers
{
public:
	virtual ~Controllers() = default;

	/**
	 * @brief Subscribes the controller to its events.
	 * @param bus Event queue to subscribe to.
	 * @param ops Sink for recording undoable operations at mutation time.
	 */
	virtual void Init(EventQueue& bus, IOperationSink& ops) = 0;
};

} // namespace neurus
