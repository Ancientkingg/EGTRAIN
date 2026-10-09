#ifndef HIGHLIGHTEFFECT_H
#define HIGHLIGHTEFFECT_H

#include <QGraphicsEffect>
#include <QMetaObject>
#include <QPointer>

#include "graphics/items/SelectionCueItem.h"

// Marks the item it is installed on as selected. The item is drawn unchanged; while the effect is
// attached, one SelectionCueItem in the scene of the item shows the selection. The effect removes
// the cue when it is detached or deleted. The scene of the item must be shown by a QGraphicsView.
// The colour and strength only keep the call sites unchanged: the cue always has kSelectionCueColor.
class HighlightEffect : public QGraphicsEffect {
	Q_OBJECT

public:
	HighlightEffect(QColor color, qreal strength, QObject* parent = 0);
	~HighlightEffect();

	// Device pixels per scene unit of the view; the cue keeps its size on screen.
	void setViewScale(qreal scale);
	SelectionCueItem* cue() const { return m_cue; }

protected:
	void draw(QPainter* painter) override;
	void sourceChanged(ChangeFlags flags) override;

private:
	void attachCue();
	void removeCue();

	QPointer<SelectionCueItem> m_cue;
	QMetaObject::Connection m_sceneChanged;
};

#endif // HIGHLIGHTEFFECT_H
