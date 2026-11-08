#include "widgets/CompactDoubleSpinBox.h"

#include <QApplication>
#include <QHelpEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QTest>
#include <QToolTip>
#include <QVBoxLayout>

#include <limits>

// The field shows a compact text and edits the full value, also for large values, and shows the full value in its
// tooltip when the text is not it.
class TestCompactDoubleSpinBox : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void init();
	void cleanupTestCase();
	void showsCompactTextAndEditsFullValue_data();
	void showsCompactTextAndEditsFullValue();
	void takesTypedText();
	void keepsValueWithPrefixAndSuffix();
	void showsTypedDigitsWhileFocused();
	void showsLargeValuesWithoutExponent();
	void showsFullValueInToolTip();
	void showsNoToolTipForPlainValue_data();
	void showsNoToolTipForPlainValue();
	void showsHiddenDigitsInToolTip();
	void showsOwnToolTipWhenTextIsFull();

private:
	QString shownToolTip();

	QWidget m_window;
	QVBoxLayout m_layout{&m_window};
	CompactDoubleSpinBox m_spin;
	QPushButton m_other{"Other field"};
};

void TestCompactDoubleSpinBox::initTestCase() {
	m_layout.addWidget(&m_spin);
	m_layout.addWidget(&m_other);
	m_spin.setDecimals(std::numeric_limits<double>::max_digits10);
	m_spin.setRange(-1e15, 1e15);
	m_spin.setKeyboardTracking(false);
	m_window.show();
	m_window.activateWindow();
}

// Each slot starts with the focus in the other field and without prefix, suffix or tooltip.
void TestCompactDoubleSpinBox::init() {
	m_other.setFocus();
	QApplication::processEvents();
	m_spin.setPrefix(QString());
	m_spin.setSuffix(QString());
	m_spin.setToolTip(QString());
}

void TestCompactDoubleSpinBox::cleanupTestCase() {
	QToolTip::hideText();
}

// A visible tip stays up for a moment after it is hidden, so a marker shows whether the field showed a tip of its own.
QString TestCompactDoubleSpinBox::shownToolTip() {
	const QString marker = QStringLiteral("marker");
	QToolTip::showText(m_spin.mapToGlobal(QPoint(2, 2)), marker, &m_spin);
	QHelpEvent help(QEvent::ToolTip, QPoint(2, 2), m_spin.mapToGlobal(QPoint(2, 2)));
	QApplication::sendEvent(&m_spin, &help);
	return QToolTip::text() == marker ? QString() : QToolTip::text();
}

void TestCompactDoubleSpinBox::showsCompactTextAndEditsFullValue_data() {
	QTest::addColumn<double>("input");
	QTest::newRow("small") << 0.12345678901234567;
	QTest::newRow("negative") << -573.29999999999995;
	QTest::newRow("large") << 1234567890.1234567;
}

void TestCompactDoubleSpinBox::showsCompactTextAndEditsFullValue() {
	QFETCH(double, input);
	m_spin.setValue(input);
	const double original = m_spin.value();
	const QString compact = original >= 1e6 ? QString::number(original, 'f', 0) : QString::number(original, 'g', 6);
	QCOMPARE(m_spin.text(), compact);
	m_spin.interpretText();
	QCOMPARE_EQ(m_spin.value(), original);
	m_spin.setFocus(Qt::TabFocusReason);
	QApplication::processEvents();
	QCOMPARE(m_spin.text(), QString::number(original, 'f', QLocale::FloatingPointShortest));
	QCOMPARE_EQ(m_spin.text().toDouble(), original);
	m_other.setFocus();
	QApplication::processEvents();
	QCOMPARE_EQ(m_spin.value(), original);
	QCOMPARE(m_spin.text(), compact);
}

// Text that is typed counts when the field loses the focus, and when the field interprets it while it has the focus.
void TestCompactDoubleSpinBox::takesTypedText() {
	m_spin.setFocus();
	QApplication::processEvents();
	auto* edit = m_spin.findChild<QLineEdit*>();
	edit->setText("12.3456789012345");
	edit->setModified(true);
	m_other.setFocus();
	QApplication::processEvents();
	QCOMPARE_EQ(m_spin.value(), 12.3456789012345);
	edit->setText("111.25");
	edit->setFocus();
	QApplication::processEvents();
	m_spin.interpretText();
	QCOMPARE_EQ(m_spin.value(), 111.25);
}

