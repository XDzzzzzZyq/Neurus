/**
 * @file ShaderRegistration.cpp
 * @brief Cereal polymorphic registration for pooled shaders.
 *
 * Registers RenderShader (the concrete leaf of the abstract Shader base) as a
 * polymorphic leaf of the core UID base, so the ResourceManager pool can
 * serialize per-mesh shaders by type name. The abstract Shader base is never
 * a dynamic type and is deliberately not registered.
 */

#include <cereal/archives/json.hpp>
#include <cereal/types/polymorphic.hpp>

#include "core/UID.h"
#include "render/shaders/RenderShader.h"

// --- Derived shader registration (concrete leaf only) ---
CEREAL_REGISTER_TYPE(neurus::RenderShader)

// --- Polymorphic relation (UID base -> shader leaf) ---
CEREAL_REGISTER_POLYMORPHIC_RELATION(neurus::UID, neurus::RenderShader)

CEREAL_REGISTER_DYNAMIC_INIT(neurus_shader_types)
