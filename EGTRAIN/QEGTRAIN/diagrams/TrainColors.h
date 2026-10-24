#ifndef TRAINCOLORS_H
#define TRAINCOLORS_H

#include "diagrams/RunResults.h"

#include <QColor>
#include <QHash>
#include <QString>
#include <QVector>

struct SceneModel;
QT_BEGIN_NAMESPACE
class QAreaSeries;
class QLineSeries;
QT_END_NAMESPACE

// The colour of every train of a completed run whose service has a valid
// visualization colour in the scene, keyed by the train id of the run results.
// Trains of a service without a colour, or with an invalid one, have no entry.
QHash<QString, QColor> trainColorsForRun(const RunResults& run, const SceneModel& scene);

// The service of every train of a completed run that has one, keyed by the train id of the run results, which groups
// the Trains list of a diagram window.
QHash<QString, QString> trainServicesForRun(const RunResults& run);

/// One train of a diagram window as the colour rule sees it.
struct TrainColourInput {
	QString id;		   ///< The id of the train, the group id of its series
	QString service;   ///< The service of the train; empty makes the train a service of its own
	bool shown = true; ///< The Trains list shows the train
	QString copyOf;	   ///< The train of the window that this train copies, such as the first train of a capacity cycle
					   ///< one period later; empty for a train of its own
};

/// The colours that the colour rule of the diagram windows gives the trains of a window.
struct TrainColourPlan {
	/// How the shown trains are coloured.
	enum class Mode {
		PerTrain,	///< Six shown trains or fewer: a colour of the palette for each train
		PerService, ///< More shown trains in six services or fewer: a colour of the palette for each service
		Grey		///< More shown trains in more services: all grey, and the selected train in the first colour
	};
	Mode mode = Mode::PerTrain;		///< How the shown trains are coloured
	QHash<QString, QColor> colours; ///< The colour of every train by its id; a hidden train has the light grey of a hidden row
};

/// The colour rule of the diagram windows, by the trains shown. With six shown trains or fewer each train takes a colour
/// of the palette #2a78d6, #c2448a, #4c8a12, #6127b1, #e35b6b, #842a65; with more shown trains in six services or fewer
/// each service takes one; otherwise every shown train is grey, #8a97a1, and the selected train takes #2a78d6. A train
/// whose service has a colour in the case, in `serviceColours` by train id (trainColorsForRun), takes that colour wherever
/// the rule gives it one of the palette. The palette goes in the order of `trains`: by the place of a train, or of a
/// service, among all the trains of the window while there are six of them or fewer, so that showing and hiding moves
/// no colour, and otherwise among the shown ones. A hidden train is light grey, #c3cbd1, the colour of a hidden row in
/// the Trains list. A train without a service is a service of its own. A copy takes the colour of the train it copies,
/// or light grey while it is hidden, and is counted as that train, which the rule then counts as shown while the train
/// or a copy of it is shown; selecting a copy selects its train. The ids of `trains` are distinct, and a train that a
/// copy names is no copy.
TrainColourPlan trainColourPlan(const QVector<TrainColourInput>& trains, const QHash<QString, QColor>& serviceColours,
	const QString& selectedTrain = QString());

/// The kind of a blocking time, which gives it its look.
enum class BlockingTimeKind {
	Ordinary,	   ///< A blocking time, keyed "Blocking time"
	CriticalBlock, ///< The critical block of a compressed chart, keyed "Critical block"
	Conflict	   ///< A block that two trains occupy at once, keyed "Conflict"
};

/// Gives the fill and the closed outline of a blocking time the look of its kind and its kind in the Key ("keyKind"). An
/// ordinary blocking time has a neutral grey fill and an outline 1.4 px wide that the diagram window paints in the colour
/// of the train; the Key shows its outline neutral grey. A critical block is amber with a 45 degree hatch and an outline
/// of amber brown, and a conflict is #9e1b1b with a cross hatch and an outline of that red; both keep their colours
/// ("ownColour"). The pen of the sample in the Key is the property "keyPen" of the outline. The hatches are textures
/// at two pixels per pixel, so that a figure at twice the size of the window draws them sharp. `fill` is null for a
/// blocking time of no width.
void styleBlockingTime(QAreaSeries* fill, QLineSeries* outline, BlockingTimeKind kind);

#endif // TRAINCOLORS_H
