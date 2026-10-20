// SceneModel has a member named signals, so its header comes before the Qt headers,
// which define that word as a macro.
#include "scene/SceneModel.h"

#include "diagrams/TrainColors.h"

QHash<QString, QColor> trainColorsForRun(const RunResults& run, const SceneModel& scene) {
	QHash<QString, QColor> colors;
	for (const TrainRunResult& train : run.trains) {
		int red = 0, green = 0, blue = 0;
		if (sceneParseVisualizationColor(sceneServiceVisualizationColor(scene, train.serviceId), &red, &green, &blue))
			colors.insert(QString::fromStdString(train.trainId), QColor(red, green, blue));
	}
	return colors;
}
