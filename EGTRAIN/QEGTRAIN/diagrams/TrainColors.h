#ifndef TRAINCOLORS_H
#define TRAINCOLORS_H

#include "diagrams/RunResults.h"

#include <QColor>
#include <QHash>
#include <QString>

struct SceneModel;

// The colour of every train of a completed run whose service has a valid
// visualization colour in the scene, keyed by the train id of the run results.
// Trains of a service without a colour, or with an invalid one, have no entry.
QHash<QString, QColor> trainColorsForRun(const RunResults& run, const SceneModel& scene);

#endif // TRAINCOLORS_H
