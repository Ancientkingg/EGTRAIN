#ifndef EGTRAIN_DISTRIBUTION_H
#define EGTRAIN_DISTRIBUTION_H

#include <QString>

// The kind of copy that is running. It is not the manifest key that updateDistributionKey()
// returns, which names an entry of the update manifest.
enum class Distribution {
	WindowsInstalled,
	WindowsPortable,
	MacApp,
	LinuxAppImage,
	Unsupported
};

// The file that the installer puts next to QEGTRAIN.exe. Its content is never read.
QString installerMarkerName();

// A Windows copy is installed when installerMarkerName() is a regular file directly in the
// folder of QEGTRAIN.exe, and portable otherwise. A folder that is not an absolute path is
// portable. Nothing else is consulted. It works on every platform.
Distribution windowsDistributionOf(const QString& applicationDirectory);

// The kind of this platform and, on Windows, of the folder of the running executable. It does
// not check that the process runs from a bundle or an AppImage: SelfUpdater::capability does.
Distribution currentDistribution();

// Why a copy of this kind must not replace its own files, as a sentence that ends with a full
// stop, or an empty string when it may.
QString selfReplacementBlockedReason(Distribution distribution);

#endif
