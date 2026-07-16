#include "presets/LightProperties.h"
#include "items/ScalarSlider.h"

#include <QCheckBox>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace neurus {

// =========================================================================
// Constructor
// =========================================================================

LightProperties::LightProperties(QWidget* parent)
	: QWidget(parent)
{
	auto* outerLayout = new QVBoxLayout(this);
	outerLayout->setContentsMargins(0, 0, 0, 0);

	auto* groupBox = new QGroupBox("Light", this);
	outerLayout->addWidget(groupBox);

	auto* groupLayout = new QVBoxLayout(groupBox);
	groupLayout->setSpacing(6);

	// --- Row: Type label + type name (readonly, bold) ---
	auto* typeRow = new QHBoxLayout();
	typeRow->addWidget(new QLabel("Type"));

	m_typeLabel = new QLabel("Unknown");
	QFont typeFont = m_typeLabel->font();
	typeFont.setBold(true);
	m_typeLabel->setFont(typeFont);
	typeRow->addWidget(m_typeLabel, 1);
	groupLayout->addLayout(typeRow);

	// --- Row: Power label + ScalarSlider ---
	auto* powerRow = new QHBoxLayout();
	powerRow->addWidget(new QLabel("Power"));
	m_powerSlider = new ScalarSlider(0.0, 100.0, 1000, 10.0);
	powerRow->addWidget(m_powerSlider, 1);
	groupLayout->addLayout(powerRow);

	// --- Row: Radius label + ScalarSlider ---
	auto* radiusRow = new QHBoxLayout();
	radiusRow->addWidget(new QLabel("Radius"));
	m_radiusSlider = new ScalarSlider(0.0, 10.0, 1000, 0.05);
	radiusRow->addWidget(m_radiusSlider, 1);
	groupLayout->addLayout(radiusRow);

	// --- Row: Cast Shadow checkbox ---
	m_shadowChk = new QCheckBox("Cast Shadow");
	groupLayout->addWidget(m_shadowChk);

	outerLayout->addStretch();

	// --- Signal wiring ---
	QObject::connect(m_powerSlider, &ScalarSlider::valueChanged, this,
		[this]() {
			if (m_objectId < 0) return;
			emit powerChanged(m_objectId, static_cast<float>(m_powerSlider->value()));
		});

	QObject::connect(m_radiusSlider, &ScalarSlider::valueChanged, this,
		[this]() {
			if (m_objectId < 0) return;
			emit radiusChanged(m_objectId, static_cast<float>(m_radiusSlider->value()));
		});

	QObject::connect(m_shadowChk, &QCheckBox::toggled, this,
		[this](bool checked) {
			if (m_objectId < 0) return;
			emit shadowChanged(m_objectId, checked);
		});
}

// =========================================================================
// Setters (all dirty-checked)
// =========================================================================

void LightProperties::setObjectId(int id)
{
	if (m_objectId != id)
	{
		m_objectId = id;
		m_cachedType.clear();
		m_cachedPower  = -1.0f;
		m_cachedRadius = -1.0f;
		m_cachedShadow = -1;
	}
}

void LightProperties::setLightType(const std::string& typeName)
{
	if (m_cachedType == typeName) return;
	m_cachedType = typeName;
	m_typeLabel->setText(QString::fromStdString(typeName));
}

void LightProperties::setPower(float power)
{
	if (m_cachedPower == power) return;
	m_cachedPower = power;
	m_powerSlider->setValue(static_cast<double>(power));
}

void LightProperties::setRadius(float radius)
{
	if (m_cachedRadius == radius) return;
	m_cachedRadius = radius;
	m_radiusSlider->setValue(static_cast<double>(radius));
}

void LightProperties::setShadowEnabled(bool enabled)
{
	int val = enabled ? 1 : 0;
	if (m_cachedShadow == val) return;
	m_cachedShadow = val;
	m_shadowChk->blockSignals(true);
	m_shadowChk->setChecked(enabled);
	m_shadowChk->blockSignals(false);
}

} // namespace neurus
