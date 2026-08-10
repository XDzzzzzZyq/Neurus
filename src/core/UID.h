/**
 * @file UID.h
 * @brief Generic unique identifier primitive (core layer).
 *
 * Provides the base UID class for unique ID generation. Every object that
 * needs identity tracking inherits from UID: scene graph objects via
 * ObjectID (scene layer), and data resources (MeshData, ImageData, Shader)
 * directly.
 *
 * Architecture:
 * - UID provides globally unique integer IDs (sequential, not thread-safe)
 * - ObjectID (scene/ObjectID.h) extends UID with scene-specific metadata
 * - Data resources inherit UID directly and serialize themselves, bound by
 *   UID + cereal polymorphism in the ResourceManager pool
 * - ID-based lookups enable efficient object management
 */

#pragma once

#include <cereal/cereal.hpp>

namespace neurus
{

/**
 * @brief Base class providing globally unique integer IDs.
 *
 * UID generates sequential unique IDs for all instances. Each UID tracks
 * a monotonically increasing ID counter to ensure no collisions.
 *
 * @note Thread-safety: Not thread-safe. IDs should be allocated on main thread.
 * @note Lifetime: IDs are never reused, even after object destruction.
 * @note Value semantics: non-copyable and non-movable - identity must not be
 *       duplicated or transferred. Objects are held via shared_ptr.
 */
class UID
{
private:
	int o_id;            ///< Unique identifier for this instance
	static int s_count;  ///< Global counter for ID generation

public:
	/**
	 * @brief Constructs a UID and assigns a unique ID.
	 */
	UID();

	/**
	 * @brief Virtual destructor for polymorphic use.
	 */
	virtual ~UID() = default;

	/**
	 * @brief Deleted copy constructor - UIDs must be unique.
	 */
	UID(const UID&) = delete;

	/**
	 * @brief Deleted copy assignment - UIDs must be unique.
	 */
	UID& operator=(const UID&) = delete;

	/**
	 * @brief Deleted move constructor - UIDs must be unique.
	 */
	UID(UID&&) = delete;

	/**
	 * @brief Deleted move assignment - UIDs must be unique.
	 */
	UID& operator=(UID&&) = delete;

	/**
	 * @brief Returns the unique ID of this object.
	 * @return Unique integer identifier.
	 */
	inline int GetObjectID() const
	{
		return o_id;
	}

	/**
	 * @brief Returns total number of UIDs allocated.
	 * @return Total allocation count (includes destroyed objects).
	 */
	static int GetTotalAllocated()
	{
		return s_count;
	}

	/**
	 * @brief Cereal serialization for the unique ID.
	 *
	 * Serializes o_id to preserve identity across save/load cycles.
	 * On deserialization, bumps s_count to stay ahead of the loaded ID
	 * so newly created objects do not collide with restored ones.
	 *
	 * @tparam Archive Cereal archive type (input or output).
	 * @param ar Archive to serialize to/from.
	 */
	template<class Archive>
	void serialize(Archive& ar)
	{
		ar(CEREAL_NVP(o_id));
		if constexpr (Archive::is_loading::value)
		{
			if (o_id >= s_count)
				s_count = o_id + 1;
		}
	}
};

} // namespace neurus
