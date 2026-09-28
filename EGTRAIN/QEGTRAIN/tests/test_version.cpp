#include "util/Version.h"

#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

int main() {
	bool ok = true;
	const auto older = parseStableVersion("1.9.0");
	const auto newer = parseStableVersion("1.10.0");
	ok &= expect(older && newer && *older < *newer, "numeric semantic-version ordering");
	ok &= expect(*newer == *newer, "equal versions compare equal");
	ok &= expect(parseStableTag("v1.10.0") == newer, "stable v tag parses");

	for (const char* malformed : {"", "1.0", "1.0.0.0", "1.a.0", "01.0.0", "1.0.00"})
		ok &= expect(!parseStableVersion(malformed), "malformed stable version rejected");
	for (const char* nonStable : {"1.0.0-alpha", "1.0.0+build", "1.0.0-rc.1"})
		ok &= expect(!parseStableVersion(nonStable), "prerelease/build metadata rejected");
	ok &= expect(!parseStableTag("1.0.0") && !parseStableTag("v1.0.0-alpha"),
		"non-stable tags rejected");
	return ok ? 0 : 1;
}
