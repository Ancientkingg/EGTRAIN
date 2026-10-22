#ifndef EGTRAIN_UPDATE_NOTICES_H
#define EGTRAIN_UPDATE_NOTICES_H

class QWidget;

/// Tells the user, once, what an update that replaced the installation in place left: that EGTRAIN was
/// updated to the version that runs, that the update to a version could not be started and that the
/// previous version was restored, or that it could not be applied and that the previous version is still
/// installed. The update helper leaves the record that triggers it (update/UpdateRecord.h). After a version
/// that could not be started, the version is stored so that automatic update checks stop offering it
/// (update/UpdateSettings.h). A scripted launch shows nothing and keeps the record.
void reportInPlaceUpdate(QWidget* parent);

/// Tells the user, once, what the installer of an installed Windows copy left: the record that the update
/// helper writes after it ran the installer (update/UpdateRecord.h). The version that runs now decides
/// whether the update is reported as done, as not applied or as failed, so success is never assumed from
/// the end of the installer. A scripted launch shows nothing and keeps the record. Call it before
/// reportInPlaceUpdate, which removes any record it finds.
void reportInstallerUpdate(QWidget* parent);

#endif
