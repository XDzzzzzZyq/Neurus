#pragma once

#include <QStyledItemDelegate>

class QComboBox;
class QModelIndex;
class QStyleOptionViewItem;
class QWidget;

namespace neurus {

/**
 * @brief Delegate for editing shader struct fields in a QTreeView.
 *
 * Renders a QComboBox for type columns and a QLineEdit for name columns.
 * Section headers are not editable.
 */
class ShaderFieldDelegate : public QStyledItemDelegate
{
	Q_OBJECT

public:
	explicit ShaderFieldDelegate(QObject* parent = nullptr);
	~ShaderFieldDelegate() override = default;

	ShaderFieldDelegate(const ShaderFieldDelegate&) = delete;
	ShaderFieldDelegate& operator=(const ShaderFieldDelegate&) = delete;

	QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
	                      const QModelIndex& index) const override;
	void setEditorData(QWidget* editor, const QModelIndex& index) const override;
	void setModelData(QWidget* editor, QAbstractItemModel* model,
	                  const QModelIndex& index) const override;
	void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
	                          const QModelIndex& index) const override;

private:
	void populateTypeCombo(QComboBox* combo) const;
};

} // namespace neurus
