#include "ShaderStructSection.h"
#include "ShaderFieldRow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace neurus
{

ShaderStructSection::ShaderStructSection(QWidget* parent)
	: QWidget(parent)
{
	auto* outerLayout = new QVBoxLayout(this);
	outerLayout->setContentsMargins(0, 0, 0, 0);
	outerLayout->setSpacing(1);

	// Title label
	m_titleLabel = new QLabel(this);
	QFont boldFont = m_titleLabel->font();
	boldFont.setBold(true);
	m_titleLabel->setFont(boldFont);
	outerLayout->addWidget(m_titleLabel);

	// Content layout (rows inserted here)
	m_contentLayout = new QVBoxLayout();
	m_contentLayout->setContentsMargins(4, 4, 4, 2);
	m_contentLayout->setSpacing(1);
	outerLayout->addLayout(m_contentLayout);

	// "+" button row
	auto* btnLayout = new QHBoxLayout();
	btnLayout->addStretch();
	m_addBtn = new QPushButton("+", this);
	m_addBtn->setFixedSize(24, 24);
	m_addBtn->setToolTip("Add new entry");
	btnLayout->addWidget(m_addBtn);
	outerLayout->addLayout(btnLayout);

	QObject::connect(m_addBtn, &QPushButton::clicked,
	                 this, &ShaderStructSection::addButtonClicked);
}

void ShaderStructSection::setTitle(const QString& title)
{
	m_titleLabel->setText(title);
}

void ShaderStructSection::setFields(const std::vector<FieldData>& fields)
{
	// Ensure enough rows exist in pool (create if needed)
	while (static_cast<int>(m_rowPool.size()) < static_cast<int>(fields.size()))
	{
		auto* row = new ShaderFieldRow(this);
		m_contentLayout->addWidget(row);
		m_rowPool.push_back(row);
	}

	// Update visible rows with new data
	for (size_t i = 0; i < fields.size(); ++i)
	{
		auto* row = m_rowPool[i];
		row->setVisible(true);

		row->setField(fields[i].type, fields[i].name);

		// Disconnect old fieldChanged connection, reconnect to emit fieldEdited
		QObject::disconnect(row, &ShaderFieldRow::fieldChanged, nullptr, nullptr);
		int rowIndex = static_cast<int>(i);
		// Track previous values to only emit what changed
		QObject::connect(row, &ShaderFieldRow::fieldChanged,
		                 [this, rowIndex, prevType = fields[i].type, prevName = fields[i].name]
		                 (const QString& type, const QString& name) mutable {
			if (type != QString::fromStdString(prevType))
			{
				prevType = type.toStdString();
				emit fieldEdited(rowIndex, "type", type);
			}
			if (name != QString::fromStdString(prevName))
			{
				prevName = name.toStdString();
				emit fieldEdited(rowIndex, "name", name);
			}
		});
	}

	// Hide remaining rows (never destroy)
	for (size_t i = fields.size(); i < m_rowPool.size(); ++i)
	{
		m_rowPool[i]->setVisible(false);
		m_rowPool[i]->resetCaches();
	}
}

void ShaderStructSection::setAddButtonVisible(bool visible)
{
	m_addBtn->setVisible(visible);
}

} // namespace neurus
