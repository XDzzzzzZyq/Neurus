/**
 * @file Scene.cpp
 * @brief Scene container implementation - status tracking and object lookup.
 */

#include "Scene.h"

namespace neurus
{

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Scene::Scene() = default;

Scene::~Scene() = default;

// ---------------------------------------------------------------------------
// Status tracking
// ---------------------------------------------------------------------------

void Scene::UpdateSceneStatus(int tar, bool value)
{
	if (value)
	{
		m_status = static_cast<SceneModifStatus>(static_cast<int>(m_status) | tar);
	}
	else
	{
		m_status = static_cast<SceneModifStatus>(static_cast<int>(m_status) & ~tar);
	}
}

void Scene::SetSceneStatus(int tar, bool /*value*/)
{
	m_status = static_cast<SceneModifStatus>(tar);
}

bool Scene::CheckStatus(SceneModifStatus tar)
{
	return (static_cast<int>(m_status) & static_cast<int>(tar)) != 0;
}

void Scene::ResetStatus()
{
	m_status = SceneModifStatus::NoChanges;
}

// ---------------------------------------------------------------------------
// Object lookup
// ---------------------------------------------------------------------------

ObjectID* Scene::GetObjectID(int id)
{
	auto it = obj_list.find(id);
	if (it != obj_list.end())
	{
		return it->second.get();
	}
	return nullptr;
}

Camera* Scene::GetActiveCamera()
{
	if (!cam_list.empty())
	{
		return cam_list.begin()->second.get();
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Scene-wide operations
// ---------------------------------------------------------------------------

void Scene::UpdateObjTransforms()
{
	for (auto& [id, obj] : obj_list)
	{
		void* transformVoid = obj->GetTransform();
		if (transformVoid)
		{
			auto* t3d = static_cast<Transform3D*>(transformVoid);
			t3d->GetModelMatrix(); // Force cached matrix recomputation if dirty.
		}
	}
}

} // namespace neurus
