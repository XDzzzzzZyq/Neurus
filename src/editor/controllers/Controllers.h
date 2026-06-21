#pragma once

namespace neurus {

class EventQueue;

class Controllers
{
public:
	virtual ~Controllers() = default;
	virtual void Init(EventQueue& bus) = 0;
};

} // namespace neurus
