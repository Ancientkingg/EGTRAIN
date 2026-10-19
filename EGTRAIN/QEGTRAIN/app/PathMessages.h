#ifndef PATHMESSAGES_H
#define PATHMESSAGES_H

#include <QString>

#include "widgets/DisplayPath.h"

/// The texts of the window that name a path the person chose. A path in a dialog comes back with forward slashes, so
/// each text writes it as the person reads it on this system.
namespace path_message {

/// A write that failed. what completes "Could not", for example "write the image to".
inline QString writeFailed(const QString& what, const QString& path) {
	return QStringLiteral("Could not %1:\n%2").arg(what, display_path::format(path));
}

/// The status bar text that names the output folder of the next runs.
inline QString outputFolder(const QString& folder) {
	return QStringLiteral("Output folder: %1").arg(display_path::format(folder));
}

}

#endif
