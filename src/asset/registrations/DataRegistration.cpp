/**
 * @file DataRegistration.cpp
 * @brief Cereal polymorphic registration for pooled data resources.
 *
 * Registers MeshData and ImageData (asset layer, CPU-side data resources) as
 * polymorphic leaves of the core UID base, so the ResourceManager pool
 * (unordered_map<int, shared_ptr<UID>>) can serialize them by type name.
 */

#include <cereal/archives/json.hpp>
#include <cereal/types/polymorphic.hpp>

#include "asset/data/ImageData.h"
#include "asset/data/MeshData.h"
#include "core/UID.h"

// --- Base type registration (UID is in the scene TypeRegistration TU, but
//     re-registering the leaves here is required for this TU to be complete) ---

// --- Derived data-resource registration ---
CEREAL_REGISTER_TYPE(neurus::MeshData)
CEREAL_REGISTER_TYPE(neurus::ImageData)

// --- Polymorphic relations (UID base -> data resource leaves) ---
CEREAL_REGISTER_POLYMORPHIC_RELATION(neurus::UID, neurus::MeshData)
CEREAL_REGISTER_POLYMORPHIC_RELATION(neurus::UID, neurus::ImageData)

CEREAL_REGISTER_DYNAMIC_INIT(neurus_data_types)
