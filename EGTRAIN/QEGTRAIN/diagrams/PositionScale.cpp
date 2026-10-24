#include "diagrams/PositionScale.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// Geo X values of consecutive sections that differ by no more meet.
constexpr double kJumpToleranceKm = 0.001;
// Route distances that differ by no more are the same position, as for the ends of sections and envelopes.
constexpr double kPositionToleranceKm = 1e-6;
// The least distance between two labels.
constexpr double kLabelSpacingPx = 60.0;

// 1, 2 or 5 times a power of ten.
struct RoundStep {
	int mantissa = 1;
	int exponent = 0;

	// k times the step, the double nearest to the decimal value.
	double multiple(double k) const {
		const double power = std::pow(10.0, std::abs(exponent));
		return exponent >= 0 ? k * mantissa * power : k * mantissa / power;
	}
	double value() const { return multiple(1.0); }
};

// The least round step that is at least `minimum`, which is positive and finite.
RoundStep roundStepAtLeast(double minimum) {
	RoundStep step;
	step.exponent = static_cast<int>(std::floor(std::log10(minimum)));
	for (const int mantissa : {1, 2, 5, 10}) {
		step.mantissa = mantissa;
		if (step.value() >= minimum * (1.0 - 1e-9))
			break;
	}
	if (step.mantissa == 10) {
		step.mantissa = 1;
		++step.exponent;
	}
	return step;
}

} // namespace

double PositionScale::Piece::lowerKm() const {
	return std::min(startKm, endKm);
}

double PositionScale::Piece::upperKm() const {
	return std::max(startKm, endKm);
}

double PositionScale::Piece::geoAt(double routeKm) const {
	return geoStartKm + (routeKm - startKm) / (endKm - startKm) * (geoEndKm - geoStartKm);
}

PositionScale::PositionScale(const std::vector<BlockSectionExtent>& extents) {
	for (const BlockSectionExtent& extent : extents)
		if (extent.geometryValid && std::isfinite(extent.geoStartKm) && std::isfinite(extent.geoEndKm))
			m_pieces.push_back({extent.startKm, extent.endKm, extent.geoStartKm, extent.geoEndKm});
	for (std::size_t i = 0; i < m_pieces.size(); ++i) {
		const Piece& piece = m_pieces[i];
		if (i > 0 && std::abs(piece.geoStartKm - m_pieces[i - 1].geoEndKm) <= kJumpToleranceKm) {
			m_stretches.back().endKm = piece.endKm;
			m_stretches.back().geoEndKm = piece.geoEndKm;
			continue;
		}
		if (i > 0)
			m_jumps.push_back({piece.startKm, m_pieces[i - 1].geoEndKm, piece.geoStartKm});
		m_stretches.push_back({piece.startKm, piece.endKm, piece.geoStartKm, piece.geoEndKm});
	}
}

double PositionScale::geoAt(double routeKm) const {
	return geoAt(routeKm, routeKm);
}

double PositionScale::geoAt(double routeKm, double towardKm) const {
	if (m_pieces.empty() || !std::isfinite(routeKm))
		return std::numeric_limits<double>::quiet_NaN();
	const Piece* nearest = &m_pieces.front();
	double nearestDistance = std::numeric_limits<double>::infinity();
	for (const Piece& piece : m_pieces) {
		const double distance = std::max({0.0, piece.lowerKm() - routeKm, routeKm - piece.upperKm()});
		const bool extendsToward = !std::isfinite(towardKm) || towardKm == routeKm
			|| (towardKm > routeKm ? routeKm < piece.upperKm() - kPositionToleranceKm : routeKm > piece.lowerKm() + kPositionToleranceKm);
		if (distance <= kPositionToleranceKm && extendsToward)
			return piece.geoAt(routeKm);
		if (distance < nearestDistance) {
			nearest = &piece;
			nearestDistance = distance;
		}
	}
	return nearest->geoAt(routeKm);
}

double PositionScale::minimumLabelStepKm(double lowKm, double highKm, double widthPx) const {
	if (m_pieces.empty() || !std::isfinite(lowKm) || !std::isfinite(highKm) || highKm <= lowKm || !(widthPx > 0.0))
		return 0.0;
	double slope = 0.0;
	for (const Piece& piece : m_pieces)
		if (piece.upperKm() > lowKm && piece.lowerKm() < highKm)
			slope = std::max(slope, std::abs((piece.geoEndKm - piece.geoStartKm) / (piece.endKm - piece.startKm)));
	const double minimum = kLabelSpacingPx * (highKm - lowKm) / widthPx * slope;
	return std::isfinite(minimum) ? minimum : 0.0;
}

double PositionScale::labelStepKm(double lowKm, double highKm, double widthPx) const {
	const double minimum = minimumLabelStepKm(std::min(lowKm, highKm), std::max(lowKm, highKm), widthPx);
	return minimum > 0.0 ? roundStepAtLeast(minimum).value() : 0.0;
}

std::vector<PositionScale::Label> PositionScale::roundLabels(double lowKm, double highKm, double widthPx) const {
	if (lowKm > highKm)
		std::swap(lowKm, highKm);
	std::vector<Label> labels;
	const double minimum = minimumLabelStepKm(lowKm, highKm, widthPx);
	if (minimum <= 0.0)
		return labels;
	const RoundStep step = roundStepAtLeast(minimum);
	const double stepKm = step.value();
	for (const Piece& piece : m_pieces) {
		const double from = std::max(lowKm, piece.lowerKm());
		const double to = std::min(highKm, piece.upperKm());
		if (from > to || piece.geoEndKm == piece.geoStartKm)
			continue;
		const double geoLow = std::min(piece.geoAt(from), piece.geoAt(to));
		const double geoHigh = std::max(piece.geoAt(from), piece.geoAt(to));
		const auto first = static_cast<long long>(std::ceil(geoLow / stepKm - 1e-9));
		const auto last = static_cast<long long>(std::floor(geoHigh / stepKm + 1e-9));
		for (long long k = first; k <= last; ++k) {
			const double geoKm = step.multiple(static_cast<double>(k));
			const double routeKm = piece.startKm
				+ (geoKm - piece.geoStartKm) / (piece.geoEndKm - piece.geoStartKm) * (piece.endKm - piece.startKm);
			labels.push_back({std::clamp(routeKm, from, to), geoKm});
		}
	}
	std::stable_sort(labels.begin(), labels.end(), [](const Label& a, const Label& b) { return a.routeKm < b.routeKm; });
	// Labels lie at least as far apart in route distance as the spacing, wherever they come from.
	const double spacingKm = kLabelSpacingPx * (highKm - lowKm) / widthPx * (1.0 - 1e-6);
	std::vector<Label> spaced;
	for (const Label& label : labels)
		if (spaced.empty() || label.routeKm - spaced.back().routeKm >= spacingKm)
			spaced.push_back(label);
	return spaced;
}
