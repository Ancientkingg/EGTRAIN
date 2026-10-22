#pragma once

#include <QSize>
#include <QStyledItemDelegate>

/// Draws the rows of a list as the list style does and asks for no width, so that a row takes the width of the list and
/// elides its text instead of widening the list.
class NarrowRowDelegate : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;
	/// The height that the style gives the row, and no width.
	QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
		return QSize(0, QStyledItemDelegate::sizeHint(option, index).height());
	}
};
