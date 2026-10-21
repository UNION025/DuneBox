/***********************************************************************
Main.cpp
Copyright (c) 2016-2017 Thomas Wolf and Rasmus R. Paulsen (people.compute.dtu.dk/rapa)

This file is part of the Magic Sand.

The Magic Sand is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

The Magic Sand is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License along
with the Augmented Reality Sandbox; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
***********************************************************************/


#include "ofMain.h"
#include "ofApp.h"
#include "Localization/Localization.h"
#include "LogFile.h"

// Direct GLFW access for multi-monitor enumeration. OF bundles GLFW; define
// GLFW_INCLUDE_NONE so it doesn't pull in its own GL headers (OF already does).
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

const std::string MagicSandVersion = "1.5.4.2";

bool setWindowDimensions(ofGLFWWindowSettings& settings, int windowsNum) {
	int count;
	GLFWmonitor** monitors = glfwGetMonitors(&count);
	cout << "Number of screens found: " << count << endl;
	if (count > windowsNum) {
		int xM; int yM;
		glfwGetMonitorPos(monitors[windowsNum], &xM, &yM); // We take the first monitor
		const GLFWvidmode * desktopMode = glfwGetVideoMode(monitors[windowsNum]);

        cout << "Monitor " << windowsNum << " size: " << desktopMode->width << "x" << desktopMode->height << endl;
		
		if (windowsNum == 0)
		{
			// Make main window almost full screen - but just a bit of space around to be able to grab other windows
			settings.setSize(desktopMode->width * 4.0 / 5.0, desktopMode->height * 4.0 / 5.0);
		}
		else
		{
			// Projector window full screen
			settings.setSize(desktopMode->width, desktopMode->height);
		}

		settings.setPosition(ofVec2f(xM, yM));

		return true;
	}
	else {
		settings.setSize(1600, 800); // Default settings
		settings.setPosition(ofVec2f(0, 0));
		return false;
	}

}


//========================================================================
int main() {
	dunebox::logfile::startUnlessTerminal();
	ofGLFWWindowSettings settings;

	// Try GL 4.3 first (needed for compute shaders in water simulation).
	// If the driver/hardware doesn't support it, fall back to GL 3.2.
	// macOS tops out at GL 4.1, and a failed ofCreateWindow() leaves a dead
	// window registered in the main loop (crash), so go straight to 3.2 there.
#ifdef __APPLE__
	bool useGL43 = false;
	settings.setGLVersion(3, 2);
#else
	bool useGL43 = true;
	settings.setGLVersion(4, 3);
#endif
	settings.setSize(1600, 800);
	settings.setPosition(ofVec2f(0, 0));
	settings.resizable = true;
	settings.decorated = true;
	settings.title = "Magic-Sand " + MagicSandVersion;

	shared_ptr<ofAppBaseWindow> mainWindow;
	try {
		mainWindow = ofCreateWindow(settings);
	} catch (...) {
		mainWindow = nullptr;
	}

	if (!useGL43) {
		// already created at GL 3.2 (macOS)
	} else if (!mainWindow || mainWindow->getWindowSize().x == 0) {
		// GL 4.3 failed - fall back to GL 3.2
		ofLogNotice("main") << "GL 4.3 not available, falling back to GL 3.2";
		useGL43 = false;
		settings.setGLVersion(3, 2);
		mainWindow = ofCreateWindow(settings);
	} else {
		ofLogNotice("main") << "GL 4.3 context created - compute shaders available";
	}
    
	setWindowDimensions(settings, 0);
	mainWindow->setWindowPosition(ofGetScreenWidth() / 2 - settings.getWidth() / 2, ofGetScreenHeight() / 2 - settings.getHeight() / 2);
    mainWindow->setWindowShape(settings.getWidth(), settings.getHeight());
    
	bool hasSecondMonitor = setWindowDimensions(settings, 1);
	settings.resizable = false;
	if (hasSecondMonitor) {
		// Real projector monitor present: borderless full-screen on it.
		settings.decorated = false;
	} else {
		// Single-monitor setup: make the projector window an obvious,
		// draggable, decorated window offset from the main GUI so it does
		// not overlay it as a confusing blank window.
		settings.decorated = true;
		settings.title = dunebox::i18n::tr("DuneBox Projector Output (drag to your projector)");
		settings.setPosition(ofVec2f(60, 60));
	}
	settings.shareContextWith = mainWindow;
	shared_ptr<ofAppBaseWindow> secondWindow = ofCreateWindow(settings);
	secondWindow->setVerticalSync(false);

	shared_ptr<ofApp> mainApp(new ofApp);
	ofAddListener(secondWindow->events().draw, mainApp.get(), &ofApp::drawProjWindow);
	mainApp->projWindow = secondWindow;
		
	ofRunApp(mainWindow, mainApp);
	ofRunMainLoop();
}
