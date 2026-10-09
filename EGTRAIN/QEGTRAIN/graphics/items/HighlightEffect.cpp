#include "graphics/items/HighlightEffect.h"

#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QSet>

namespace {
// Qt does not give a graphics effect the item it is installed on. The item is found in the scenes
// of the views.
QGraphicsItem* itemOf(const QGraphicsEffect* effect) {
	QSet<const QGraphicsScene*> seen;
	for (const QWidget* widget : QApplication::allWidgets()) {
		const auto* view = qobject_cast<const QGraphicsView*>(widget);
		const QGraphicsScene* scene = view ? view->scene() : nullptr;
		if (!scene || seen.contains(scene))
			continue;
		seen.insert(scene);
		for (QGraphicsItem* item : scene->items())
			if (item->graphicsEffect() == effect)
				return item;
	}
	return nullptr;
}
} // namespace

HighlightEffect::HighlightEffect(QColor, qreal, QObject* parent)
	: QGraphicsEffect(parent) {
}

HighlightEffect::~HighlightEffect() {
	removeCue();
}

void HighlightEffect::setViewScale(qreal scale) {
	if (m_cue)
		m_cue->setViewScale(scale);
}

void HighlightEffect::draw(QPainter* painter) {
	drawSource(painter);
}

void HighlightEffect::sourceChanged(ChangeFlags flags) {
	if (flags & SourceDetached)
		removeCue();
	if (flags & SourceAttached)
		attachCue();
}

void HighlightEffect::attachCue() {
	removeCue();
	QGraphicsItem* item = itemOf(this);
	if (!item)
		return;
	m_cue = new SelectionCueItem(item);
	item->scene()->addItem(m_cue);
	m_sceneChanged = connect(item->scene(), &QGraphicsScene::changed, this, [this]() {
		if (m_cue)
			m_cue->sync();
	});
}

void HighlightEffect::removeCue() {
	disconnect(m_sceneChanged);
	delete m_cue.data();
}
