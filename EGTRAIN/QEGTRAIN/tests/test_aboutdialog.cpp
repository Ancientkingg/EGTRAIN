#include "widgets/AboutDialog.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QProxyStyle>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QTest>
#include <QUrl>

class UrlReceiver : public QObject {
	Q_OBJECT
public:
	QList<QUrl> opened;
public slots:
	void receive(const QUrl& url) { opened.append(url); }
};

// The order of the footer and the focus policy of its buttons on macOS, where the footer buttons are not auto default.
class MacFooterStyle : public QProxyStyle {
public:
	MacFooterStyle() : QProxyStyle(QStringLiteral("Fusion")) {}
	int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData) const override {
		if (hint == SH_DialogButtonLayout)
			return QDialogButtonBox::MacLayout;
		if (hint == SH_Button_FocusPolicy)
			return Qt::TabFocus;
		return QProxyStyle::styleHint(hint, option, widget, returnData);
	}
};

namespace {
const char* const kNames[] = {"aboutResearchProfile", "aboutOriginalResearch", "aboutWebsite",
	"aboutGitHub", "aboutSourceCode", "aboutReportIssue"};
const char* const kAddresses[] = {"https://orcid.org/0000-0002-7936-5832",
	"https://doi.org/10.6092/unina/fedoa/8599",
	"https://samuelbruin.com/", "https://github.com/Ancientkingg",
	"https://github.com/Ancientkingg/EGTRAIN",
	"https://github.com/Ancientkingg/EGTRAIN/issues"};
} // namespace

// About shows the version, the authors and the links, opens a link only when it is activated, and closes with Escape,
// Close and Return. The https links go to a receiver instead of a browser.
class TestAboutDialog : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void init();
	void cleanupTestCase();
	void showsContentAndOpensLinks_data();
	void showsContentAndOpensLinks();
	void closeIsDefaultAndFocused();
	void macFooterKeepsCloseDefault();
	void browserFailureShowsAddress();

private:
	UrlReceiver m_receiver;
};

void TestAboutDialog::initTestCase() {
	QCoreApplication::setApplicationVersion(QStringLiteral("9.8.7-test"));
	QDesktopServices::setUrlHandler(QStringLiteral("https"), &m_receiver, "receive");
}

void TestAboutDialog::init() {
	m_receiver.opened.clear();
}

void TestAboutDialog::cleanupTestCase() {
	QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
}

void TestAboutDialog::showsContentAndOpensLinks_data() {
	QTest::addColumn<qreal>("scale");
	QTest::newRow("1.0") << qreal(1.0);
	QTest::newRow("1.5") << qreal(1.5);
}

void TestAboutDialog::showsContentAndOpensLinks() {
	QFETCH(qreal, scale);
	AboutDialog dialog;
	QFont font = dialog.font();
	font.setPointSizeF(12 * scale);
	dialog.setFont(font);
	dialog.show();
	QApplication::processEvents();
	// The dialog has its title.
	QCOMPARE(dialog.windowTitle(), QStringLiteral("About EGTRAIN"));
	QVERIFY2(m_receiver.opened.isEmpty(), "opening About launched a browser");
	const auto labels = dialog.findChildren<QLabel*>();
	QString text;
	for (const auto* label : labels)
		text += label->text() + QLatin1Char('\n');
	// About shows its content and the application version.
	QVERIFY(text.contains(QStringLiteral("9.8.7-test")));
	QVERIFY(text.contains(QStringLiteral("Dr.ir. Egidio Quaglietta")));
	QVERIFY(text.contains(QStringLiteral("Original EGTRAIN model and research.")));
	QVERIFY(text.contains(QStringLiteral("Samuel Bruin")));
	QVERIFY(text.contains(QStringLiteral("Continued desktop application development and maintenance.")));
	QVERIFY(text.contains(QStringLiteral("Microscopic railway simulation for students and researchers.")));
	auto* context = dialog.findChild<QLabel*>(QStringLiteral("dialogContext"));
	// The version and the description are selectable.
	if (context == nullptr)
		QFAIL("version and description must be selectable");
	QVERIFY(context->textInteractionFlags().testFlag(Qt::TextSelectableByKeyboard));
	auto* scroll = dialog.findChild<QScrollArea*>(QStringLiteral("dialogBodyScroll"));
	auto* footer = dialog.findChild<QDialogButtonBox*>();
	// The bounded dialog retains its Close footer.
	QCOMPARE_LE(dialog.width(), 1152);
	QCOMPARE_LE(dialog.height(), 640);
	if (scroll == nullptr || footer == nullptr)
		QFAIL("bounded dialog must retain its Close footer");
	QVERIFY(footer->isVisible());
	QCOMPARE_GE(footer->geometry().top(), scroll->geometry().bottom());
	for (int i = 0; i < 6; ++i) {
		auto* link = dialog.findChild<QPushButton*>(QString::fromLatin1(kNames[i]));
		// The link has its address as tooltip and description.
		if (link == nullptr)
			QFAIL("wrong link address");
		QCOMPARE(link->toolTip(), QString::fromLatin1(kAddresses[i]));
		QCOMPARE(link->accessibleDescription(), link->toolTip());
		link->setFocus();
		QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
		QApplication::sendEvent(link, &enter);
		// Keyboard activation opens only the selected link.
		QCOMPARE(m_receiver.opened.size(), i + 1);
		QCOMPARE(m_receiver.opened.last(), QUrl(QString::fromLatin1(kAddresses[i])));
		QVERIFY(dialog.isVisible());
	}
	m_receiver.opened.clear();
	QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
	QApplication::sendEvent(&dialog, &escape);
	// Escape rejects About.
	QVERIFY(!dialog.isVisible());
	QCOMPARE(dialog.result(), int(QDialog::Rejected));
	dialog.show();
	footer->button(QDialogButtonBox::Close)->click();
	QVERIFY2(!dialog.isVisible(), "Close button must dismiss About");
}

