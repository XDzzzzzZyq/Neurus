/**
 * @file RenderConfigPanel.cpp
 * @brief RenderConfigPanel implementation — hard-coded UI layout for render settings.
 */

#include "ui/RenderConfigPanel.h"

#include "render/RenderConfig.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace neurus
{

// =========================================================================
// Constructor / Destructor
// =========================================================================

RenderConfigPanel::RenderConfigPanel(QWidget* parent)
	: QWidget(parent)
{
	// --- Main layout ---
	auto* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);

	// --- Scroll area ---
	auto* scrollArea = new QScrollArea(this);
	scrollArea->setWidgetResizable(true);
	scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	mainLayout->addWidget(scrollArea);

	// --- Form container ---
	auto* formContainer = new QWidget();
	auto* formLayout = new QVBoxLayout(formContainer);
	formLayout->setContentsMargins(6, 6, 6, 6);
	formLayout->setSpacing(6);

	// --- Build sections ---
	BuildShadowsSection();
	formLayout->addWidget(m_shadowsGroup);

	BuildAmbientOcclusionSection();
	formLayout->addWidget(m_aoGroup);

	BuildLightingSection();
	formLayout->addWidget(m_lightingGroup);

	BuildPostProcessingSection();
	formLayout->addWidget(m_postGroup);

	BuildPipelineSection();
	formLayout->addWidget(m_pipelineGroup);

	// --- Bottom stretch ---
	formLayout->addStretch();

	scrollArea->setWidget(formContainer);

	// --- Wire all signals ---
	ConnectAllSignals();
}

// =========================================================================
// Section builders
// =========================================================================

// --- Helper: create a labelled combo box row in a form layout ---
static QComboBox* addComboRow(QFormLayout* form, const QString& label, const QStringList& items)
{
	auto* combo = new QComboBox();
	combo->addItems(items);
	form->addRow(label, combo);
	return combo;
}

// --- Helper: create a QSpinBox row ---
static QSpinBox* addSpinRow(QFormLayout* form, const QString& label, int min, int max, int initial)
{
	auto* spin = new QSpinBox();
	spin->setRange(min, max);
	spin->setValue(initial);
	form->addRow(label, spin);
	return spin;
}

// --- Helper: slider + spinbox pair for float values ---
RenderConfigPanel::SliderSpinPair RenderConfigPanel::CreateFloatSlider(
	double min, double max, double step, int sliderSteps, double initial)
{
	SliderSpinPair pair{};

	pair.spin = new QDoubleSpinBox();
	pair.spin->setRange(min, max);
	pair.spin->setSingleStep(step);
	pair.spin->setDecimals(2);
	pair.spin->setValue(initial);
	pair.spin->setMinimumWidth(70);

	pair.slider = new QSlider(Qt::Horizontal);
	pair.slider->setRange(0, sliderSteps);
	pair.slider->setValue(static_cast<int>((initial - min) / (max - min) * sliderSteps));

	// Bidirectional sync: slider ↔ spinbox
	QObject::connect(pair.slider, &QSlider::valueChanged, [=](int val) {
		double d = min + (max - min) * val / sliderSteps;
		pair.spin->blockSignals(true);
		pair.spin->setValue(d);
		pair.spin->blockSignals(false);
	});

	QObject::connect(pair.spin,
		QOverload<double>::of(&QDoubleSpinBox::valueChanged), [=](double val) {
			int s = static_cast<int>((val - min) / (max - min) * sliderSteps);
			pair.slider->blockSignals(true);
			pair.slider->setValue(s);
			pair.slider->blockSignals(false);
		});

	return pair;
}

// --- Helper: add a slider+spin pair row ---
static void addSliderSpinRow(QFormLayout* form, const QString& label,
	const RenderConfigPanel::SliderSpinPair& pair)
{
	auto* row = new QHBoxLayout();
	row->setSpacing(4);
	row->addWidget(pair.slider);
	row->addWidget(pair.spin);
	form->addRow(label, row);
}

// =========================================================================
// Shadows
// =========================================================================

void RenderConfigPanel::BuildShadowsSection()
{
	m_shadowsGroup = new QGroupBox("Shadows");
	m_shadowsGroup->setCheckable(true);
	m_shadowsGroup->setChecked(true);

	auto* form = new QFormLayout(m_shadowsGroup);
	form->setContentsMargins(8, 12, 8, 8);
	form->setSpacing(4);

	m_shadowAlgCombo = addComboRow(form, "Algorithm",
		{"None", "Shadow Mapping", "SDF Soft Shadow", "VSSM"});

	m_shadowPCFCombo = addComboRow(form, "PCF Filter",
		{"Hard", "Soft PCF 16", "Soft PCF 64"});
}

// =========================================================================
// Ambient Occlusion
// =========================================================================

