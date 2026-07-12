#include "widgets/ConsoleWidget.h"

#include <iostream>
#include <sstream>
#include <string>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

class ReentrantBuf : public std::stringbuf {
protected:
	std::streamsize xsputn(const char* s, std::streamsize n) override {
		std::cerr << "mirror";
		return std::stringbuf::xsputn(s, n);
	}
};

class RejectingBuf : public std::streambuf {
protected:
	std::streamsize xsputn(const char*, std::streamsize) override { return 0; }
};

int main() {
	std::stringbuf coutOriginal;
	std::stringbuf cerrOriginal;
	std::string callback;

	std::streambuf* realCout = std::cout.rdbuf(&coutOriginal);
	std::streambuf* realCerr = std::cerr.rdbuf(&cerrOriginal);
	{
		ConsoleStreambuf capture([&callback](QString text) {
			callback += text.toStdString();
		});
		capture.install();

		std::cout << "cout-bulk";
		std::cout.put('!');
		std::cout.flush();
		std::cerr << "cerr-bulk";
		std::cerr.put('?');
		std::cerr.flush();
		std::cout << "cout-tail";
		std::cerr << "cerr-tail";
		capture.restore();
		capture.restore();
	}
	std::cout.rdbuf(realCout);
	std::cerr.rdbuf(realCerr);

	bool ok = true;
	ok &= expect(callback == "cout-bulk!cerr-bulk?cout-tailcerr-tail", "callback receives each write exactly once");
	ok &= expect(coutOriginal.str() == "cout-bulk!cout-tail", "cout original receives only cout text");
	ok &= expect(cerrOriginal.str() == "cerr-bulk?cerr-tail", "cerr original receives only cerr text");

	ReentrantBuf reentrant;
	std::stringbuf reentrantCerr;
	std::string reentrantCallback;
	std::cout.rdbuf(&reentrant);
	std::cerr.rdbuf(&reentrantCerr);
	{
		ConsoleStreambuf capture([&reentrantCallback](QString text) {
			reentrantCallback += text.toStdString();
		});
		capture.install();
		std::cout.write("x", 1);
		std::cout.flush();
		capture.restore();
	}
	ok &= expect(reentrantCallback == "mirrorx", "reentrant original buffer does not deadlock or lose callback text");
	ok &= expect(reentrant.str() == "x", "reentrant original receives cout text");
	ok &= expect(reentrantCerr.str() == "mirror", "reentrant original may write through cerr");

	RejectingBuf rejecting;
	std::stringbuf rejectingCerr;
	std::string rejectingCallback;
	std::cout.rdbuf(&rejecting);
	std::cerr.rdbuf(&rejectingCerr);
	{
		ConsoleStreambuf capture([&rejectingCallback](QString text) {
			rejectingCallback += text.toStdString();
		});
		capture.install();
		std::cout.write("lost", 4);
		capture.restore();
	}
	ok &= expect(rejectingCallback == "lost", "callback keeps text rejected by original destination");

	std::cout.rdbuf(realCout);
	std::cerr.rdbuf(realCerr);
	std::cout.clear();
	std::cerr.clear();
	return ok ? 0 : 1;
}