void TestCompactDoubleSpinBox::keepsValueWithPrefixAndSuffix() {
	m_spin.setPrefix("Speed: ");
	m_spin.setSuffix(" km/h");
	m_spin.setValue(12.3456789012345);
	const double decoratedValue = m_spin.value();
	m_spin.interpretText();
	QCOMPARE_EQ(m_spin.value(), decoratedValue);
	m_spin.setValue(decoratedValue);
	m_spin.setFocus(Qt::TabFocusReason);
	QApplication::processEvents();
	QCOMPARE(m_spin.text(), "Speed: 12.3456789012345 km/h");
	m_other.setFocus();
	QApplication::processEvents();
	QCOMPARE_EQ(m_spin.value(), decoratedValue);
	QCOMPARE(m_spin.text(), "Speed: " + QString::number(decoratedValue, 'g', 6) + " km/h");
}

// A value that a double holds as 36.111111111111001 shows as it was typed while the field has focus.
void TestCompactDoubleSpinBox::showsTypedDigitsWhileFocused() {
	m_spin.setValue(36.111111111111);
	m_spin.setFocus(Qt::TabFocusReason);
	QApplication::processEvents();
	QCOMPARE(m_spin.text(), "36.111111111111");
}

// A value of twelve digits shows without exponent.
void TestCompactDoubleSpinBox::showsLargeValuesWithoutExponent() {
	m_spin.setValue(123456789012.0);
	QCOMPARE(m_spin.text(), "123456789012");
	m_spin.setValue(-987654321098.0);
	QCOMPARE(m_spin.text(), "-987654321098");
	m_spin.setValue(999999999999999.0);
	QCOMPARE(m_spin.text(), "999999999999999");
	m_spin.setValue(1e15);
	QCOMPARE(m_spin.text(), "1e+15");
	m_spin.setValue(1234567.5);
	QCOMPARE(m_spin.text(), "1234568");
	m_spin.setValue(999999.6);
	QCOMPARE(m_spin.text(), "1000000");
	m_spin.setValue(123456789012.0);
	m_spin.interpretText();
	QCOMPARE_EQ(m_spin.value(), 123456789012.0);
	m_spin.setValue(0.5);
	QCOMPARE(m_spin.text(), "0.5");
}

// The field shows the full value in its tooltip only when the text is not it, after the tooltip it has.
void TestCompactDoubleSpinBox::showsFullValueInToolTip() {
	m_spin.setValue(123456789012.5);
	QCOMPARE(shownToolTip(), "123456789012.5");
	m_spin.setValue(1234567.5);
	QCOMPARE(shownToolTip(), "1234567.5");
	m_spin.setPrefix("L: ");
	m_spin.setSuffix(" m");
	m_spin.setValue(1234567.5);
	QCOMPARE(shownToolTip(), "L: 1234567.5 m");
	m_spin.setToolTip("Length");
	QCOMPARE(shownToolTip(), "Length\nL: 1234567.5 m");
}

void TestCompactDoubleSpinBox::showsNoToolTipForPlainValue_data() {
	QTest::addColumn<double>("plainValue");
	for (double plainValue : {0.1, 22.2, -573.3, 0.0001, 1e-7, 1e15})
		QTest::newRow(qPrintable(QString::number(plainValue))) << plainValue;
}

void TestCompactDoubleSpinBox::showsNoToolTipForPlainValue() {
	QFETCH(double, plainValue);
	m_spin.setValue(plainValue);
	QCOMPARE(shownToolTip(), QString());
}

void TestCompactDoubleSpinBox::showsHiddenDigitsInToolTip() {
	m_spin.setValue(1234.5678);
	QCOMPARE(shownToolTip(), "1234.5678");
	m_spin.setValue(0.123456789);
	QCOMPARE(shownToolTip(), "0.123456789");
}

void TestCompactDoubleSpinBox::showsOwnToolTipWhenTextIsFull() {
	m_spin.setPrefix("L: ");
	m_spin.setSuffix(" m");
	m_spin.setToolTip("Length");
	m_spin.setValue(0.5);
	QCOMPARE(m_spin.text(), "L: 0.5 m");
	QCOMPARE(shownToolTip(), "Length");
}

QTEST_MAIN(TestCompactDoubleSpinBox)
#include "test_compactdoublespinbox.moc"
