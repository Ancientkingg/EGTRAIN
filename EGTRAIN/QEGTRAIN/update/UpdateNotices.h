#ifndef EGTRAIN_UPDATE_NOTICES_H
#define EGTRAIN_UPDATE_NOTICES_H

class QWidget;

// Tells the user, once, that the update to a version could not be started and that the previous
// version was restored. The update helper leaves the record that triggers it (update/UpdateRecord.h).
// The version is stored so that automatic update checks stop offering it (update/UpdateSettings.h).
// A scripted launch shows nothing and keeps the record.
void reportRolledBackUpdate(QWidget* parent);

#endif
