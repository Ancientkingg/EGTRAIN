#ifndef NETWORKIMAGE_H
#define NETWORKIMAGE_H

#include <QImage>
#include <QRectF>
#include <QSize>
#include <QString>

#include <optional>
#include <string>

class QGraphicsScene;

/// Pictures of the whole network as a NetworkView shows it at Fit: the canvas and its grid, and the items of the scene
/// that are shown. They come from a view of their own on `scene`, `viewportSize` large, fitted to `fitBounds` in scene
/// coordinates, or to the track of the scene when `fitBounds` is empty, so that no window view changes its zoom or
/// scroll. Widgets over the viewport, such as the cards of stations, are not part of the scene and are not drawn.

/// A PNG-ready image of the padded bounds of the fit, at twice the size of the view and at least 1600 pixels wide. A
/// null image when the scene has nothing to show or the size is empty.
QImage renderNetworkImage(QGraphicsScene& scene, const QRectF& fitBounds, const QSize& viewportSize);

/// The same picture as an SVG document with the view box of the size of the view and `title` as its title; nothing
/// when the scene has nothing to show or the size is empty.
std::optional<std::string> renderNetworkSvg(QGraphicsScene& scene, const QRectF& fitBounds, const QSize& viewportSize, const QString& title);

#endif // NETWORKIMAGE_H
