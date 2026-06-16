#include "UIEvents.h"

namespace neurus {

UIEvents& UIEvents::instance()
{
	static UIEvents bus;
	return bus;
}

QString UIEvents::gpuName() const
{
	return m_gpuName;
}

void UIEvents::setGpuName(const QString& name)
{
	if (m_gpuName != name)
	{
		m_gpuName = name;
		emit gpuNameChanged();
	}
}

} // namespace neurus
