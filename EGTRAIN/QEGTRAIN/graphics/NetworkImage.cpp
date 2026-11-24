#include "graphics/NetworkImage.h"

#include "graphics/NetworkView.h"

#include <QBuffer>
#include <QPainter>
#include <QRegularExpression>
#include <QSvgGenerator>

#include <algorithm>
#include <cmath>

namespace {

constexpr qreal kImageScale = 2.0;
constexpr int kMinimumImageWidth = 1600;

// A view of its own at Fit on the scene, and the part of its viewport that holds the padded bounds of the fit.
struct FittedView {
	NetworkView view;
	QRect source;

	FittedView(QGraphicsScene& scene, const QRectF& fitBounds, const QSize& viewportSize) {
		view.setFrameShape(QFrame::NoFrame);
		view.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		view.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		view.setScene(&scene);
		// A widget that was never shown keeps the geometry of its viewport until it is laid out. Showing it without a window
		// on the screen lays it out, so that the fit uses the size of the viewport of the window.
		view.setAttribute(Qt::WA_DontShowOnScreen);
		view.resize(viewportSize);
		view.show();
		if (fitBounds.isEmpty())
			view.fitToTopology();
		else
			view.fitToBounds(fitBounds);
		// The padded bounds in the viewport are all of the network, whatever part of the viewport they take.
		source = view.viewportTransform().mapRect(view.paddedTopologyBounds()).toAlignedRect();
	}
};

void prepare(QPainter& painter) {
	painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
}

} // namespace

QImage renderNetworkImage(QGraphicsScene& scene, const QRectF& fitBounds, const QSize& viewportSize) {
	if (!viewportSize.isValid() || viewportSize.isEmpty())
		return {};
	FittedView fitted(scene, fitBounds, viewportSize);
	if (fitted.source.isEmpty())
		return {};
	const qreal scale = std::max(kImageScale, qreal(kMinimumImageWidth) / fitted.source.width());
	const QSize size(std::max(1, int(std::lround(fitted.source.width() * scale))), std::max(1, int(std::lround(fitted.source.height() * scale))));
	QImage image(size, QImage::Format_RGB32);
	image.fill(Qt::white);
	QPainter painter(&image);
	prepare(painter);
	fitted.view.render(&painter, QRectF(QPointF(0.0, 0.0), size), fitted.source);
	painter.end();
	return image;
}

std::optional<std::string> renderNetworkSvg(QGraphicsScene& scene, const QRectF& fitBounds, const QSize& viewportSize, const QString& title) {
	if (!viewportSize.isValid() || viewportSize.isEmpty())
		return std::nullopt;
	FittedView fitted(scene, fitBounds, viewportSize);
	if (fitted.source.isEmpty())
		return std::nullopt;
	QByteArray svg;
	QBuffer buffer(&svg);
	if (!buffer.open(QIODevice::WriteOnly))
		return std::nullopt;
	QSvgGenerator generator;
	generator.setOutputDevice(&buffer);
	generator.setSize(fitted.source.size());
	generator.setViewBox(QRect(QPoint(0, 0), fitted.source.size()));
	generator.setTitle(title);
	QPainter painter;
	if (!painter.begin(&generator))
		return std::nullopt;
	prepare(painter);
	fitted.view.render(&painter, QRectF(QPointF(0.0, 0.0), fitted.source.size()), fitted.source);
	painter.end();
	// The generator names the font of the platform, such as .AppleSystemUIFont on macOS, which other systems do not have and
	// replace with a serif font. A sans-serif fallback keeps the labels close to the view there.
	QString text = QString::fromUtf8(svg);
	static const QRegularExpression family(QStringLiteral("font-family=\"([^\"']*)\""));
	text.replace(family, QStringLiteral("font-family=\"'\\1', sans-serif\""));
	const QByteArray result = text.toUtf8();
	return std::string(result.constData(), static_cast<std::size_t>(result.size()));
}
