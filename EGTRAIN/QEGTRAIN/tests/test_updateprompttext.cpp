#include "app/UpdatePromptText.h"

#include <QTest>

class TestUpdatePromptText : public QObject {
	Q_OBJECT

private slots:
	void firstStartTexts();
	void updateDialogText();
};

// macOS shows no window title, so the main text alone has to carry the question and the versions.
void TestUpdatePromptText::firstStartTexts() {
	// The question of the first start.
	QCOMPARE(update_prompt::automaticChecksQuestion(), "Check for EGTRAIN updates automatically?");
	// The detail of the first start says what the check does and where to change it.
	QVERIFY(update_prompt::automaticChecksDetail().contains("GitHub"));
	QVERIFY(update_prompt::automaticChecksDetail().endsWith("in the Help menu."));
}

void TestUpdatePromptText::updateDialogText() {
	// The text of the update dialog.
	QCOMPARE(update_prompt::availableText("1.0.2", "1.0.0"), "EGTRAIN 1.0.2 is available. You are using 1.0.0.");
}

QTEST_APPLESS_MAIN(TestUpdatePromptText)
#include "test_updateprompttext.moc"
