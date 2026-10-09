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
#include <QThread>
#include <atomic>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>

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

int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	assert(argc == 5 && QSslSocket::supportsSsl());
	const int scenario = QByteArray(argv[3]).toInt(); // 0 untrusted 202; 1 trusted 202; 2 trusted 302.
	assert(scenario >= 0 && scenario <= 2);
	QFile certFile(QString::fromLocal8Bit(argv[1]));
	assert(certFile.open(QIODevice::ReadOnly));
	const QSslCertificate certificate(certFile.readAll(), QSsl::Pem);
	QFile authorityFile(QString::fromLocal8Bit(argv[4]));
	assert(authorityFile.open(QIODevice::ReadOnly));
	const QSslCertificate authority(authorityFile.readAll(), QSsl::Pem);
	assert(!certificate.isNull() && !authority.isNull());
	const QSslConfiguration defaultTrust = QSslConfiguration::defaultConfiguration();
	QSslConfiguration trust = defaultTrust;
	if (scenario) {
		auto authorities = trust.caCertificates();
		authorities.append(authority);
		trust.setCaCertificates(authorities);
		trust.setPeerVerifyMode(QSslSocket::VerifyPeer);
	}
	assert(QSslConfiguration::defaultConfiguration() == defaultTrust);
	QTemporaryDir directory;
	assert(directory.isValid());
	const QString settingsFile = directory.filePath(QStringLiteral("consent.ini"));
	const QString storage = directory.filePath(QStringLiteral("queue"));
	TelemetryContext context{true, true, true,
		QStringLiteral("https://127.0.0.1:%1/collect").arg(QString::fromLocal8Bit(argv[2])), QStringLiteral("1")};
	{
		QNetworkAccessManager preflight;
		QEventLoop loop;
		QTimer timer;
		timer.setSingleShot(true);
		QNetworkRequest probeRequest(QUrl(context.endpoint));
		probeRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
			QNetworkRequest::ManualRedirectPolicy);
		if (scenario) probeRequest.setSslConfiguration(trust);
		QNetworkReply* probe = preflight.post(probeRequest, QByteArray());
		bool rejectedCertificate = false;
		QStringList errorsSeen;
		QObject::connect(probe, &QNetworkReply::sslErrors, &loop,
			[&](const QList<QSslError>& errors) {
				for (const QSslError& error : errors) {
					errorsSeen.append(error.errorString());
					if (error.error() == QSslError::SelfSignedCertificate
						|| error.error() == QSslError::CertificateUntrusted
						|| error.error() == QSslError::UnableToGetLocalIssuerCertificate)
						rejectedCertificate = true;
				}
			});
		QObject::connect(probe, &QNetworkReply::finished, &loop, &QEventLoop::quit);
		QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
		timer.start(5000);
		loop.exec();
		if (scenario == 0) assert(rejectedCertificate && probe->error() == QNetworkReply::SslHandshakeFailedError);
		else if (probe->error() != QNetworkReply::NoError) {
			if (QSslSocket::sslLibraryVersionString().startsWith(QStringLiteral("Secure Transport"))
				&& errorsSeen.size() == 1
				&& errorsSeen.first() == QStringLiteral("The root CA certificate is not trusted for this purpose")) {
				fprintf(stderr, "UNSUPPORTED: Qt5 Secure Transport rejects request-local test CA: %s\n",
					qPrintable(errorsSeen.first()));
				return 77;
			}
			qFatal("trusted probe error %d: %s", probe->error(), qPrintable(errorsSeen.join(';')));
		}
		probe->deleteLater();
	}
	QSettings settings(settingsFile, QSettings::IniFormat);
	TelemetryConsent consent(settings, context);
	assert(consent.save(true, false));
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
	if (scenario) options.loopbackTrust = trust;
	options.onWorkerExit = [&] { exited.release(); };
	telemetry::Application metadata{QStringLiteral("1.0.0"), {}, {}};
	telemetry::TelemetrySender sender(context, metadata, storage, options); // Real QNAM: post unset.
	// A published usage permit is not a completed initial category-observation poll.
	// Wait for startup and durable queueing before making this a transport test.
	assert(until(app, [&] { return polled.available() > 0; }));
	assert(until(app, [&] { return sender.tryEnqueue({telemetry::Name::SessionStarted}); }));
	const QString usagePath = storage + QStringLiteral("/usage.json");
	QJsonObject queuedRow;
	assert(until(app, [&] {
		QFile usage(usagePath);
		if (!usage.open(QIODevice::ReadOnly)) return false;
		const auto doc = QJsonDocument::fromJson(usage.readAll());
		assert(doc.isObject());
		const auto rows = doc.object().value(QStringLiteral("events")).toArray();
		if (rows.isEmpty()) return false;
		assert(rows.size() == 1);
		queuedRow = rows.first().toObject();
		return true;
	}));
	const auto queuedEvent = queuedRow.value(QStringLiteral("event")).toObject();
	telemetry::TelemetryEvent parsed;
	assert(telemetry::readEvent(queuedEvent, telemetry::Category::Usage, &parsed));
	fprintf(stdout, "TLS_TEST_QUEUED_EVENT=%s\n", QJsonDocument(queuedEvent).toJson(QJsonDocument::Compact).constData());
	clock = 60001;
	assert(until(app, [&] { return completed.available() > 0; }, 12000));
	assert(reserved.available() == 1 && completed.available() == 1);
	assert(QSslConfiguration::defaultConfiguration() == defaultTrust);
	sender.stop();
	assert(until(app, [&] { return exited.available() > 0; }));
	QFile usage(usagePath);
	assert(usage.open(QIODevice::ReadOnly));
	const auto rows = QJsonDocument::fromJson(usage.readAll()).object().value(QStringLiteral("events")).toArray();
	assert(rows.size() == (scenario == 1 ? 0 : 1));
	if (scenario != 1) assert(rows.first().toObject() == queuedRow);
	QFile control(storage + QStringLiteral("/control.json"));
	assert(control.open(QIODevice::ReadOnly));
	const auto retry = QJsonDocument::fromJson(control.readAll()).object().value(QStringLiteral("retry")).toObject();
	assert(retry.value(QStringLiteral("failures")).toInt() == (scenario == 0 ? 1 : 0));
	const auto next = QDateTime::fromString(retry.value(QStringLiteral("next")).toString(), Qt::ISODateWithMs);
	const qint64 delay = now.addMSecs(60001).msecsTo(next);
	assert(scenario == 0 ? delay >= 30000 && delay <= 90000 : delay == (scenario == 1 ? 60000 : 86400000));
	return 0;
}
