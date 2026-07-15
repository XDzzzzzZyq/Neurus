#include "panels/PropertyPanel.h"

#include "UIContext.h"

#include <QGridLayout>

namespace neurus {

// =========================================================================
// Constructor
// =========================================================================

PropertyPanel::PropertyPanel(QWidget* parent)
	: UIPanel(PanelType::PropertyPanel, QString(), parent)
{
	m_mainLayout = new QVBoxLayout(this);
	m_mainLayout->setContentsMargins(8, 8, 8, 8);

	BuildTransformEditor();
	m_mainLayout->addWidget(m_transformGroup);
	m_mainLayout->addStretch();
}

// =========================================================================
// Refresh(const UIContext&)
// =========================================================================

void PropertyPanel::Refresh(const UIContext& /*ctx*/)
{
	// No-op — updated externally via LoadTransform/ClearTransform.
}

// =========================================================================
// Public API
// =========================================================================

void PropertyPanel::LoadTransform(int objectId,
                                  float posX, float posY, float posZ,
                                  float rotX, float rotY, float rotZ,
                                  float sclX, float sclY, float sclZ)
{
	m_currentObjectId = objectId;

	QSignalBlocker b1(m_posX); QSignalBlocker b2(m_posY); QSignalBlocker b3(m_posZ);
	QSignalBlocker b4(m_rotX); QSignalBlocker b5(m_rotY); QSignalBlocker b6(m_rotZ);
	QSignalBlocker b7(m_sclX); QSignalBlocker b8(m_sclY); QSignalBlocker b9(m_sclZ);

	m_posX->setValue(static_cast<double>(posX));
	m_posY->setValue(static_cast<double>(posY));
	m_posZ->setValue(static_cast<double>(posZ));
	m_rotX->setValue(static_cast<double>(rotX));
	m_rotY->setValue(static_cast<double>(rotY));
	m_rotZ->setValue(static_cast<double>(rotZ));
	m_sclX->setValue(static_cast<double>(sclX));
	m_sclY->setValue(static_cast<double>(sclY));
	m_sclZ->setValue(static_cast<double>(sclZ));

	SetEnabled(true);
}

void PropertyPanel::ClearTransform()
{
	m_currentObjectId = -1;
	SetEnabled(false);
}

// =========================================================================
// Transform Editor — QGroupBox with QGridLayout + Reset button
// =========================================================================

void PropertyPanel::BuildTransformEditor()
{
	m_transformGroup = new QGroupBox("Transform");
	m_transformGroup->setCheckable(false);

	auto* grid = new QGridLayout(m_transformGroup);
	grid->setContentsMargins(10, 16, 10, 10);
	grid->setHorizontalSpacing(6);
	grid->setVerticalSpacing(6);

	auto makeAxisLabel = [](const QString& text, const QString& color) {
		auto* lbl = new QLabel(text);
		lbl->setAlignment(Qt::AlignCenter);
		lbl->setStyleSheet(QString("QLabel { color: %1; font-weight: bold; }").arg(color));
		return lbl;
	};

	auto makeSpinBox = [](double min, double max, double step, int decimals,
	                      const QString& suffix = QString()) {
		auto* spin = new QDoubleSpinBox();
		spin->setRange(min, max);
		spin->setSingleStep(step);
		spin->setDecimals(decimals);
		spin->setAlignment(Qt::AlignRight);
		spin->setMinimumWidth(80);
		if (!suffix.isEmpty())
			spin->setSuffix(suffix);
		return spin;
	};

	// --- Row 0: Axis headers ---
	grid->addWidget(new QLabel(""), 0, 0);
	grid->addWidget(makeAxisLabel("X", "#e74c3c"), 0, 1);
	grid->addWidget(makeAxisLabel("Y", "#2ecc71"), 0, 2);
	grid->addWidget(makeAxisLabel("Z", "#3498db"), 0, 3);

	// --- Row 1: Position ---
	{
		grid->addWidget(new QLabel("Position"), 1, 0);

		m_posX = makeSpinBox(-100000.0, 100000.0, 0.01, 2);
		m_posY = makeSpinBox(-100000.0, 100000.0, 0.01, 2);
		m_posZ = makeSpinBox(-100000.0, 100000.0, 0.01, 2);

		grid->addWidget(m_posX, 1, 1);
		grid->addWidget(m_posY, 1, 2);
		grid->addWidget(m_posZ, 1, 3);
	}

	// --- Row 2: Rotation ---
	{
		grid->addWidget(new QLabel("Rotation"), 2, 0);

		m_rotX = makeSpinBox(-360.0, 360.0, 1.0, 1, "°");
		m_rotY = makeSpinBox(-360.0, 360.0, 1.0, 1, "°");
		m_rotZ = makeSpinBox(-360.0, 360.0, 1.0, 1, "°");

		grid->addWidget(m_rotX, 2, 1);
		grid->addWidget(m_rotY, 2, 2);
		grid->addWidget(m_rotZ, 2, 3);
	}

	// --- Row 3: Scale ---
	{
		grid->addWidget(new QLabel("Scale"), 3, 0);

		m_sclX = makeSpinBox(0.001, 1000.0, 0.1, 3);
		m_sclY = makeSpinBox(0.001, 1000.0, 0.1, 3);
		m_sclZ = makeSpinBox(0.001, 1000.0, 0.1, 3);

		m_sclX->setValue(1.0);
		m_sclY->setValue(1.0);
		m_sclZ->setValue(1.0);

		grid->addWidget(m_sclX, 3, 1);
		grid->addWidget(m_sclY, 3, 2);
		grid->addWidget(m_sclZ, 3, 3);
	}

	// --- Row 4: Reset button ---
	m_resetBtn = new QPushButton("Reset Transform");
	m_resetBtn->setToolTip("Reset position, rotation, and scale to identity values.");
	grid->addWidget(m_resetBtn, 4, 0, 1, 4, Qt::AlignCenter);

	// --- Signal wiring ---
	auto emitTransform = [this]() {
		if (m_currentObjectId >= 0)
		{
			emit transformChanged(CollectTransform());
		}
	};

	QObject::connect(m_posX, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
	                 this, emitTransform);
	QObject::connect(m_posY, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
	                 this, emitTransform);
	QObject::connect(m_posZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
	                 this, emitTransform);
	QObject::connect(m_rotX, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
	                 this, emitTransform);
	QObject::connect(m_rotY, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
	                 this, emitTransform);
	QObject::connect(m_rotZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
	                 this, emitTransform);
	QObject::connect(m_sclX, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
	                 this, emitTransform);
	QObject::connect(m_sclY, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
	                 this, emitTransform);
	QObject::connect(m_sclZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
	                 this, emitTransform);

	QObject::connect(m_resetBtn, &QPushButton::clicked, this, [this, emitTransform]() {
		{
			QSignalBlocker b1(m_posX); QSignalBlocker b2(m_posY); QSignalBlocker b3(m_posZ);
			QSignalBlocker b4(m_rotX); QSignalBlocker b5(m_rotY); QSignalBlocker b6(m_rotZ);
			QSignalBlocker b7(m_sclX); QSignalBlocker b8(m_sclY); QSignalBlocker b9(m_sclZ);

			m_posX->setValue(0.0); m_posY->setValue(0.0); m_posZ->setValue(0.0);
			m_rotX->setValue(0.0); m_rotY->setValue(0.0); m_rotZ->setValue(0.0);
			m_sclX->setValue(1.0); m_sclY->setValue(1.0); m_sclZ->setValue(1.0);
		}
		emitTransform();
	});
}

void PropertyPanel::SetEnabled(bool enabled)
{
	m_transformGroup->setEnabled(enabled);
}

TransformChanged PropertyPanel::CollectTransform() const
{
	TransformChanged ev;
	ev.objectId = m_currentObjectId;
	ev.posX = static_cast<float>(m_posX->value());
	ev.posY = static_cast<float>(m_posY->value());
	ev.posZ = static_cast<float>(m_posZ->value());
	ev.rotX = static_cast<float>(m_rotX->value());
	ev.rotY = static_cast<float>(m_rotY->value());
	ev.rotZ = static_cast<float>(m_rotZ->value());
	ev.sclX = static_cast<float>(m_sclX->value());
	ev.sclY = static_cast<float>(m_sclY->value());
	ev.sclZ = static_cast<float>(m_sclZ->value());
	return ev;
}

} // namespace neurus
