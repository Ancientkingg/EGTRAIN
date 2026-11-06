#pragma once

#include <QFile>
#include <QString>

#include <cstdio>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

/// Sends what the process writes to stderr to a file, from construction until text() or the end of the object. Only POSIX
/// systems can do this: on Windows stderr stays as it is and text() is empty.
class StderrCapture {
public:
	/// True when the capture works on this system.
	static constexpr bool available() {
#if defined(_WIN32)
		return false;
#else
		return true;
#endif
	}

	/// Starts to send stderr to `file`, which it creates or empties.
	explicit StderrCapture(const QString& file) : m_file(file) {
#if !defined(_WIN32)
		std::fflush(stderr);
		const int target = ::open(QFile::encodeName(file).constData(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (target >= 0) {
			m_saved = ::dup(STDERR_FILENO);
			if (m_saved >= 0)
				::dup2(target, STDERR_FILENO);
			::close(target);
		}
#endif
	}
	StderrCapture(const StderrCapture&) = delete;
	StderrCapture& operator=(const StderrCapture&) = delete;
	~StderrCapture() { restore(); }

	/// Gives stderr back and returns what was written to it since the start.
	QString text() {
		restore();
		QFile written(m_file);
		return available() && written.open(QIODevice::ReadOnly) ? QString::fromLocal8Bit(written.readAll()) : QString();
	}

private:
	void restore() {
#if !defined(_WIN32)
		if (m_saved < 0)
			return;
		std::fflush(stderr);
		::dup2(m_saved, STDERR_FILENO);
		::close(m_saved);
		m_saved = -1;
#endif
	}

	QString m_file;
	int m_saved = -1;
};
