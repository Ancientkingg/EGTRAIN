#pragma once

#include <fstream>
#include <iostream>
#include <string>
#include "util/timeutil.hpp"
#include "util/Util.hpp"

class Logger;
// extern Logger owlInstance;

// logger for EGTRAIN
extern Logger logger;
// #define OwlInit( settings ) logger.init( settings );
#define LoggerShutdown() logger.shutdown();
#define eglogger (logger << logger.prefix(__FILE__, __LINE__))

// #define OwlInit( settings ) owlInstance.init( settings );
// #define LoggerShutdown() owlInstance.shutdown();
// #define owl (owlInstance << owlInstance.prefix( __FILE__ , __LINE__ ))

class LoggerSettings {
public:
	// path to the location of the log file, working directory by default
	std::string path;
	// filename, default "owl.log"
	std::string filename;
	// the log file will be overwritten, if false a new log file with the date and time is created
	bool b_overwriteFile;

	// print to the log file
	bool b_file;
	// print to the standard output
	bool b_stdout;

	// print the date and time
	bool b_datetime;
	// print the file and line
	bool b_fileline;

	LoggerSettings() {
		this->path = "";
		this->filename = "owl.log";
		this->b_overwriteFile = true;
		this->b_file = true;
		this->b_stdout = true;
		this->b_datetime = true;
		this->b_fileline = true;
	}
};

class Logger {
public:
	Logger();
	~Logger();

	bool init(const LoggerSettings& settings);
	void shutdown();

	// overload the << operator to log all standard data types
	template <typename T>
	Logger& operator<<(T t);
	// this is needed so << std::endl works
	Logger& operator<<(std::ostream& (*fun)(std::ostream&));

	// creates the prefix for every line depending on the settings
	std::string prefix(const std::string& file, const int line);

private:
	bool b_init;
	LoggerSettings settings;
	std::ofstream ofs;
};

template <typename T>
inline Logger& Logger::operator<<(T t) {
	if (this->b_init) {
		if (this->settings.b_file)
			this->ofs << t;
		if (this->settings.b_stdout)
			std::cout << t;
	}
	return *this;
}

inline Logger& Logger::operator<<(std::ostream& (*fun)(std::ostream&)) {
	if (this->b_init) {
		if (this->settings.b_file)
			this->ofs << std::endl;
		if (this->settings.b_stdout)
			std::cout << std::endl;
	}
	return *this;
}