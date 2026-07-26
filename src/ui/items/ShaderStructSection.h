#pragma once

#include <QWidget>

#include <vector>

class QGroupBox;
class QPushButton;
class QVBoxLayout;

namespace neurus
{

class ShaderFieldRow;

/**
 * @brief Collapsible section widget for the Shader Editor's Struct mode.
 *
 * Each section contains a titled QGroupBox with ShaderFieldRow children
 * and an optional "+" button to add new entries.
 */
class ShaderStructSection : public QWidget
{
	Q_OBJECT

public:
	explicit ShaderStructSection(QWidget* parent = nullptr);
	~ShaderStructSection() override = default;

	ShaderStructSection(const ShaderStructSection&) = delete;
	ShaderStructSection& operator=(const ShaderStructSection&) = delete;

	/** @brief Sets the group box title. */
	void setTitle(const QString& title);

	/** @brief Show or hide the "Add" button. */
	void setAddButtonVisible(bool visible);

	/** @brief Creates a new ShaderFieldRow and adds it before the "+" button. */
	ShaderFieldRow* addRow();

	/** @brief Returns all ShaderFieldRows in this section. */
	std::vector<ShaderFieldRow*> rows() const { return m_rows; }

	/** @brief Removes and deletes all row widgets. */
	void clearRows();

	/** @brief Returns the internal QGroupBox for layout access. */
	QGroupBox* groupBox() const { return m_groupBox; }

signals:
	void addButtonClicked();

private:
	QGroupBox*                    m_groupBox      = nullptr;
	QVBoxLayout*                  m_contentLayout = nullptr;
	QPushButton*                  m_addBtn         = nullptr;
	std::vector<ShaderFieldRow*>  m_rows;
};

} // namespace neurus
