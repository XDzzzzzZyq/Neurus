/**
 * @file Context.cpp
 * @brief Context system implementation - SceneContext accessors, EditorContext, Context constructor.
 */

#include "editor/Context.h"

#include "scene/Scene.h"
#include "scene/Camera.h"
#include "scene/UID.h"
#include "editor/events/EventBus.h"
#include "editor/events/EditorEvents.h"

namespace neurus {

// ===========================================================================
// SceneContext - const accessors delegating to Scene
// ===========================================================================

const Camera* SceneContext::GetActiveCamera() const
{
	if (m_activeScene == nullptr)
	{
		return nullptr;
	}
	return m_activeScene->GetActiveCamera();
}

const ObjectID* SceneContext::GetObjectID(int id) const
{
	if (m_activeScene == nullptr)
	{
		return nullptr;
	}
	return m_activeScene->GetObjectID(id);
}

std::vector<const ObjectID*> SceneContext::GetObjectIDs() const
{
	if (m_activeScene == nullptr)
	{
		return {};
	}

	std::vector<const ObjectID*> objList{};
	objList.reserve(m_activeScene->obj_list.size());
	for (const auto& [id, obj] : m_activeScene->obj_list)
	{
		objList.push_back(obj.get());
	}
	return objList;
}

// ===========================================================================
// EditorContext
// ===========================================================================

EditorContext::EditorContext(QObject* parent)
	: QObject(parent)
{
}

void EditorContext::SetScene(Scene* scene)
{
	if (m_sceneCtx)
	{
		m_sceneCtx->UseScene(scene);
	}
}

void EditorContext::NotifySceneChanged(int status)
{
	EventBus().enqueue(SceneStatusChanged{status});
}

const Scene* EditorContext::activeScene() const
{
	if (m_sceneCtx == nullptr)
	{
		return nullptr;
	}
	return m_sceneCtx->GetActiveScene();
}

// ===========================================================================
// Context - aggregates sub-contexts, wires event subscriptions
// ===========================================================================

Context::Context(EventQueue& pool)
{
	// Wire back-pointer: EditorContext needs access to its sibling SceneContext
	editor.m_sceneCtx = &scene;

	// Subscribe to scene status changes → propagate to EditorContext signal
	pool.subscribe<SceneStatusChanged>([this](const SceneStatusChanged& e) {
		// Update scene modification status on the scene itself
		Scene* activeScene = const_cast<Scene*>(scene.GetActiveScene());
		if (activeScene)
		{
			activeScene->SetSceneStatus(e.status, true);
		}

		// Emit Qt signal so UI can refresh
		emit editor.sceneChanged();
	});
}

} // namespace neurus
