#include "EventBus.h"

namespace neurus {

EventBus& EventBus::instance()
{
	static EventBus bus;
	return bus;
}

QString EventBus::gpuName() const
{
	return m_gpuName;
}

void EventBus::setGpuName(const QString& name)
{
	if (m_gpuName != name)
	{
		m_gpuName = name;
		emit gpuNameChanged();
	}
}

} // namespace neurus
