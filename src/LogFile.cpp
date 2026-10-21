/***********************************************************************
LogFile.cpp - send console output to data/DebugFiles when not in a terminal

This file is part of DuneBox, a fork of Magic Sand.
***********************************************************************/

#include "LogFile.h"
#include "ofMain.h"

#ifndef _WIN32
#include <unistd.h>
#include <cstdio>
#endif

namespace dunebox {
namespace logfile {

#ifndef _WIN32
namespace {
const long kMaxBytes = 20L * 1024 * 1024;
bool active = false;
std::string logPath, prevPath;

void openFresh() {
	std::rename(logPath.c_str(), prevPath.c_str());
	if (std::freopen(logPath.c_str(), "w", stdout)) {
		setvbuf(stdout, nullptr, _IOLBF, 0);
		dup2(fileno(stdout), STDERR_FILENO);
		active = true;
	}
}
}

void startUnlessTerminal() {
	if (isatty(STDOUT_FILENO)) return;
	std::string dir = ofToDataPath("DebugFiles", true);
	ofDirectory::createDirectory(dir, false, true);
	logPath = ofFilePath::join(dir, "dunebox.log");
	prevPath = ofFilePath::join(dir, "dunebox-prev.log");
	openFresh();
}

void rotateIfLarge() {
	if (!active) return;
	std::fflush(stdout);
	long size = std::ftell(stdout);
	if (size < kMaxBytes) return;
	openFresh();
	std::printf("[notice ] LogFile: log reached %ld bytes - previous part is dunebox-prev.log\n", size);
}
#else
void startUnlessTerminal() {}
void rotateIfLarge() {}
#endif

} // namespace logfile
} // namespace dunebox
