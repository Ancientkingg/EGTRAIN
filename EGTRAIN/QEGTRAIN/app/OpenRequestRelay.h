#pragma once

#include <QLocalServer>
#include <QObject>
#include <QString>
#include <QTimer>

class QLocalSocket;

// Hands a file that the system asked a second process to open to the instance that already
// runs. The running instance listens on a local socket (a named pipe on Windows). A second
// process that has only a file to open sends the path, waits for the answer and ends; the
// running instance emits openRequested.
//
// A request names the application folder of the sender, and an instance accepts only requests
// that name its own folder, so a copy of EGTRAIN never receives the files of another copy.
class OpenRequestRelay : public QObject {
	Q_OBJECT

public:
	explicit OpenRequestRelay(QObject* parent = nullptr);

	// False when a variable QEGTRAIN_E2E_* is set, unless QEGTRAIN_E2E_OPEN_RELAY is set as
	// well, so that scripted runs never meet the relay.
	static bool allowedByEnvironment();

	// The name of the local server of the current user.
	static QString defaultServerName();

	// The application folder of this process, in the form that requests name.
	static QString currentApplicationDir();

	// Listens on serverName for requests that name applicationDir. A name that a running
	// instance holds is left to it and the answer is false; a name that only a killed instance
	// left behind is taken over.
	bool listen(const QString& serverName, const QString& applicationDir);

	// Sends path to the instance on serverName and returns true when that instance accepted it:
	// the instance runs, it has the same applicationDir and it answered within timeoutMs. The
	// caller then has nothing left to open. The call blocks until the answer or the timeout.
	static bool forward(const QString& serverName, const QString& applicationDir, const QString& path, int timeoutMs = 5000);

	// Emits openRequested(path) again shortly, for a receiver that cannot open the file yet. Only
	// the newest path waits: a request that arrives later replaces it.
	void postpone(const QString& path);

signals:
	void openRequested(const QString& path);

private:
	void acceptConnections();
	void readRequest(QLocalSocket* socket);

	QLocalServer m_server;
	QString m_applicationDir;
	QString m_postponed;
	QTimer m_postponeTimer;
};
