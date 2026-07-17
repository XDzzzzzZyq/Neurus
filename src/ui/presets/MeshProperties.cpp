#include "presets/MeshProperties.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QFont>

namespace neurus
{

MeshProperties::MeshProperties(QWidget* parent)
    : QWidget(parent)
{
	auto* outerLayout = new QVBoxLayout(this);
	outerLayout->setContentsMargins(0, 0, 0, 0);
	outerLayout->setSpacing(0);

	// --- Top-level group ---
	auto* groupBox = new QGroupBox(QStringLiteral("Mesh"));
	outerLayout->addWidget(groupBox);

	auto* innerLayout = new QVBoxLayout(groupBox);
	innerLayout->setSpacing(8);

	// --- Section: Asset ---
	auto* assetLabel = new QLabel(QStringLiteral("Asset"));
	QFont boldFont = assetLabel->font();
	boldFont.setBold(true);
	assetLabel->setFont(boldFont);
	innerLayout->addWidget(assetLabel);

	// --- Row: Path label + mesh path ---
	auto* pathRow = new QHBoxLayout();
	pathRow->setSpacing(4);

	auto* pathPrefix = new QLabel(QStringLiteral("Path"));
	pathPrefix->setStyleSheet(QStringLiteral("QLabel { color: #aaa; }"));
	pathRow->addWidget(pathPrefix);

	m_pathLabel = new QLabel();
	m_pathLabel->setWordWrap(true);
	m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	QFont pathFont = m_pathLabel->font();
	pathFont.setPointSize(pathFont.pointSize() - 1);
	m_pathLabel->setFont(pathFont);
	m_pathLabel->setStyleSheet(QStringLiteral("QLabel { color: #aaa; }"));
	pathRow->addWidget(m_pathLabel, 1);

	innerLayout->addLayout(pathRow);

	// --- Section: Flags ---
	auto* flagsLabel = new QLabel(QStringLiteral("Flags"));
	flagsLabel->setFont(boldFont);
	innerLayout->addWidget(flagsLabel);

	// --- Row: Cast Shadow checkbox ---
	m_shadowChk = new QCheckBox(QStringLiteral("Cast Shadow"));
	innerLayout->addWidget(m_shadowChk);

	// --- Row: Use Material checkbox ---
	m_materialChk = new QCheckBox(QStringLiteral("Use Material"));
	innerLayout->addWidget(m_materialChk);

	outerLayout->addStretch();

	// --- Signal wiring ---
	QObject::connect(m_shadowChk, &QCheckBox::toggled, this,
		[this](bool checked) {
			if (m_objectId < 0)
				return;
			emit shadowChanged(m_objectId, checked);
		});

	QObject::connect(m_materialChk, &QCheckBox::toggled, this,
		[this](bool checked) {
			if (m_objectId < 0)
				return;
			emit materialChanged(m_objectId, checked);
		});
}

void MeshProperties::setObjectId(int id)
{
	if (m_objectId != id)
	{
		m_objectId = id;
		m_cachedPath.clear();
		m_cachedShadow   = -1;
		m_cachedMaterial = -1;
	}
}

void MeshProperties::setMeshPath(const std::string& path)
{
	if (m_cachedPath == path)
		return;
	m_cachedPath = path;
	m_pathLabel->setText(QString::fromStdString(path));
}

void MeshProperties::setShadowEnabled(bool enabled)
{
	int val = enabled ? 1 : 0;
	if (m_cachedShadow == val)
		return;
	m_cachedShadow = val;
	m_shadowChk->blockSignals(true);
	m_shadowChk->setChecked(enabled);
	m_shadowChk->blockSignals(false);
}

void MeshProperties::setMaterialEnabled(bool enabled)
{
	int val = enabled ? 1 : 0;
	if (m_cachedMaterial == val)
		return;
	m_cachedMaterial = val;
	m_materialChk->blockSignals(true);
	m_materialChk->setChecked(enabled);
	m_materialChk->blockSignals(false);
}

} // namespace neurus
