#ifndef ICONITEM_H
#define ICONITEM_H

#include <QGraphicsPixmapItem>

/// A picture that draws over track lines, connections and nodes.
class IconItem : public QGraphicsPixmapItem {
public:
	/// An item that shows pixmap, one scene unit per pixel.
	IconItem(const QPixmap& pixmap, QGraphicsItem* parent = nullptr);
	~IconItem() override;

	enum { Type = UserType + 3 /**< The item type for qgraphicsitem_cast. */ };
	/// Returns Type.
	int type() const override {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // ICONITEM_H
