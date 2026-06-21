#include "ui/PropertyEditor.h"

#include "scene/Camera.h"
#include "scene/Light.h"
#include "scene/Scene.h"
#include "scene/Transform.h"
#include "scene/UID.h"

#include "editor/events/EditorEvents.h"
#include "editor/events/EventBus.h"

#include <QFont>
#include <QVBoxLayout>

namespace neurus {

// =========================================================================
// Constructor / Destructor
// =========================================================================

PropertyEditor::PropertyEditor(Scene* scene, QWidget* parent)
	: QWidget(parent)
	, m_scene(scene)
{
	// --- Main layout ---
	m_mainLayout = new QVBoxLayout(this);
	m_mainLayout->setContentsMargins(0, 0, 0, 0);

	// --- Scroll area ---
	m_scrollArea = new QScrollArea(this);
	m_scrollArea->setWidgetResizable(true);
	m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_mainLayout->addWidget(m_scrollArea);

	// --- Initial form container ---
	ClearFormLayout();

	// --- Subscribe to selection events ---
	GetEventQueue().subscribe<ObjectSelected>(
		[this](const ObjectSelected& e) {
			LoadObject(e.objectId);
		}
	);

	GetEventQueue().subscribe<ObjectDeselected>(
		[this](const ObjectDeselected& /*e*/) {
			Clear();
		}
	);
}

// =========================================================================
// Public API
// =========================================================================

void PropertyEditor::SetScene(Scene* scene)
{
	m_scene = scene;
}

void PropertyEditor::LoadObject(int objectId)
{
	if (!m_scene)
	{
		return;
	}

	ObjectID* obj = m_scene->GetObjectID(objectId);
	if (!obj)
	{
		Clear();
		return;
	}

	ClearFormLayout();

	// --- Populate header ---
	PopulateHeader(obj);

	// --- Populate transform (all scene objects have Transform3D) ---
	void* transformPtr = obj->GetTransform();
	if (transformPtr)
	{
		auto* transform = static_cast<Transform3D*>(transformPtr);
		PopulateTransform(transform);
	}

	// --- Type-specific properties ---
	switch (obj->o_type)
	{
	case ObjectID::GOType::GO_CAM:
	{
		auto* cam = static_cast<Camera*>(obj);
		PopulateCamera(cam);
		break;
	}
	case ObjectID::GOType::GO_LIGHT:
	{
		auto* light = static_cast<Light*>(obj);
		PopulateLight(light);
		break;
	}
	default:
		// GO_MESH and others: only transform for MVP
		break;
	}

	// Add stretch to push content to top
	m_formLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
}

void PropertyEditor::Clear()
{
	ClearFormLayout();
	// Add stretch so the scroll area doesn't collapse
	m_formLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
}

// =========================================================================
// Layout helpers
// =========================================================================

void PropertyEditor::ClearFormLayout()
{
	// Delete old form container if it exists
	if (m_formContainer)
	{
		m_formContainer->deleteLater();
	}

	// Create new form container
	m_formContainer = new QWidget();
	m_formLayout = new QFormLayout(m_formContainer);
	m_formLayout->setContentsMargins(8, 8, 8, 8);
	m_formLayout->setSpacing(4);

	m_scrollArea->setWidget(m_formContainer);
}

void PropertyEditor::AddSectionHeader(const QString& title)
{
	auto* label = new QLabel(title);
	QFont font = label->font();
	font.setBold(true);
	font.setPointSize(font.pointSize() + 1);
	label->setFont(font);
	m_formLayout->addRow(label);
}

QDoubleSpinBox* PropertyEditor::CreateReadOnlySpinBox(double value)
{
	auto* spin = new QDoubleSpinBox();
	spin->setRange(-100000.0, 100000.0);
	spin->setDecimals(2);
	spin->setValue(value);
	spin->setReadOnly(true);
	spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
	spin->setMinimumWidth(70);
	return spin;
}

// =========================================================================
// Population helpers
// =========================================================================

void PropertyEditor::PopulateHeader(const ObjectID* obj)
{
	QString typeStr;
	switch (obj->o_type)
	{
	case ObjectID::GOType::GO_CAM:   typeStr = "Camera";   break;
	case ObjectID::GOType::GO_MESH:  typeStr = "Mesh";     break;
	case ObjectID::GOType::GO_LIGHT: typeStr = "Light";    break;
	default:                         typeStr = "Object";   break;
	}

	auto* headerLabel = new QLabel(QString("%1 [%2]").arg(QString::fromStdString(obj->o_name), typeStr));
	QFont font = headerLabel->font();
	font.setBold(true);
	font.setPointSize(font.pointSize() + 2);
	headerLabel->setFont(font);
	m_formLayout->addRow(headerLabel);
}

void PropertyEditor::PopulateTransform(const Transform3D* transform)
{
	if (!transform) return;

	AddSectionHeader("Transform");

	const glm::vec3& pos = transform->GetPosition();
	const glm::vec3& rot = transform->GetRotation();
	const glm::vec3& scl = transform->GetScale();

	// --- Position ---
	{
		auto* label = new QLabel("Position");
		QFont font = label->font();
		font.setItalic(true);
		label->setFont(font);

		auto* x = CreateReadOnlySpinBox(pos.x);
		auto* y = CreateReadOnlySpinBox(pos.y);
		auto* z = CreateReadOnlySpinBox(pos.z);

		auto* row = new QHBoxLayout();
		row->setSpacing(4);
		row->addWidget(new QLabel("X"));
		row->addWidget(x);
		row->addWidget(new QLabel("Y"));
		row->addWidget(y);
		row->addWidget(new QLabel("Z"));
		row->addWidget(z);
		row->addStretch();

		m_formLayout->addRow(label, row);
	}

	// --- Rotation ---
	{
		auto* label = new QLabel("Rotation");
		QFont font = label->font();
		font.setItalic(true);
		label->setFont(font);

		auto* x = CreateReadOnlySpinBox(rot.x);
		auto* y = CreateReadOnlySpinBox(rot.y);
		auto* z = CreateReadOnlySpinBox(rot.z);

		auto* row = new QHBoxLayout();
		row->setSpacing(4);
		row->addWidget(new QLabel("X"));
		row->addWidget(x);
		row->addWidget(new QLabel("Y"));
		row->addWidget(y);
		row->addWidget(new QLabel("Z"));
		row->addWidget(z);
		row->addStretch();

		m_formLayout->addRow(label, row);
	}

	// --- Scale ---
	{
		auto* label = new QLabel("Scale");
		QFont font = label->font();
		font.setItalic(true);
		label->setFont(font);

		auto* x = CreateReadOnlySpinBox(scl.x);
		auto* y = CreateReadOnlySpinBox(scl.y);
		auto* z = CreateReadOnlySpinBox(scl.z);

		auto* row = new QHBoxLayout();
		row->setSpacing(4);
		row->addWidget(new QLabel("X"));
		row->addWidget(x);
		row->addWidget(new QLabel("Y"));
		row->addWidget(y);
		row->addWidget(new QLabel("Z"));
		row->addWidget(z);
		row->addStretch();

		m_formLayout->addRow(label, row);
	}
}

void PropertyEditor::PopulateCamera(const Camera* cam)
{
	if (!cam) return;

	AddSectionHeader("Camera");

	// --- FOV ---
	{
		auto* spin = CreateReadOnlySpinBox(cam->cam_pers);
		spin->setSuffix("°");
		m_formLayout->addRow("FOV", spin);
	}

	// --- Near Plane ---
	{
		auto* spin = CreateReadOnlySpinBox(cam->cam_near);
		spin->setDecimals(3);
		m_formLayout->addRow("Near Plane", spin);
	}

	// --- Far Plane ---
	{
		auto* spin = CreateReadOnlySpinBox(cam->cam_far);
		m_formLayout->addRow("Far Plane", spin);
	}
}

void PropertyEditor::PopulateLight(const Light* light)
{
	if (!light) return;

	AddSectionHeader("Light");

	// --- Type ---
	{
		QString typeStr;
		switch (light->light_type)
		{
		case POINTLIGHT: typeStr = "Point";    break;
		case SUNLIGHT:   typeStr = "Sun";      break;
		case SPOTLIGHT:  typeStr = "Spot";     break;
		case AREALIGHT:  typeStr = "Area";     break;
		default:         typeStr = "None";     break;
		}
		auto* label = new QLabel(typeStr);
		m_formLayout->addRow("Type", label);
	}

	// --- Color ---
	{
		auto* r = CreateReadOnlySpinBox(light->light_color.r);
		auto* g = CreateReadOnlySpinBox(light->light_color.g);
		auto* b = CreateReadOnlySpinBox(light->light_color.b);

		auto* row = new QHBoxLayout();
		row->setSpacing(4);
		row->addWidget(new QLabel("R"));
		row->addWidget(r);
		row->addWidget(new QLabel("G"));
		row->addWidget(g);
		row->addWidget(new QLabel("B"));
		row->addWidget(b);
		row->addStretch();

		m_formLayout->addRow("Color", row);
	}

	// --- Power ---
	{
		auto* spin = CreateReadOnlySpinBox(light->light_power);
		m_formLayout->addRow("Power", spin);
	}

	// --- Radius (point lights) ---
	if (light->light_type == POINTLIGHT)
	{
		auto* spin = CreateReadOnlySpinBox(light->light_radius);
		spin->setDecimals(4);
		m_formLayout->addRow("Radius", spin);
	}
}

} // namespace neurus
