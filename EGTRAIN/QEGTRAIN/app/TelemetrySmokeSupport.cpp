#include "app/TelemetrySmokeSupport.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>
#include <QAbstractButton>
#include <QLineEdit>
#include <QFileInfo>
#include <new>

namespace telemetry_smoke {
namespace {
class ScriptedReply final : public QNetworkReply {
public:
	ScriptedReply(const QNetworkRequest& request, QObject* owner, int code) : QNetworkReply(owner) {
		setRequest(request);
		setUrl(request.url());
		open(QIODevice::ReadOnly);
		QTimer::singleShot(0, this, [this, code] {
			if (isFinished()) return;
			if (code) setAttribute(QNetworkRequest::HttpStatusCodeAttribute, code);
			else setError(QNetworkReply::ConnectionRefusedError, QString());
			setFinished(true);
			emit finished();
		});
	}
	void abort() override {
		if (isFinished()) return;
		setError(QNetworkReply::OperationCanceledError, QString());
		setFinished(true);
		emit finished();
	}
	qint64 readData(char*, qint64) override { return -1; }
};
}
telemetry::TelemetrySender::TestOptions options(const QString& root, const QString& mode,
	std::shared_ptr<State> state) {
	telemetry::TelemetrySender::TestOptions result;
	const QString settingsFile = QDir(root).filePath("consent.ini");
	result.settingsFactory = [settingsFile] {
		return std::make_unique<QSettings>(settingsFile, QSettings::IniFormat);
	};
	auto timer = std::make_shared<QElapsedTimer>();
	timer->start();
	result.monotonicMs = [timer] { return timer->elapsed() * 100; };
	const QDateTime base = QDateTime::currentDateTimeUtc();
	result.utcNow = [base, timer] { return base.addMSecs(timer->elapsed() * 100); };
	result.jitterPercent = [] { return 100; };
	result.afterPoll = [state] { ++state->polls; };
	result.requestTimeoutMs = 1500;
	result.post = [root, mode, state](QNetworkAccessManager& manager,
					  const QNetworkRequest& request, const QByteArray& body) {
		const int number = ++state->posts;
		if (body.contains("\"simulation.started\"")) ++state->startedPosts;
		// Bounded, private test output. This transport is scripted, not TLS.
		if (number > 100 || body.size() > 65536 || request.url() != QUrl("https://127.0.0.1:19497/collect"))
			return static_cast<QNetworkReply*>(nullptr);
		QDir directory(root);
		directory.mkpath("payloads");
		QFile file(directory.filePath(QString("payloads/%1.json").arg(number)));
		if (file.open(QIODevice::WriteOnly)) file.write(body);
		const int code = mode == "offline" ? 0 : mode == "retired" ? 410
																   : 202;
		return static_cast<QNetworkReply*>(new ScriptedReply(request, &manager, code));
	};
	return result;
}
void chooseFile(const QString& path, std::function<bool()> ready, std::function<void()> selected) {
	auto* timer = new QTimer(qApp);
	timer->setInterval(10);
	QObject::connect(timer, &QTimer::timeout, timer, [timer, path, ready, selected] {
		auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
		if (!dialog || (ready && !ready())) return;
		timer->stop();
		timer->deleteLater();
		if (path.isEmpty()) dialog->reject();
		else {
			dialog->setDirectory(QFileInfo(path).absolutePath());
			dialog->selectFile(QFileInfo(path).fileName());
			if (auto* edit = dialog->findChild<QLineEdit*>("fileNameEdit"))
				edit->setText(QFileInfo(path).fileName());
			if (selected) QObject::connect(dialog, &QFileDialog::fileSelected, dialog,
				[selected](const QString&) { selected(); });
			QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
		}
	});
	timer->start();
}
void chooseDirectory(const QString& path, const QString& title, std::function<bool()> ready) {
	auto* timer = new QTimer(qApp);
	timer->setInterval(10);
	QObject::connect(timer, &QTimer::timeout, timer, [timer, path, title, ready] {
		auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
		if (!dialog || dialog->windowTitle() != title || (ready && !ready())) return;
		timer->stop();
		timer->deleteLater();
		dialog->setDirectory(path);
		if (auto* edit = dialog->findChild<QLineEdit*>("fileNameEdit")) edit->setText(path);
		QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
	});
	timer->start();
}
void failObservationInstallation(State* state, int point) {
	if (state && state->installFailurePoint == point) {
		++state->installFailures;
		throw std::bad_alloc();
	}
}
bool recordRun(const QString& root, int ordinal, const std::string& csv, const QString& energyPath) {
	if (ordinal < 0 || ordinal > 4) return false;
	QFile file(QDir(root).filePath(QString("install_run_%1.csv").arg(ordinal)));
	const QByteArray bytes = QByteArray::fromStdString(csv);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size()
		&& QFile::copy(energyPath, QDir(root).filePath(QString("install_run_%1.energy").arg(ordinal)));
}
void dismissMessages() {
	auto* timer = new QTimer(qApp);
	timer->setInterval(10);
	QObject::connect(timer, &QTimer::timeout, timer, [] {
		if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
			for (auto standard : {QMessageBox::Yes, QMessageBox::Save, QMessageBox::Ok}) {
				if (auto* button = box->button(standard)) {
					button->click();
					return;
				}
			}
			box->accept();
		}
	});
	timer->start();
}
}
