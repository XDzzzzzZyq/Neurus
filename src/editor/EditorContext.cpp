#include "EditorContext.h"
#include "EventBus.h"

namespace neurus {

EditorContext::EditorContext(QObject* parent)
	: QObject(parent)
{
}

void EditorContext::SetScene(Scene* scene)
{
	m_scene = scene;
}

void EditorContext::NotifySceneChanged(int status)
{
	EventBus::instance().sceneStatusChanged(status);
}

} // namespace neurus
