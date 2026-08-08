/**
 * @file DataResource.h
 * @brief Base class for path-based data resources with reload support.
 *
 * DataResource extends UID for CPU-side data resources whose content is
 * loaded from disk paths relative to a project asset directory:
 * - MeshData (asset/data), ImageData (asset/data), Shader (render/shaders)
 *
 * The pool (ResourceManager) stores DataResource-derived objects and triggers
 * ReloadContent(assetDir) after deserialization, so path-based content loads
 * lazily at project open time.
 *
 * Architecture:
 * - UID provides identity (core/UID.h)
 * - DataResource adds the ReloadContent(assetDir) hook
 * - Concrete leaves (MeshData/ImageData/RenderShader) implement ReloadContent
 * - cereal does not auto-walk base classes: every link of the chain
 *   UID -> DataResource -> T must forward via cereal::base_class explicitly.
 */

#pragma once

#include <string>

#include <cereal/cereal.hpp>

#include "core/UID.h"

namespace neurus
{

/**
 * @brief Base class for UID objects whose content is loaded from disk paths.
 *
 * The source path is stored relative to the resource pool's asset directory;
 * ReloadContent() re-loads the content from `assetDir + "/" + path`. Entries
 * with an empty path are skipped (identity shell only).
 *
 * @note Non-copyable and non-movable (inherits UID semantics).
 * @note Thread-safety: Not thread-safe. Loaded on the main thread.
 */
class DataResource : public UID
{
public:
	/**
	 * @brief Constructs a DataResource (allocates a UID).
	 */
	DataResource() = default;

	/**
	 * @brief Virtual destructor for polymorphic cleanup.
	 */
	~DataResource() override = default;

	// Non-copyable / non-movable (UID identity must stay unique).
	DataResource(const DataResource&) = delete;
	DataResource& operator=(const DataResource&) = delete;
	DataResource(DataResource&&) = delete;
	DataResource& operator=(DataResource&&) = delete;

	/**
	 * @brief (Re)loads this resource's content from disk.
	 *
	 * Called by ResourceManager::Load<T> right after registration and by the
	 * pool's serialize(load) for every pooled DataResource. Implementations
	 * resolve `assetDir + "/" + storedPath` and log a warning on failure,
	 * leaving the resource as an identity shell.
	 *
	 * @param assetDir Project asset directory (absolute, may be empty).
	 */
	virtual void ReloadContent(const std::string& assetDir) = 0;

	/**
	 * @brief Cereal serialization - forwards to the UID base.
	 *
	 * cereal does not auto-walk base classes, so every derived DataResource
	 * must call cereal::base_class<DataResource>(this) in its own serialize,
	 * which in turn forwards to UID.
	 *
	 * @tparam Archive Cereal archive type (input or output).
	 * @param ar Archive to serialize to/from.
	 */
	template<class Archive>
	void serialize(Archive& ar)
	{
		ar(cereal::base_class<UID>(this));
	}
};

} // namespace neurus
