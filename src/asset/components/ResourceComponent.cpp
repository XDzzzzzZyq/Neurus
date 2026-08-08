/**
 * @file ResourceComponent.cpp
 * @brief ResourceManager pool persistence (see ResourceComponent.h).
 */

#include "asset/components/ResourceComponent.h"
#include "core/Log.h"
#include "core/ResourceManager.h"

// Force-link the polymorphic registration TUs (static libraries) so the pool's
// UID-derived types are registered before serialization runs.
#include "scene/registrations/TypeRegistration.h"
#include "asset/registrations/DataRegistration.h"
#include "render/registrations/ShaderRegistration.h"

namespace neurus::project
{

ResourceComponent::ResourceComponent(ResourceManager& resources)
	: m_resources(&resources)
{
}

void ResourceComponent::Save(cereal::JSONOutputArchive& ar) const
{
	ar(cereal::make_nvp("m_resources", *m_resources));
}

void ResourceComponent::Load(cereal::JSONInputArchive& ar)
{
	try
	{
		ar(cereal::make_nvp("m_resources", *m_resources));
	}
	catch (const cereal::Exception& e)
	{
		// Legacy project file (no "m_resources" node): leave the pool empty.
		// The Scene's ID lists will also fail to load and degrade to the
		// default-camera fallback.
		NEURUS_ERR("ResourceComponent::Load: " << e.what() << " - using empty resource pool.");
		m_resources->Clear();
	}
}

} // namespace neurus::project
