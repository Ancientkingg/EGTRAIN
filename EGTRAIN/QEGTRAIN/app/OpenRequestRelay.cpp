#include "app/OpenRequestRelay.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QIODevice>
#include <QLocalSocket>
#include <QProcessEnvironment>
#include <QStringList>

#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

// A request is two short strings. A longer message is not from this program.
constexpr qint64 kMaxRequestBytes = 65536;
constexpr int kProbeTimeoutMs = 500;
constexpr int kPostponeMs = 250;
constexpr char kAccepted = '1';
constexpr char kRefused = '0';

} // namespace

OpenRequestRelay::OpenRequestRelay(QObject* parent)
	: QObject(parent) {
	m_postponeTimer.setSingleShot(true);
	m_postponeTimer.setInterval(kPostponeMs);
	connect(&m_postponeTimer, &QTimer::timeout, this, [this] { emit openRequested(std::exchange(m_postponed, QString())); });
	connect(&m_server, &QLocalServer::newConnection, this, &OpenRequestRelay::acceptConnections);
}

bool OpenRequestRelay::allowedByEnvironment() {
	const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	if (environment.contains(QStringLiteral("QEGTRAIN_E2E_OPEN_RELAY")))
		return true;
	for (const QString& key : environment.keys())
		if (key.startsWith(QStringLiteral("QEGTRAIN_E2E_")))
			return false;
	return true;
}

QString OpenRequestRelay::defaultServerName() {
	QString user = QDir::home().dirName();
	for (QChar& character : user)
		if (!character.isLetterOrNumber())
			character = QLatin1Char('_');
	// A socket path is short on macOS and Linux, so the name stays short.
	return QStringLiteral("egtrain-open-") + user.left(32);
}

QString OpenRequestRelay::currentApplicationDir() {
	const QString dir = QCoreApplication::applicationDirPath();
	const QString canonical = QFileInfo(dir).canonicalFilePath();
	return canonical.isEmpty() ? dir : canonical;
}

bool OpenRequestRelay::listen(const QString& serverName, const QString& applicationDir) {
	m_applicationDir = applicationDir;
	// A running instance answers on the name. Windows lets a second server listen on a pipe name
	// that is in use, so the check comes before listen on every platform.
	QLocalSocket probe;
	probe.connectToServer(serverName);
	if (probe.waitForConnected(kProbeTimeoutMs))
		return false;
	if (m_server.listen(serverName))
		return true;
	if (m_server.serverError() != QAbstractSocket::AddressInUseError)
		return false;
	// Nobody answers, so the name is held by the socket file that a killed instance left behind.
	return QLocalServer::removeServer(serverName) && m_server.listen(serverName);
}

bool OpenRequestRelay::forward(const QString& serverName, const QString& applicationDir, const QString& path, int timeoutMs) {
	QLocalSocket socket;
	socket.connectToServer(serverName);
	if (!socket.waitForConnected(timeoutMs))
		return false;
#ifdef Q_OS_WIN
	// Lets the running instance bring its window to the front for this request.
	AllowSetForegroundWindow(ASFW_ANY);
#endif
	QByteArray request;
	{
		QDataStream out(&request, QIODevice::WriteOnly);
		out.setVersion(QDataStream::Qt_5_9);
		out << applicationDir << path;
	}
	socket.write(request);
	if (!socket.waitForBytesWritten(timeoutMs) || !socket.waitForReadyRead(timeoutMs))
		return false;
	return socket.readAll() == QByteArray(1, kAccepted);
}

void OpenRequestRelay::postpone(const QString& path) {
	m_postponed = path;
	m_postponeTimer.start();
}

void OpenRequestRelay::acceptConnections() {
	while (QLocalSocket* socket = m_server.nextPendingConnection()) {
		connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
		connect(socket, &QLocalSocket::readyRead, this, [this, socket] { readRequest(socket); });
		// A client that wrote before this connection was set up has no new data to announce.
		if (socket->bytesAvailable() > 0)
			readRequest(socket);
	}
}

void OpenRequestRelay::readRequest(QLocalSocket* socket) {
	if (socket->bytesAvailable() > kMaxRequestBytes) {
		socket->abort();
		return;
	}
	QDataStream in(socket);
	in.setVersion(QDataStream::Qt_5_9);
	QString applicationDir;
	QString path;
	in.startTransaction();
	in >> applicationDir >> path;
	if (!in.commitTransaction()) {
		// Without all of it yet, the rest follows; anything else is not a request.
		if (in.status() != QDataStream::ReadPastEnd)
			socket->abort();
		return;
	}
	socket->disconnect(this);
	const bool accepted = applicationDir == m_applicationDir && !path.isEmpty();
	socket->putChar(accepted ? kAccepted : kRefused);
	socket->flush();
	socket->disconnectFromServer();
	if (!accepted)
		return;
	// A newer request replaces one that waits.
	m_postponeTimer.stop();
	m_postponed.clear();
	emit openRequested(path);
}
