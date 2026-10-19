#ifndef EGTRAIN_DISTRIBUTION_H
#define EGTRAIN_DISTRIBUTION_H

#include <QString>

/// The kind of copy that is running. It is not the manifest key that updateDistributionKey()
/// returns, which names an entry of the update manifest.
enum class Distribution {
	WindowsInstalled, ///< A Windows copy set up by the installer: installerMarkerName() is next to QEGTRAIN.exe.
	WindowsPortable,  ///< A Windows copy without the installer marker, such as one unpacked from the portable zip.
	MacApp,			  ///< A copy on macOS.
	LinuxAppImage,	  ///< A copy on Linux.
	Unsupported		  ///< A copy on any other platform.
};

/// The file that the installer puts next to QEGTRAIN.exe: "installed-by-setup.ini". Its content is never read.
QString installerMarkerName();

/// A Windows copy is installed when installerMarkerName() is a regular file directly in the
/// folder of QEGTRAIN.exe, and portable otherwise. A folder that is not an absolute path is
/// portable. Nothing else is consulted. It works on every platform.
Distribution windowsDistributionOf(const QString& applicationDirectory);

/// The kind of this platform and, on Windows, of the folder of the running executable. It does
/// not check that the process runs from a bundle or an AppImage: SelfUpdater::capability does.
Distribution currentDistribution();

#endif
