/**
 * @file RenderConfigPanel.h
 * @brief Render Config dock panel exposing live-adjustable rendering settings.
 *
 * The RenderConfigPanel provides Qt widgets (sliders, combo boxes, checkboxes)
 * for all fields in RenderConfig plus additional runtime toggles (shadow PCF
 * mode, IBL toggle, exposure). Changes emit `configValueChanged()` to trigger
 * real-time renderer updates via UIEvents::renderConfigChanged().
 *
 * Architecture:
 * - QWidget subclass with QFormLayout inside QScrollArea
 * - Controls grouped under collapsible QGroupBox sections
 * - No Vulkan or Renderer header includes — pure UI layer
 * - Emits configValueChanged() Qt signal; wired by Application to renderConfigChanged()
 *
 * @note UI Layer — communicates via Qt signals (no direct Renderer coupling).
 */

#pragma once

#include "UIPanel.h"

// Forward declarations
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QSlider;
class QSpinBox;

namespace neurus
{

class RenderConfig;

class RenderConfigPanel : public UIPanel
{
	Q_OBJECT

public:
	static constexpr PanelType kType = PanelType::RenderConfig;

	explicit RenderConfigPanel(QWidget* parent = nullptr);
	~RenderConfigPanel() override = default;

	RenderConfigPanel(const RenderConfigPanel&) = delete;
	RenderConfigPanel& operator=(const RenderConfigPanel&) = delete;

	/**
	 * @brief Populates all controls from a RenderConfig snapshot.
	 * @param config Read-only reference to the current render config.
	 */
	void LoadFromConfig(const RenderConfig& config);

	/**
	 * @brief Writes current UI values back into a RenderConfig struct.
	 * @param config [out] RenderConfig to populate.
	 */
	void SaveToConfig(RenderConfig& config) const;

	// --- Helper: slider + spinbox pair for float values ---
	struct SliderSpinPair
	{
		QSlider*         slider = nullptr;
		QDoubleSpinBox*  spin   = nullptr;
	};

signals:
	/** @brief Emitted whenever any control value changes. */
	void configValueChanged();

private:
	// --- Section builders ---
	void BuildShadowsSection();
	void BuildAmbientOcclusionSection();
	void BuildLightingSection();
	void BuildPostProcessingSection();
	void BuildPipelineSection();

	SliderSpinPair CreateFloatSlider(double min, double max, double step, int sliderSteps, double initial);

	// --- Signal forwarders ---
	void ConnectAllSignals();

	// --- Shadows ---
	QGroupBox*  m_shadowsGroup      = nullptr;
	QComboBox*  m_shadowAlgCombo    = nullptr;
	QComboBox*  m_shadowPCFCombo    = nullptr;  ///< PCF filter mode (Hard/Soft16/Soft64)

	// --- Ambient Occlusion ---
	QGroupBox*  m_aoGroup           = nullptr;
	QComboBox*  m_aoAlgCombo        = nullptr;
	QSpinBox*   m_aoKernelSpin      = nullptr;
	SliderSpinPair m_aoRadiusSlider;

	// --- Lighting ---
	QGroupBox*  m_lightingGroup     = nullptr;
	QCheckBox*  m_iblCheckBox       = nullptr;
	SliderSpinPair m_exposureSlider;

	// --- Post-Processing ---
	QGroupBox*  m_postGroup         = nullptr;
	QComboBox*  m_aaCombo           = nullptr;
	SliderSpinPair m_gammaSlider;

	// --- Pipeline ---
	QGroupBox*  m_pipelineGroup     = nullptr;
	QComboBox*  m_pipelineCombo     = nullptr;
	QComboBox*  m_ssrCombo          = nullptr;
	QSpinBox*   m_samplesPerFrameSpin = nullptr;
};

} // namespace neurus
