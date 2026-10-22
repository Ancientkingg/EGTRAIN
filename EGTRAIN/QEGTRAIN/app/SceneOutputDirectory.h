#pragma once

#include <QString>
#include <string>

/// The folder that a run of the scene with this name writes to, as an absolute path:
/// <base>/Output/<scene>. The base is QEGTRAIN_OUTPUT_DIR when that is set, otherwise the Qt
/// application data folder. The folder is not created. The application and the window share this rule.
QString sceneOutputDirectory(const std::string& sceneName);
