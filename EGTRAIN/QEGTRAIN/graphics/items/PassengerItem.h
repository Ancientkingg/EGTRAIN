#ifndef PASSENGERITEM_H
#define PASSENGERITEM_H

#include <QColor>
#include <QGraphicsPixmapItem>
#include <string>

/// How full a platform is, which colours its gauge and the figures of its passengers.
enum class PlatformCrowding {
	Normal, ///< Up to the crowding threshold of the platform: teal.
	Busy,	///< Above the threshold and up to the capacity: amber.
	Over	///< Above the capacity: red.
};

/// The figure of a passenger waiting on a platform: a head on a body, in the colour of the crowding of the platform. It
/// draws over the platforms.
class PassengerItem : public QGraphicsPixmapItem {
public:
	/// An icon that shows pixmap, one scene unit per pixel before the scale of the item.
	PassengerItem(const QPixmap& pixmap, QGraphicsItem* parent = nullptr);
	~PassengerItem() override;

	/// The width of a figure in scene units before the scale of the item.
	static constexpr qreal FigureWidth = 24.0;
	/// The height of a figure in scene units before the scale of the item.
	static constexpr qreal FigureHeight = 36.0;

	/// The colour of the gauge and the figures of a platform that is as crowded as crowding says.
	static QColor crowdingColour(PlatformCrowding crowding);
	/// The figure of a passenger in the colour of crowding, FigureWidth by FigureHeight scene units drawn at four times
	/// that many pixels.
	static QPixmap figure(PlatformCrowding crowding);

	/// The id of the passenger.
	std::string passengerId;

	enum { Type = UserType + 10 /**< The item type for qgraphicsitem_cast. */ };
	/// Returns Type.
	int type() const override {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // PASSENGERITEM_H
