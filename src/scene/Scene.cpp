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
		sc_status = static_cast<SceneModifStatus>(static_cast<int>(sc_status) | tar);
	}
	else
	{
		sc_status = static_cast<SceneModifStatus>(static_cast<int>(sc_status) & ~tar);
	}
}

void Scene::SetSceneStatus(int tar, bool /*value*/)
{
	sc_status = static_cast<SceneModifStatus>(tar);
}

bool Scene::CheckStatus(SceneModifStatus tar)
{
	return (static_cast<int>(sc_status) & static_cast<int>(tar)) != 0;
}

void Scene::ResetStatus()
{
	sc_status = SceneModifStatus::NoChanges;
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

const ObjectID* Scene::GetObjectID(int id) const
{
	auto it = obj_list.find(id);
	if (it != obj_list.end())
	{
		return it->second.get();
	}
	return nullptr;
}

const Camera* Scene::GetActiveCamera() const
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

// ---------------------------------------------------------------------------
// Post-deserialization rebuild
// ---------------------------------------------------------------------------

void Scene::RebuildObjList()
{
	obj_list.clear();

	// Lambda that inserts aliasing shared_ptr<ObjectID> for each entry in a pool.
	auto rebuild = [this](auto& pool)
	{
		for (auto& [id, obj] : pool)
		{
			auto* basePtr = static_cast<ObjectID*>(obj.get());
			int id_obj = basePtr->GetObjectID();
			obj_list[id] = Resource<ObjectID>(obj, basePtr);
		}
	};

	rebuild(cam_list);
	rebuild(mesh_list);
	rebuild(light_list);
	rebuild(sprite_list);
	rebuild(dLine_list);
	rebuild(dPoints_list);
	rebuild(env_list);
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
