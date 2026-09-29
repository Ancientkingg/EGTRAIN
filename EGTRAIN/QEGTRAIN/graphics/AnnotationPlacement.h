#ifndef ANNOTATIONPLACEMENT_H
#define ANNOTATIONPLACEMENT_H

#include "scene/SceneModel.h"
#include "scene/TrackPreview.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>

// Presentation vectors are independent of the station's semantic track/node anchor.
inline bool annotationSignalNormal(const TrackPreviewLine& line, double rawX,
        std::pair<double, double>& normal) {
    if (!std::isfinite(rawX))
        return false;
    bool foundEndpoint = false;
    for (std::size_t i = 1; i < line.points.size(); ++i) {
        const auto& a = line.points[i - 1];
        const auto& b = line.points[i];
        if (!std::isfinite(a.rawX) || !std::isfinite(b.rawX)
                || !std::isfinite(a.x) || !std::isfinite(a.y)
                || !std::isfinite(b.x) || !std::isfinite(b.y)
                || a.rawX == b.rawX || rawX < std::min(a.rawX, b.rawX)
                || rawX > std::max(a.rawX, b.rawX))
            continue;
        const double direction = b.rawX > a.rawX ? 1.0 : -1.0;
        const double dx = (b.x - a.x) * direction;
        const double dy = (b.y - a.y) * direction;
        const double length = std::hypot(dx, dy);
        if (!std::isfinite(length) || length == 0.0)
            continue;
        normal = {dy / length, -dx / length};
        // Historical intervals are right-open, except at the final endpoint.
        // Prefer the outgoing interval at a bend regardless of storage order.
        if (rawX < std::max(a.rawX, b.rawX))
            return true;
        foundEndpoint = true;
    }
    return foundEndpoint;
}

// Reproduce the historical first/interior/last regional shift from ordered
// geographic station views. Invalid or insufficient regions contribute no shift.
inline double stationNamedYOffset(const std::string& name) {
	if (name == "Koge"
		|| name == "Olby"
		|| name == "KogeNord"
		|| name == "Jersie"
		|| name == "SolrodStrand"
		|| name == "Karlslunde"
		|| name == "Greve"
		|| name == "Hundige"
		|| name == "Ishoj"
		|| name == "Vallensbaek"
		|| name == "BrondbyStrand"
		|| name == "Avedore"
		|| name == "Brondbyoster"
		|| name == "Frihedem"
		|| name == "Amarken"
		|| name == "Sydhavn"
		|| name == "Sjaelor"
		|| name == "Friheden"
		|| name == "BallerupStorage"
		|| name == "NyEllebjergAE")
		return 900.0;
	if (name == "Frederikssund"
		|| name == "Vinge"
		|| name == "Olstykke"
		|| name == "Egedal"
		|| name == "Stenlose"
		|| name == "Vekso"
		|| name == "Kildedal"
		|| name == "Malov"
		|| name == "Ballerup"
		|| name == "Malmparken"
		|| name == "Skovlunde"
		|| name == "Herlev"
		|| name == "Husum"
		|| name == "Islev"
		|| name == "Jyllingevej"
		|| name == "Vanlose"
		|| name == "FlintholmCH"
		|| name == "PeterBangsvej"
		|| name == "Langgade"
		|| name == "Valby"
		|| name == "Carlsberg"
		|| name == "KobenhavnH"
		|| name == "Vesterport"
		|| name == "Norreport"
		|| name == "Osterport_t"
		|| name == "Osterport"
		|| name == "Nordhavn"
		|| name == "Dybbolsbro")
		return -920.0;
	if (name == "HojeTaastrup"
		|| name == "Taastrup"
		|| name == "Glostrup"
		|| name == "Rodovre"
		|| name == "Hvidovre"
		|| name == "DanshojBBx"
		|| name == "Albertslund")
		return -2800.0;
	if (name == "DanshojF"
		|| name == "VigerslevAlle"
		|| name == "Alholm"
		|| name == "KBHallen"
		|| name == "FlintholmF"
		|| name == "Grondal"
		|| name == "Fuglebakken"
		|| name == "Norrebro"
		|| name == "Bispebjerg"
		|| name == "Charlottenlund"
		|| name == "Ordrup"
		|| name == "Hellerup"
		|| name == "HellerupStorage"
		|| name == "RyparkenF"
		|| name == "Klampenborg"
		|| name == "NyEllebjergF")
		return -3500.0;
	if (name == "Bernstorffsvej"
		|| name == "Gentofte"
		|| name == "Lyngby"
		|| name == "Sorgenfri"
		|| name == "Virum"
		|| name == "Holte"
		|| name == "Birkerod"
		|| name == "Allerod"
		|| name == "Hillerod")
		return -2200.0;
	return 0.0;
}

