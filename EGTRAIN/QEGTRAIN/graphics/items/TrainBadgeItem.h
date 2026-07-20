#ifndef TRAINBADGEITEM_H
#define TRAINBADGEITEM_H

#include <QFontMetricsF>
#include <QPixmap>
#include <QtWidgets/QGraphicsItem>
#include <QPainter>
#include <QtWidgets/QStyleOptionGraphicsItem>
#include <QtWidgets/QWidget>

#include "graphics/VisualPolish.h"

class TrainBadgeItem : public QGraphicsItem {
public:
	TrainBadgeItem(QGraphicsItem* parent = nullptr);

	void setIdentifier(const QString& identifier);
	void setSpeedText(const QString& speedText);
	void setSpeedVisible(bool visible);
	bool isSpeedVisible() const { return m_speedVisible; }
	void setTrainVisual(const TrainVisual& visual);
	void setReversed(bool reversed);
	void setCompact(bool compact);

	static QRectF iconRect(const QRectF& body, bool reversed) {
		const qreal left = body.left() + (reversed ? 15.0 : 5.0);
		return QRectF(left, body.center().y() - 7.0, 14.0, 14.0);
	}

	static QRectF speedTextRect(const QRectF& body, bool compact, bool reversed,
		const QFontMetricsF& metrics, const QString& speedText) {
		if (compact || speedText.isEmpty())
			return QRectF();
		const qreal right = body.right() - (reversed ? 8.0 : 16.0);
		const qreal left = body.left() + 8.0;
		const qreal width = qMin(metrics.horizontalAdvance(speedText), qMax(0.0, right - left));
		return QRectF(right - width, body.top() + 1.0, width, body.height() - 2.0);
	}

	static QRectF identifierTextRect(const QRectF& body, bool compact, bool reversed,
		const QFontMetricsF& metrics, const QString& speedText) {
		const qreal left = iconRect(body, reversed).right() + 4.0;
		const QRectF speed = speedTextRect(body, compact, reversed, metrics, speedText);
		const qreal defaultRight = body.right() - (reversed ? 9.0 : 17.0);
		const qreal right = speed.isEmpty() ? defaultRight : speed.left() - 4.0;
		return QRectF(left, body.top() + 1.0, qMax(0.0, right - left), body.height() - 2.0);
	}

	static QString elidedIdentifier(const QString& identifier, const QFontMetricsF& metrics,
		const QRectF& region) {
		return metrics.elidedText(identifier, Qt::ElideRight, qMax(0.0, region.width()));
	}

	QRectF boundingRect() const override;
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

	enum { Type = UserType + 11 };
	int type() const override { return Type; }

private:
	QRectF badgeRect() const;

	QString m_identifier;
	QString m_speedText;
	TrainVisual m_visual;
	QPixmap m_icon;
	bool m_reversed;
	bool m_compact;
	bool m_speedVisible = true;
};

#endif // TRAINBADGEITEM_H
