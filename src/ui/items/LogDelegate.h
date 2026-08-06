/**
 * @file LogDelegate.h
 * @brief Paints a log row as "HH:MM:SS.mmm  [func:line]  message" in a
 *        fixed-width font, colored by severity (cyan Info, red Error).
 * @note Reads the composed Qt::DisplayRole text from LogModel and the
 *       LevelRole for color; keeps selection/hover backgrounds.
 */

#pragma once

#include <QStyledItemDelegate>

namespace neurus
{

/** @brief Delegate for the LogPanel's QListView. */
class LogDelegate : public QStyledItemDelegate
{
	Q_OBJECT

public:
	explicit LogDelegate(QObject* parent = nullptr);

	void paint(QPainter* painter, const QStyleOptionViewItem& option,
	           const QModelIndex& index) const override;
	QSize sizeHint(const QStyleOptionViewItem& option,
	               const QModelIndex& index) const override;
};

} // namespace neurus
