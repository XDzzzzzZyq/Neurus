#pragma once

#include <QWidget>
#include <string>

class QCheckBox;
class QLabel;

namespace neurus {

class ScalarSlider;

/**
 * @brief Light-specific property editor subpanel.
 *
 * Displays light_type (read-only label), light_power and light_radius
 * (ScalarSlider), and use_shadow (checkbox). All edits emit signals
 * carrying the object ID for routing through the Editor event system.
 *
 * Supports lazy update via dirty-check: each setter caches the last
 * value and no-ops if unchanged.
 */
class LightProperties : public QWidget
{
	Q_OBJECT

public:
	explicit LightProperties(QWidget* parent = nullptr);
	~LightProperties() override = default;

	LightProperties(const LightProperties&) = delete;
	LightProperties& operator=(const LightProperties&) = delete;

	/** @brief Sets the editing object ID (resets caches when ID changes). */
	void setObjectId(int id);

	/** @brief Updates the light type display name (dirty-checked). */
	void setLightType(const std::string& typeName);

	/** @brief Updates the light power slider (dirty-checked). */
	void setPower(float power);

	/** @brief Updates the light radius slider (dirty-checked). */
	void setRadius(float radius);

	/** @brief Updates the shadow checkbox (dirty-checked). */
	void setShadowEnabled(bool enabled);

signals:
	void powerChanged(int objectId, float power);
	void radiusChanged(int objectId, float radius);
	void shadowChanged(int objectId, bool enabled);

private:
	int m_objectId = -1;

	// --- Widgets ---
	QLabel*       m_typeLabel   = nullptr;
	ScalarSlider* m_powerSlider = nullptr;
	ScalarSlider* m_radiusSlider = nullptr;
	QCheckBox*    m_shadowChk   = nullptr;

	// --- Cached values for dirty-check ---
	std::string m_cachedType;
	float       m_cachedPower  = -1.0f;
	float       m_cachedRadius = -1.0f;
	int         m_cachedShadow = -1;  // -1 = uninitialized
};

} // namespace neurus
