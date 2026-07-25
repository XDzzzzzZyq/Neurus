#include "asset/ConfigComponent.h"
#include "render/RenderConfig.h"
#include "core/Log.h"

namespace neurus::project
{

ConfigComponent::ConfigComponent(RenderConfig& config) : m_config(&config) {}

void ConfigComponent::Save(cereal::JSONOutputArchive& ar) const
{
    ar(CEREAL_NVP(*m_config));
}

void ConfigComponent::Load(cereal::JSONInputArchive& ar)
{
    try { ar(CEREAL_NVP(*m_config)); }
    catch (const cereal::Exception& e)
    {
        NEURUS_ERR("ConfigComponent::Load: " << e.what()
            << " - proj_config defaulted (old project format).");
        *m_config = RenderConfig{};
    }
}

} // namespace neurus::project
