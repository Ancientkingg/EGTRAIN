#ifndef SCENEBUNDLE_H
#define SCENEBUNDLE_H

#include "scene/SceneModel.h"
#include "scene/SceneWriter.h"

#include <string>

SceneLoadResult loadSceneBundle(const std::string& bundlePath);
SceneLoadResult loadScenePath(const std::string& path);
SceneSaveResult saveSceneBundle(const SceneModel& scene, const std::string& bundlePath);
SceneSaveResult unpackSceneBundle(const std::string& bundlePath, const std::string& destinationDirectory);

#endif // SCENEBUNDLE_H
