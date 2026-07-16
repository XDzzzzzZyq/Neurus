#include "panels/PropertyPanel.h"

#include "Icons.h"
#include "UIContext.h"

#include "scene/Scene.h"
#include "scene/Transform.h"
#include "scene/UID.h"

#include <QGridLayout>

namespace neurus {

// =========================================================================
// Constructor
// =========================================================================

PropertyPanel::PropertyPanel(QWidget* parent)
	: UIPanel(PanelType::PropertyPanel, QString(), parent)
{
	auto* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(8, 8, 8, 8);

	BuildHeader();
	mainLayout->addWidget(m_headerWidget);

	BuildTransformEditor();
	mainLayout->addWidget(m_transformGroup);

	m_emptyLabel = new QLabel("No selected object");
	m_emptyLabel->setAlignment(Qt::AlignCenter);
	QFont emptyFont = m_emptyLabel->font();
	emptyFont.setPointSize(emptyFont.pointSize() + 1);
	m_emptyLabel->setFont(emptyFont);
	m_emptyLabel->setStyleSheet("QLabel { color: #888; }");
	mainLayout->addWidget(m_emptyLabel);

	mainLayout->addStretch();

	m_emptyLabel->setVisible(true);
	m_headerWidget->setVisible(false);
	m_transformGroup->setEnabled(false);
}

// =========================================================================
// BuildHeader — icon + name row
// =========================================================================

void PropertyPanel::BuildHeader()
{
	m_headerWidget = new QWidget();
	auto* row = new QHBoxLayout(m_headerWidget);
	row->setContentsMargins(0, 0, 0, 4);
	row->setSpacing(6);

	m_iconLabel = new QLabel();
	m_iconLabel->setFixedSize(20, 20);
	row->addWidget(m_iconLabel);

	m_nameLabel = new QLabel();
	QFont f = m_nameLabel->font();
	f.setBold(true);
	m_nameLabel->setFont(f);
	m_nameLabel->setWordWrap(true);
	row->addWidget(m_nameLabel, 1);
}

// =========================================================================
// Refresh — read active object from scene selections, update UI
// =========================================================================

void PropertyPanel::Refresh(const UIContext& ctx)
{
	const Scene* scene = static_cast<const Scene*>(ctx.scene);
	if (!scene)
	{
		SetEnabled(false);
		m_currentObjectId = -1;
		return;
	}

	const ObjectID* activeObj = scene->selections.GetActiveObject();
	if (!activeObj)
	{
		SetEnabled(false);
		m_currentObjectId = -1;
		return;
	}

	int objectId = activeObj->GetObjectID();

	// --- Header: icon + name ---
	m_iconLabel->setPixmap(Icons::ObjectIcon(static_cast<int>(activeObj->o_type)).pixmap(20, 20));
	m_nameLabel->setText(QString::fromStdString(activeObj->o_name));

	// --- Transform ---
	auto* obj = const_cast<ObjectID*>(activeObj);
	void* transformPtr = obj->GetTransform();
	if (transformPtr)
	{
		auto* xform = static_cast<Transform3D*>(transformPtr);
		const glm::vec3& pos = xform->GetPosition();
		const glm::vec3& rot = xform->GetRotation();
		const glm::vec3& scl = xform->GetScale();

		m_currentObjectId = objectId;
		PopulateTransform(
			static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z),
			static_cast<float>(rot.x), static_cast<float>(rot.y), static_cast<float>(rot.z),
			static_cast<float>(scl.x), static_cast<float>(scl.y), static_cast<float>(scl.z));
		SetEnabled(true);
	}
	else
	{
		m_currentObjectId = objectId;
		SetEnabled(false);
	}
}

