/**
 * @file DataRegistration.h
 * @brief Forces linking of the pooled data-resource polymorphic registrations.
 *
 * The registrations live in DataRegistration.cpp (MeshData, ImageData as
 * polymorphic leaves of the UID base). Include this header at any
 * serialization site that touches the ResourceManager pool (e.g.
 * ResourceComponent.cpp) so the static-library TU is not dead-stripped.
 */

#pragma once

#include <cereal/types/polymorphic.hpp>

CEREAL_FORCE_DYNAMIC_INIT(neurus_data_types)
