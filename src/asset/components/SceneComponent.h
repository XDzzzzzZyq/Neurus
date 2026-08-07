#pragma once

#include "asset/Serializable.h"

namespace neurus { class Scene; }

namespace neurus::project
{

class SceneComponent : public Serializable
{
public:
    explicit SceneComponent(Scene& scene);
    const char* Key() const noexcept override { return "m_scene"; }
    void Save(cereal::JSONOutputArchive& ar) const override;
    void Load(cereal::JSONInputArchive& ar) override;
private:
    Scene* m_scene;
};

} // namespace neurus::project
