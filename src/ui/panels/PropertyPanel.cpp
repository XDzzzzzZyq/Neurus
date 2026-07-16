#include "panels/PropertyPanel.h"

#include "Icons.h"
#include "UIContext.h"
#include "items/Vec3Spin.h"

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
	if (m_currentObjectId != objectId){
		m_iconLabel->setPixmap(Icons::ObjectIcon(static_cast<int>(activeObj->o_type)).pixmap(20, 20));
		m_nameLabel->setText(QString::fromStdString(activeObj->o_name));
		m_currentObjectId = objectId;
	}

	// --- Transform ---
	auto* obj = const_cast<ObjectID*>(activeObj);
	void* transformPtr = obj->GetTransform();
	if (transformPtr)
	{
		auto* xform = static_cast<Transform3D*>(transformPtr);
		const glm::vec3& pos = xform->GetPosition();
		const glm::vec3& rot = xform->GetRotation();
		const glm::vec3& scl = xform->GetScale();


		// Vec3Spin::setValue handles dirty-check internally
		m_posSpin->setValue(pos.x, pos.y, pos.z);
		m_rotSpin->setValue(rot.x, rot.y, rot.z);
		m_sclSpin->setValue(scl.x, scl.y, scl.z);

		SetEnabled(true);
	}
	else
	{
		SetEnabled(false);
	}
}

// =========================================================================
// BuildTransformEditor — QGroupBox with Vec3Spin rows + Reset button
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

	// Row 0: axis column headers (X=red, Y=green, Z=blue)
	grid->addWidget(new QLabel(""), 0, 0);
	grid->addWidget(makeAxisLabel("X", "#e74c3c"), 0, 1);
	grid->addWidget(makeAxisLabel("Y", "#2ecc71"), 0, 2);
	grid->addWidget(makeAxisLabel("Z", "#3498db"), 0, 3);

	// Row 1: Position — Vec3Spin spans columns 1–3
	grid->addWidget(new QLabel("Position"), 1, 0);
	m_posSpin = new Vec3Spin(-100000.0, 100000.0, 0.01, 2, QString());
	grid->addWidget(m_posSpin, 1, 1, 1, 3);

	// Row 2: Rotation — Vec3Spin spans columns 1–3
	grid->addWidget(new QLabel("Rotation"), 2, 0);
	m_rotSpin = new Vec3Spin(-360.0, 360.0, 1.0, 1, "\u00B0");
	grid->addWidget(m_rotSpin, 2, 1, 1, 3);

	// Row 3: Scale — Vec3Spin spans columns 1–3
	grid->addWidget(new QLabel("Scale"), 3, 0);
	m_sclSpin = new Vec3Spin(0.001, 1000.0, 0.1, 3, QString());
	m_sclSpin->setValue(1.0, 1.0, 1.0);  // initial identity
	grid->addWidget(m_sclSpin, 3, 1, 1, 3);

	// Row 4: Reset button — spans all 4 columns
	auto* resetBtn = new QPushButton("Reset Transform");
	resetBtn->setToolTip("Reset position, rotation, and scale to identity values.");
	grid->addWidget(resetBtn, 4, 0, 1, 4, Qt::AlignCenter);

	// --- Signal wiring ---
	// Each Vec3Spin emits valueChanged(x, y, z); one signal per transform component.
	QObject::connect(m_posSpin, &Vec3Spin::valueChanged, this,
		[this](float x, float y, float z) {
			if (m_currentObjectId < 0) return;
			emit positionChanged({m_currentObjectId, x, y, z});
		});

	QObject::connect(m_rotSpin, &Vec3Spin::valueChanged, this,
		[this](float x, float y, float z) {
			if (m_currentObjectId < 0) return;
			emit rotationChanged({m_currentObjectId, x, y, z});
		});

	QObject::connect(m_sclSpin, &Vec3Spin::valueChanged, this,
		[this](float x, float y, float z) {
			if (m_currentObjectId < 0) return;
			emit scaleChanged({m_currentObjectId, x, y, z});
		});

	// --- Reset button ---
	QObject::connect(resetBtn, &QPushButton::clicked, this, [this]() {
		m_posSpin->setValue(0.0, 0.0, 0.0);
		m_rotSpin->setValue(0.0, 0.0, 0.0);
		m_sclSpin->setValue(1.0, 1.0, 1.0);

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
