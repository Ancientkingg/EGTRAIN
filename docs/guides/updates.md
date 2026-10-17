# Updating EGTRAIN

This page says how EGTRAIN checks for a newer release and how it installs one. The [README](../../README.md#quick-start) describes the first install of each system.

## Checks and consent

At first launch EGTRAIN asks whether to check for releases automatically. You
can disable or re-enable automatic checks from **Help**. **Check for Updates...**
always performs a manual check, even when automatic checks are disabled. An
available update is downloaded only after consent; **Update and Restart**
installs it and relaunches the application. If the install location is not
writable, download the release and install it with the platform's normal
permissions instead. On macOS, EGTRAIN cannot update itself while it runs from a
disk image or another read-only location; move it to the Applications folder
first.

When a newer release exists, the dialog **EGTRAIN <version> is Available** shows the release notes with their line breaks and lists. Long notes end at a word and point to the release page. The buttons are **Update and Restart** (when this copy can update itself), **Open Release Page**, **Later** and **Stop Checking**.

Production updates are stable versioned releases. With automatic checks enabled,
EGTRAIN offers newer releases on the next application start; it does not interrupt
an already-running session when a release is published. Use **Help > Check for
Updates...** to check immediately. Installing an update always requires consent.

## Installed and portable Windows copies

EGTRAIN counts a Windows copy as managed by an installer when the file
`installed-by-setup.ini` is in the same folder as `QEGTRAIN.exe`. The EGTRAIN
installer writes that file and the portable package does not contain it. Such a
copy does not replace its own files. **Update and Restart** downloads the Setup
program of the newer release instead of the portable package, checks it against
the update manifest, closes EGTRAIN, runs the Setup without windows and starts
EGTRAIN again. A portable folder that holds the file, for example one zipped from
an installed copy, is treated the same way until the file is deleted.

EGTRAIN does not take the end of the Setup for a successful update. At the next
start it compares the version that the update was to install with the version
that runs. It tells you once that EGTRAIN was updated, that the Setup finished but
an older version runs, or that the Setup did not finish and the installation may
be incomplete. In the last case download the Setup from the release page and run
it again.

## What happens during an update

An update is prepared in a hidden `.qegtrain-update-*` folder next to the
installation, and the updated application starts in its own directory. After a
successful update the folder is removed. On Windows the update helper cannot
delete itself, so the application removes the rest about 20 seconds after it
started, or at its next start when it was closed before that. Folders left by an
interrupted update are removed at a start of the application once they are older
than an hour.

When the new version cannot be started, the update helper puts the previous
version back and starts it again. That version then tells you once which version
could not be started. Automatic checks do not offer that version again, a newer
version is offered as usual, and **Help > Check for Updates...** still offers it.

The helper tries again for a few seconds when it cannot rename the installation,
because on Windows a file of the application that just ended can stay locked for a
short time. If the files still cannot be replaced, the previous version stays
installed and starts again, and the helper removes the staging folder (on
Windows the helper itself stays until the next start removes it). That
version then tells you once that the update to the new version could not be
applied. Automatic checks keep offering it, and **Help > Check for Updates...**
tries again.

## Earlier builds

Production build 107 and earlier have no updater and need one manual upgrade.
The Linux AppImage from production build 111 also needs one manual upgrade to
correct its update-staging path. After that, use **Update and Restart** for
future releases. Windows and macOS build 111 can update in place.
