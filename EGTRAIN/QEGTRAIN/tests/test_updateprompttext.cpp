#include "app/UpdatePromptText.h"

#include <cstdlib>
#include <iostream>

namespace {
void requireText(const QString& actual, const QString& expected, const char* message) {
	if (actual != expected) {
		std::cerr << "failed: " << message << ": expected '" << expected.toStdString() << "', got '" << actual.toStdString() << "'\n";
		std::exit(1);
	}
}

void require(bool condition, const char* message) {
	if (!condition) {
		std::cerr << "failed: " << message << '\n';
		std::exit(1);
	}
}
}

int main() {
	// macOS shows no window title, so the main text alone has to carry the question and the versions.
	requireText(update_prompt::automaticChecksQuestion(), "Check for EGTRAIN updates automatically?", "question of the first start");
	require(update_prompt::automaticChecksDetail().contains("GitHub Releases") && update_prompt::automaticChecksDetail().endsWith("from Help."),
		"detail of the first start does not say what the check does and where to change it");
	requireText(update_prompt::availableText("1.0.2", "1.0.0"), "EGTRAIN 1.0.2 is available. You are using 1.0.0.", "text of the update dialog");
	return 0;
}
