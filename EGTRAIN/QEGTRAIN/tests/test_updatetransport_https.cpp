#include "update/HttpTransport.h"
#include "update/ReleaseInfo.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <iostream>
#include <optional>

// One real GET of the GitHub release list through the transport that the application uses, read with the
// parser of the update check. It tells a connection that cannot be made from an answer of the server: a
// reply with a status code reached the server, so the TLS handshake and the certificate check worked,
// whatever the status is.
//
//   test_updatetransport_https [URL]
//
// A test passes the address of a server on 127.0.0.1 as URL, which the Windows transport fetches only for
// a test. The output starts with PASS, SKIP or FAIL, and the exit code is
//   0   PASS: the release list arrived and was read
//   77  SKIP: the server answered, but not with a release list, because of its rate limit (403, 429) or an
//       outage (5xx). CTest shows a test with this code as skipped.
//   1   FAIL: no response arrived, or the answer is not a release list

namespace {

constexpr int kExitPassed = 0;
constexpr int kExitFailed = 1;
constexpr int kExitSkipped = 77;
constexpr int kTimeoutMs = 45000;

struct Outcome {
	bool timedOut = false;
	bool failed = false;
	int status = 0;
	QString error;
	QByteArray body;
};

struct Verdict {
	int exitCode;
	QString text;
};

Verdict pass(const QString& text) {
	return {kExitPassed, QStringLiteral("PASS ") + text};
}

Verdict skip(const QString& text) {
	return {kExitSkipped, QStringLiteral("SKIP ") + text};
}

Verdict fail(const QString& text) {
	return {kExitFailed, QStringLiteral("FAIL ") + text};
}

Verdict judge(const Outcome& outcome) {
	if (outcome.timedOut)
		return fail(QStringLiteral("The request did not finish within %1 s.").arg(kTimeoutMs / 1000));
	if (outcome.status == 0)
		return fail(QStringLiteral("No HTTP response arrived, so the connection, the TLS handshake or the certificate check failed: %1")
				.arg(outcome.error));

	const QString answered = QStringLiteral("The server answered HTTP %1 over HTTPS").arg(outcome.status);
	if (outcome.status == 403 || outcome.status == 429)
		return skip(answered + QStringLiteral(", probably its rate limit. No release list was read."));
	if (outcome.status >= 500)
		return skip(answered + QStringLiteral(", probably an outage. No release list was read."));
	if (outcome.failed || outcome.status < 200 || outcome.status >= 300)
		return fail(answered + QStringLiteral(", not a release list. This is not a TLS failure: %1").arg(outcome.error));

	QString parseError;
	const std::optional<StableRelease> release = parseLatestStableRelease(outcome.body, &parseError);
	if (release)
		return pass(answered + QStringLiteral(" and the newest stable release is %1.").arg(release->tag));
	// The list holds only the newest release, which may be a prerelease. The answer still came over HTTPS.
	if (QJsonDocument::fromJson(outcome.body).isArray())
		return pass(answered + QStringLiteral(" with a release list that has no stable release: %1").arg(parseError));
	return fail(answered + QStringLiteral(", but the body is not a release list: %1").arg(parseError));
}

} // namespace

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);
	const QStringList arguments = application.arguments();
	const bool loopback = arguments.size() > 1;

	HttpRequest request;
	request.url = loopback ? QUrl(arguments.at(1)) : QUrl(QStringLiteral("https://api.github.com/repos/Ancientkingg/EGTRAIN/releases?per_page=1"));
	request.userAgent = QByteArrayLiteral("EGTRAIN-https-probe");
	request.accept = QByteArrayLiteral("application/vnd.github+json");
	HttpTransport* transport = createHttpTransport(&application, loopback);
	HttpReply* reply = transport->get(request);

	Outcome outcome;
	QObject::connect(reply, &HttpReply::finished, &application, &QCoreApplication::quit);
	QTimer::singleShot(kTimeoutMs, reply, [&outcome, reply]() {
		outcome.timedOut = true;
		reply->abort();
	});
	application.exec();

	outcome.failed = reply->failed();
	outcome.status = reply->statusCode();
	outcome.error = reply->errorString();
	if (!outcome.failed)
		outcome.body = reply->readAll();
	const Verdict verdict = judge(outcome);
	std::cout << verdict.text.toUtf8().constData() << std::endl;
	return verdict.exitCode;
}
