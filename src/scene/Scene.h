/**
 * @file Scene.h
 * @brief Scene container holding all scene objects and scene-wide state.
 *
 * Provides the Scene class which aggregates cameras, lights, meshes,
 * sprites, debug primitives, and other renderable objects into a unified
 * scene graph via typed object pools (ResPool).
 *
 * Architecture:
 * - Scene: Owns all scene objects via shared_ptr maps, indexed by UID
 * - Each object type has a dedicated ResPool (cam_list, mesh_list, etc.)
 * - A master obj_list holds all ObjectID instances for global lookup
 * - Renderer reads Scene as immutable scene graph via GetObjectID()
 *
 * Scene Modification Tracking:
 * - SceneModifStatus bitfield flags track which aspects changed
 * - Renderer can optimize updates by checking status flags
 * - Flags reset after processing to avoid redundant work
 *
 * Object Lifetime:
 * - Objects owned via shared_ptr (reference counted)
 * - Removing from map triggers destruction if no other references exist
 * - GPU resources released in object destructors
 *
 * @note Inheritance: Scene inherits UID for unique scene identifier.
 * @note Thread-safety: Not thread-safe. Access from main thread only.
 */

#pragma once

#include <memory>
#include <unordered_map>

#include <cereal/types/memory.hpp>
#include <cereal/types/unordered_map.hpp>

#include "UID.h"

#include "Camera.h"
#include "DebugLine.h"
#include "DebugPoints.h"
#include "Light.h"
#include "Mesh.h"
#include "Sprite.h"
#include "Environment.h"

namespace neurus
{

/**
 * @brief Container for all scene objects, environments, and scene-wide state.
 *
 * Scene aggregates all renderable and non-renderable objects in a scene.
 * It provides typed pools (ResPool) for each object category, enabling
 * efficient iteration and lookup by ID. Active objects (camera, environment)
 * are cached for fast access during rendering.
 *
 * Scene Modification Tracking:
 * - SceneModifStatus flags track which aspects of the scene changed
 * - Renderer can optimize updates by checking status flags
 * - Flags reset after processing to avoid redundant work
 *
 * Object Lifetime:
 * - Objects owned via shared_ptr (reference counted)
 * - Removing from map triggers destruction if no other references exist
 * - GPU resources released in object destructors
 *
 * @note Inheritance: UID provides unique scene identifier.
 * @note Thread-safety: Not thread-safe. Access from main thread only.
 */
class Scene : public UID
{
public:
	// Bring base class GetObjectID() into scope alongside GetObjectID(int)
	using UID::GetObjectID;
	/**
	 * @brief Shared ownership wrapper for scene objects.
	 * @tparam Object Scene object type (Camera, Mesh, Light, etc.)
	 */
	template<class Object>
	using Resource = std::shared_ptr<Object>;

	/**
	 * @brief Typed object pool indexed by unique object ID.
	 *
	 * Maps an object's UID (int) to its shared_ptr. Enables O(1) lookup
	 * and removal of objects from the scene.
	 *
	 * @tparam Object Scene object type stored in this pool.
	 */
	template<class Object>
	using ResPool = std::unordered_map<int, Resource<Object>>;

	// -------------------------------------------------------------------
	// SceneModifStatus - change tracking bitfield
	// -------------------------------------------------------------------

	/**
	 * @brief Scene modification status flags for change tracking.
	 *
	 * Bitfield enum that tracks which aspects of the scene have been
	 * modified since the last render cycle. Renderer queries these
	 * flags to determine which resources need updating.
	 */
	enum SceneModifStatus
	{
		NoChanges          = 0,                             ///< No modifications since last check
		ObjectTransChanged = 1 << 0,                        ///< Object transforms updated
		LightChanged       = 1 << 1,                        ///< Light properties changed
		CameraChanged      = 1 << 2,                        ///< Camera properties changed
		ShaderChanged      = 1 << 3,                        ///< Shaders recompiled
		MaterialChanged    = 1 << 4,                        ///< Material properties changed

		/** Any core scene content changed (transforms, lights, camera, shaders, materials). */
		SceneChanged = ObjectTransChanged | LightChanged | CameraChanged | ShaderChanged | MaterialChanged,

		SDFChanged         = 1 << 8,                        ///< SDF field requires rebuild
	};

	// -------------------------------------------------------------------
	// Construction / destruction
	// -------------------------------------------------------------------

	/**
	 * @brief Constructs an empty scene.
	 */
	Scene();

	/**
	 * @brief Destroys the scene and all owned objects.
	 * @note GPU resources released via object destructors.
	 */
	~Scene();

	/**
	 * @brief Cereal serialization for scene pools.
	 * @tparam Archive Cereal archive type (input or output).
	 * @param ar Archive to serialize to/from.
	 * @note Only typed pools are serialized. obj_list (master pool) is
	 *       reconstructed from typed pools on deserialization.
	 *       m_status (runtime state) is not serialized.
	 */
	template<class Archive>
	void serialize(Archive& ar)
	{
		ar(CEREAL_NVP(cam_list), CEREAL_NVP(mesh_list), CEREAL_NVP(light_list),
		   CEREAL_NVP(sprite_list), CEREAL_NVP(dLine_list), CEREAL_NVP(dPoints_list),
		   CEREAL_NVP(env_list));
	}

	// -------------------------------------------------------------------
	// Status tracking
	// -------------------------------------------------------------------

	/**
	 * @brief Updates scene status flag (bitwise OR on set, AND NOT on clear).
	 * @param tar Target status flag(s) to modify (int for OR-of-enum convenience).
	 * @param value If true, set flag(s); if false, clear flag(s).
	 * @note Multiple flags can be combined via bitwise OR in tar.
	 */
	void UpdateSceneStatus(int tar, bool value);

