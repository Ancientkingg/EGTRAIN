#ifndef TRAINBADGEITEM_H
#define TRAINBADGEITEM_H

#include <QFont>
#include <QPainter>
#include <QPixmap>
#include <QtWidgets/QGraphicsItem>

#include "graphics/VisualPolish.h"

class TrainBadgeItem : public QGraphicsItem {
public:
	enum class Presentation { Overview, Identity, Detailed };

	TrainBadgeItem(QGraphicsItem* parent = nullptr);
	void setIdentifier(const QString& identifier);
	void setTooltipDetails(const QString& description, const QString& operatingCode,
		const QString& trainType);
	void setSpeedText(const QString& speedText);
	void setSpeedVisible(bool visible);
	bool isSpeedVisible() const { return m_speedVisible; }
	void setTrainVisual(const TrainVisual& visual);
	void setReversed(bool reversed);
	void setPresentation(Presentation presentation);
	Presentation presentation() const { return m_presentation; }
	void setPromoted(bool promoted);
	bool isPromoted() const { return m_promoted; }
	bool showsIdentifier() const { return m_presentation != Presentation::Overview; }
	bool showsSpeed() const {
		return m_presentation == Presentation::Detailed && m_speedVisible && !m_speedText.isEmpty();
	}

	static QColor badgeSurfaceColor() { return QColor("#26313B"); }
	static QColor badgePrimaryTextColor() { return QColor("#F2F5F7"); }
	static QColor badgeSecondaryTextColor() { return QColor("#C5D0D6"); }
	static QColor promotedBorderColor() { return QColor("#315A70"); }
	static QSizeF markerSize() { return QSizeF(18.0, 16.0); }
	static qreal identityZoomThreshold() { return 1.8; }
	static qreal detailedZoomThreshold() { return 3.0; }
	static Presentation presentationForZoom(qreal zoom, bool promoted);
	static qreal maximumWidth(Presentation presentation) {
		return presentation == Presentation::Identity ? 88.0
			: presentation == Presentation::Detailed ? 132.0 : 18.0;
	}

	QRectF badgeRect() const;
	QRectF iconRect() const;
	QRectF identifierTextRect() const;
	QRectF speedTextRect() const;
	QString displayedIdentifier() const;
	QPolygonF directionNose() const;
	QRectF boundingRect() const override;
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
		QWidget* widget = nullptr) override;

	enum { Type = UserType + 11 };
	int type() const override { return Type; }

private:
	QFont identifierFont() const;
	QFont speedFont() const;
	qreal badgeWidth() const;
	void updateToolTip();

	QString m_identifier;
	QString m_description;
	QString m_operatingCode;
	QString m_trainType;
	QString m_speedText;
	TrainVisual m_visual;
	QPixmap m_icon;
	Presentation m_presentation = Presentation::Overview;
	bool m_reversed = false;
	bool m_promoted = false;
	bool m_speedVisible = true;
};

#endif
