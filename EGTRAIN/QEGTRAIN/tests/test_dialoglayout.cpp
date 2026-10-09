#include "widgets/DialogLayout.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace {
bool check(bool condition, const char* message) {
	if (!condition)
		std::cerr << message << '\n';
	return condition;
}

// Counts pixels in the leftmost 40 px of an item row that differ clearly from the row background,
// and reports the largest difference. Items have no text, so only the indicator is in that strip.
struct Indicator {
	int strongPixels = 0;
	int maxDifference = 0;
};

Indicator measureIndicator(const QImage& image, const QRect& row) {
	const int background = qGray(image.pixel(row.left() + 90, row.center().y()));
	Indicator result;
	for (int y = row.top(); y <= row.bottom(); ++y) {
		for (int x = row.left(); x < row.left() + 40; ++x) {
			const int difference = std::abs(qGray(image.pixel(x, y)) - background);
			result.maxDifference = std::max(result.maxDifference, difference);
			result.strongPixels += difference >= 90 ? 1 : 0;
		}
	}
	return result;
}

bool checkIndicators(const QImage& image, const QRect& unchecked, const QRect& alternate,
	const QRect& selected, const QRect& checked, const char* surface) {
	const Indicator plain = measureIndicator(image, unchecked);
	const Indicator striped = measureIndicator(image, alternate);
	const Indicator highlighted = measureIndicator(image, selected);
	const Indicator ticked = measureIndicator(image, checked);
	bool ok = check(plain.maxDifference >= 90, surface);
	ok &= check(striped.maxDifference >= 90, surface);
	ok &= check(highlighted.maxDifference >= 90, surface);
	ok &= check(ticked.strongPixels >= 2 * plain.strongPixels && plain.strongPixels > 0, surface);
	return ok;
}

bool exerciseItemViewIndicators() {
	const Qt::CheckState states[] = {Qt::Unchecked, Qt::Unchecked, Qt::Checked, Qt::Unchecked};
	QListWidget list;
	list.setAlternatingRowColors(true);
	list.resize(200, 160);
	QTableWidget table(4, 1);
	table.setAlternatingRowColors(true);
	table.setColumnWidth(0, 120);
	table.horizontalHeader()->hide();
	table.verticalHeader()->hide();
	table.resize(200, 160);
	for (int row = 0; row < 4; ++row) {
		auto* listItem = new QListWidgetItem(&list);
		listItem->setFlags(listItem->flags() | Qt::ItemIsUserCheckable);
		listItem->setCheckState(states[row]);
		auto* tableItem = new QTableWidgetItem;
		tableItem->setFlags(tableItem->flags() | Qt::ItemIsUserCheckable);
		tableItem->setCheckState(states[row]);
		table.setItem(row, 0, tableItem);
	}
	list.setCurrentRow(3);
	table.setCurrentCell(3, 0);
	list.show();
	table.show();
	QApplication::processEvents();

	const QImage listImage = list.viewport()->grab().toImage();
	const QImage tableImage = table.viewport()->grab().toImage();
	auto listRow = [&](int row) { return list.visualItemRect(list.item(row)); };
	auto tableRow = [&](int row) { return table.visualItemRect(table.item(row, 0)); };
	bool ok = checkIndicators(listImage, listRow(0), listRow(1), listRow(3), listRow(2),
		"list item indicators are not clearly visible");
	ok &= checkIndicators(tableImage, tableRow(0), tableRow(1), tableRow(3), tableRow(2),
		"table item indicators are not clearly visible");
	return ok;
}

