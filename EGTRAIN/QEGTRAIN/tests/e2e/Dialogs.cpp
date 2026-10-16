#include "tests/e2e/MainWindowTestAccess.h"

#include "app/MainWindowTestHooks.h"
#include "widgets/CaseChooserDialog.h"
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFont>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QList>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPoint>
#include <QPushButton>
#include <QRect>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QToolButton>
#include <QVariantList>
#include <QVariantMap>
#include <cstdio>
#include <cstring>

namespace {

// What the chooser reports in E2E_STARTUP_CHOOSER_CONTRACT: the rows and the menus when it opens, the action that the
// script takes, and what the window has afterwards.
QVariantMap chooserContract;
QString chooserSceneDirBefore;
quint64 chooserSceneRevisionBefore = 0;

// What the run review reports in E2E_RUN_REVIEW_RENDERED, and what the window held when the review opened.
QJsonObject reviewDiagnostic;
quint64 reviewRevisionBefore = 0;
SceneRunSelection reviewSelectionBefore;
bool reviewAdvanced = false;

void sendKey(QWidget* widget, int code) {
	QKeyEvent press(QEvent::KeyPress, code, Qt::NoModifier);
	QKeyEvent release(QEvent::KeyRelease, code, Qt::NoModifier);
	QCoreApplication::sendEvent(widget, &press);
	QCoreApplication::sendEvent(widget, &release);
}

void sendMouse(QWidget* widget, QEvent::Type type, const QPoint& point) {
	QMouseEvent event(type, QPointF(point), Qt::LeftButton,
		type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(widget, &event);
}

void printMarker(const char* name, const QString& value) {
	std::fprintf(stdout, "%s=%s\n", name, value.toUtf8().constData());
}

} // namespace

// The scripts of the chooser and of the run review run for QEGTRAIN_E2E_STARTUP_CHOOSER and QEGTRAIN_E2E_REVIEW_CANCEL, and
// the privacy dialog saves a picture of itself to QEGTRAIN_E2E_SCREENSHOT when its preview is on.
void MainWindowTestAccess::dialogShown(MainWindow& w, QDialog& dialog, const char* kind) {
	if (std::strcmp(kind, "startupChooser") == 0) {
		auto* chooser = dynamic_cast<CaseChooserDialog*>(&dialog);
		if (chooser && qEnvironmentVariableIsSet("QEGTRAIN_E2E_STARTUP_CHOOSER"))
			chooserShown(w, *chooser);
	} else if (std::strcmp(kind, "runReview") == 0) {
		if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_REVIEW_CANCEL"))
			reviewShown(w, dialog);
	} else if (std::strcmp(kind, "privacySettings") == 0) {
		const QString screenshot = qEnvironmentVariable("QEGTRAIN_E2E_SCREENSHOT");
		if (mainWindowTestHooks().privacyPreview && !screenshot.isEmpty())
			QTimer::singleShot(200, &dialog, [&dialog, screenshot] { dialog.grab().save(screenshot); });
	}
}

void MainWindowTestAccess::dialogClosed(MainWindow& w, QDialog& dialog, const char* kind) {
	if (std::strcmp(kind, "startupChooser") == 0) {
		auto* chooser = dynamic_cast<CaseChooserDialog*>(&dialog);
		if (chooser && qEnvironmentVariableIsSet("QEGTRAIN_E2E_STARTUP_CHOOSER"))
			chooserClosed(w, *chooser);
	} else if (std::strcmp(kind, "runReview") == 0) {
		if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_REVIEW_CANCEL"))
			reviewClosed(w, dialog);
	}
}

// Records what the chooser holds and then takes the action that QEGTRAIN_E2E_CHOOSER_INTERACTION names on the row of
// QEGTRAIN_E2E_CHOOSER_PATH: select, keyboard, doubleclick or open, and Continue for anything else.
void MainWindowTestAccess::chooserShown(MainWindow& w, CaseChooserDialog& dialog) {
	QListWidget* list = dialog.list();
	QPushButton* openBtn = dialog.openButton();
	QPushButton* continueBtn = dialog.continueButton();
	chooserSceneDirBefore = w.m_sceneDir;
	chooserSceneRevisionBefore = w.m_sceneRevision;
	chooserContract = QVariantMap();
	QVariantList rows;
	for (int row = 0; row < list->count(); ++row) {
		const auto* item = list->item(row);
		rows.append(QVariantMap{{"title", item->text()}, {"path", item->data(Qt::UserRole)},
			{"enabled", bool(item->flags() & Qt::ItemIsEnabled)},
			{"selectable", bool(item->flags() & Qt::ItemIsSelectable)}, {"tooltip", item->toolTip()}});
	}
	chooserContract.insert("rows", rows);
	chooserContract.insert("initialPath", dialog.selectedPath());
	chooserContract.insert("loadedBefore", chooserSceneDirBefore);
	chooserContract.insert("defaultOpen", openBtn->isDefault());
	QStringList menuTexts;
	for (const auto* action : dialog.otherMenu()->actions()) menuTexts.append(action->text());
	chooserContract.insert("otherActions", menuTexts);
	QVariantList recentActions;
	if (w.m_recentScenesMenu) {
		for (const auto* action : w.m_recentScenesMenu->actions())
			recentActions.append(QVariantMap{{"title", action->text()}, {"path", action->data()},
				{"enabled", action->isEnabled()}, {"tooltip", action->toolTip()}});
	}
	chooserContract.insert("recentActions", recentActions);
	QTimer::singleShot(0, &dialog, [&dialog, list, openBtn, continueBtn]() {
		const QString interaction = qEnvironmentVariable("QEGTRAIN_E2E_CHOOSER_INTERACTION");
		const QString target = qEnvironmentVariable("QEGTRAIN_E2E_CHOOSER_PATH");
		QListWidgetItem* targetItem = nullptr;
		for (int row = 0; row < list->count(); ++row)
			if (list->item(row)->data(Qt::UserRole).toString() == target && !target.isEmpty())
				targetItem = list->item(row);
		if (targetItem) {
			list->setFocus();
			if (interaction == "keyboard") {
				sendKey(list, Qt::Key_Home);
				for (int step = 0; step < list->count() && list->currentItem() != targetItem; ++step)
					sendKey(list, Qt::Key_Down);
			} else {
				list->scrollToItem(targetItem);
				const QPoint point = list->visualItemRect(targetItem).center();
				sendMouse(list->viewport(), QEvent::MouseButtonPress, point);
				sendMouse(list->viewport(), QEvent::MouseButtonRelease, point);
			}
		}
		chooserContract.insert("selectedPath", dialog.selectedPath());
		chooserContract.insert("openEnabled", openBtn->isEnabled());
		chooserContract.insert("interaction", interaction);
		if (interaction == "keyboard") {
			sendKey(list, Qt::Key_Return);
		} else if (interaction == "doubleclick" && targetItem) {
			const QPoint point = list->visualItemRect(targetItem).center();
			sendMouse(list->viewport(), QEvent::MouseButtonDblClick, point);
			sendMouse(list->viewport(), QEvent::MouseButtonRelease, point);
		} else if (interaction == "open") {
			sendMouse(openBtn, QEvent::MouseButtonPress, openBtn->rect().center());
			sendMouse(openBtn, QEvent::MouseButtonRelease, openBtn->rect().center());
		} else {
			continueBtn->click();
		}
	});
	// A failed event dispatch must fail the contract rather than hang the subprocess.
	chooserContract.insert("timedOut", false);
	QTimer::singleShot(5000, &dialog, [&dialog]() {
		chooserContract.insert("timedOut", true);
		dialog.reject();
	});
}

// Prints the contract and what the window shows after the chooser, and ends the run.
void MainWindowTestAccess::chooserClosed(MainWindow& w, CaseChooserDialog& dialog) {
	QListWidget* list = dialog.list();
	chooserContract.insert("chosenAction",
		dialog.outcome() == CaseChooserDialog::Outcome::OpenSelected ? "OpenSelected" : "ContinueCurrent");
	chooserContract.insert("chosenPath", dialog.chosenPath());
	chooserContract.insert("accepted", dialog.result() == QDialog::Accepted);
	chooserContract.insert("loadedAfter", w.m_sceneDir);
	chooserContract.insert("loaded", w.m_sceneLoaded);
	printMarker("E2E_STARTUP_CHOOSER_CONTRACT", QString::fromUtf8(QJsonDocument::fromVariant(chooserContract).toJson(QJsonDocument::Compact)));
	printMarker("E2E_STARTUP_CHOOSER_PROMPT", dialog.findChild<QLabel*>("dialogContext")->text());
	printMarker("E2E_STARTUP_CHOOSER_ACTION", dialog.continueButton()->text());
	printMarker("E2E_STARTUP_CHOOSER_GROUP", list->item(0)->text());
	printMarker("E2E_STARTUP_CHOOSER_GROUP_SELECTABLE", list->item(0)->flags() & Qt::ItemIsEnabled ? "yes" : "no");
	printMarker("E2E_STARTUP_CHOOSER_FIRST", list->currentItem() ? list->currentItem()->text() : QString());
	printMarker("E2E_STARTUP_CHOOSER_OPEN_ENABLED", dialog.openButton()->isEnabled() ? "yes" : "no");
	printMarker("E2E_STARTUP_CHOOSER_MODEL", QString::fromStdString(w.m_sceneModel.name));
	printMarker("E2E_STARTUP_CHOOSER_CASE_LABEL", w.m_caseNameLabel ? w.m_caseNameLabel->text() : QString());
	printMarker("E2E_STARTUP_CHOOSER_READINESS", w.m_caseReadinessLabel ? w.m_caseReadinessLabel->text() : QString());
	printMarker("E2E_STARTUP_CHOOSER_STATUS", w.statusBar()->currentMessage());
	printMarker("E2E_STARTUP_CHOOSER_UNCHANGED",
		w.m_sceneLoaded && w.m_sceneDir == chooserSceneDirBefore && w.m_sceneRevision == chooserSceneRevisionBefore
			? QStringLiteral("yes")
			: QStringLiteral("no"));
	std::fflush(stdout);
	QCoreApplication::exit(0);
}

// Inspects the run review once it shows: the text, the geometry in the collapsed, expanded and scrolled states, and the focus chain.
// QEGTRAIN_E2E_REVIEW_ENLARGED enlarges the fonts first. The script then cancels the review with the Cancel button, or with
// Escape when QEGTRAIN_E2E_REVIEW_CANCEL is escape.
void MainWindowTestAccess::reviewShown(MainWindow& w, QDialog& review) {
	reviewDiagnostic = QJsonObject();
	reviewRevisionBefore = w.m_sceneRevision;
	reviewSelectionBefore = w.m_runSelection.selectedOccurrences(w.m_sceneModel, w.serviceOccurrenceDuration());
	reviewAdvanced = w.advancedDetailsEnabled();
	auto* body = review.findChild<QWidget*>("runReviewBody");
	auto* buttons = review.findChild<QDialogButtonBox*>("runReviewButtons");
	auto* runButton = review.findChild<QPushButton*>("runReviewRunButton");
	auto* details = review.findChild<QLabel*>("runReviewDetails");
	auto* detailsToggle = review.findChild<QToolButton*>("runReviewDetailsToggle");
	auto* status = review.findChild<QLabel*>("runReviewStatus");
	const QList<QLabel*> factLabels = review.findChildren<QLabel*>("runReviewFactLabel");
	const QList<QLabel*> factValues = review.findChildren<QLabel*>("runReviewFactValue");
	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_REVIEW_ENLARGED")) {
		QFont font = review.font();
		font.setPointSizeF(18);
		review.setFont(font);
		// Styled widgets have already resolved their fonts before this test seam.
		for (QWidget* widget : review.findChildren<QWidget*>()) {
			font = widget->font();
			font.setPointSizeF(widget->objectName() == "dialogHeading" ? 22.5 : 18);
			widget->setFont(font);
		}
		review.resize(qMin(560, review.maximumWidth()), qMin(420, review.maximumHeight()));
	}
	QTimer::singleShot(0, &review, [&review, body, buttons, runButton, details, detailsToggle, status, factLabels, factValues]() {
		// Modal show and disclosure changes post LayoutRequest events in Qt 5.
		const auto settle = []() {
			for (int i = 0; i < 3; ++i) {
				QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
				QApplication::processEvents();
			}
		};
		settle();
		auto* scroll = review.findChild<QScrollArea*>("dialogBodyScroll");
		auto* context = review.findChild<QLabel*>("dialogContext");
		QScreen* screen = QGuiApplication::screenAt(review.mapToGlobal(QPoint()));
		if (!screen)
			screen = QGuiApplication::primaryScreen();
		const QRect available = screen->availableGeometry();
		reviewDiagnostic.insert("availableScreen", QJsonArray{available.width(), available.height()});
		const auto rectangle = [&](QWidget* widget) {
			const QPoint origin = widget->mapTo(&review, QPoint());
			return QJsonArray{origin.x(), origin.y(), widget->width(), widget->height()};
		};
		const auto geometry = [&]() {
			settle();
			return QJsonObject{{"dialog", QJsonArray{0, 0, review.width(), review.height()}},
				{"maximum", QJsonArray{review.maximumWidth(), review.maximumHeight()}},
				{"scroll", rectangle(scroll)}, {"footer", rectangle(buttons)},
				{"run", rectangle(runButton)}, {"cancel", rectangle(buttons->button(QDialogButtonBox::Cancel))},
				{"scrollMaximum", scroll->verticalScrollBar()->maximum()},
				{"footerVisible", buttons->isVisible()}, {"fontPoints", review.font().pointSizeF()},
				{"controlFontPoints",
					QJsonArray{context->font().pointSizeF(), details->font().pointSizeF(), runButton->font().pointSizeF(),
						buttons->button(QDialogButtonBox::Cancel)->font().pointSizeF()}}};
		};
		reviewDiagnostic.insert("initialFocus", review.focusWidget() == runButton ? "run" : review.focusWidget() ? review.focusWidget()->objectName()
																												 : "none");
		reviewDiagnostic.insert("runDefault", runButton->isDefault());
		reviewDiagnostic.insert("cancelDefault", buttons->button(QDialogButtonBox::Cancel)->isDefault());
		reviewDiagnostic.insert("context", context->text());
		reviewDiagnostic.insert("plainContext", context->textFormat() == Qt::PlainText);
		reviewDiagnostic.insert("plainDetails", details->textFormat() == Qt::PlainText);
		reviewDiagnostic.insert("collapsedGeometry", geometry());
		const auto detailState = [&]() {
			return QJsonObject{{"checked", detailsToggle->isChecked()},
				{"visible", details->isVisible()},
				{"arrow", detailsToggle->arrowType() == Qt::DownArrow ? "down" : "right"}};
		};
		QJsonObject summary;
		for (int row = 0; row < factLabels.size(); ++row)
			summary.insert(factLabels[row]->text(), factValues[row]->text());
		reviewDiagnostic.insert("summary", summary);
		reviewDiagnostic.insert("status", status->text());
		reviewDiagnostic.insert("warning", status->property("warning").toBool());
		reviewDiagnostic.insert("runEnabled", runButton->isEnabled());
		reviewDiagnostic.insert("collapsed", detailState());
		detailsToggle->click();
		reviewDiagnostic.insert("expanded", detailState());
		reviewDiagnostic.insert("details", details->text());
		reviewDiagnostic.insert("expandedGeometry", geometry());
		scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
		reviewDiagnostic.insert("scrolledGeometry", geometry());

		// Exercise the real focus chain without activating Run or inspection actions.
		detailsToggle->setFocus();
		QWidget* lastBody = detailsToggle;
		QJsonArray bodyFocus;
		QWidget* firstFooter = nullptr;
		for (int i = 0; i < 12; ++i) {
			QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
			QApplication::sendEvent(review.focusWidget(), &tab);
			settle();
			QWidget* focused = review.focusWidget();
			if (focused && buttons->isAncestorOf(focused)) {
				firstFooter = focused;
				break;
			}
			if (!focused || !body->isAncestorOf(focused) || focused == detailsToggle)
				break;
			lastBody = focused;
			bodyFocus.append(focused->property("text").toString());
		}
		scroll->verticalScrollBar()->setValue(0);
		QJsonArray footerBacktab;
		if (firstFooter) {
			for (int i = 0; i <= buttons->buttons().size(); ++i) {
				QKeyEvent backtab(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
				QApplication::sendEvent(review.focusWidget(), &backtab);
				settle();
				QWidget* focused = review.focusWidget();
				if (!focused || !buttons->isAncestorOf(focused))
					break;
				footerBacktab.append(focused->property("text").toString());
			}
		}
		const QRect focusedRect(lastBody->mapTo(scroll->viewport(), QPoint()), lastBody->size());
		reviewDiagnostic.insert("keyboard",
			QJsonObject{{"bodyFocus", bodyFocus},
				{"tabReachedFooter", firstFooter != nullptr},
				{"backtabReturned", firstFooter && review.focusWidget() == lastBody},
				{"firstFooter", firstFooter == runButton ? "run" : "cancel"},
				{"footerBacktab", footerBacktab},
				{"backtabFocus",
					review.focusWidget()
						? QString::fromLatin1(review.focusWidget()->metaObject()->className()) + ":"
							+ review.focusWidget()->objectName() + ":" + review.focusWidget()->property("text").toString()
						: "none"},
				{"focusedBodyRect", QJsonArray{focusedRect.x(), focusedRect.y(), focusedRect.width(), focusedRect.height()}},
				{"viewport", QJsonArray{0, 0, scroll->viewport()->width(), scroll->viewport()->height()}},
				{"scrollValue", scroll->verticalScrollBar()->value()}});
		detailsToggle->click();
		reviewDiagnostic.insert("recollapsed", detailState());
		reviewDiagnostic.insert("recollapsedGeometry", geometry());
		const bool escape = qEnvironmentVariable("QEGTRAIN_E2E_REVIEW_CANCEL") == "escape";
		reviewDiagnostic.insert("cancellation", escape ? "escape" : "button");
		if (escape) {
			QKeyEvent key(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
			QApplication::sendEvent(&review, &key);
		} else {
			buttons->button(QDialogButtonBox::Cancel)->click();
		}
	});
}

// Prints what the review showed and whether the window is as it was before, and ends the run.
void MainWindowTestAccess::reviewClosed(MainWindow& w, QDialog& review) {
	const QList<QLabel*> factValues = review.findChildren<QLabel*>("runReviewFactValue");
	const QString details = review.findChild<QLabel*>("runReviewDetails")->text();
	const bool cancelled = review.result() == QDialog::Rejected;
	reviewDiagnostic.insert("rejected", cancelled);
	reviewDiagnostic.insert("workerStarted", w.m_worker != nullptr);
	std::fprintf(stdout, "E2E_RUN_REVIEW_RENDERED=%s\n", QJsonDocument(reviewDiagnostic).toJson(QJsonDocument::Compact).constData());
	std::fprintf(stdout, "E2E_RUN_REVIEW_SUMMARY=%s\n", factValues[0]->text().toUtf8().constData());
	std::fprintf(stdout, "E2E_RUN_REVIEW_START=%s\n", factValues[1]->text().toUtf8().constData());
	std::fprintf(stdout, "E2E_RUN_REVIEW_DURATION=%s\n", factValues[2]->text().toUtf8().constData());
	std::fprintf(stdout, "E2E_RUN_REVIEW_DETAILS=%d\n",
		details.contains("Service definitions:") && details.contains("Configured total:") && details.contains("Selected:")
				&& details.contains("Compositions:") && details.contains("Counting rule:")
			? 1
			: 0);
	std::fprintf(stdout, "E2E_RUN_REVIEW_ADVANCED=%d\n", reviewAdvanced ? 1 : 0);
	std::fprintf(stdout, "E2E_RUN_REVIEW_VALIDATION_VISIBLE=%d\n", details.contains("Validation:") ? 1 : 0);
	std::fprintf(stdout, "E2E_RUN_REVIEW_CANCELLED=%d\n", cancelled ? 1 : 0);
	std::fprintf(stdout, "E2E_RUN_REVIEW_UNCHANGED=%d\n",
		w.m_sceneRevision == reviewRevisionBefore && w.m_runSelection.selectedOccurrences(w.m_sceneModel, w.serviceOccurrenceDuration()) == reviewSelectionBefore ? 1 : 0);
	std::fflush(stdout);
	QCoreApplication::exit(0);
}
