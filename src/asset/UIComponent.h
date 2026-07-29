#pragma once

#include <string>

#include "asset/Serializable.h"

namespace neurus::project
{

/**
 * @brief Serializable component wrapping an opaque UI-state blob.
 *
 * The blob is produced and consumed entirely by the UI layer
 * (UIManager::ExportLayout / ApplyLayout) and bundles the main-window
 * geometry plus the ADS dock-manager state. Asset/Application layers
 * treat it as an opaque string, keeping this component Qt-free.
 */
class UIComponent : public Serializable
{
public:
    explicit UIComponent(std::string& ui);
    const char* Key() const noexcept override { return "ui"; }
    void Save(cereal::JSONOutputArchive& ar) const override;
    void Load(cereal::JSONInputArchive& ar) override;
private:
    std::string* m_ui;
};

} // namespace neurus::project
