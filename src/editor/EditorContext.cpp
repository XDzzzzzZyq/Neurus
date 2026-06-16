#include "EditorContext.h"
#include "events/UIEvents.h"
#include "events/EventBus.h"
#include "events/EditorEvents.h"

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
	EventBus().enqueue(SceneStatusChanged{status});
}

} // namespace neurus