inline std::pair<double, double> annotationStationShift(
        const std::vector<SceneStationView>& views, const std::string& stationId) {
    struct Position {
        double chainage;
        std::string id;
        double x;
        double y;
    };
    std::map<int, std::vector<Position>> regions;
    std::vector<int> stationRegions;
    constexpr double pi = 3.14159265358979323846;
    for (const auto& view : views) {
        if (!std::isfinite(view.latitude) || std::abs(view.latitude) >= 90.0
                || !std::isfinite(view.longitude))
            continue;
        const double y = -std::log(std::tan(pi / 4.0 + view.latitude * pi / 360.0)) * 180.0 / pi;
        if (!std::isfinite(y))
            continue;
        for (const auto& region : view.regions) {
            if (!std::isfinite(region.second))
                continue;
            regions[region.first].push_back({region.second, view.stationId, view.longitude, y});
            if (view.stationId == stationId
                    && std::find(stationRegions.begin(), stationRegions.end(), region.first) == stationRegions.end())
                stationRegions.push_back(region.first);
        }
    }
    double sumX = 0.0, sumY = 0.0;
    std::size_t count = 0;
    for (int region : stationRegions) {
        auto& positions = regions[region];
        std::sort(positions.begin(), positions.end(), [](const Position& a, const Position& b) {
            return a.chainage < b.chainage;
        });
        if (stationId.empty() || std::adjacent_find(positions.begin(), positions.end(),
                [](const Position& a, const Position& b) { return a.chainage == b.chainage; })
                != positions.end())
            continue;
        const auto target = std::find_if(positions.begin(), positions.end(),
                [&stationId](const Position& p) { return p.id == stationId; });
        if (target == positions.end() || positions.size() < 2)
            continue;
        const std::size_t index = static_cast<std::size_t>(target - positions.begin());
        const std::size_t middle = index == positions.size() - 1 ? index - 1 : index;
        const Position& a = positions[middle == 0 ? 0 : middle - 1];
        const Position& b = positions[middle == 0 ? 1 : middle];
        const Position& c = positions[std::min(middle == 0 ? 2 : middle + 1, positions.size() - 1)];
        double sx, sy;
        if (positions.size() == 2) {
            const double dx = positions[1].x - positions[0].x;
            const double dy = positions[1].y - positions[0].y;
            const double length = std::hypot(dx, dy);
            if (!std::isfinite(length) || length == 0.0)
                continue;
            sx = dy / length;
            sy = -dx / length;
        } else {
            const double ux = a.x - b.x, uy = a.y - b.y;
            const double vx = c.x - b.x, vy = c.y - b.y;
            if (!std::isfinite(std::hypot(ux, uy)) || !std::isfinite(std::hypot(vx, vy))
                    || std::hypot(ux, uy) == 0.0 || std::hypot(vx, vy) == 0.0)
                continue;
            double alpha = std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
            if (alpha < 0.0)
                alpha += 2.0 * pi;
            const double beta = std::atan2(-uy, ux);
            const double gamma = alpha / 2.0 - beta;
            sx = std::cos(gamma);
            sy = std::sin(gamma);
        }
        if (!std::isfinite(sx) || !std::isfinite(sy))
            continue;
        sumX += sx;
        sumY += sy;
        ++count;
    }
    if (count == 0)
        return {0.0, 0.0};
    return {-1200.0 * sumX / count, -1200.0 * sumY / count};
}

#endif
