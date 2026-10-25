#include "telemetry/TelemetrySender.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QSemaphore>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <atomic>
#include <cstdio>
#include <vector>

static bool until(QCoreApplication& app, const std::function<bool()>& predicate, int ms = 6000) {
	QElapsedTimer elapsed;
	elapsed.start();
	while (elapsed.elapsed() < ms) {
		if (predicate()) return true;
		app.processEvents();
		QThread::msleep(5);
	}
	return false;
}

// telemetry_tls_receiver.py passes the certificate, the port, the scenario and the authority. The constructor reads
// them and, outside Windows, probes the receiver once with Qt's own TLS, before Qt Test runs: main returns the skip
// code when the probe shows that Secure Transport rejects the test authority. initTestCase checks what it found.
class TestTelemetryNetwork : public QObject {
	Q_OBJECT

public:
	TestTelemetryNetwork(int argc, char** argv);
	bool unsupportedTrust() const { return m_unsupportedTrust; }

private slots:
	void initTestCase();
	void sendsOverLoopbackTls();

private:
	int m_arguments = 0;
	int m_scenario = -1; // 0 untrusted 202; 1 trusted 202; 2 trusted 302.
	bool m_authorityOpened = false;
	QByteArray m_authorityPem;
	TelemetryContext m_context{true, true, true, QString(), QStringLiteral("1")};
#ifndef Q_OS_WIN
	bool m_certificateOpened = false;
	QSslCertificate m_certificate;
	QSslCertificate m_authority;
	QSslConfiguration m_defaultTrust;
	bool m_defaultTrustKept = false;
	bool m_rejectedCertificate = false;
	QNetworkReply::NetworkError m_probeError = QNetworkReply::NoError;
	QStringList m_errorsSeen;
#endif
	bool m_unsupportedTrust = false;
};

TestTelemetryNetwork::TestTelemetryNetwork(int argc, char** argv) : m_arguments(argc - 1) {
	if (argc < 5) return;
	m_scenario = QByteArray(argv[3]).toInt();
	QFile authorityFile(QString::fromLocal8Bit(argv[4]));
	m_authorityOpened = authorityFile.open(QIODevice::ReadOnly);
	m_authorityPem = authorityFile.readAll();
	m_context.endpoint = QStringLiteral("https://127.0.0.1:%1/collect").arg(QString::fromLocal8Bit(argv[2]));
	// The sender goes through WinHTTP on Windows, so this program does not probe the server with Qt's
	// own TLS there.
#ifndef Q_OS_WIN
	QFile certFile(QString::fromLocal8Bit(argv[1]));
	m_certificateOpened = certFile.open(QIODevice::ReadOnly);
	m_certificate = QSslCertificate(certFile.readAll(), QSsl::Pem);
	m_authority = QSslCertificate(m_authorityPem, QSsl::Pem);
	m_defaultTrust = QSslConfiguration::defaultConfiguration();
	QSslConfiguration trust = m_defaultTrust;
	if (m_scenario) {
		auto authorities = trust.caCertificates();
		authorities.append(m_authority);
		trust.setCaCertificates(authorities);
		trust.setPeerVerifyMode(QSslSocket::VerifyPeer);
	}
	m_defaultTrustKept = QSslConfiguration::defaultConfiguration() == m_defaultTrust;
	QNetworkAccessManager preflight;
	QEventLoop loop;
	QTimer timer;
	timer.setSingleShot(true);
	QNetworkRequest probeRequest(QUrl(m_context.endpoint));
	probeRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::ManualRedirectPolicy);
	if (m_scenario) probeRequest.setSslConfiguration(trust);
	QNetworkReply* probe = preflight.post(probeRequest, QByteArray());
	QObject::connect(probe, &QNetworkReply::sslErrors, &loop,
		[&](const QList<QSslError>& errors) {
			for (const QSslError& error : errors) {
				m_errorsSeen.append(error.errorString());
				if (error.error() == QSslError::SelfSignedCertificate
					|| error.error() == QSslError::CertificateUntrusted
					|| error.error() == QSslError::UnableToGetLocalIssuerCertificate)
					m_rejectedCertificate = true;
			}
		});
	QObject::connect(probe, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
	timer.start(5000);
	loop.exec();
	m_probeError = probe->error();
	if (m_scenario != 0 && m_probeError != QNetworkReply::NoError
		&& QSslSocket::sslLibraryVersionString().startsWith(QStringLiteral("Secure Transport"))
		&& m_errorsSeen.size() == 1
		&& m_errorsSeen.first() == QStringLiteral("The root CA certificate is not trusted for this purpose")) {
		fprintf(stderr, "UNSUPPORTED: Secure Transport rejects request-local test CA: %s\n",
			qPrintable(m_errorsSeen.first()));
		m_unsupportedTrust = true;
	}
	probe->deleteLater();
#endif
}

void TestTelemetryNetwork::initTestCase() {
	// Qt Test reads the arguments after the four and rejects the ones it does not know.
	QVERIFY2(m_arguments >= 4, "the program needs the certificate, the port, the scenario and the authority");
#ifndef Q_OS_WIN
	QVERIFY(QSslSocket::supportsSsl());
#endif
	QCOMPARE_GE(m_scenario, 0);
	QCOMPARE_LE(m_scenario, 2);
	QVERIFY(m_authorityOpened);
	QVERIFY(!m_authorityPem.isEmpty());
#ifndef Q_OS_WIN
	QVERIFY(m_certificateOpened);
	QVERIFY(!m_certificate.isNull());
	QVERIFY(!m_authority.isNull());
	QVERIFY(m_defaultTrustKept);
	if (m_scenario == 0) {
		QVERIFY(m_rejectedCertificate);
		QCOMPARE(m_probeError, QNetworkReply::SslHandshakeFailedError);
	} else {
		const QByteArray probeFailure =
			QStringLiteral("trusted probe error %1: %2").arg(int(m_probeError)).arg(m_errorsSeen.join(';')).toLocal8Bit();
		QVERIFY2(m_probeError == QNetworkReply::NoError, probeFailure.constData());
	}
#endif
}