bool exercise(const QRect& screen, qreal scale) {
	QDialog dialog;
	QFont font = dialog.font();
	font.setPointSizeF(12 * scale);
	dialog.setFont(font);
	auto* body = new QWidget;
	auto* fields = new QVBoxLayout(body);
	for (int i = 0; i < 80; ++i) {
		auto* label = new QLabel(QStringLiteral("Service %1: Copenhagen to a long destination with a long platform name")
									 .arg(i),
			body);
		label->setWordWrap(true);
		fields->addWidget(label);
	}
	auto* lastField = new QLineEdit(body);
	lastField->setPlaceholderText(QStringLiteral("Last field"));
	fields->addWidget(lastField);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	buttons->button(QDialogButtonBox::Ok)->setDefault(true);
	const QString longName(2500, QLatin1Char('X'));
	auto* scroll = DialogLayout::install(dialog, QStringLiteral("Review <simulation> & ") + longName.left(120),
		QStringLiteral("Case <identifier>: ") + longName,
		body, buttons, screen);
	dialog.show();
	QApplication::processEvents();
	bool ok = check(dialog.width() <= screen.width() * 9 / 10 && dialog.height() <= screen.height() * 4 / 5,
		"dialog exceeds available screen budget");
	ok &= check(scroll->verticalScrollBar()->maximum() > 0,
		"long content does not scroll");
	scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
	ok &= check(buttons->isVisible() && buttons->geometry().bottom() <= dialog.height() && scroll->geometry().bottom() <= buttons->geometry().top(),
		"footer must remain reachable outside the scrolling body");
	auto* heading = dialog.findChild<QLabel*>(QStringLiteral("dialogHeading"));
	auto* context = dialog.findChild<QLabel*>(QStringLiteral("dialogContext"));
	ok &= check(
		heading && heading->font().pointSizeF() > font.pointSizeF() && heading->textFormat() == Qt::PlainText && context
			&& context->textFormat() == Qt::PlainText && context->parentWidget() == scroll->widget(),
		"heading scales, authored labels remain plain and context scrolls");
	buttons->button(QDialogButtonBox::Ok)->setFocus();
	scroll->verticalScrollBar()->setValue(0);
	QKeyEvent backtab(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
	QApplication::sendEvent(buttons->button(QDialogButtonBox::Ok), &backtab);
	QApplication::processEvents();
	const QRect fieldRect(lastField->mapTo(scroll->viewport(), QPoint()), lastField->size());
	ok &= check(lastField->hasFocus() && scroll->viewport()->rect().contains(fieldRect),
		"Backtab from footer must reveal the focused body field");
	QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
	QApplication::sendEvent(lastField, &tab);
	ok &= check(buttons->button(QDialogButtonBox::Ok)->hasFocus() || buttons->button(QDialogButtonBox::Cancel)->hasFocus(),
		"Tab from last body field reaches fixed footer");
	buttons->button(QDialogButtonBox::Ok)->setFocus();
	QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
	QApplication::sendEvent(buttons->button(QDialogButtonBox::Ok), &enter);
	ok &= check(dialog.result() == QDialog::Accepted, "default Enter must accept");

	QDialog cancelDialog;
	auto* cancelBody = new QWidget;
	cancelBody->setLayout(new QVBoxLayout);
	auto* cancelButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	QObject::connect(cancelButtons, &QDialogButtonBox::accepted, &cancelDialog, &QDialog::accept);
	QObject::connect(cancelButtons, &QDialogButtonBox::rejected, &cancelDialog, &QDialog::reject);
	DialogLayout::install(cancelDialog, QStringLiteral("Choose case"), QString(),
		cancelBody, cancelButtons, screen);
	cancelDialog.show();
	QApplication::processEvents();
	QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
	QApplication::sendEvent(&cancelDialog, &escape);
	ok &= check(cancelDialog.result() == QDialog::Rejected && !cancelDialog.isVisible(),
		"Escape must reject without accepting");
	return ok;
}
} // namespace

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	QFile qss(QStringLiteral(EGTRAIN_DIALOG_QSS));
	if (!qss.open(QIODevice::ReadOnly)) {
		std::cerr << "cannot load application QSS\n";
		return 1;
	}
	// The test binary has no Qt resources, so point the stylesheet at the icon files.
	QString styleSheet = QString::fromUtf8(qss.readAll());
	styleSheet.replace(QStringLiteral(":/icons/"),
		QFileInfo(qss).absolutePath() + QStringLiteral("/../icons/"));
	app.setStyleSheet(styleSheet);
	const bool small = exercise(QRect(0, 0, 1280, 800), 1.0);
	const bool scaledSmall = exercise(QRect(0, 0, 1280, 800), 1.5);
	const bool large = exercise(QRect(0, 0, 1920, 1080), 1.5);
	const bool indicators = exerciseItemViewIndicators();
	return small && scaledSmall && large && indicators ? 0 : 1;
}
