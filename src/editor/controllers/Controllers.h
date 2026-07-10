#pragma once

#include "editor/events/EventBus.h"

namespace neurus {

class Controllers
{
public:
	virtual ~Controllers() = default;
	virtual void Init(EventQueue& bus) = 0;
};

} // namespace neurus
