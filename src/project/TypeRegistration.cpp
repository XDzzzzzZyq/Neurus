/**
 * @file TypeRegistration.cpp
 * @brief Cereal polymorphic type registration for scene objects.
 *
 * Registers all serializable scene types with cereal's polymorphic
 * serialization system. This enables serializing derived types through
 * base class pointers (e.g. shared_ptr<ObjectID> in Scene::obj_list).
 *
 * These macros generate template specializations at global scope.
 * They MUST reside in a .cpp file (not a header) to avoid ODR violations.
 */

#include <cereal/types/polymorphic.hpp>

#include "scene/Camera.h"
#include "scene/DebugLine.h"
#include "scene/DebugPoints.h"
#include "scene/Light.h"
#include "scene/Mesh.h"
#include "scene/Sprite.h"
#include "scene/UID.h"

// --- Base type registration ---
CEREAL_REGISTER_TYPE(neurus::ObjectID)

// --- Derived type registration ---
CEREAL_REGISTER_TYPE(neurus::Camera)
CEREAL_REGISTER_TYPE(neurus::Mesh)
CEREAL_REGISTER_TYPE(neurus::Light)
CEREAL_REGISTER_TYPE(neurus::Sprite)
CEREAL_REGISTER_TYPE(neurus::DebugLine)
CEREAL_REGISTER_TYPE(neurus::DebugPoints)

// --- Polymorphic relations (base → derived) ---
CEREAL_REGISTER_POLYMORPHIC_RELATION(neurus::ObjectID, neurus::Camera)
CEREAL_REGISTER_POLYMORPHIC_RELATION(neurus::ObjectID, neurus::Mesh)
CEREAL_REGISTER_POLYMORPHIC_RELATION(neurus::ObjectID, neurus::Light)
CEREAL_REGISTER_POLYMORPHIC_RELATION(neurus::ObjectID, neurus::Sprite)
CEREAL_REGISTER_POLYMORPHIC_RELATION(neurus::ObjectID, neurus::DebugLine)
CEREAL_REGISTER_POLYMORPHIC_RELATION(neurus::ObjectID, neurus::DebugPoints)