void TestAboutDialog::closeIsDefaultAndFocused() {
	AboutDialog dialog;
	dialog.show();
	QApplication::processEvents();
	auto* close = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Close);
	// Close is the default and focused button.
	if (close == nullptr)
		QFAIL("Close must be the default and focused button");
	QCOMPARE(dialog.focusWidget(), close);
	QVERIFY(close->isDefault());
	if (QWidget* focus = dialog.focusWidget()) {
		QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
		QApplication::sendEvent(focus, &enter);
	}
	// Enter closes About without opening a link.
	QVERIFY(!dialog.isVisible());
	QCOMPARE(dialog.result(), int(QDialog::Rejected));
	QVERIFY(m_receiver.opened.isEmpty());
}

// With the footer of macOS Close is the default when About opens. A link takes the default while it has the focus, and
// once the focus leaves it for a widget that is not a button Close is the default again, so Return closes About. The
// application style stays changed for the checks after this one.
void TestAboutDialog::macFooterKeepsCloseDefault() {
	QApplication::setStyle(new MacFooterStyle);
	AboutDialog dialog;
	dialog.show();
	dialog.activateWindow();
	QApplication::processEvents();
	auto* close = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Close);
	QVERIFY2(close->isDefault(), "Close is not the default when About opens with the footer of macOS");
	if (QWidget* focus = dialog.focusWidget()) {
		QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
		QApplication::sendEvent(focus, &tab);
	}
	// Tab from Close reaches a link.
	const QWidget* focused = dialog.focusWidget();
	if (focused == nullptr)
		QFAIL("Tab from Close does not reach a link");
	QVERIFY(focused->property("aboutLink").toBool());
	auto* context = dialog.findChild<QLabel*>(QStringLiteral("dialogContext"));
	context->setFocus(Qt::MouseFocusReason);
	// Close is the default after the focus left a link for the text.
	QVERIFY(context->hasFocus());
	QVERIFY(close->isDefault());
	QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
	QApplication::sendEvent(context, &enter);
	// Return in the text closes About with the footer of macOS.
	QVERIFY(!dialog.isVisible());
	QCOMPARE(dialog.result(), int(QDialog::Rejected));
	QVERIFY(m_receiver.opened.isEmpty());
}

// Without a handler the link goes to the desktop, which fails on the offscreen platform. About stays open and shows the
// address.
void TestAboutDialog::browserFailureShowsAddress() {
	// The offscreen platform has no browser launcher. Never exercise the real
	// desktop handler when this test is run with a native platform plugin.
	if (QGuiApplication::platformName() != QStringLiteral("offscreen"))
		QSKIP("only the offscreen platform has no browser launcher");
	QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
	const auto restoreHandler = qScopeGuard([this] { QDesktopServices::setUrlHandler(QStringLiteral("https"), &m_receiver, "receive"); });
	AboutDialog dialog;
	dialog.show();
	dialog.findChild<QPushButton*>(QStringLiteral("aboutWebsite"))->click();
	auto* status = dialog.findChild<QLabel*>(QStringLiteral("aboutLinkStatus"));
	// A browser failure preserves About and shows the address.
	QVERIFY(dialog.isVisible());
	if (status == nullptr)
		QFAIL("browser failure must preserve About and show the address");
	QVERIFY(status->isVisible());
	QVERIFY(status->text().contains(QStringLiteral("https://samuelbruin.com/")));
	dialog.close();
}

QTEST_MAIN(TestAboutDialog)
#include "test_aboutdialog.moc"
