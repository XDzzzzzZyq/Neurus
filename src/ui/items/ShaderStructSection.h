#pragma once

#include <QWidget>
#include <string>
#include <vector>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace neurus
{

class ShaderFieldRow;

class ShaderStructSection : public QWidget
{
	Q_OBJECT

public:
	struct FieldData
	{
		std::string type;
		std::string name;
		int location = -1;
	};

	explicit ShaderStructSection(QWidget* parent = nullptr);
	~ShaderStructSection() override = default;

	ShaderStructSection(const ShaderStructSection&) = delete;
	ShaderStructSection& operator=(const ShaderStructSection&) = delete;

	void setTitle(const QString& title);
	void setFields(const std::vector<FieldData>& fields);  // Takes full list, manages pool internally
	void setAddButtonVisible(bool visible);

signals:
	void addButtonClicked();
	/** @brief Emitted when a field row's type or name is edited by the user. */
	void fieldEdited(int row);

private:
	QLabel*       m_titleLabel    = nullptr;
	QVBoxLayout*  m_contentLayout = nullptr;
	QPushButton*  m_addBtn        = nullptr;
	std::vector<ShaderFieldRow*> m_rowPool;  // Pool persists for lifetime
};

} // namespace neurus