	/**
	 * @brief Directly sets scene status (replaces existing value).
	 * @param tar New status bitfield value.
	 * @param value Unused (legacy parameter, kept for API compatibility).
	 */
	void SetSceneStatus(int tar, bool value);

	/**
	 * @brief Checks if a specific status flag is set.
	 * @param tar Status flag(s) to check.
	 * @return True if any of the specified flags are set.
	 */
	bool CheckStatus(SceneModifStatus tar);

	/**
	 * @brief Resets scene status to NoChanges.
	 * @note Called after Renderer processes all scene updates.
	 */
	void ResetStatus();

	// -------------------------------------------------------------------
	// Object pools (public for direct iteration by Renderer)
	// -------------------------------------------------------------------

	ResPool<ObjectID>    obj_list;      ///< All scene objects (base type, master pool)
	ResPool<Camera>      cam_list;      ///< Camera objects
	ResPool<Mesh>        mesh_list;     ///< Mesh objects
	ResPool<Light>       light_list;    ///< Light objects (point, sun, spot, area)
	ResPool<Sprite>      sprite_list;   ///< 2D sprite overlays
	ResPool<DebugLine>   dLine_list;    ///< Debug line primitives
	ResPool<DebugPoints> dPoints_list;  ///< Debug point primitives
	ResPool<Environment> env_list;      ///< Environment objects (IBL)

	// -------------------------------------------------------------------
	// Registration - store in both type-specific pool AND obj_list
	// -------------------------------------------------------------------

	/**
	 * @brief Registers a camera in the scene.
	 * @param camera Shared pointer to Camera object.
	 * @note Adds to cam_list and obj_list.
	 */
	void UseCamera(Resource<Camera> camera)
	{
		RegisterObject(camera, cam_list);
	}

	/**
	 * @brief Registers a mesh in the scene.
	 * @param mesh Shared pointer to Mesh object.
	 * @note Adds to mesh_list and obj_list.
	 */
	void UseMesh(Resource<Mesh> mesh)
	{
		RegisterObject(mesh, mesh_list);
	}

	/**
	 * @brief Registers a light in the scene.
	 * @param light Shared pointer to Light object.
	 * @note Adds to light_list and obj_list.
	 */
	void UseLight(Resource<Light> light)
	{
		RegisterObject(light, light_list);
	}

	/**
	 * @brief Registers a sprite in the scene.
	 * @param sprite Shared pointer to Sprite object.
	 * @note Adds to sprite_list and obj_list.
	 */
	void UseSprite(Resource<Sprite> sprite)
	{
		RegisterObject(sprite, sprite_list);
	}

	/**
	 * @brief Registers debug lines in the scene.
	 * @param dline Shared pointer to DebugLine object.
	 * @note Adds to dLine_list and obj_list.
	 */
	void UseDebugLine(Resource<DebugLine> dline)
	{
		RegisterObject(dline, dLine_list);
	}

	/**
	 * @brief Registers debug points in the scene.
	 * @param dpoints Shared pointer to DebugPoints object.
	 * @note Adds to dPoints_list and obj_list.
	 */
	void UseDebugPoints(Resource<DebugPoints> dpoints)
	{
		RegisterObject(dpoints, dPoints_list);
	}

	/**
	 * @brief Registers an environment object in the scene.
	 * @param env Shared pointer to Environment object.
	 * @note Adds to env_list and obj_list.
	 */
	void UseEnvironment(Resource<Environment> env)
	{
		RegisterObject(env, env_list);
	}

	// -------------------------------------------------------------------
	// Lookup
	// -------------------------------------------------------------------

	/**
	 * @brief Retrieves an object by its unique ID from the master pool.
	 * @param id Unique object identifier (from UID::GetObjectID()).
	 * @return Non-owning pointer to ObjectID, or nullptr if not found.
	 */
	ObjectID* GetObjectID(int id);

	/**
	 * @brief Returns the active camera (first in cam_list).
	 * @return Non-owning pointer to Camera, or nullptr if no cameras.
	 * @note First camera in cam_list is considered active.
	 */
	Camera* GetActiveCamera();

	/**
	 * @brief Retrieves an object by its unique ID from the master pool (const).
	 * @param id Unique object identifier.
	 * @return Const pointer to ObjectID, or nullptr if not found.
	 */
	const ObjectID* GetObjectID(int id) const;

	/**
	 * @brief Returns the active camera (first in cam_list) (const).
	 * @return Const pointer to Camera, or nullptr if no cameras.
	 */
	const Camera* GetActiveCamera() const;

	// -------------------------------------------------------------------
	// Scene-wide operations
	// -------------------------------------------------------------------

	/**
	 * @brief Updates all object transforms in the scene hierarchy.
	 * @note Stub for MVP - implementation deferred until scene objects exist.
	 */
	void UpdateObjTransforms();

private:
	SceneModifStatus m_status = SceneModifStatus::SceneChanged; ///< Current scene modification state

	/**
	 * @brief Registers an object in both its type-specific pool and the master obj_list.
	 * @tparam T Object type (Camera, Mesh, Light, Sprite, DebugLine, DebugPoints).
	 * @param obj Shared pointer to the object.
	 * @param typePool Type-specific pool to register in.
	 * @note Uses shared_ptr aliasing constructor to store the base ObjectID* in
	 *       obj_list while sharing ownership with the typed shared_ptr.
	 */
	template<typename T>
	void RegisterObject(Resource<T> obj, ResPool<T>& typePool)
	{
		auto* basePtr = static_cast<ObjectID*>(obj.get());
		int id = basePtr->GetObjectID();
		typePool[id] = obj;
		obj_list[id] = Resource<ObjectID>(obj, basePtr);
	}
};

} // namespace neurus
