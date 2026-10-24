#include "graphics/NetworkScene.h"

#include "graphics/items/HighlightEffect.h"
#include "graphics/items/StationOverlayItem.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QImage>
#include <QPainter>

#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static void fillLeftButtonEvent(QGraphicsSceneMouseEvent& event, QGraphicsView& view, const QPointF& pressScenePos,
	const QPointF& scenePos) {
	event.setButton(Qt::LeftButton);
	event.setPos(view.mapFromScene(scenePos));
	event.setScenePos(scenePos);
	event.setScreenPos(view.viewport()->mapToGlobal(view.mapFromScene(scenePos)));
	event.setButtonDownPos(Qt::LeftButton, view.mapFromScene(pressScenePos));
	event.setButtonDownScenePos(Qt::LeftButton, pressScenePos);
	event.setButtonDownScreenPos(Qt::LeftButton, view.viewport()->mapToGlobal(view.mapFromScene(pressScenePos)));
	event.setWidget(view.viewport());
}

// A left press at one scene position and the release at another.
static void sendLeftDrag(NetworkScene& scene, QGraphicsView& view, const QPointF& pressScenePos,
	const QPointF& releaseScenePos) {
	QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
	fillLeftButtonEvent(press, view, pressScenePos, pressScenePos);
	press.setButtons(Qt::LeftButton);
	scene.mousePressEvent(&press);
	QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
	fillLeftButtonEvent(release, view, pressScenePos, releaseScenePos);
	scene.mouseReleaseEvent(&release);
}

static void sendLeftClick(NetworkScene& scene, QGraphicsView& view, const QPointF& scenePos) {
	sendLeftDrag(scene, view, scenePos, scenePos);
}

static bool sendContextMenu(NetworkScene& scene, QGraphicsView& view,
	QGraphicsSceneContextMenuEvent::Reason reason, const QPointF& scenePos,
	const QPoint& screenPos) {
	QGraphicsSceneContextMenuEvent event(QEvent::GraphicsSceneContextMenu);
	event.setReason(reason);
	event.setScenePos(scenePos);
	event.setScreenPos(screenPos);
	event.setWidget(view.viewport());
	scene.contextMenuEvent(&event);
	return event.isAccepted();
}

static bool sendViewportContextMenu(QGraphicsView& view, QContextMenuEvent::Reason reason,
	const QPointF& scenePos) {
	const QPoint viewportPos = view.mapFromScene(scenePos);
	QContextMenuEvent event(reason, viewportPos, view.viewport()->mapToGlobal(viewportPos));
	QApplication::sendEvent(view.viewport(), &event);
	return event.isAccepted();
}

static void scaleView(QGraphicsView& view) {
	view.resize(240, 180);
	view.scale(2.0, 2.0);
}

