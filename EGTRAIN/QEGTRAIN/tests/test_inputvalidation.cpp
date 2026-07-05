#include "io/InputValidation.h"

#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

int main() {
	InputCheckResult r = validateCaseStudyInput("/no/such/folder/xyz");

	bool ok = true;
	ok &= expect(!r.ok, "missing folder is invalid");
	ok &= expect(!r.message.empty(), "missing folder reports message");

	if (!ok)
		return 1;

	std::cout << "all InputValidation tests passed\n";
	return 0;
}
