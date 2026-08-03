/**
 * @file OperationContext.cpp
 * @brief UID -> object resolution for OperationContext.
 *
 * The Scene downcast lives here so OperationContext.h can hold the scene as its
 * lightweight UID base (mirroring EditorContext) without including scene/Scene.h.
 */

#include "editor/operations/OperationContext.h"

#include "scene/Scene.h"

namespace neurus {

ObjectID* OperationContext::Resolve(int uid) const
{
	// `scene` is always a Scene upcast to UID by the OperationManager.
	return static_cast<Scene&>(scene).GetObjectID(uid);
}

} // namespace neurus
