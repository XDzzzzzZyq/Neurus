/**
 * @file OperationRegistration.h
 * @brief Forces linking of the operation polymorphic-type registrations.
 *
 * The registrations live in OperationRegistration.cpp (CEREAL_REGISTER_TYPE +
 * CEREAL_REGISTER_POLYMORPHIC_RELATION). When the editor layer is built as a
 * static library, that translation unit is dropped by the linker unless a
 * symbol from it is odr-used. Any TU that (de)serializes an Operation pointer
 * must include this header so the CEREAL_FORCE_DYNAMIC_INIT below pulls the
 * registration TU in and runs its static initializers before serialization.
 */

#pragma once

#include <cereal/types/polymorphic.hpp>

CEREAL_FORCE_DYNAMIC_INIT(neurus_operations)