// =========================================================================
// BuildTransformEditor — QGroupBox with QGridLayout + Reset button
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

	// Row 0: Axis headers
	grid->addWidget(new QLabel(""), 0, 0);
	grid->addWidget(makeAxisLabel("X", "#e74c3c"), 0, 1);
	grid->addWidget(makeAxisLabel("Y", "#2ecc71"), 0, 2);
	grid->addWidget(makeAxisLabel("Z", "#3498db"), 0, 3);

	// Row 1: Position
	grid->addWidget(new QLabel("Position"), 1, 0);
	m_posX = makeSpinBox(-100000.0, 100000.0, 0.01, 2);
	m_posY = makeSpinBox(-100000.0, 100000.0, 0.01, 2);
	m_posZ = makeSpinBox(-100000.0, 100000.0, 0.01, 2);
	grid->addWidget(m_posX, 1, 1);
	grid->addWidget(m_posY, 1, 2);
	grid->addWidget(m_posZ, 1, 3);

	// Row 2: Rotation
	grid->addWidget(new QLabel("Rotation"), 2, 0);
	m_rotX = makeSpinBox(-360.0, 360.0, 1.0, 1, "\u00B0");
	m_rotY = makeSpinBox(-360.0, 360.0, 1.0, 1, "\u00B0");
	m_rotZ = makeSpinBox(-360.0, 360.0, 1.0, 1, "\u00B0");
	grid->addWidget(m_rotX, 2, 1);
	grid->addWidget(m_rotY, 2, 2);
	grid->addWidget(m_rotZ, 2, 3);

	// Row 3: Scale
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

	// Row 4: Reset button
	auto* resetBtn = new QPushButton("Reset Transform");
	resetBtn->setToolTip("Reset position, rotation, and scale to identity values.");
	grid->addWidget(resetBtn, 4, 0, 1, 4, Qt::AlignCenter);

	// --- Signal wiring ---
	QObject::connect(m_posX, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
	                 this, [this]() {
		if (m_currentObjectId < 0) return;
		PositionChanged e;
		e.objectId = m_currentObjectId;
		e.posX = static_cast<float>(m_posX->value());
		e.posY = static_cast<float>(m_posY->value());
		e.posZ = static_cast<float>(m_posZ->value());
		emit positionChanged(e);
	});
	auto emitPosition = [this]() {
		if (m_currentObjectId < 0) return;
		PositionChanged e;
		e.objectId = m_currentObjectId;
		e.posX = static_cast<float>(m_posX->value());
		e.posY = static_cast<float>(m_posY->value());
		e.posZ = static_cast<float>(m_posZ->value());
		emit positionChanged(e);
	};
	QObject::connect(m_posY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, emitPosition);
	QObject::connect(m_posZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, emitPosition);

	auto emitRotation = [this]() {
		if (m_currentObjectId < 0) return;
		RotationChanged e;
		e.objectId = m_currentObjectId;
		e.rotX = static_cast<float>(m_rotX->value());
		e.rotY = static_cast<float>(m_rotY->value());
		e.rotZ = static_cast<float>(m_rotZ->value());
		emit rotationChanged(e);
	};
	QObject::connect(m_rotX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, emitRotation);
	QObject::connect(m_rotY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, emitRotation);
	QObject::connect(m_rotZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, emitRotation);

	auto emitScale = [this]() {
		if (m_currentObjectId < 0) return;
		ScaleChanged e;
		e.objectId = m_currentObjectId;
		e.sclX = static_cast<float>(m_sclX->value());
		e.sclY = static_cast<float>(m_sclY->value());
		e.sclZ = static_cast<float>(m_sclZ->value());
		emit scaleChanged(e);
	};
	QObject::connect(m_sclX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, emitScale);
	QObject::connect(m_sclY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, emitScale);
	QObject::connect(m_sclZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, emitScale);

	QObject::connect(resetBtn, &QPushButton::clicked, this, [this, emitPosition, emitRotation, emitScale]() {
		{
			QSignalBlocker b1(m_posX); QSignalBlocker b2(m_posY); QSignalBlocker b3(m_posZ);
			QSignalBlocker b4(m_rotX); QSignalBlocker b5(m_rotY); QSignalBlocker b6(m_rotZ);
			QSignalBlocker b7(m_sclX); QSignalBlocker b8(m_sclY); QSignalBlocker b9(m_sclZ);

			m_posX->setValue(0.0); m_posY->setValue(0.0); m_posZ->setValue(0.0);
			m_rotX->setValue(0.0); m_rotY->setValue(0.0); m_rotZ->setValue(0.0);
			m_sclX->setValue(1.0); m_sclY->setValue(1.0); m_sclZ->setValue(1.0);
		}
		if (m_currentObjectId >= 0)
		{
			PositionChanged pe = {m_currentObjectId, 0, 0, 0};
			RotationChanged re = {m_currentObjectId, 0, 0, 0};
			ScaleChanged    se = {m_currentObjectId, 1, 1, 1};
			emit positionChanged(pe);
			emit rotationChanged(re);
			emit scaleChanged(se);
		}
	});
}

// =========================================================================
// PopulateTransform — set spinboxes from raw floats (with QSignalBlocker)
// =========================================================================

void PropertyPanel::PopulateTransform(
	float px, float py, float pz,
	float rx, float ry, float rz,
	float sx, float sy, float sz)
{
	QSignalBlocker b1(m_posX); QSignalBlocker b2(m_posY); QSignalBlocker b3(m_posZ);
	QSignalBlocker b4(m_rotX); QSignalBlocker b5(m_rotY); QSignalBlocker b6(m_rotZ);
	QSignalBlocker b7(m_sclX); QSignalBlocker b8(m_sclY); QSignalBlocker b9(m_sclZ);

	m_posX->setValue(static_cast<double>(px));
	m_posY->setValue(static_cast<double>(py));
	m_posZ->setValue(static_cast<double>(pz));
	m_rotX->setValue(static_cast<double>(rx));
	m_rotY->setValue(static_cast<double>(ry));
	m_rotZ->setValue(static_cast<double>(rz));
	m_sclX->setValue(static_cast<double>(sx));
	m_sclY->setValue(static_cast<double>(sy));
	m_sclZ->setValue(static_cast<double>(sz));
}

// =========================================================================
// SetEnabled
// =========================================================================

void PropertyPanel::SetEnabled(bool enabled)
{
	m_headerWidget->setVisible(enabled);
	m_transformGroup->setVisible(enabled);
	m_transformGroup->setEnabled(enabled);
	m_emptyLabel->setVisible(!enabled);
}

} // namespace neurus
