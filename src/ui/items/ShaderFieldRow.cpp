#include "ShaderFieldRow.h"

#include "render/shaders/ShaderStruct.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>

namespace neurus
{

ShaderFieldRow::ShaderFieldRow(QWidget* parent)
	: QWidget(parent)
{
	auto* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(4);

	// Location spinbox (hidden by default)
	m_locationSpin = new QSpinBox(this);
	m_locationSpin->setRange(0, 31);
	m_locationSpin->setFixedWidth(50);
	m_locationSpin->setVisible(false);
	layout->addWidget(m_locationSpin);

	// Type combo
	m_typeCombo = new QComboBox(this);
	m_typeCombo->setMinimumWidth(90);
	populateTypeCombo();
	layout->addWidget(m_typeCombo);

	// Interpolation combo (hidden by default)
	m_interpCombo = new QComboBox(this);
	m_interpCombo->addItem("smooth");
	m_interpCombo->addItem("flat");
	m_interpCombo->addItem("noperspective");
	m_interpCombo->setVisible(false);
	layout->addWidget(m_interpCombo);

	// Name field
	m_nameField = new QLineEdit(this);
	m_nameField->setPlaceholderText("name");
	layout->addWidget(m_nameField, 1);

	// Connect signals
	auto emitChanged = [this]()
	{
		emit fieldChanged(
			m_locationSpin->value(),
			m_typeCombo->currentText(),
			m_nameField->text());
	};

	QObject::connect(m_locationSpin, QOverload<int>::of(&QSpinBox::valueChanged),
	                 this, emitChanged);
	QObject::connect(m_typeCombo, &QComboBox::currentTextChanged,
	                 this, emitChanged);
	QObject::connect(m_nameField, &QLineEdit::textChanged,
	                 this, emitChanged);
}

void ShaderFieldRow::populateTypeCombo()
{
	m_typeCombo->clear();
	// Add commonly used GLSL types
	m_typeCombo->addItem("float");
	m_typeCombo->addItem("vec2");
	m_typeCombo->addItem("vec3");
	m_typeCombo->addItem("vec4");
	m_typeCombo->addItem("mat3");
	m_typeCombo->addItem("mat4");
	m_typeCombo->addItem("int");
	m_typeCombo->addItem("uint");
	// Add all registered types from ShaderStruct type table
	for (const auto& typeName : ShaderStruct::type_table)
	{
		if (typeName.empty()) continue;
		// Skip duplicates from hardcoded list above
		if (m_typeCombo->findText(QString::fromStdString(typeName)) == -1)
			m_typeCombo->addItem(QString::fromStdString(typeName));
	}
}

void ShaderFieldRow::setShowLocation(bool show)
{
	if (m_showLocation == show) return;
	m_showLocation = show;
	m_locationSpin->setVisible(show);
}

void ShaderFieldRow::setShowInterpolation(bool show)
{
	if (m_showInterp == show) return;
	m_showInterp = show;
	m_interpCombo->setVisible(show);
}

void ShaderFieldRow::setField(int location, const std::string& type, const std::string& name)
{
	// Dirty-check all three values
	bool changed = false;
	if (m_cachedLocation != location) { m_cachedLocation = location; changed = true; }
	if (m_cachedType != type)         { m_cachedType = type;         changed = true; }
	if (m_cachedName != name)         { m_cachedName = name;         changed = true; }

	if (!changed) return;

	QSignalBlocker lockLoc(m_locationSpin);
	QSignalBlocker lockType(m_typeCombo);
	QSignalBlocker lockName(m_nameField);

	m_locationSpin->setValue(location);

	int idx = m_typeCombo->findText(QString::fromStdString(type));
	if (idx >= 0)
		m_typeCombo->setCurrentIndex(idx);

	m_nameField->setText(QString::fromStdString(name));
}

void ShaderFieldRow::setField(const std::string& type, const std::string& name)
{
	setField(0, type, name);
}

void ShaderFieldRow::setReadOnly(bool readOnly)
{
	m_typeCombo->setEnabled(!readOnly);
	m_nameField->setReadOnly(readOnly);
	m_locationSpin->setReadOnly(readOnly);
	m_interpCombo->setEnabled(!readOnly);
}

void ShaderFieldRow::resetCaches()
{
	m_cachedLocation = -1;
	m_cachedType.clear();
	m_cachedName.clear();
}

} // namespace neurus
