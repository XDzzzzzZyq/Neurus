#include "asset/components/SceneComponent.h"
#include "scene/Scene.h"
#include "scene/Camera.h"
#include "core/Log.h"

namespace neurus::project
{

SceneComponent::SceneComponent(Scene& scene) : m_scene(&scene) {}

void SceneComponent::Save(cereal::JSONOutputArchive& ar) const
{
    ar(cereal::make_nvp("m_scene", *m_scene));
}

void SceneComponent::Load(cereal::JSONInputArchive& ar)
{
    try { ar(cereal::make_nvp("m_scene", *m_scene)); }
    catch (const cereal::Exception& e)
    {
        NEURUS_ERR("SceneComponent::Load: " << e.what() << " - using empty scene.");
    }

    if (m_scene->cam_list.empty())
    {
        NEURUS_ERR("[SceneComponent] No camera in scene, adding default camera.");
        auto defaultCam = std::make_shared<Camera>();
        defaultCam->SetPosition(glm::vec3(0.0f, -5.0f, 2.0f));
        defaultCam->cam_tar = glm::vec3(0.0f, 0.0f, 0.0f);
        m_scene->UseCamera(defaultCam);
    }
}

} // namespace neurus::project
