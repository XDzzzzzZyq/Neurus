/**
 * @file ResourceManager.h
 * @brief Central factory + registry for every persistent UID object.
 *
 * ResourceManager is the single factory and registry for UID-derived objects
 * (scene objects via ObjectID, data resources via DataResource). It is
 * app-scoped (owned by the Editor), type-erased (holds shared_ptr<UID>), and
 * owns the polymorphic serialization of the whole pool.
 *
 * Design (see docs/superpowers/plans/2026-08-07-resource-manager-design.md):
 * - Load<T>(args...) constructs + registers in one step; for DataResource
 *   types it also triggers ReloadContent(assetDir) so path-based content loads
 *   immediately.
 * - Scene neither owns nor references the pool; it holds plain typed pools and
 *   resolves references against the pool via Scene::ResolveReferences.
 * - No UID object knows the ResourceManager and no UID object constructs
 *   another UID object internally - construction happens through Load<T>.
 * - The pool serializes the real objects (polymorphically); Scene serializes
 *   only ID references. Pool is loaded before the Scene.
 *
 * @note Thread-safety: Not thread-safe. Main thread only.
 */

#pragma once

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <cereal/cereal.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/unordered_map.hpp>

#include "core/DataResource.h"
#include "core/UID.h"

namespace neurus
{

/**
 * @brief Single factory + registry for all UID objects (scene + data).
 *
 * Owned by the Editor (app-scoped). Cleared on project new/open so no stale
 * object leaks into a save. Serializes the whole pool polymorphically via
 * cereal, then triggers DataResource::ReloadContent for every pooled data
 * resource.
 */
class ResourceManager
{
public:
	ResourceManager() = default;
	~ResourceManager() = default;

	ResourceManager(const ResourceManager&) = delete;
	ResourceManager& operator=(const ResourceManager&) = delete;

	/**
	 * @brief Sets the project asset directory for path-based content reload.
	 * @param dir Absolute path to the res/ directory (may be empty).
	 */
	void SetAssetDir(std::string dir) { m_assetDir = std::move(dir); }

	/**
	 * @brief Returns the configured asset directory.
	 */
	const std::string& GetAssetDir() const { return m_assetDir; }

	/**
	 * @brief Constructs a resource and registers it by its UID.
	 *
	 * For DataResource types, triggers ReloadContent(m_assetDir) right after
	 * registration so content loads immediately (e.g. Load<MeshData>(path)).
	 *
	 * @tparam T Resource type (must inherit UID, directly or transitively).
	 * @tparam Args Constructor argument types.
	 * @param args Forwarded to T's constructor.
	 * @return Typed shared_ptr to the registered resource.
	 * @throws std::runtime_error if the UID is already registered (safety net;
	 *         unreachable in normal flow because UIDs are monotonic).
	 */
	template<typename T, typename... Args>
	std::shared_ptr<T> Load(Args&&... args)
	{
		static_assert(std::is_base_of_v<UID, T>, "Load<T>: T must derive from UID");

		auto resource = std::make_shared<T>(std::forward<Args>(args)...);
		const int id = resource->GetObjectID();
		auto [it, inserted] = resources_.emplace(id, std::move(resource));
		if (!inserted)
		{
			throw std::runtime_error("Resource UID already exists: " + std::to_string(id));
		}

		if constexpr (std::is_base_of_v<DataResource, T>)
		{
			if (auto* dr = dynamic_cast<DataResource*>(it->second.get()))
				dr->ReloadContent(m_assetDir);
		}

		return std::dynamic_pointer_cast<T>(it->second);
	}

	/**
	 * @brief Registers an already-constructed object (tests, legacy paths).
	 * @param obj Shared pointer to the UID object.
	 * @return The registered object's UID.
	 * @throws std::runtime_error if the UID is already registered.
	 */
	int Register(std::shared_ptr<UID> obj);

	/**
	 * @brief Looks up a pooled resource by ID with type checking.
	 * @tparam T Requested type (may be a base like UID/ObjectID).
	 * @param id Resource UID.
	 * @return Typed shared_ptr, or nullptr if absent or of a different type.
	 */
	template<class T>
	std::shared_ptr<T> Get(int id) const
	{
		auto it = resources_.find(id);
		if (it == resources_.end())
			return nullptr;
		return std::dynamic_pointer_cast<T>(it->second);
	}

	/**
	 * @brief True if a resource with the given ID is registered.
	 */
	bool Contains(int id) const { return resources_.count(id) != 0; }

	/**
	 * @brief Removes a resource by ID.
	 * @return True if an entry was removed.
	 */
	bool Remove(int id) { return resources_.erase(id) != 0; }

	/**
	 * @brief Drops every pooled object (project new/open).
	 */
	void Clear() { resources_.clear(); }

	/**
	 * @brief Number of pooled resources.
	 */
	size_t Size() const { return resources_.size(); }

	/**
	 * @brief Cereal serialization of the whole pool.
	 *
	 * Save: writes every pooled UID object polymorphically (this is what makes
	 * the type registrations load-bearing). Load: restores objects (UID
	 * serialize bumps s_count past restored IDs) and then triggers
	 * DataResource::ReloadContent(m_assetDir) on every pooled data resource.
	 *
	 * @tparam Archive Cereal archive type (input or output).
	 * @param ar Archive to serialize to/from.
	 */
	template<class Archive>
	void serialize(Archive& ar)
	{
		ar(CEREAL_NVP(resources_));

		if constexpr (Archive::is_loading::value)
		{
			for (auto& [id, res] : resources_)
			{
				if (auto* dr = dynamic_cast<DataResource*>(res.get()))
					dr->ReloadContent(m_assetDir);
			}
		}
	}

private:
	/// Pool of every registered UID object, keyed by UID.
	std::unordered_map<int, std::shared_ptr<UID>> resources_;

	/// Project asset directory (not serialized).
	std::string m_assetDir;
};

} // namespace neurus