int main(int argc, char* argv[]) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
	bool ok = true;

	{
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		scaleView(view);

		StationNodeItem station(QRectF(-10.0, -10.0, 20.0, 20.0));
		QPixmap decorationPixmap(24, 24);
		decorationPixmap.fill(Qt::white);
		QGraphicsPixmapItem decoration(decorationPixmap);
		decoration.setPos(-12.0, -12.0);
		decoration.setFlag(QGraphicsItem::ItemIgnoresTransformations);
		decoration.setZValue(10.0);
		scene.addItem(&station);
		scene.addItem(&decoration);

		int stationClicks = 0;
		int disableHighlight = 0;
		QObject::connect(&scene, &NetworkScene::MousePressedOnStationNode, [&](StationNodeItem*) { ++stationClicks; });
		QObject::connect(&scene, &NetworkScene::DisableHighlight, [&]() { ++disableHighlight; });

		sendLeftClick(scene, view, QPointF(0.0, 0.0));
		ok &= expect(stationClicks == 1, "station click passes through higher-z ignored-transform decoration");
		ok &= expect(disableHighlight == 0, "station click does not disable highlight");
	}

	{
		// The near look draws no mark, so the station squares take the clicks again: a click on a square picks that
		// square, the one that holds the mark too, a click beside the squares picks none and a click on a track node
		// beside them picks the node.
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		scaleView(view);
		StationNodeItem anchor(QRectF(-2.0, -2.0, 4.0, 4.0));
		StationNodeItem other(QRectF(-2.0, -2.0, 4.0, 4.0));
		other.setPos(0.0, 20.0);
		anchor.setZValue(StationMarkItem::HostZValue);
		other.setZValue(1.0);
		scene.addItem(&anchor);
		scene.addItem(&other);
		auto* mark = new StationMarkItem(StationClass::Station, 24, &anchor);
		mark->setNodeCentres({QPointF(0.0, 0.0), QPointF(0.0, 20.0)});
		mark->setNearLook(true);
		StationNodeItem* picked = nullptr;
		QObject::connect(&scene, &NetworkScene::MousePressedOnStationNode, [&](StationNodeItem* item) { picked = item; });
		sendLeftClick(scene, view, QPointF(0.0, 20.0));
		ok &= expect(picked == &other, "a click on a station square in the near look picks that square");
		picked = nullptr;
		sendLeftClick(scene, view, QPointF(0.0, 0.0));
		ok &= expect(picked == &anchor, "a click on the square that holds the mark picks that square");
		picked = nullptr;
		sendLeftClick(scene, view, QPointF(4.0, 20.0));
		ok &= expect(!picked, "a click beside the squares in the near look picks no square");
		NodeItem track(QRectF(2.0, -1.0, 2.0, 2.0));
		scene.addItem(&track);
		NodeItem* pickedNode = nullptr;
		QObject::connect(&scene, &NetworkScene::MousePressedOnNode, [&](NodeItem* item) { pickedNode = item; });
		picked = nullptr;
		sendLeftClick(scene, view, QPointF(3.0, 0.0));
		ok &= expect(pickedNode == &track && !picked, "a click on a track node beside the squares picks the node");
	}

	{
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		scaleView(view);

		StationNodeItem station(QRectF(-10.0, -10.0, 20.0, 20.0));
		QGraphicsRectItem child(QRectF(-8.0, -8.0, 16.0, 16.0), &station);
		child.setZValue(10.0);
		scene.addItem(&station);

		int stationClicks = 0;
		QObject::connect(&scene, &NetworkScene::MousePressedOnStationNode, [&](StationNodeItem*) { ++stationClicks; });

		sendLeftClick(scene, view, QPointF(0.0, 0.0));
		ok &= expect(stationClicks == 1, "station click resolves through unrecognized child");
	}

	{
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		scaleView(view);

		TrackLineItem track(QLineF(-60.0, 0.0, 60.0, 0.0));
		QGraphicsTextItem decoration;
		decoration.setPlainText("label");
		decoration.setPos(-20.0, -10.0);
		decoration.setZValue(10.0);
		scene.addItem(&track);
		scene.addItem(&decoration);

		int arcClicks = 0;
		int disableHighlight = 0;
		QObject::connect(&scene, &NetworkScene::MousePressedOnArc, [&](TrackLineItem*) { ++arcClicks; });
		QObject::connect(&scene, &NetworkScene::DisableHighlight, [&]() { ++disableHighlight; });

		sendLeftClick(scene, view, QPointF(0.0, 0.0));
		ok &= expect(arcClicks == 1, "track click passes through higher-z text decoration");
		ok &= expect(disableHighlight == 0, "track click does not disable highlight");
	}

	{
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		scaleView(view);

		int entitySignals = 0;
		int disableHighlight = 0;
		QObject::connect(&scene, &NetworkScene::MousePressedOnNode, [&](NodeItem*) { ++entitySignals; });
		QObject::connect(&scene, &NetworkScene::MousePressedOnStationNode, [&](StationNodeItem*) { ++entitySignals; });
		QObject::connect(&scene, &NetworkScene::MousePressedOnArc, [&](TrackLineItem*) { ++entitySignals; });
		QObject::connect(&scene, &NetworkScene::MousePressedOnConnection, [&](ConnectionItem*) { ++entitySignals; });
		QObject::connect(&scene, &NetworkScene::MousePressedOnSignal, [&](SignalItem*) { ++entitySignals; });
		QObject::connect(&scene, &NetworkScene::MousePressedOnTrain, [&](TrainBodyItem*) { ++entitySignals; });
		QObject::connect(&scene, &NetworkScene::MousePressedOnPassenger, [&](PassengerItem*) { ++entitySignals; });
		QObject::connect(&scene, &NetworkScene::DisableHighlight, [&]() { ++disableHighlight; });

		sendLeftClick(scene, view, QPointF(100.0, 100.0));
		ok &= expect(entitySignals == 0, "empty click emits no entity signal");
		ok &= expect(disableHighlight == 1, "empty click disables highlight once");
	}

	{
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		scaleView(view);

		TrackLineItem track(QLineF(-60.0, 0.0, 60.0, 0.0));
		scene.addItem(&track);

		int arcClicks = 0;
		int disableHighlight = 0;
		QObject::connect(&scene, &NetworkScene::MousePressedOnArc, [&](TrackLineItem*) { ++arcClicks; });
		QObject::connect(&scene, &NetworkScene::DisableHighlight, [&]() { ++disableHighlight; });

		sendLeftDrag(scene, view, QPointF(100.0, 100.0), QPointF(60.0, 80.0));
		ok &= expect(disableHighlight == 0, "a drag from empty canvas keeps the highlight");
		sendLeftDrag(scene, view, QPointF(100.0, 100.0), QPointF(100.5, 100.0));
		ok &= expect(disableHighlight == 1, "a release one pixel from the press on empty canvas is a click");
		sendLeftDrag(scene, view, QPointF(0.0, 0.0), QPointF(100.0, 100.0));
		ok &= expect(arcClicks == 1 && disableHighlight == 1, "a drag from a track selects it and disables no highlight");
		sendLeftDrag(scene, view, QPointF(0.0, 0.0), QPointF(0.0, 0.0));
		ok &= expect(arcClicks == 2 && disableHighlight == 1, "a click on a track after an empty click disables no highlight");
	}

	{
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		scaleView(view);

		NodeItem node(QRectF(-8.0, -8.0, 16.0, 16.0));
		node.setPos(-120.0, -80.0);
		StationNodeItem station(QRectF(-8.0, -8.0, 16.0, 16.0));
		station.setPos(-80.0, -80.0);
		TrackLineItem track(QLineF(-12.0, 0.0, 12.0, 0.0));
		track.setPos(-40.0, -80.0);
		ConnectionItem connection(QLineF(-12.0, 0.0, 12.0, 0.0));
		connection.setPos(0.0, -80.0);
		SignalItem signal(QRectF(-6.0, -8.0, 12.0, 16.0));
		signal.setAspectCode(75);
		signal.setReversedDirection(true);
		signal.trackID = 3;
		signal.sectionAheadId = "protected";
		signal.sectionAheadLength = 125.0;
		signal.sectionAheadTrackId = 4;
		ok &= expect(signal.rect() == QRectF(-6.0, -8.0, 12.0, 16.0)
				&& signal.aspectCode() == 75 && signal.reversedDirection
				&& signal.trackID == 3 && signal.sectionAheadId == "protected"
				&& signal.sectionAheadLength == 125.0 && signal.sectionAheadTrackId == 4,
			"signal marker keeps scene bounds and inspector metadata");
		ok &= expect(!signal.flags().testFlag(QGraphicsItem::ItemIgnoresTransformations),
			"signal head follows scene zoom without a device-sized minimum");
		signal.setPos(40.0, -80.0);
		TrainBodyItem train(QPolygonF() << QPointF(-10.0, -6.0) << QPointF(10.0, -6.0)
										<< QPointF(10.0, 6.0) << QPointF(-10.0, 6.0));
		train.setPos(80.0, -80.0);
		QPixmap passengerPixmap(14, 14);
		passengerPixmap.fill(Qt::white);
		PassengerItem passenger(passengerPixmap);
		passenger.setPos(120.0, -80.0);
		scene.addItem(&node);
		scene.addItem(&station);
		scene.addItem(&track);
		scene.addItem(&connection);
		scene.addItem(&signal);
		scene.addItem(&train);
		scene.addItem(&passenger);

		QGraphicsItem* requestedItem = nullptr;
		QPointF requestedScenePos;
		QPoint requestedScreenPos;
		bool requestedKeyboard = false;
		int contextRequests = 0;
		QObject::connect(&scene, &NetworkScene::ContextMenuRequested,
			[&](QGraphicsItem* item, const QPointF& scenePos, const QPoint& screenPos, bool keyboard) {
				requestedItem = item;
				requestedScenePos = scenePos;
				requestedScreenPos = screenPos;
				requestedKeyboard = keyboard;
				++contextRequests;
			});
		int sceneMousePresses = 0;
		QObject::connect(&scene, &NetworkScene::MousePressedOnScene, [&]() { ++sceneMousePresses; });
		train.setFlag(QGraphicsItem::ItemIsSelectable);

		auto expectContext = [&](QGraphicsItem* expectedItem, const QPointF& expectedScenePos,
								 const QPoint& expectedScreenPos, QGraphicsSceneContextMenuEvent::Reason reason,
								 const char* message) {
			const int before = contextRequests;
			const bool accepted = sendContextMenu(scene, view, reason, expectedScenePos, expectedScreenPos);
			ok &= expect(accepted, "context event is accepted");
			ok &= expect(contextRequests == before + 1, "context request emits exactly once");
			ok &= expect(requestedItem == expectedItem, message);
			ok &= expect(requestedScenePos == expectedScenePos, "context request preserves scene position");
			ok &= expect(requestedScreenPos == expectedScreenPos, "context request preserves screen position");
			ok &= expect(requestedKeyboard == (reason == QGraphicsSceneContextMenuEvent::Keyboard),
				"context request preserves keyboard reason");
		};

		expectContext(&node, QPointF(-120.0, -80.0), QPoint(11, 12),
			QGraphicsSceneContextMenuEvent::Mouse, "context request resolves node");
		expectContext(&station, QPointF(-80.0, -80.0), QPoint(21, 22),
			QGraphicsSceneContextMenuEvent::Mouse, "context request resolves station");
		expectContext(&track, QPointF(-40.0, -80.0), QPoint(31, 32),
			QGraphicsSceneContextMenuEvent::Mouse, "context request resolves track");
		expectContext(&connection, QPointF(0.0, -80.0), QPoint(41, 42),
			QGraphicsSceneContextMenuEvent::Mouse, "context request resolves connection");
		expectContext(&signal, QPointF(40.0, -80.0), QPoint(51, 52),
			QGraphicsSceneContextMenuEvent::Mouse, "context request resolves signal");
		expectContext(&train, QPointF(80.0, -80.0), QPoint(61, 62),
			QGraphicsSceneContextMenuEvent::Keyboard, "context request resolves train");
		expectContext(&passenger, QPointF(120.0, -80.0), QPoint(71, 72),
			QGraphicsSceneContextMenuEvent::Mouse, "context request resolves passenger");
		expectContext(nullptr, QPointF(220.0, 140.0), QPoint(81, 82),
			QGraphicsSceneContextMenuEvent::Mouse, "context request resolves empty space");

		const int viewportMouseBefore = contextRequests;
		const bool viewportMouseAccepted = sendViewportContextMenu(view, QContextMenuEvent::Mouse,
			QPointF(80.0, -80.0));
		ok &= expect(viewportMouseAccepted, "viewport mouse context event is accepted");
		ok &= expect(contextRequests == viewportMouseBefore + 1,
			"viewport mouse context request emits exactly once");
		ok &= expect(requestedItem == &train,
			"viewport mouse context request resolves the semantic train target");
		ok &= expect(!requestedKeyboard,
			"viewport mouse context request preserves mouse reason");

		const int viewportKeyboardBefore = contextRequests;
		const bool viewportKeyboardAccepted = sendViewportContextMenu(view, QContextMenuEvent::Keyboard,
			QPointF(80.0, -80.0));
		ok &= expect(viewportKeyboardAccepted, "viewport keyboard context event is accepted");
		ok &= expect(contextRequests == viewportKeyboardBefore + 1,
			"viewport keyboard context request emits exactly once");
		ok &= expect(requestedItem == &train,
			"viewport keyboard context request resolves the semantic train target");
		ok &= expect(requestedKeyboard,
			"viewport keyboard context request preserves keyboard reason");

		sendLeftClick(scene, view, QPointF(80.0, -80.0));
		ok &= expect(sceneMousePresses == 1, "left click emits scene signal once");
		ok &= expect(train.isSelected(), "left click still dispatches to selectable item");
	}

	{
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		scaleView(view);

		StationNodeItem station(QRectF(-10.0, -10.0, 20.0, 20.0));
		QPixmap decorationPixmap(24, 24);
		decorationPixmap.fill(Qt::white);
		QGraphicsPixmapItem decoration(decorationPixmap);
		decoration.setPos(-12.0, -12.0);
		decoration.setFlag(QGraphicsItem::ItemIgnoresTransformations);
		decoration.setZValue(10.0);
		scene.addItem(&station);
		scene.addItem(&decoration);

		QGraphicsItem* requestedItem = nullptr;
		QPointF requestedScenePos;
		QPoint requestedScreenPos;
		bool requestedKeyboard = true;
		int contextRequests = 0;
		QObject::connect(&scene, &NetworkScene::ContextMenuRequested,
			[&](QGraphicsItem* item, const QPointF& scenePos, const QPoint& screenPos, bool keyboard) {
				requestedItem = item;
				requestedScenePos = scenePos;
				requestedScreenPos = screenPos;
				requestedKeyboard = keyboard;
				++contextRequests;
			});

		const QPointF scenePos(0.0, 0.0);
		const QPoint screenPos(91, 92);
		const bool accepted = sendContextMenu(scene, view, QGraphicsSceneContextMenuEvent::Mouse,
			scenePos, screenPos);
		ok &= expect(accepted, "ignored-transform context event is accepted");
		ok &= expect(contextRequests == 1, "ignored-transform context request emits once");
		ok &= expect(requestedItem == &station,
			"context request passes through higher-z ignored-transform decoration");
		ok &= expect(requestedScenePos == scenePos, "ignored-transform request preserves scene position");
		ok &= expect(requestedScreenPos == screenPos, "ignored-transform request preserves screen position");
		ok &= expect(!requestedKeyboard, "ignored-transform request preserves mouse reason");
	}

	{
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		scaleView(view);

		StationNodeItem station(QRectF(-10.0, -10.0, 20.0, 20.0));
		QGraphicsRectItem child(QRectF(-8.0, -8.0, 16.0, 16.0), &station);
		child.setZValue(10.0);
		scene.addItem(&station);

		QGraphicsItem* requestedItem = nullptr;
		QPointF requestedScenePos;
		QPoint requestedScreenPos;
		bool requestedKeyboard = true;
		int contextRequests = 0;
		QObject::connect(&scene, &NetworkScene::ContextMenuRequested,
			[&](QGraphicsItem* item, const QPointF& scenePos, const QPoint& screenPos, bool keyboard) {
				requestedItem = item;
				requestedScenePos = scenePos;
				requestedScreenPos = screenPos;
				requestedKeyboard = keyboard;
				++contextRequests;
			});

		const QPointF scenePos(0.0, 0.0);
		const QPoint screenPos(101, 102);
		const bool accepted = sendContextMenu(scene, view, QGraphicsSceneContextMenuEvent::Mouse,
			scenePos, screenPos);
		ok &= expect(accepted, "unrecognized-child context event is accepted");
		ok &= expect(contextRequests == 1, "unrecognized-child context request emits once");
		ok &= expect(requestedItem == &station, "context request resolves through unrecognized child");
		ok &= expect(requestedScenePos == scenePos, "unrecognized-child request preserves scene position");
		ok &= expect(requestedScreenPos == screenPos, "unrecognized-child request preserves screen position");
		ok &= expect(!requestedKeyboard, "unrecognized-child request preserves mouse reason");
	}

	{
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		view.resize(240, 180);
		view.scale(0.08, 0.08);
		SignalItem reversed(QRectF(-10.0, -10.0, 20.0, 20.0));
		SignalItem forward(QRectF(-10.0, -10.0, 20.0, 20.0));
		reversed.setPos(-8.0, -30.0);
		forward.setPos(8.0, 30.0);
		reversed.setAspectCode(0);
		forward.setAspectCode(180);
		reversed.setReversedDirection(true);
		forward.setReversedDirection(false);
		scene.addItem(&reversed);
		scene.addItem(&forward); // inserted last, so its synthetic target is topmost
		SignalItem* clicked = nullptr;
		QGraphicsItem* context = nullptr;
		QObject::connect(&scene, &NetworkScene::MousePressedOnSignal,
			[&](SignalItem* signal) { clicked = signal; });
		QObject::connect(&scene, &NetworkScene::ContextMenuRequested,
			[&](QGraphicsItem* item, const QPointF&, const QPoint&, bool) { context = item; });
		sendLeftClick(scene, view, reversed.scenePos());
		ok &= expect(clicked == &reversed && clicked->aspectCode() == 0,
			"scaled Stop head wins over opposing Proceed head's invisible padding");
		sendContextMenu(scene, view, QGraphicsSceneContextMenuEvent::Mouse,
			reversed.scenePos(), QPoint());
		ok &= expect(context == &reversed,
			"opposing signal context resolves the painted head");
	}

	{
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		SignalItem signal(QRectF(-10.0, -10.0, 20.0, 20.0));
		signal.setZValue(3);
		TrainBodyItem train(QPolygonF(QRectF(-20.0, -20.0, 40.0, 40.0)));
		train.setBrush(Qt::blue);
		train.setZValue(4);
		scene.addItem(&signal);
		scene.addItem(&train);
		TrainBodyItem* clicked = nullptr;
		QGraphicsItem* context = nullptr;
		QObject::connect(&scene, &NetworkScene::MousePressedOnTrain,
			[&](TrainBodyItem* item) { clicked = item; });
		QObject::connect(&scene, &NetworkScene::ContextMenuRequested,
			[&](QGraphicsItem* item, const QPointF&, const QPoint&, bool) { context = item; });
		sendLeftClick(scene, view, QPointF());
		ok &= expect(clicked == &train, "painted train wins over covered signal on click");
		sendContextMenu(scene, view, QGraphicsSceneContextMenuEvent::Mouse, QPointF(), QPoint());
		ok &= expect(context == &train, "painted train wins over covered signal for context menu");
	}

	// At Fit a train is a mark of at least MinPixels, and a click on the mark selects the train.
	{
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		view.scale(0.05, 0.05);
		TrainItemGroup group;
		// Points in the order of NetworkPresenter::getTrainPolygon: along one side, then back along the other.
		auto* body = new TrainBodyItem(QPolygonF({{-20.0, -6.0}, {20.0, -6.0}, {20.0, 6.0}, {-20.0, 6.0}}));
		body->setPen(QPen(Qt::black, 3.0));
		group.addToGroup(body);
		group.trainPolygonItemList = new QList<TrainBodyItem*>{body};
		scene.addItem(&group);
		const QRectF unscaled = body->boundingRect();
		TrainBodyItem* clicked = nullptr;
		QObject::connect(&scene, &NetworkScene::MousePressedOnTrain,
			[&](TrainBodyItem* item) { clicked = item; });
		const QPointF beside(20.0 + 3.0 / 0.05, 0.0); // 3 device pixels beside the polygon
		sendLeftClick(scene, view, beside);
		ok &= expect(!clicked, "click beside a train body before its mark is applied selects nothing");
		group.applyViewScale(0.05);
		sendLeftClick(scene, view, beside);
		ok &= expect(clicked == body, "click beside a train mark at Fit selects the train");
		ok &= expect(view.viewportTransform().mapRect(body->boundingRect()).height() >= TrainBodyItem::MinPixels,
			"train mark is at least the minimum size on screen");
		group.applyViewScale(1.0); // the body is 15 pixels thick, its outline 3
		ok &= expect(body->boundingRect() == unscaled, "train body thicker than the minimum keeps its own size");
	}

	{
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		view.scale(0.08, 0.08);
		NodeItem node(QRectF(-8.0, -8.0, 16.0, 16.0));
		node.setZValue(4);
		SignalItem signal(QRectF(-2.0, -2.0, 4.0, 4.0));
		signal.setPos(14.0, 0.0);
		signal.setZValue(3);
		scene.addItem(&node);
		scene.addItem(&signal);
		SignalItem* clicked = nullptr;
		QGraphicsItem* context = nullptr;
		QObject::connect(&scene, &NetworkScene::MousePressedOnSignal,
			[&](SignalItem* item) { clicked = item; });
		QObject::connect(&scene, &NetworkScene::ContextMenuRequested,
			[&](QGraphicsItem* item, const QPointF&, const QPoint&, bool) { context = item; });
		sendLeftClick(scene, view, signal.scenePos());
		ok &= expect(clicked == &signal, "painted signal wins over higher invisible node padding");
		sendContextMenu(scene, view, QGraphicsSceneContextMenuEvent::Mouse, signal.scenePos(), QPoint());
		ok &= expect(context == &signal, "node padding does not obscure painted signal context");
	}

	// A small head keeps its device-sized pick target above the track or connection beneath it.
	for (bool useConnection : {false, true}) {
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		TrackLineItem track(QLineF(-60.0, 0.0, 60.0, 0.0));
		ConnectionItem connection(QLineF(-60.0, 0.0, 60.0, 0.0));
		QGraphicsItem* line = useConnection ? static_cast<QGraphicsItem*>(&connection) : &track;
		SignalItem signal(QRectF(-1.0, -1.0, 2.0, 2.0));
		signal.setPos(0.0, 10.0);
		scene.addItem(line);
		scene.addItem(&signal);
		QGraphicsItem* clicked = nullptr;
		QObject::connect(&scene, &NetworkScene::MousePressedOnSignal, [&](SignalItem* item) { clicked = item; });
		QObject::connect(&scene, &NetworkScene::MousePressedOnArc, [&](TrackLineItem* item) { clicked = item; });
		QObject::connect(&scene, &NetworkScene::MousePressedOnConnection, [&](ConnectionItem* item) { clicked = item; });
		const auto clickedAt = [&](const QPointF& offset) {
			clicked = nullptr;
			sendLeftClick(scene, view, signal.scenePos() + offset);
			return clicked;
		};
		ok &= expect(clickedAt(QPointF(0.0, 0.0)) == &signal, "click inside a small head selects the signal");
		ok &= expect(clickedAt(QPointF(3.0, 0.0)) == &signal, "pick target of a small head wins over the line beneath it");
		ok &= expect(clickedAt(QPointF(20.0, 0.0)) == line, "click beyond the pick target of a head selects the line");
	}

	{
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		QPixmap pixmap(40, 40);
		pixmap.fill(Qt::white);
		QGraphicsPixmapItem artwork(pixmap);
		artwork.setData(PreviewGraphics::Kind, "station");
		artwork.setPos(-20.0, -20.0);
		artwork.setZValue(1.0);
		TrackLineItem track(QLineF(-60.0, 0.0, 60.0, 0.0));
		SignalItem signal(QRectF(-1.0, -1.0, 2.0, 2.0));
		scene.addItem(&track);
		scene.addItem(&artwork);
		scene.addItem(&signal);
		QGraphicsItem* clicked = nullptr;
		QObject::connect(&scene, &NetworkScene::MousePressedOnPreview, [&](QGraphicsItem* item) { clicked = item; });
		sendLeftClick(scene, view, QPointF(3.0, 0.0));
		ok &= expect(clicked == &artwork, "station artwork keeps its priority over the pick target of a head");
	}

	for (bool useConnection : {false, true}) {
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		view.scale(0.08, 0.08);
		NodeItem node(QRectF(0, 0, 0, 0));
		TrackLineItem track(QLineF(-60, 0, 60, 0));
		ConnectionItem connection(QLineF(-60, 0, 60, 0));
		scene.addItem(&node);
		scene.addItem(useConnection ? static_cast<QGraphicsItem*>(&connection) : &track);
		NodeItem* clicked = nullptr;
		QGraphicsItem* context = nullptr;
		QObject::connect(&scene, &NetworkScene::MousePressedOnNode,
			[&](NodeItem* item) { clicked = item; });
		QObject::connect(&scene, &NetworkScene::ContextMenuRequested,
			[&](QGraphicsItem* item, const QPointF&, const QPoint&, bool) { context = item; });
		sendLeftClick(scene, view, QPointF(1, 0));
		ok &= expect(clicked == &node, "zero-size node target wins over underlying line on click");
		sendContextMenu(scene, view, QGraphicsSceneContextMenuEvent::Mouse, QPointF(1, 0), QPoint());
		ok &= expect(context == &node, "zero-size node target wins over underlying line for context menu");
	}

	// The scene picks a node dot or a signal head in a circle that keeps its size on screen, at Fit and zoomed in.
	for (const qreal scale : {0.05, 4.0}) {
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		view.resize(240, 180);
		view.scale(scale, scale);
		const qreal pixel = 1.0 / scale;
		// Both are smaller than a pixel on screen at either zoom.
		NodeItem node(QRectF(-0.1, -0.1, 0.2, 0.2));
		SignalItem signal(QRectF(-0.1, -0.1, 0.2, 0.2));
		signal.setPos(100.0 * pixel, 100.0 * pixel);
		TrackLineItem track(QLineF(-60.0 * pixel, 0.0, 20.0 * pixel, 0.0));
		scene.addItem(&track);
		scene.addItem(&node);
		scene.addItem(&signal);
		QGraphicsItem* picked = nullptr;
		QObject::connect(&scene, &NetworkScene::MousePressedOnNode, [&](NodeItem* item) { picked = item; });
		QObject::connect(&scene, &NetworkScene::MousePressedOnSignal, [&](SignalItem* item) { picked = item; });
		QObject::connect(&scene, &NetworkScene::MousePressedOnArc, [&](TrackLineItem* item) { picked = item; });
		const auto pickedAt = [&](const QPointF& offset, const QGraphicsItem& item) {
			picked = nullptr;
			sendLeftClick(scene, view, item.scenePos() + offset * pixel);
			return picked;
		};
		ok &= expect(pickedAt(QPointF(1.0, 0.0), node) == &node && pickedAt(QPointF(-1.0, -1.0), node) == &node,
			"a click 1 px beside a node dot picks it over the track under it");
		ok &= expect(pickedAt(QPointF(4.0, 0.0), node) == &track, "a click 4 px beside a node dot picks the track");
		ok &= expect(pickedAt(QPointF(-5.0, 0.0), signal) == &signal && pickedAt(QPointF(0.0, 5.0), signal) == &signal,
			"a click 5 px beside a signal head picks it");
		ok &= expect(!pickedAt(QPointF(0.0, 8.0), signal), "a click 8 px beside a signal head picks nothing");
	}

	{
		TrackLineItem track(QLineF(4, 20, 44, 20));
		QPen speed(QColor(30, 130, 210), 4);
		speed.setCosmetic(true);
		track.setPen(speed);
		const auto paintTrack = [&track]() {
			QImage image(48, 40, QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::black);
			{
				QPainter painter(&image);
				track.paint(&painter, nullptr, nullptr);
			}
			return image;
		};
		const auto pixelsOf = [](const QImage& image, const QColor& color) {
			int count = 0;
			for (int y = 0; y < image.height(); ++y)
				for (int x = 0; x < image.width(); ++x)
					count += image.pixelColor(x, y) == color;
			return count;
		};
		const int speedPixels = pixelsOf(paintTrack(), speed.color());
		ok &= expect(speedPixels > 0 && pixelsOf(paintTrack(), occupiedTrackColor()) == 0, "a free track has its speed colour");
		for (const TrackOperationalState state : {TrackOperationalState::Prepared, TrackOperationalState::Blocked}) {
			track.setOperationalState(state);
			ok &= expect(pixelsOf(paintTrack(), speed.color()) == speedPixels && pixelsOf(paintTrack(), occupiedTrackColor()) == 0,
				"a prepared or blocked track keeps its speed colour");
		}
		track.setOperationalState(TrackOperationalState::Occupied);
		ok &= expect(pixelsOf(paintTrack(), occupiedTrackColor()) == speedPixels && pixelsOf(paintTrack(), speed.color()) == 0,
			"an occupied track is red at the same width");
		track.setOperationalState(TrackOperationalState::Blocked, true);
		ok &= expect(pixelsOf(paintTrack(), occupiedTrackColor()) == speedPixels && pixelsOf(paintTrack(), speed.color()) == 0,
			"a blocked track that a train occupies is red");
		track.setOperationalState(TrackOperationalState::Free);
		ok &= expect(pixelsOf(paintTrack(), speed.color()) == speedPixels && pixelsOf(paintTrack(), occupiedTrackColor()) == 0,
			"a track that is free again has its speed colour");
		track.setGraphicsEffect(new HighlightEffect(Qt::blue, 1.0));
		ok &= expect(paintTrack().pixelColor(24, 20) == speed.color(), "a selected track keeps its speed colour");
	}

	{
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		scaleView(view);
		auto* node = new NodeItem(QRectF(-4.5, -4.5, 9.0, 9.0));
		auto* track = new TrackLineItem(QLineF(-60, 0, 60, 0));
		auto* station = new StationNodeItem(QRectF(30, -10, 20, 20));
		scene.addItem(track);
		scene.addItem(node);
		scene.addItem(station);
		const QList<QPointF> points = {QPointF(0, 0), QPointF(0, 12), QPointF(40, 0), QPointF(-40, 10), QPointF(80, 80)};
		QString clicked;
		QObject::connect(&scene, &NetworkScene::MousePressedOnNode, [&](NodeItem*) { clicked += "node "; });
		QObject::connect(&scene, &NetworkScene::MousePressedOnStationNode, [&](StationNodeItem*) { clicked += "station "; });
		QObject::connect(&scene, &NetworkScene::MousePressedOnArc, [&](TrackLineItem*) { clicked += "arc "; });
		QObject::connect(&scene, &NetworkScene::DisableHighlight, [&]() { clicked += "none "; });
		const auto pick = [&]() {
			clicked.clear();
			for (const QPointF& point : points)
				sendLeftClick(scene, view, point);
			return clicked;
		};
		const QString before = pick();
		ok &= expect(before.contains("node") && before.contains("station") && before.contains("arc") && before.contains("none"),
			"the clicks reach every kind of item");
		auto* effect = new HighlightEffect(Qt::blue, 1.0);
		node->setGraphicsEffect(effect);
		ok &= expect(pick() == before, "a cue on a node does not change what a click picks");
		effect->setViewScale(2.0);
		track->setGraphicsEffect(effect);
		station->setGraphicsEffect(effect);
		ok &= expect(pick() == before, "a cue does not change what a click picks");
		ok &= expect(effect->cue() && effect->cue()->shape().isEmpty(), "the cue has no shape");
		QList<QGraphicsItem*> underCue = scene.items(effect->cue()->targetRect().center(), Qt::IntersectsItemShape,
			Qt::DescendingOrder, view.viewportTransform());
		ok &= expect(!underCue.contains(effect->cue()), "the cue is not found by a hit test");
	}

	{
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		scaleView(view);
		auto* station = new StationNodeItem(QRectF(-10, -10, 20, 20));
		station->setData(PreviewGraphics::Kind, "station-node");
		station->setData(PreviewGraphics::Id, "node.non-numeric");
		QPixmap pixmap(20, 20);
		pixmap.fill(Qt::white);
		auto* artwork = new QGraphicsPixmapItem(pixmap, station);
		artwork->setPos(30, -40);
		artwork->setData(PreviewGraphics::Kind, "station");
		artwork->setData(PreviewGraphics::Id, "own-station");
		scene.addItem(station);
		QGraphicsItem* picked = nullptr;
		int legacyClicks = 0;
		QObject::connect(&scene, &NetworkScene::MousePressedOnPreview, [&](QGraphicsItem* item) { picked = item; });
		QObject::connect(&scene, &NetworkScene::MousePressedOnStationNode, [&](StationNodeItem*) { ++legacyClicks; });
		sendLeftClick(scene, view, QPointF(40, -30));
		ok &= expect(picked == artwork && legacyClicks == 0,
			"canonical artwork identity takes precedence over semantic ancestor");
		sendLeftClick(scene, view, QPointF());
		ok &= expect(picked == station && legacyClicks == 0,
			"tagged station square bypasses legacy pointer inspector");
		QGraphicsItem* context = nullptr;
		QObject::connect(&scene, &NetworkScene::ContextMenuRequested,
			[&](QGraphicsItem* item, const QPointF&, const QPoint&, bool) { context = item; });
		sendContextMenu(scene, view, QGraphicsSceneContextMenuEvent::Mouse, QPointF(40, -30), QPoint());
		ok &= expect(context == artwork, "context dispatcher retains artwork identity");
		artwork->setParentItem(nullptr);
		artwork->setPos(60, -40);
		sendLeftClick(scene, view, QPointF(70, -30));
		ok &= expect(picked == artwork, "position-only tagged artwork fallback is selectable");
	}
	{
		TrackLineItem arc(QLineF(0, 0, 20, 0));
		ConnectionItem connection(QLineF(0, 0, 20, 0));
		ok &= expect(arc.track == -1 && arc.arc == nullptr && connection.connection == nullptr,
			"semantic infrastructure constructors have safe preview backing defaults");
	}

	if (!ok)
		return 1;

	std::cout << "all NetworkScene selection tests passed\n";
	return 0;
}
