#include "ShaderStructSection.h"
#include "ShaderFieldRow.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

namespace neurus
{

ShaderStructSection::ShaderStructSection(QWidget* parent)
	: QWidget(parent)
{
	auto* outerLayout = new QVBoxLayout(this);
	outerLayout->setContentsMargins(0, 0, 0, 0);

	m_groupBox = new QGroupBox(this);
	m_groupBox->setCheckable(true);
	m_groupBox->setChecked(true);

	auto* groupLayout = new QVBoxLayout(m_groupBox);
	groupLayout->setContentsMargins(4, 4, 4, 4);

	m_contentLayout = new QVBoxLayout();
	groupLayout->addLayout(m_contentLayout);

	// "+" button row
	auto* btnLayout = new QHBoxLayout();
	btnLayout->addStretch();
	m_addBtn = new QPushButton("+", this);
	m_addBtn->setFixedSize(24, 24);
	m_addBtn->setToolTip("Add new entry");
	btnLayout->addWidget(m_addBtn);
	groupLayout->addLayout(btnLayout);

	QObject::connect(m_addBtn, &QPushButton::clicked,
	                 this, &ShaderStructSection::addButtonClicked);

	outerLayout->addWidget(m_groupBox);
}

void ShaderStructSection::setTitle(const QString& title)
{
	m_groupBox->setTitle(title);
}

void ShaderStructSection::setAddButtonVisible(bool visible)
{
	m_addBtn->setVisible(visible);
}

ShaderFieldRow* ShaderStructSection::addRow()
{
	auto* row = new ShaderFieldRow(this);
	m_contentLayout->addWidget(row);
	m_rows.push_back(row);
	return row;
}

void ShaderStructSection::clearRows()
{
	for (auto* row : m_rows)
	{
		m_contentLayout->removeWidget(row);
		row->deleteLater();
	}
	m_rows.clear();
}

} // namespace neurus
