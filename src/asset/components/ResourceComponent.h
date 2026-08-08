/**
 * @file ResourceComponent.h
 * @brief Serializable adapter that persists the ResourceManager pool.
 *
 * Mirrors SceneComponent/ConfigComponent: it implements project::Serializable
 * and wraps a non-owning ResourceManager pointer. Save writes the whole UID
 * pool (scene objects + data resources) polymorphically; Load restores it
 * (each pooled object's own serialize(load) restores its content).
 *
 * Ordering: this component is registered FIRST in BuildProject so the pool is
 * restored before SceneComponent resolves the Scene's ID references against it.
 *
 * Layering: the header stays lightweight (forward-declares ResourceManager);
 * the .cpp includes the cereal force-init headers for scene/data/shader types
 * so the static-library registration TUs are linked in.
 */

#pragma once

#include "asset/Serializable.h"

namespace neurus { class ResourceManager; }

namespace neurus::project
{

/**
 * @brief Persists a ResourceManager's UID pool in a project file.
 */
class ResourceComponent : public Serializable
{
public:
	explicit ResourceComponent(ResourceManager& resources);

	const char* Key() const noexcept override { return "m_resources"; }
	void Save(cereal::JSONOutputArchive& ar) const override;
	void Load(cereal::JSONInputArchive& ar) override;

private:
	ResourceManager* m_resources;
};

} // namespace neurus::project
