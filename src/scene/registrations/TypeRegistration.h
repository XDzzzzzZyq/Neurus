/**
 * @file TypeRegistration.h
 * @brief Forces linking of the scene polymorphic-type registrations.
 *
 * The registrations live in TypeRegistration.cpp (CEREAL_REGISTER_TYPE +
 * CEREAL_REGISTER_POLYMORPHIC_RELATION for scene types, including the
 * UID-level relations the ResourceManager pool relies on). When the scene
 * layer is built as a static library, that translation unit is dropped by the
 * linker unless a symbol from it is odr-used. Any TU that (de)serializes a
 * pooled UID pointer or an ObjectID pointer must include this header so the
 * CEREAL_FORCE_DYNAMIC_INIT below pulls the registration TU in and runs its
 * static initializers before serialization.
 *
 * Mirrors editor/operations/registrations/OperationRegistration.h.
 */

#pragma once

#include <cereal/types/polymorphic.hpp>

CEREAL_FORCE_DYNAMIC_INIT(neurus_scene_types)
