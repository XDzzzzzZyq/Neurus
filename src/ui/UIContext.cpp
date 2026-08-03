/**
 * @file UIContext.cpp
 * @brief UIContext implementation — scene object enumeration.
 */

#include "UIContext.h"

#include "scene/Scene.h"

namespace neurus
{

// =========================================================================
// GetObjectIDs — collect all scene objects from the opaque scene pointer
// =========================================================================

std::vector<const ObjectID*> UIContext::GetObjectIDs() const
{
	std::vector<const ObjectID*> result;
	if (!editor.scene)
	{
		return result;
	}

	const Scene* s = static_cast<const Scene*>(editor.scene);
	result.reserve(s->obj_list.size());
	for (const auto& [id, obj] : s->obj_list)
	{
		result.push_back(obj.get());
	}
	return result;
}

} // namespace neurus
