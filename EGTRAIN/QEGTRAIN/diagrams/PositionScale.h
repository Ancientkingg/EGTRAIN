#ifndef POSITIONSCALE_H
#define POSITIONSCALE_H

#include "diagrams/BlockingTimeDiagram.h"

#include <vector>

/// Maps the route distance of one reference route to the geo X of the scene, both in km: linearly within each section,
/// from the geo X of its ends. The geo X counts down where the route runs against the scene coordinate and jumps where
/// consecutive sections do not meet in it.
class PositionScale {
public:
	/// A run of consecutive sections whose geo X meet, from the start of the first to the end of the last.
	struct Stretch {
		double startKm = 0.0;	 ///< Route distance where it starts
		double endKm = 0.0;		 ///< Route distance where it ends
		double geoStartKm = 0.0; ///< Geo X at startKm
		double geoEndKm = 0.0;	 ///< Geo X at endKm
	};
	/// Where the geo X of a section and the next one differ by more than 1 m.
	struct Jump {
		double routeKm = 0.0;	  ///< Route distance where the next section starts
		double geoBeforeKm = 0.0; ///< Geo X where the section before the jump ends
		double geoAfterKm = 0.0;  ///< Geo X where the section after the jump starts
	};
	/// A round geo X value and where it lies on the route.
	struct Label {
		double routeKm = 0.0; ///< Route distance of the label
		double geoKm = 0.0;	  ///< Geo X that it reads
	};

	/// No sections: every geo X is NaN.
	PositionScale() = default;
	/// From the sections of one route in route order. A section without geometry or geo X is left out.
	explicit PositionScale(const std::vector<BlockSectionExtent>& extents);

	/// True without a section.
	bool empty() const { return m_pieces.empty(); }
	/// The stretches in route order.
	const std::vector<Stretch>& stretches() const { return m_stretches; }
	/// The jumps in route order.
	const std::vector<Jump>& jumps() const { return m_jumps; }

	/// The geo X at a route distance. Where two sections meet it is the end of the first; outside every section the
	/// nearest one extends linearly, so before the first or after the last section of the route. NaN when empty.
	double geoAt(double routeKm) const;
	/// The geo X at a route distance as seen from towardKm: where two sections meet, of the one that extends toward it.
	/// The ends of an interval map with the other end as towardKm, so each takes the geo X of its own section.
	double geoAt(double routeKm, double towardKm) const;

	/// The step of round labels for the route distances lowKm to highKm drawn over widthPx pixels: 1, 2 or 5 times a power
	/// of ten, so that labels lie at least about 60 px apart. 0 when empty or nothing is drawn.
	double labelStepKm(double lowKm, double highKm, double widthPx) const;
	/// Labels on the multiples of labelStepKm in the sections between lowKm and highKm, in order of route distance, each at
	/// least as far from the one before as labels must be apart; that also holds across a jump. A geo X can occur more than
	/// once where the route turns back.
	std::vector<Label> roundLabels(double lowKm, double highKm, double widthPx) const;

private:
	struct Piece {
		double startKm = 0.0;
		double endKm = 0.0;
		double geoStartKm = 0.0;
		double geoEndKm = 0.0;
		double lowerKm() const;
		double upperKm() const;
		double geoAt(double routeKm) const;
	};
	// The least step for labels at least the spacing apart, in the steepest section in view; 0 when nothing is drawn.
	double minimumLabelStepKm(double lowKm, double highKm, double widthPx) const;
	std::vector<Piece> m_pieces;
	std::vector<Stretch> m_stretches;
	std::vector<Jump> m_jumps;
};

#endif // POSITIONSCALE_H