void RenderConfigPanel::BuildAmbientOcclusionSection()
{
	m_aoGroup = new QGroupBox("Ambient Occlusion");
	m_aoGroup->setCheckable(true);
	m_aoGroup->setChecked(true);

	auto* form = new QFormLayout(m_aoGroup);
	form->setContentsMargins(8, 12, 8, 8);
	form->setSpacing(4);

	m_aoAlgCombo = addComboRow(form, "Algorithm", {"None", "SSAO"});
	m_aoAlgCombo->setCurrentIndex(1);  // Default: SSAO

	m_aoKernelSpin = addSpinRow(form, "Kernel Size", 1, 64, 16);

	m_aoRadiusSlider = CreateFloatSlider(0.01, 5.0, 0.01, 500, 0.5);
	addSliderSpinRow(form, "Radius", m_aoRadiusSlider);
}

// =========================================================================
// Lighting
// =========================================================================

void RenderConfigPanel::BuildLightingSection()
{
	m_lightingGroup = new QGroupBox("Lighting");
	m_lightingGroup->setCheckable(false);  // Always enabled

	auto* form = new QFormLayout(m_lightingGroup);
	form->setContentsMargins(8, 12, 8, 8);
	form->setSpacing(4);

	m_iblCheckBox = new QCheckBox("Enable IBL");
	m_iblCheckBox->setChecked(true);
	form->addRow(m_iblCheckBox);

	m_exposureSlider = CreateFloatSlider(0.0, 5.0, 0.05, 100, 1.0);
	addSliderSpinRow(form, "Exposure", m_exposureSlider);
}

// =========================================================================
// Post-Processing
// =========================================================================

void RenderConfigPanel::BuildPostProcessingSection()
{
	m_postGroup = new QGroupBox("Post-Processing");
	m_postGroup->setCheckable(false);

	auto* form = new QFormLayout(m_postGroup);
	form->setContentsMargins(8, 12, 8, 8);
	form->setSpacing(4);

	m_aaCombo = addComboRow(form, "Anti-Aliasing", {"None", "MSAA", "FXAA"});

	m_gammaSlider = CreateFloatSlider(1.0, 3.0, 0.01, 200, 1.0);
	addSliderSpinRow(form, "Gamma", m_gammaSlider);
}

// =========================================================================
// Pipeline
// =========================================================================

void RenderConfigPanel::BuildPipelineSection()
{
	m_pipelineGroup = new QGroupBox("Pipeline");
	m_pipelineGroup->setCheckable(false);

	auto* form = new QFormLayout(m_pipelineGroup);
	form->setContentsMargins(8, 12, 8, 8);
	form->setSpacing(4);

	m_pipelineCombo = addComboRow(form, "Pipeline",
		{"Forward", "Deferred"});
	m_pipelineCombo->setCurrentIndex(1);  // Default: Deferred

	m_ssrCombo = addComboRow(form, "SSR",
		{"None", "Ray Marching", "SDF Ray Marching", "SDF Resolved"});

	m_samplesPerFrameSpin = addSpinRow(form, "Samples / Frame", 1, 1024, 128);
}

// =========================================================================
// Signal wiring
// =========================================================================

void RenderConfigPanel::ConnectAllSignals()
{
	// --- Shadows ---
	connect(m_shadowsGroup, &QGroupBox::toggled, this, &RenderConfigPanel::configValueChanged);
	connect(m_shadowAlgCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &RenderConfigPanel::configValueChanged);
	connect(m_shadowPCFCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &RenderConfigPanel::configValueChanged);

	// --- Ambient Occlusion ---
	connect(m_aoGroup, &QGroupBox::toggled, this, &RenderConfigPanel::configValueChanged);
	connect(m_aoAlgCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &RenderConfigPanel::configValueChanged);
	connect(m_aoKernelSpin, QOverload<int>::of(&QSpinBox::valueChanged),
		this, &RenderConfigPanel::configValueChanged);
	connect(m_aoRadiusSlider.spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
		this, &RenderConfigPanel::configValueChanged);

	// --- Lighting ---
	connect(m_iblCheckBox, &QCheckBox::toggled, this, &RenderConfigPanel::configValueChanged);
	connect(m_exposureSlider.spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
		this, &RenderConfigPanel::configValueChanged);

	// --- Post-Processing ---
	connect(m_aaCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &RenderConfigPanel::configValueChanged);
	connect(m_gammaSlider.spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
		this, &RenderConfigPanel::configValueChanged);

	// --- Pipeline ---
	connect(m_pipelineCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &RenderConfigPanel::configValueChanged);
	connect(m_ssrCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &RenderConfigPanel::configValueChanged);
	connect(m_samplesPerFrameSpin, QOverload<int>::of(&QSpinBox::valueChanged),
		this, &RenderConfigPanel::configValueChanged);
}

// =========================================================================
// RenderConfig ↔ UI sync
// =========================================================================

