#ifndef HIGHLIGHTEFFECT_H
#define HIGHLIGHTEFFECT_H

#include <QGraphicsEffect>
#include <QMetaObject>
#include <QPointer>

#include "graphics/items/SelectionCueItem.h"

/// Marks the item it is installed on as selected. The item is drawn unchanged; while the effect is
/// attached, one SelectionCueItem in the scene of the item shows the selection. The effect removes
/// the cue when it is detached or deleted. The scene of the item must be shown by a QGraphicsView.
/// The colour and strength only keep the call sites unchanged: the cue always has kSelectionCueColor.
class HighlightEffect : public QGraphicsEffect {
	Q_OBJECT

public:
	/// An effect without a cue until it is installed on an item. color and strength are not used.
	HighlightEffect(const QColor& color, qreal strength, QObject* parent = nullptr);
	/// Deletes the cue.
	~HighlightEffect() override;

	/// Logical pixels per scene unit of the view; the cue keeps its size on screen.
	void setViewScale(qreal scale);
	/// The cue in the scene of the item, or null while the effect is not installed on an item in a shown scene.
	SelectionCueItem* cue() const { return m_cue; }

protected:
	/// Draws the item unchanged.
	void draw(QPainter* painter) override;
	/// Adds the cue when the effect is installed on an item and removes it when the effect is taken off.
	void sourceChanged(ChangeFlags flags) override;

private:
	void attachCue();
	void removeCue();

	QPointer<SelectionCueItem> m_cue;
	QMetaObject::Connection m_sceneChanged;
};

#endif // HIGHLIGHTEFFECT_H
