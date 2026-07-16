#include "presets/EnvironmentProperties.h"
#include "items/ScalarSlider.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QFont>

namespace neurus {

EnvironmentProperties::EnvironmentProperties(QWidget* parent)
    : QWidget(parent)
{
	auto* outerLayout = new QVBoxLayout(this);
	outerLayout->setContentsMargins(0, 0, 0, 0);

	// --- Environment Group Box ---
	auto* groupBox = new QGroupBox(QStringLiteral("Environment"), this);
	outerLayout->addWidget(groupBox);

	auto* groupLayout = new QVBoxLayout(groupBox);
	groupLayout->setSpacing(6);

	// --- Section: Map ---
	auto* mapHeader = new QLabel(QStringLiteral("Map"));
	QFont headerFont = mapHeader->font();
	headerFont.setBold(true);
	mapHeader->setFont(headerFont);
	groupLayout->addWidget(mapHeader);

	auto* pathRow = new QHBoxLayout();
	auto* pathLabel = new QLabel(QStringLiteral("Path"));
	pathLabel->setStyleSheet(QStringLiteral("QLabel { color: #aaa; }"));
	pathRow->addWidget(pathLabel);

	m_pathLabel = new QLabel();
	m_pathLabel->setWordWrap(true);
	m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	QFont pathFont = m_pathLabel->font();
	pathFont.setPointSize(pathFont.pointSize() - 1);
	m_pathLabel->setFont(pathFont);
	m_pathLabel->setStyleSheet(QStringLiteral("QLabel { color: #aaa; }"));
	pathRow->addWidget(m_pathLabel, 1);
	groupLayout->addLayout(pathRow);

	// --- Section: IBL ---
	auto* iblHeader = new QLabel(QStringLiteral("IBL"));
	iblHeader->setFont(headerFont);
	groupLayout->addWidget(iblHeader);

	auto* intensityRow = new QHBoxLayout();
	intensityRow->addWidget(new QLabel(QStringLiteral("Intensity")));
	m_intensitySlider = new ScalarSlider(0.0, 10.0, 1000, 1.0, this);
	intensityRow->addWidget(m_intensitySlider, 1);
	groupLayout->addLayout(intensityRow);

	auto* rotationRow = new QHBoxLayout();
	rotationRow->addWidget(new QLabel(QStringLiteral("Rotation (\u00B0)")));
	m_rotationSlider = new ScalarSlider(0.0, 360.0, 360, 0.0, this);
	rotationRow->addWidget(m_rotationSlider, 1);
	groupLayout->addLayout(rotationRow);

	outerLayout->addStretch();

	// --- Signal wiring ---
	QObject::connect(m_intensitySlider, &ScalarSlider::valueChanged, this,
		[this]() {
			if (m_objectId < 0)
			{
				return;
			}
			emit intensityChanged(m_objectId, static_cast<float>(m_intensitySlider->value()));
		});

	QObject::connect(m_rotationSlider, &ScalarSlider::valueChanged, this,
		[this]() {
			if (m_objectId < 0)
			{
				return;
			}
			emit rotationChanged(m_objectId, static_cast<float>(m_rotationSlider->value()));
		});
}

void EnvironmentProperties::setObjectId(int id)
{
	if (m_objectId != id)
	{
		m_objectId = id;
		// Reset caches to sentinel values so the next setIntensity() /
		// setRotation() / setEquirectPath() always applies — forces a
		// full refresh for the new object.
		m_cachedIntensity = -1.0f;
		m_cachedRotation  = -999.0f;
		m_cachedPath.clear();
	}
}

void EnvironmentProperties::setIntensity(float intensity)
{
	if (m_cachedIntensity == intensity)
	{
		return;
	}
	m_cachedIntensity = intensity;
	m_intensitySlider->setValue(static_cast<double>(intensity));
}

void EnvironmentProperties::setRotation(float rotation)
{
	if (m_cachedRotation == rotation)
	{
		return;
	}
	m_cachedRotation = rotation;
	m_rotationSlider->setValue(static_cast<double>(rotation));
}

void EnvironmentProperties::setEquirectPath(const std::string& path)
{
	if (m_cachedPath == path)
	{
		return;
	}
	m_cachedPath = path;
	m_pathLabel->setText(QString::fromStdString(path));
}

} // namespace neurus