void RenderConfigPanel::LoadFromConfig(const RenderConfig& config)
{
	// --- Shadows ---
	{
		QSignalBlocker b1(m_shadowAlgCombo);
		QSignalBlocker b2(m_shadowsGroup);
		switch (config.r_shadow)
		{
		case ShadowAlg::None:           m_shadowAlgCombo->setCurrentIndex(0); break;
		case ShadowAlg::ShadowMapping:  m_shadowAlgCombo->setCurrentIndex(1); break;
		case ShadowAlg::SDFSoftShadow:  m_shadowAlgCombo->setCurrentIndex(2); break;
		case ShadowAlg::VSSM:           m_shadowAlgCombo->setCurrentIndex(3); break;
		}
		m_shadowsGroup->setChecked(config.r_shadow != ShadowAlg::None);
	}

	// --- Ambient Occlusion ---
	{
		QSignalBlocker b1(m_aoAlgCombo);
		QSignalBlocker b2(m_aoGroup);
		switch (config.r_ao)
		{
		case AOAlg::None: m_aoAlgCombo->setCurrentIndex(0); break;
		case AOAlg::SSAO: m_aoAlgCombo->setCurrentIndex(1); break;
		}
		m_aoGroup->setChecked(config.r_ao != AOAlg::None);

		QSignalBlocker b3(m_aoKernelSpin);
		m_aoKernelSpin->setValue(config.r_ao_ksize);

		QSignalBlocker b4(m_aoRadiusSlider.spin);
		QSignalBlocker b5(m_aoRadiusSlider.slider);
		m_aoRadiusSlider.spin->setValue(config.r_ao_radius);
	}

	// --- Post-Processing ---
	{
		QSignalBlocker b1(m_aaCombo);
		switch (config.r_aa)
		{
		case AAAlg::None: m_aaCombo->setCurrentIndex(0); break;
		case AAAlg::MSAA: m_aaCombo->setCurrentIndex(1); break;
		case AAAlg::FXAA: m_aaCombo->setCurrentIndex(2); break;
		}

		QSignalBlocker b2(m_gammaSlider.spin);
		QSignalBlocker b3(m_gammaSlider.slider);
		m_gammaSlider.spin->setValue(config.r_gamma);
	}

	// --- Pipeline ---
	{
		QSignalBlocker b1(m_pipelineCombo);
		switch (config.r_pipeline)
		{
		case RenderPipeLine::Forward:  m_pipelineCombo->setCurrentIndex(0); break;
		case RenderPipeLine::Deferred: m_pipelineCombo->setCurrentIndex(1); break;
		case RenderPipeLine::Custom0:  m_pipelineCombo->setCurrentIndex(0); break;  // fallback
		}

		QSignalBlocker b2(m_ssrCombo);
		switch (config.r_ssr)
		{
		case SSRAlg::None:                    m_ssrCombo->setCurrentIndex(0); break;
		case SSRAlg::RayMarching:             m_ssrCombo->setCurrentIndex(1); break;
		case SSRAlg::SDFRayMarching:          m_ssrCombo->setCurrentIndex(2); break;
		case SSRAlg::SDFResolvedRayMarching:  m_ssrCombo->setCurrentIndex(3); break;
		}

		QSignalBlocker b3(m_samplesPerFrameSpin);
		m_samplesPerFrameSpin->setValue(config.r_sample_pf);
	}
}

void RenderConfigPanel::SaveToConfig(RenderConfig& config) const
{
	// --- Shadows ---
	switch (m_shadowAlgCombo->currentIndex())
	{
	case 0: config.r_shadow = ShadowAlg::None;          break;
	case 1: config.r_shadow = ShadowAlg::ShadowMapping; break;
	case 2: config.r_shadow = ShadowAlg::SDFSoftShadow; break;
	case 3: config.r_shadow = ShadowAlg::VSSM;          break;
	}
	if (!m_shadowsGroup->isChecked())
		config.r_shadow = ShadowAlg::None;

	// --- Ambient Occlusion ---
	switch (m_aoAlgCombo->currentIndex())
	{
	case 0: config.r_ao = AOAlg::None; break;
	case 1: config.r_ao = AOAlg::SSAO; break;
	}
	if (!m_aoGroup->isChecked())
		config.r_ao = AOAlg::None;

	config.r_ao_ksize  = m_aoKernelSpin->value();
	config.r_ao_radius = static_cast<float>(m_aoRadiusSlider.spin->value());

	// --- Post-Processing ---
	switch (m_aaCombo->currentIndex())
	{
	case 0: config.r_aa = AAAlg::None; break;
	case 1: config.r_aa = AAAlg::MSAA; break;
	case 2: config.r_aa = AAAlg::FXAA; break;
	}
	config.r_gamma = static_cast<float>(m_gammaSlider.spin->value());

	// --- Pipeline ---
	switch (m_pipelineCombo->currentIndex())
	{
	case 0: config.r_pipeline = RenderPipeLine::Forward;  break;
	case 1: config.r_pipeline = RenderPipeLine::Deferred; break;
	}

	switch (m_ssrCombo->currentIndex())
	{
	case 0: config.r_ssr = SSRAlg::None;                    break;
	case 1: config.r_ssr = SSRAlg::RayMarching;             break;
	case 2: config.r_ssr = SSRAlg::SDFRayMarching;          break;
	case 3: config.r_ssr = SSRAlg::SDFResolvedRayMarching;  break;
	}
	config.r_sample_pf = m_samplesPerFrameSpin->value();
}

} // namespace neurus