void TestTelemetryNetwork::sendsOverLoopbackTls() {
	QCoreApplication& app = *QCoreApplication::instance();
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString settingsFile = directory.filePath(QStringLiteral("consent.ini"));
	const QString storage = directory.filePath(QStringLiteral("queue"));
	QSettings settings(settingsFile, QSettings::IniFormat);
	TelemetryConsent consent(settings, m_context);
	QVERIFY(consent.save(true, false));
	const QDateTime now = QDateTime::currentDateTimeUtc().addSecs(10);
	std::atomic<qint64> clock{0};
	QSemaphore polled, reserved, completed, exited;
	telemetry::TelemetrySender::TestOptions options;
	options.settingsFactory = [settingsFile] {
		return std::unique_ptr<QSettings>(new QSettings(settingsFile, QSettings::IniFormat));
	};
	options.utcNow = [&] { return now.addMSecs(clock.load()); };
	options.monotonicMs = [&] { return clock.load(); };
	options.afterPoll = [&] { polled.release(); };
	options.afterReservation = [&] { reserved.release(); };
	options.afterCompletion = [&] { completed.release(); };
	if (m_scenario) options.loopbackRootPem = m_authorityPem;
	options.onWorkerExit = [&] { exited.release(); };
	telemetry::Application metadata{QStringLiteral("1.0.0"), {}, {}};
	telemetry::TelemetrySender sender(m_context, metadata, storage, options); // The real transport: post unset.
	// A published usage permit is not a completed initial category-observation poll.
	// Wait for startup and durable queueing before making this a transport test.
	QVERIFY(until(app, [&] { return polled.available() > 0; }));
	QVERIFY(until(app, [&] { return sender.tryEnqueue({telemetry::Name::SessionStarted}); }));
	const QString usagePath = storage + QStringLiteral("/usage.json");
	QJsonObject queuedRow;
	// The predicate cannot fail the slot, so it keeps what it read for the checks after the wait.
	bool queuedDocument = true;
	qsizetype queuedRows = 0;
	QVERIFY(until(app, [&] {
		QFile usage(usagePath);
		if (!usage.open(QIODevice::ReadOnly)) return false;
		const auto doc = QJsonDocument::fromJson(usage.readAll());
		queuedDocument = doc.isObject();
		if (!queuedDocument) return true;
		const auto rows = doc.object().value(QStringLiteral("events")).toArray();
		if (rows.isEmpty()) return false;
		queuedRows = rows.size();
		queuedRow = rows.first().toObject();
		return true;
	}));
	QVERIFY(queuedDocument);
	QCOMPARE(queuedRows, 1);
	const auto queuedEvent = queuedRow.value(QStringLiteral("event")).toObject();
	telemetry::TelemetryEvent parsed;
	QVERIFY(telemetry::readEvent(queuedEvent, telemetry::Category::Usage, &parsed));
	fprintf(stdout, "TLS_TEST_QUEUED_EVENT=%s\n", QJsonDocument(queuedEvent).toJson(QJsonDocument::Compact).constData());
	clock = 60001;
	QVERIFY(until(app, [&] { return completed.available() > 0; }, 12000));
	QCOMPARE(reserved.available(), 1);
	QCOMPARE(completed.available(), 1);
#ifndef Q_OS_WIN
	QVERIFY(QSslConfiguration::defaultConfiguration() == m_defaultTrust);
#endif
	sender.stop();
	QVERIFY(until(app, [&] { return exited.available() > 0; }));
	QFile usage(usagePath);
	QVERIFY(usage.open(QIODevice::ReadOnly));
	const auto rows = QJsonDocument::fromJson(usage.readAll()).object().value(QStringLiteral("events")).toArray();
	QCOMPARE(rows.size(), m_scenario == 1 ? 0 : 1);
	if (m_scenario != 1) QCOMPARE(rows.first().toObject(), queuedRow);
	QFile control(storage + QStringLiteral("/control.json"));
	QVERIFY(control.open(QIODevice::ReadOnly));
	const auto retry = QJsonDocument::fromJson(control.readAll()).object().value(QStringLiteral("retry")).toObject();
	QCOMPARE(retry.value(QStringLiteral("failures")).toInt(), m_scenario == 0 ? 1 : 0);
	const auto next = QDateTime::fromString(retry.value(QStringLiteral("next")).toString(), Qt::ISODateWithMs);
	const qint64 delay = now.addMSecs(60001).msecsTo(next);
	if (m_scenario == 0) {
		QCOMPARE_GE(delay, qint64(30000));
		QCOMPARE_LE(delay, qint64(90000));
	} else
		QCOMPARE(delay, qint64(m_scenario == 1 ? 60000 : 86400000));
}

int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	TestTelemetryNetwork test(argc, argv);
	if (test.unsupportedTrust()) return 77;
	// Qt Test gets the arguments after the four of telemetry_tls_receiver.py, such as a slot name.
	std::vector<char*> testArguments{argv[0]};
	for (int i = argc >= 5 ? 5 : 1; i < argc; ++i) testArguments.push_back(argv[i]);
	return QTest::qExec(&test, int(testArguments.size()), testArguments.data());
}

#include "test_telemetrynetwork.moc"
