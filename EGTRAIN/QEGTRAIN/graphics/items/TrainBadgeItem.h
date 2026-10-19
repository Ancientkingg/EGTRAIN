#ifndef TRAINBADGEITEM_H
#define TRAINBADGEITEM_H

#include <QFont>
#include <QPainter>
#include <QPixmap>
#include <QtWidgets/QGraphicsItem>

#include "graphics/VisualPolish.h"

/// The badge of a train, placed at the centre of its bodies. It ignores the transformation of the view, so
/// its geometry is in logical pixels from its position. It keeps the identifier, the speed, the kind and the
/// direction of the train and works out its layout, but it paints nothing, takes no click and shows no
/// tooltip: the canvas shows the bodies of the train alone.
class TrainBadgeItem : public QGraphicsItem {
public:
	/// How much the badge holds.
	enum class Presentation { Overview, ///< A marker of markerSize().
		Identity,						///< The icon and the identifier.
		Detailed /**< The icon, the identifier and the speed. */ };

	/// A badge in the Overview presentation with the look of TrainVisualKind::Passenger.
	TrainBadgeItem(QGraphicsItem* parent = nullptr);
	/// Sets the identifier of the train.
	void setIdentifier(const QString& identifier);
	/// Keeps the description, the operating code and the type of the train. The badge shows no tooltip.
	void setTooltipDetails(const QString& description, const QString& operatingCode,
		const QString& trainType);
	/// Sets the speed of the train as text.
	void setSpeedText(const QString& speedText);
	/// Sets whether the Detailed presentation holds the speed.
	void setSpeedVisible(bool visible);
	/// The Detailed presentation holds the speed.
	bool isSpeedVisible() const { return m_speedVisible; }
	/// Sets the look of the train, which gives the icon.
	void setTrainVisual(const TrainVisual& visual);
	/// The train runs in the reversed direction, so the nose of the badge points left.
	void setReversed(bool reversed);
	/// Sets how much the badge holds.
	void setPresentation(Presentation presentation);
	/// How much the badge holds.
	Presentation presentation() const { return m_presentation; }
	/// The train is selected or followed, which raises its badge over the others.
	void setPromoted(bool promoted);
	/// The train is selected or followed.
	bool isPromoted() const { return m_promoted; }
	/// The presentation holds the identifier.
	bool showsIdentifier() const { return m_presentation != Presentation::Overview; }
	/// The presentation holds a speed text that is to be shown.
	bool showsSpeed() const {
		return m_presentation == Presentation::Detailed && m_speedVisible && !m_speedText.isEmpty();
	}

	/// The fill of a badge: RGB (38, 49, 59).
	static QColor badgeSurfaceColor() { return QColor("#26313B"); }
	/// The colour of the identifier: RGB (242, 245, 247).
	static QColor badgePrimaryTextColor() { return QColor("#F2F5F7"); }
	/// The colour of the speed: RGB (197, 208, 214).
	static QColor badgeSecondaryTextColor() { return QColor("#C5D0D6"); }
	/// The border of the badge of a promoted train: RGB (49, 90, 112).
	static QColor promotedBorderColor() { return QColor("#315A70"); }
	/// The size of the Overview marker in logical pixels.
	static QSizeF markerSize() { return QSizeF(18.0, 16.0); }
	/// The zoom ratio from which a badge holds the identifier.
	static qreal identityZoomThreshold() { return 1.8; }
	/// The zoom ratio from which a badge also holds the speed.
	static qreal detailedZoomThreshold() { return 3.0; }
	/// The presentation at a zoom ratio of the view: Detailed for a promoted train and from
	/// detailedZoomThreshold(), Identity from identityZoomThreshold(), Overview below.
	static Presentation presentationForZoom(qreal zoom, bool promoted);
	/// The largest width of the badge in logical pixels.
	static qreal maximumWidth(Presentation presentation) {
		return presentation == Presentation::Identity ? 88.0
			: presentation == Presentation::Detailed  ? 132.0
													  : 18.0;
	}

	/// The badge, 8 logical pixels to the right of its position and 8 above it.
	QRectF badgeRect() const;
	/// The icon inside the badge.
	QRectF iconRect() const;
	/// The room for the identifier; empty when the presentation does not hold it.
	QRectF identifierTextRect() const;
	/// The room for the speed; empty when the presentation does not hold it.
	QRectF speedTextRect() const;
	/// The identifier, elided in the middle to fit its room.
	QString displayedIdentifier() const;
	/// The triangle at the end of the badge that points in the direction of the train.
	QPolygonF directionNose() const;
	/// The badge with a margin of 1 logical pixel.
	QRectF boundingRect() const override;
	/// An empty shape, so the badge is never picked.
	QPainterPath shape() const override;
	/// Paints nothing.
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
		QWidget* widget = nullptr) override;

	enum { Type = UserType + 11 /**< The item type for qgraphicsitem_cast. */ };
	/// Returns Type.
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
