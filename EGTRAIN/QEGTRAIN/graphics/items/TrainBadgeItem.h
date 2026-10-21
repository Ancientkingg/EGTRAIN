#ifndef TRAINBADGEITEM_H
#define TRAINBADGEITEM_H

#include <QFont>
#include <QPainter>
#include <QPixmap>
#include <QtWidgets/QGraphicsItem>

#include "graphics/VisualPolish.h"

/// The badge of a train, placed at the centre of its bodies. It ignores the transformation of the view, so
/// its geometry is in logical pixels from its position. It keeps the identifier, the speed, the kind and the
/// direction of the train, but draws only the speed, as white text with a dark outline beside the train. It
/// takes no click and shows no tooltip.
class TrainBadgeItem : public QGraphicsItem {
public:
	/// How much the badge shows.
	enum class Presentation { Overview, ///< Nothing.
		Identity,						///< Nothing.
		Detailed /**< The speed. */ };

	/// A badge in the Overview presentation with the look of TrainVisualKind::Passenger.
	TrainBadgeItem(QGraphicsItem* parent = nullptr);
	/// Sets the identifier of the train.
	void setIdentifier(const QString& identifier);
	/// Keeps the description, the operating code and the type of the train. The badge shows no tooltip.
	void setTooltipDetails(const QString& description, const QString& operatingCode,
		const QString& trainType);
	/// Sets the speed of the train as text.
	void setSpeedText(const QString& speedText);
	/// Sets whether the Detailed presentation shows the speed.
	void setSpeedVisible(bool visible);
	/// The Detailed presentation shows the speed.
	bool isSpeedVisible() const { return m_speedVisible; }
	/// Sets the look of the train, which gives the icon.
	void setTrainVisual(const TrainVisual& visual);
	/// Keeps whether the train runs in the reversed direction.
	void setReversed(bool reversed);
	/// Sets how much the badge shows.
	void setPresentation(Presentation presentation);
	/// How much the badge shows.
	Presentation presentation() const { return m_presentation; }
	/// The train is selected or followed, which raises its badge over the others.
	void setPromoted(bool promoted);
	/// The train is selected or followed.
	bool isPromoted() const { return m_promoted; }
	/// The badge draws the speed: in the Detailed presentation, while the speed is visible and not empty.
	bool showsSpeed() const {
		return m_presentation == Presentation::Detailed && m_speedVisible && !m_speedText.isEmpty();
	}

	/// The zoom ratio from which a badge is in the Identity presentation.
	static qreal identityZoomThreshold() { return 1.8; }
	/// The zoom ratio from which a badge shows the speed.
	static qreal detailedZoomThreshold() { return 3.0; }
	/// The presentation at a zoom ratio of the view: Detailed for a promoted train and from
	/// detailedZoomThreshold(), Identity from identityZoomThreshold(), Overview below.
	static Presentation presentationForZoom(qreal zoom, bool promoted);

	/// The speed text, starting 8 logical pixels to the right of the position, with the bottom of its line
	/// 8 above it; empty when showsSpeed() is false.
	QRectF speedTextRect() const;
	/// The speed text with its outline and a margin of 1 logical pixel; empty when showsSpeed() is false.
	QRectF boundingRect() const override;
	/// An empty shape, so the badge is never picked.
	QPainterPath shape() const override;
	/// Draws the speed in white with a dark outline when showsSpeed() is true, and nothing otherwise.
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
		QWidget* widget = nullptr) override;

	enum { Type = UserType + 11 /**< The item type for qgraphicsitem_cast. */ };
	/// Returns Type.
	int type() const override { return Type; }

private:
	QFont speedFont() const;
	QPointF speedBaseline() const;
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
