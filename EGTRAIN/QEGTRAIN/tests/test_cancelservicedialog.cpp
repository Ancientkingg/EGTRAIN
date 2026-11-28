// The dialog that asks how a train is cancelled by hand during a run (widgets/CancelServiceDialog.h).

#include "widgets/CancelServiceDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTest>

class TestCancelServiceDialog : public QObject {
	Q_OBJECT

private slots:
	void startsWithACompleteCancellation();
	void offersEachStationAheadOnce();
	void offersNoStationWithoutStopsAhead();
};

void TestCancelServiceDialog::startsWithACompleteCancellation() {
	CancelServiceDialog dialog("Q1-1", {"B", "C"});
	QCOMPARE(dialog.windowTitle(), QString("Cancel Service"));
	QVERIFY(dialog.completeButton()->isChecked());
	QVERIFY(!dialog.stationCombo()->isEnabled());
	QCOMPARE(dialog.fromStation(), QString());
	QCOMPARE(dialog.buttons()->button(QDialogButtonBox::Ok)->text(), QString("Cancel Service"));
	QCOMPARE(dialog.buttons()->button(QDialogButtonBox::Cancel)->text(), QString("Keep Running"));
}

void TestCancelServiceDialog::offersEachStationAheadOnce() {
	CancelServiceDialog dialog("Q1-1", {"B", "A", "B", "C"});
	QCOMPARE(dialog.stationCombo()->count(), 3);
	QCOMPARE(dialog.stationCombo()->itemText(1), QString("A"));
	dialog.fromStationButton()->setChecked(true);
	QVERIFY(dialog.stationCombo()->isEnabled());
	QCOMPARE(dialog.fromStation(), QString("B"));
	dialog.stationCombo()->setCurrentIndex(2);
	QCOMPARE(dialog.fromStation(), QString("C"));
	dialog.completeButton()->setChecked(true);
	QVERIFY(!dialog.stationCombo()->isEnabled());
	QCOMPARE(dialog.fromStation(), QString());
}

void TestCancelServiceDialog::offersNoStationWithoutStopsAhead() {
	CancelServiceDialog dialog("Q1-1", {});
	QVERIFY(!dialog.fromStationButton()->isEnabled());
	QVERIFY(dialog.completeButton()->isChecked());
	QCOMPARE(dialog.fromStation(), QString());
}

QTEST_MAIN(TestCancelServiceDialog)
#include "test_cancelservicedialog.moc"
