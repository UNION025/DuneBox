/***********************************************************************
ofApp.cpp - main openframeworks app
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

#include "ofApp.h"
#include "DuneBoxGuiTheme.h"
#include "Localization/Localization.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

ofApp::~ofApp() = default;

void ofApp::setup() {
	if (const char* dir = std::getenv("DUNEBOX_MONITOR_DIR")) {
		monitorDir = dir;
		ofDirectory::createDirectory(monitorDir, false, true);
		// Run after the app and the GUI/modal overlays have drawn.
		ofAddListener(ofEvents().draw, this, &ofApp::onMonitorDrawMain, OF_EVENT_ORDER_AFTER_APP + 1000);
		if (projWindow)
			ofAddListener(projWindow->events().draw, this, &ofApp::onMonitorDrawProj, OF_EVENT_ORDER_AFTER_APP + 1000);
		ofLogNotice("ofApp") << "screen monitor: saving windows to " << monitorDir;
	}

	// OF basics
	ofSetFrameRate(60);
	ofBackground(0);
	ofSetVerticalSync(true);
	ofSetLogLevel(OF_LOG_VERBOSE);
	ofSetLogLevel("ofThread", OF_LOG_WARNING);
	ofSetLogLevel("ofFbo", OF_LOG_ERROR);
	ofSetLogLevel("ofShader", OF_LOG_ERROR);
	ofSetLogLevel("ofxKinect", OF_LOG_WARNING);

	dunebox::GuiTheme::install();

	// Setup kinectProjector
	kinectProjector = std::make_shared<KinectProjector>(projWindow);
	kinectProjector->setup(true);
	
	// Setup sandSurfaceRenderer
	sandSurfaceRenderer = std::make_unique<SandSurfaceRenderer>(kinectProjector, projWindow);
	sandSurfaceRenderer->setup(true);

	// Theme display overlay state
	themeDisplayName = sandSurfaceRenderer->getThemeName();
	themeDisplayTimer = 0;
	preLavaThemeIndex = 0;
	
	// Retrieve variables
	ofVec2f kinectRes = kinectProjector->getKinectRes();
	ofVec2f projRes = ofVec2f(projWindow->getWidth(), projWindow->getHeight());
	ofRectangle kinectROI = kinectProjector->getKinectROI();
	////mainWindowROI = ofRectangle(600, 30, 600, 450);
	//mainWindowROI = ofRectangle(0, 0, 640, 480);
	mainWindowROI = ofRectangle((ofGetWindowWidth()-kinectRes.x)/2, (ofGetWindowHeight()-kinectRes.y)/2, kinectRes.x, kinectRes.y);

	mapGameController.setup(kinectProjector);
	mapGameController.setProjectorRes(projRes);
	mapGameController.setKinectRes(kinectRes);
	mapGameController.setKinectROI(kinectROI);

	boidGameController.setup(kinectProjector);
	boidGameController.setProjectorRes(projRes);
	boidGameController.setKinectRes(kinectRes);
	boidGameController.setKinectROI(kinectROI);

	// Setup water simulation at Kinect depth resolution
	int waterW = (int)kinectRes.x;
	int waterH = (int)kinectRes.y;
	if (waterW == 0) waterW = 640;
	if (waterH == 0) waterH = 480;
	waterSimSetup(waterW, waterH);

	// Allocate test terrain FBO for no-Kinect fallback
	useTestTerrain = false;
	ofFboSettings terrainSettings;
	terrainSettings.width = waterW;
	terrainSettings.height = waterH;
	terrainSettings.internalformat = GL_RGBA32F;
	terrainSettings.textureTarget = GL_TEXTURE_2D;
	terrainSettings.minFilter = GL_LINEAR;
	terrainSettings.maxFilter = GL_LINEAR;
	terrainSettings.useDepth = false;
	testTerrainFbo.allocate(terrainSettings);
	generateTestTerrain();

	ofLogNotice("ofApp") << "Water simulation initialized (" << waterW << "x" << waterH
	                     << "). Press 'w' to toggle.";

	// Run startup diagnostics. Pass the real Kinect status from kinectProjector
	// (set up above) so the report shows the configured v1/v2 sensor correctly
	// instead of only probing the legacy v1 API. Results are logged to the
	// console (no blocking on-screen overlay - see Diagnostics.h).
	diagnostics.probe(kinectProjector->getKinectVersion(), kinectProjector->isKinectConnected());
	diagnostics.log();

	// Inter-app bridge to DuneBox-sandcam
	bridge.setup("127.0.0.1", 9876);
}


void ofApp::update() {
    // Call kinectProjector->update() first during the update function()
	kinectProjector->update();
   	sandSurfaceRenderer->update();
    
    //if (kinectProjector->isROIUpdated())
	if (kinectProjector->getKinectROI() != mapGameController.getKinectROI())
	{
		ofRectangle kinectROI = kinectProjector->getKinectROI();
		mapGameController.setKinectROI(kinectROI);
		boidGameController.setKinectROI(kinectROI);
	}

	mapGameController.update();
	boidGameController.update();

	// --- Inter-app bridge ---
	bridge.update();
	for (auto& msg : bridge.poll()) {
		std::string type = msg.value("type", "");
		if (type == "marker_detected") {
			int markerId = msg.value("marker_id", -1);
			ofLogNotice("Bridge") << "Marker detected: " << markerId;
		} else if (type == "mode_change") {
			std::string mode = msg.value("mode", "");
			bool on = msg.value("enabled", false);
			if (mode == "water") {
				if (useComputeWaterSim) waterSimCompute.setEnabled(on);
				else waterSimFragment.setEnabled(on);
				ofLogNotice("Bridge") << "Water simulation: " << (on ? "ON" : "OFF");
			}
		} else if (type == "command") {
			handleDashboardCommand(msg);
		} else if (type == "pong") {
			ofLogVerbose("Bridge") << "Pong received";
		} else if (type == "volcano_eruption") {
			// Eruption from Python side: enable lava and add lava source
			float vx = msg.value("x", 0.5f);
			float vy = msg.value("y", 0.5f);
			if (!waterSimIsLavaMode()) {
				preLavaThemeIndex = sandSurfaceRenderer->getThemeIndex();
				sandSurfaceRenderer->setTheme(2); // Volcanic
				themeDisplayName = "Volcanic (Eruption!)";
				themeDisplayTimer = 3.0f;
				if (!waterSimIsEnabled()) waterSimToggleEnabled();
				waterSimSetLavaMode(true);
			}
			float simX = vx * waterSimGetSimWidth();
			float simY = vy * waterSimGetSimHeight();
			volcanoEruptionActive = true;
			volcanoEruptionTimer = 3.0f;
			volcanoSourceX = simX;
			volcanoSourceY = simY;
			ofLogNotice("Bridge") << "Volcano eruption at (" << vx << ", " << vy << ")";
		}
	}

	// Report state for the sandcam web dashboard twice a second
	bridgeFrameCounter++;
	if (bridgeFrameCounter >= 30) {
		bridgeFrameCounter = 0;
		if (bridge.isConnected()) {
			std::string appState = "idle";
			switch (kinectProjector->GetApplicationState()) {
				case KinectProjector::APPLICATION_STATE_SETUP: appState = "setup"; break;
				case KinectProjector::APPLICATION_STATE_CALIBRATING: appState = "calibrating"; break;
				case KinectProjector::APPLICATION_STATE_RUNNING: appState = "running"; break;
				default: break;
			}
			ofJson status;
			status["app_state"] = appState;
			status["kinect_connected"] = kinectProjector->isKinectConnected();
			status["kinect_version"] = kinectProjector->getKinectVersion();
			status["water"] = waterSimIsEnabled();
			status["lava"] = waterSimIsLavaMode();
			status["theme"] = sandSurfaceRenderer->getThemeName();
			status["day_night"] = sandSurfaceRenderer->isDayNightEnabled();
			status["fps"] = std::round(ofGetFrameRate() * 10.0f) / 10.0f;
			float evap = waterSimGetEvaporation();
			status["evaporation"] = evap <= 0.0f ? "off" : (evap < 0.08f ? "slow" : "fast");
			status["hand_rain"] = handRainEnabled;
			status["erosion"] = waterSimIsErosion();
			status["roi_calibrated"] = kinectProjector->isROICalibrated();
			status["projector_calibrated"] = kinectProjector->isCalibrated();
			status["calibration_text"] = kinectProjector->getCalibrationText();
			status["awaiting_confirm"] = kinectProjector->isAwaitingConfirmation();
			bridge.send("dunebox_state", status);
		}
	}

	// Theme display timer
	if (themeDisplayTimer > 0)
		themeDisplayTimer -= ofGetLastFrameTime();

	// Volcano eruption: inject lava source over several frames
	if (volcanoEruptionActive) {
		volcanoEruptionTimer -= ofGetLastFrameTime();
		if (volcanoEruptionTimer > 0) {
			waterSimAddWater(volcanoSourceX, volcanoSourceY, 20.0f, 0.5f);
		} else {
			volcanoEruptionActive = false;
		}
	}

	// --- Water simulation update ---
	if (waterSimIsEnabled()) {
		// Determine depth texture source
		if (kinectProjector->isKinectConnected() &&
		    kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING)
		{
			// Use live Kinect depth data
			useTestTerrain = false;
			ofTexture& depthTex = kinectProjector->getTexture();
			waterSimUpdate(depthTex, ofGetLastFrameTime());
		}
		else
		{
			// No Kinect - use procedural test terrain
			useTestTerrain = true;
			generateTestTerrain();
			ofTexture& testTex = testTerrainFbo.getTexture();
			waterSimUpdate(testTex, ofGetLastFrameTime());
		}

		// Rain gesture detection
		if (handRainEnabled) detectRainGesture();
	}
}


void ofApp::draw() 
{
	float x = mainWindowROI.x;
	float y = mainWindowROI.y;
	float w = mainWindowROI.width;
	float h = mainWindowROI.height;

	if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING)
	{
		sandSurfaceRenderer->drawMainWindow(x, y, w, h);//400, 20, 400, 300);
		boidGameController.drawMainWindow(x, y, w, h);
	}

	kinectProjector->drawMainWindow(x, y, w, h);

	if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_SETUP)
	{
		// Overlay the setup guidance ON TOP of the raw Kinect color view, which
		// is otherwise meaningless to a setup operator and looks like a broken
		// running screen. Shown only in SETUP (not while CALIBRATING, which has
		// its own modal prompts).
		const std::string hint = dunebox::i18n::tr(
			"DuneBox is in setup\n"
			"The sand map appears here once calibration is loaded.\n"
			"\n"
			"Press Space to start. If it stays in setup, calibrate this rig\n"
			"from the Calibration panel on the right:\n"
			"  1) Draw the sand region (skip if Sand region says set),\n"
			"  2) flatten the sand, then Calibrate projector,\n"
			"  3) press Space again. The Status panel shows what's missing.");
		ofTrueTypeFont& font = dunebox::i18n::uiFont(11);
		// getStringBoundingBox() over-reports the width of multi-line UTF-8
		// text, so size the box from the widest line instead.
		std::vector<std::string> lines = ofSplitString(hint, "\n");
		float lineHeight = font.getLineHeight();
		ofRectangle box(0, -font.getAscenderHeight(), 0, lineHeight * lines.size());
		for (const auto& line : lines)
			box.width = std::max(box.width, font.stringWidth(line));
		int hx = ofGetWidth() / 2 - (int)(box.width / 2);
		int hy = ofGetHeight() / 2 - (int)(box.height / 2 + box.y);
		// Dark backing box so the white text is readable over any color feed.
		ofSetColor(0, 0, 0, 170);
		ofDrawRectangle(hx - 12, hy + box.y - 12, box.width + 24, box.height + 24);
		ofSetColor(255);
		font.drawString(hint, hx, hy);
		ofSetColor(255);
	}

	// Bottom-centre status indicator (the bottom corners hold the Status and
	// Animals panels). Only advertise the active theme once the
	// app is actually RUNNING; otherwise show the real state so the screen
	// doesn't look like a broken "Theme: Topo" running view.
	{
		KinectProjector::Application_state appState = kinectProjector->GetApplicationState();
		bool running = appState == KinectProjector::APPLICATION_STATE_RUNNING;
		std::string name = sandSurfaceRenderer->getThemeName();
		std::string label;
		if (running)
			label = dunebox::i18n::tr("Theme: ") + dunebox::i18n::tr(name);
		else if (appState == KinectProjector::APPLICATION_STATE_CALIBRATING)
			label = dunebox::i18n::tr("CALIBRATING - follow the on-screen prompts");
		else
			label = dunebox::i18n::tr("SETUP - not calibrated (calibrate via the GUI panel)");
		float alpha = 180;
		if (running && themeDisplayTimer > 0) {
			// Bright flash when recently switched
			alpha = 255;
			if (themeDisplayTimer < 0.5f)
				alpha = ofMap(themeDisplayTimer, 0, 0.5f, 180, 255);
		}
		ofTrueTypeFont& font = dunebox::i18n::uiFont(10);
		int tx = (ofGetWidth() - (int)font.stringWidth(label)) / 2;
		int ty = ofGetHeight() - 14;
		// Shadow
		ofSetColor(0, 0, 0, (int)(alpha * 0.7f));
		font.drawString(label, tx + 1, ty + 1);
		// Foreground
		ofSetColor(255, 255, 255, (int)alpha);
		font.drawString(label, tx, ty);
		ofSetColor(255);
	}

	// Theme switch banner (temporary large text)
	if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING && themeDisplayTimer > 0) {
		float fade = (themeDisplayTimer < 0.5f) ? themeDisplayTimer / 0.5f : 1.0f;
		ofTrueTypeFont& font = dunebox::i18n::uiFont(16);
		const std::string banner = dunebox::i18n::tr(themeDisplayName);
		int cx = ofGetWidth() / 2 - (int)(font.stringWidth(banner) / 2);
		int cy = ofGetHeight() / 2;
		ofSetColor(0, 0, 0, (int)(200 * fade));
		font.drawString(banner, cx + 1, cy + 1);
		ofSetColor(255, 255, 80, (int)(255 * fade));
		font.drawString(banner, cx, cy);
		ofSetColor(255);
	}

	// Normalise the ambient GL draw state for the ofxModal overlay. The
	// calibration flow (askToFlattenSand / "cover the sandbox with a board")
	// drives ofxModalWindow, which renders itself as an AFTER_APP draw listener
	// - i.e. AFTER this draw() returns and OUTSIDE KinectProjector's
	// beginGuiDrawState() normalisation. ofxModalWindow::onDraw does
	// ofPushStyle()/ofFill() but never sets depth-test/alpha-blending, so it
	// inherits whatever the preceding FBO / compute-water / sand draws left
	// behind. On this GL 4.3 core-profile build that meant the modal's
	// translucent blackout and panels could render with depth test on / alpha
	// blending off (invisible or opaque-black), so the calibration prompts the
	// operator must click never appeared. Leave a clean 2D state here; the
	// renderer resets per frame so this does not affect anything else.
	ofDisableDepthTest();
	ofEnableAlphaBlending();
	ofSetColor(255);
}

namespace {
const uint64_t kMonitorIntervalMs = 3000;

// Grab the current window's framebuffer and swap it in atomically so a reader
// never sees a half-written PNG.
void saveWindowShot(const std::string& dir, const std::string& name) {
	std::string tmp = ofFilePath::join(dir, name + ".tmp.png");
	ofSaveScreen(tmp); // RGB only: the framebuffer's alpha would make the PNG transparent
	ofFile::moveFromTo(tmp, ofFilePath::join(dir, name + ".png"), false, true);
}
}

void ofApp::onMonitorDrawMain(ofEventArgs& args) {
	if (ofGetElapsedTimeMillis() - monitorLastMain < kMonitorIntervalMs) return;
	monitorLastMain = ofGetElapsedTimeMillis();
	saveWindowShot(monitorDir, "main");
}

void ofApp::onMonitorDrawProj(ofEventArgs& args) {
	if (ofGetElapsedTimeMillis() - monitorLastProj < kMonitorIntervalMs) return;
	monitorLastProj = ofGetElapsedTimeMillis();
	saveWindowShot(monitorDir, "projector");
}

void ofApp::drawProjWindow(ofEventArgs &args) 
{
	if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING)
	{
		sandSurfaceRenderer->drawProjectorWindow();

		// Water overlay - composited on top of terrain, under game layers
		if (waterSimIsEnabled()) {
			float projW = projWindow->getWidth();
			float projH = projWindow->getHeight();
			waterSimDraw(projW, projH);
		}

		mapGameController.drawProjectorWindow();
		boidGameController.drawProjectorWindow();
	}
	else if (useTestTerrain && waterSimIsEnabled())
	{
		// No Kinect running - still show water on test terrain
		float projW = projWindow->getWidth();
		float projH = projWindow->getHeight();
		ofSetColor(40, 30, 20); // Dark brown background for test terrain
		ofDrawRectangle(0, 0, projW, projH);
		ofSetColor(255);
		waterSimDraw(projW, projH);
	}
	kinectProjector->drawProjectorWindow();
}

void ofApp::keyPressed(int key) 
{
	if (key == 'c')
	{
		kinectProjector->SaveKinectColorImage();
	}
	else if (key == 'd')
	{
		kinectProjector->SaveFilteredDepthImage();
	}
	else if (key == ' ')
	{
		if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING && 
			boidGameController.isIdle()) // do not start map game if boidgame is not idle
		{
			if (mapGameController.isIdle())
			{
				mapGameController.setDebug(kinectProjector->getDumpDebugFiles());
				mapGameController.StartGame();
			}
			else
			{
				mapGameController.ButtonPressed();
			}
		}
		else if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_SETUP)
		{
			// Try to start the application
			kinectProjector->startApplication();
		}
	}
	else if (key == 'f' || key == 'r')
	{
		if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING)
		{
			if (mapGameController.isIdle())
			{
				boidGameController.setDebug(kinectProjector->getDumpDebugFiles());
				boidGameController.StartGame(2);
			}
			else 
			{
				mapGameController.EndButtonPressed();
			}
		}
	}
	else if (key == '1') // Absolute beginner
	{
		if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING && mapGameController.isIdle())
		{
			boidGameController.setDebug(kinectProjector->getDumpDebugFiles());
			boidGameController.StartGame(0);
		}
	}
	else if (key == '2') 
	{
		if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING && mapGameController.isIdle())
		{
			boidGameController.setDebug(kinectProjector->getDumpDebugFiles());
			boidGameController.StartGame(1);
		}
	}
	else if (key == '3')
	{
		if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING && mapGameController.isIdle())
		{
			boidGameController.setDebug(kinectProjector->getDumpDebugFiles());
			boidGameController.StartGame(2);
		}
	}
	else if (key == '4')
	{
		if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING && mapGameController.isIdle())
		{
			boidGameController.setDebug(kinectProjector->getDumpDebugFiles());
			boidGameController.StartGame(3);
		}
	}
	else if (key == 'm')
	{
		if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING && mapGameController.isIdle())
		{
			boidGameController.setDebug(kinectProjector->getDumpDebugFiles());
			boidGameController.StartSeekMotherGame();
		}
	}
	else if (key == 't')
	{
		// Cycle color theme (biome mode)
		sandSurfaceRenderer->cycleTheme();
		themeDisplayName = sandSurfaceRenderer->getThemeName();
		themeDisplayTimer = 2.5f; // show name for 2.5 seconds
		ofLogNotice("ofApp") << "Theme: " << themeDisplayName;
	}
	else if (key == 'T')
	{
		// Debug: RealTimeTestMe (was on 't')
		mapGameController.setDebug(kinectProjector->getDumpDebugFiles());
		mapGameController.RealTimeTestMe();
	}
	else if (key == 'w')
	{
		// Toggle water simulation
		waterSimToggleEnabled();
		ofLogNotice("ofApp") << "Water simulation: " << (waterSimIsEnabled() ? "ON" : "OFF");
		cout << "Water simulation: " << (waterSimIsEnabled() ? "ON" : "OFF") << endl;
	}
	else if (key == 'W')
	{
		// Original debug test (moved from 'w')
		mapGameController.setDebug(kinectProjector->getDumpDebugFiles());
		mapGameController.DebugTestMe();
	}
	else if (key == 'b')
	{
		// Send a ping to the Python bridge
		if (bridge.isConnected()) {
			bridge.send("ping");
			ofLogNotice("Bridge") << "Ping sent";
		} else {
			ofLogNotice("Bridge") << "Not connected";
		}
	}
	else if (key == 'n')
	{
		// Toggle day/night cycle
		sandSurfaceRenderer->toggleDayNight();
	}
	else if (key == 'l')
	{
		// Toggle lava simulation mode
		bool newLavaState = !waterSimIsLavaMode();
		if (newLavaState) {
			// Save current theme and switch to Volcanic
			preLavaThemeIndex = sandSurfaceRenderer->getThemeIndex();
			sandSurfaceRenderer->setTheme(2); // Volcanic
			themeDisplayName = "Volcanic (Lava)";
			themeDisplayTimer = 2.5f;
			// Enable water sim if not already on
			if (!waterSimIsEnabled()) {
				waterSimToggleEnabled();
			}
		} else {
			// Restore previous theme
			sandSurfaceRenderer->setTheme(preLavaThemeIndex);
			themeDisplayName = sandSurfaceRenderer->getThemeName();
			themeDisplayTimer = 2.5f;
		}
		waterSimSetLavaMode(newLavaState);
		ofLogNotice("ofApp") << "Lava mode: " << (newLavaState ? "ON" : "OFF");
	}
	else if (key == 'v')
	{
		// Manual volcano eruption at center of sandbox
		if (!waterSimIsLavaMode()) {
			preLavaThemeIndex = sandSurfaceRenderer->getThemeIndex();
			sandSurfaceRenderer->setTheme(2); // Volcanic
			themeDisplayName = "Volcanic (Eruption!)";
			themeDisplayTimer = 3.0f;
			if (!waterSimIsEnabled()) waterSimToggleEnabled();
			waterSimSetLavaMode(true);
		}
		float cx = waterSimGetSimWidth() * 0.5f;
		float cy = waterSimGetSimHeight() * 0.5f;
		volcanoEruptionActive = true;
		volcanoEruptionTimer = 3.0f;
		volcanoSourceX = cx;
		volcanoSourceY = cy;
		ofLogNotice("ofApp") << "Volcano eruption triggered at center";
	}
	else if (key == 'x')
	{
		waterSimClear();
		ofLogNotice("ofApp") << "Water cleared";
	}
	else if (key == 'e')
	{
		bool on = !waterSimIsErosion();
		waterSimSetErosion(on);
		if (on && !waterSimIsEnabled()) waterSimToggleEnabled();
		themeDisplayName = on ? "Erosion: on" : "Erosion: off";
		themeDisplayTimer = 2.0f;
		ofLogNotice("ofApp") << "Erosion view: " << (on ? "ON" : "OFF");
	}
}

void ofApp::handleDashboardCommand(const ofJson& msg)
{
	// Commands that already have a key map to it, so keyboard and dashboard
	// behave identically.
	static const std::map<std::string, int> commandKeys = {
		{"toggle_water", 'w'},
		{"toggle_lava", 'l'},
		{"erupt_volcano", 'v'},
		{"cycle_theme", 't'},
		{"toggle_day_night", 'n'},
		{"start_app", ' '},
		{"dry_water", 'x'},
		{"toggle_erosion", 'e'},
	};
	const std::string action = msg.value("action", "");
	ofLogNotice("Bridge") << "Dashboard command: " << action;

	auto it = commandKeys.find(action);
	if (it != commandKeys.end()) {
		keyPressed(it->second);
	} else if (action == "set_evaporation") {
		const std::string level = (msg.contains("value") && msg["value"].is_string())
			? msg["value"].get<std::string>() : "off";
		waterSimSetEvaporation(level == "fast" ? 0.12f : level == "slow" ? 0.03f : 0.0f);
	} else if (action == "toggle_hand_rain") {
		handRainEnabled = !handRainEnabled;
	} else if (action == "detect_sand_region") {
		kinectProjector->startAutomaticROIDetection();
	} else if (action == "calibrate_projector") {
		kinectProjector->startAutomaticKinectProjectorCalibration();
	} else if (action == "full_calibration") {
		kinectProjector->startFullCalibration();
	} else if (action == "calibration_confirm") {
		kinectProjector->remoteConfirm();
	} else if (action == "calibration_cancel") {
		kinectProjector->remoteCancel();
	} else {
		ofLogWarning("Bridge") << "Unknown dashboard command: " << action;
	}
}

void ofApp::keyReleased(int key) {

}

void ofApp::mouseMoved(int x, int y) {

}

void ofApp::mouseDragged(int x, int y, int button) {

	// We assume that we only use this during ROI annotation
	kinectProjector->mouseDragged(x - mainWindowROI.x, y - mainWindowROI.y, button);
}

void ofApp::mousePressed(int x, int y, int button) 
{
	if (mainWindowROI.inside((float)x, (float)y))
	{
		kinectProjector->mousePressed(x-mainWindowROI.x, y-mainWindowROI.y, button);
	}
}

void ofApp::mouseReleased(int x, int y, int button) {
	// We assume that we only use this during ROI annotation
	kinectProjector->mouseReleased(x - mainWindowROI.x, y - mainWindowROI.y, button);

}

void ofApp::mouseEntered(int x, int y) {

}

void ofApp::mouseExited(int x, int y) {

}

void ofApp::windowResized(int w, int h) {
	if (!kinectProjector) return;
	ofVec2f kinectRes = kinectProjector->getKinectRes();
	if (kinectRes.x <= 0 || kinectRes.y <= 0) return;
	mainWindowROI = ofRectangle((w - kinectRes.x) / 2, (h - kinectRes.y) / 2, kinectRes.x, kinectRes.y);
}

void ofApp::gotMessage(ofMessage msg) {

}

void ofApp::dragEvent(ofDragInfo dragInfo) {

}

// --- Water simulation helpers ---------------------------------------

void ofApp::generateTestTerrain() {
	testTerrainFbo.begin();
	ofClear(0, 0, 0, 0);

	// Draw a sine-wave heightfield as a grayscale image.
	// The water sim treats the red channel as terrain elevation.
	float t = ofGetElapsedTimef() * 0.1f; // very slow drift for visual interest
	int w = testTerrainFbo.getWidth();
	int h = testTerrainFbo.getHeight();

	ofMesh mesh;
	mesh.setMode(OF_PRIMITIVE_POINTS);

	for (int y = 0; y < h; y += 2) {
		for (int x = 0; x < w; x += 2) {
			float fx = (float)x / w;
			float fy = (float)y / h;
			// Rolling hills with a central valley
			float elev = 0.5f
			    + 0.2f * sin(fx * 4.0f * PI + t)
			    + 0.15f * sin(fy * 3.0f * PI + t * 0.7f)
			    + 0.1f * cos((fx + fy) * 5.0f * PI);
			// Clamp to [0,1]
			elev = ofClamp(elev, 0.0f, 1.0f);

			ofFloatColor c(elev, 0.0f, 0.0f, 1.0f);
			mesh.addVertex(ofVec3f(x, y, 0));
			mesh.addColor(c);
		}
	}

	ofSetColor(255);
	mesh.draw();
	testTerrainFbo.end();
}

void ofApp::detectRainGesture() {
	// Only detect rain when Kinect is running and calibrated
	if (!kinectProjector->isKinectConnected() ||
	    kinectProjector->GetApplicationState() != KinectProjector::APPLICATION_STATE_RUNNING)
	{
		return;
	}

	// Hands are local spikes above nearby sand. A ridge in the same 80–400 mm
	// band used to rain every frame because occupancy alone still let cones
	// through. Require prominence vs the 3x3 coarse neighbourhood.
	const float rainMinMm = 80.0f;
	const float rainMaxMm = 400.0f;
	const float minProminenceMm = 50.0f;
	const float maxOccupancy = 0.08f;
	const int minHits = 3;
	const int maxSources = 8;
	const int step = 8;

	ofRectangle roi = kinectProjector->getKinectROI();
	if (roi.width <= 0 || roi.height <= 0) return;

	int startX = (int)roi.x;
	int startY = (int)roi.y;
	int endX = (int)(roi.x + roi.width);
	int endY = (int)(roi.y + roi.height);
	int cols = std::max(1, (endX - startX + step - 1) / step);
	int rows = std::max(1, (endY - startY + step - 1) / step);

	std::vector<float> elevs((size_t)rows * (size_t)cols, 0.0f);
	int samples = 0;
	for (int r = 0; r < rows; ++r) {
		for (int c = 0; c < cols; ++c) {
			int x = startX + c * step;
			int y = startY + r * step;
			elevs[(size_t)r * cols + c] = kinectProjector->elevationAtKinectCoord(x, y);
			++samples;
		}
	}

	std::vector<ofVec2f> hits;
	for (int r = 0; r < rows; ++r) {
		for (int c = 0; c < cols; ++c) {
			float elev = elevs[(size_t)r * cols + c];
			if (elev < rainMinMm || elev > rainMaxMm) continue;
			float localMin = elev;
			for (int dr = -1; dr <= 1; ++dr) {
				for (int dc = -1; dc <= 1; ++dc) {
					int rr = r + dr;
					int cc = c + dc;
					if (rr < 0 || cc < 0 || rr >= rows || cc >= cols) continue;
					localMin = std::min(localMin, elevs[(size_t)rr * cols + cc]);
				}
			}
			if (elev - localMin < minProminenceMm) continue;
			hits.push_back(ofVec2f((float)(startX + c * step), (float)(startY + r * step)));
		}
	}
	if (samples == 0 || (int)hits.size() < minHits) return;
	if ((float)hits.size() / (float)samples > maxOccupancy) return;

	const float simW = (float)waterSimGetSimWidth();
	const float simH = (float)waterSimGetSimHeight();
	const int stride = std::max(1, (int)hits.size() / maxSources);
	int added = 0;
	for (size_t i = 0; i < hits.size() && added < maxSources; i += (size_t)stride) {
		float simX = ((hits[i].x - startX) / roi.width) * simW;
		float simY = ((hits[i].y - startY) / roi.height) * simH;
		waterSimAddWater(simX, simY, 15.0f, 0.3f);
		++added;
	}
}

// --- Unified water simulation accessors -----------------------------

void ofApp::waterSimSetup(int w, int h) {
	// Auto-detect compute shader support (requires GL 4.3+)
	useComputeWaterSim = ComputeWaterSimulation::isComputeSupported();

	if (useComputeWaterSim) {
		ofLogNotice("ofApp") << "GL 4.3+ detected - using COMPUTE SHADER water simulation";
		waterSimCompute.setup(w, h);
		// setup() can fail (e.g. a compute shader fails to compile/link). Don't
		// silently run a no-op water sim - fall back to the fragment path.
		if (!waterSimCompute.isInitialized()) {
			ofLogError("ofApp") << "Compute water simulation failed to initialize - falling back to FRAGMENT SHADER water simulation";
			useComputeWaterSim = false;
			waterSimFragment.setup(w, h);
		}
	} else {
		ofLogNotice("ofApp") << "GL < 4.3 - using FRAGMENT SHADER water simulation (fallback)";
		waterSimFragment.setup(w, h);
	}
}

void ofApp::waterSimUpdate(ofTexture& depthTex, float dt) {
	if (useComputeWaterSim) waterSimCompute.update(depthTex, dt);
	else waterSimFragment.update(depthTex, dt);
}

void ofApp::waterSimDraw(float w, float h) {
	if (useComputeWaterSim) waterSimCompute.draw(w, h);
	else waterSimFragment.draw(w, h);
}

void ofApp::waterSimAddWater(float x, float y, float radius, float amount) {
	if (useComputeWaterSim) waterSimCompute.addWater(x, y, radius, amount);
	else waterSimFragment.addWater(x, y, radius, amount);
}

bool ofApp::waterSimIsEnabled() const {
	if (useComputeWaterSim) return waterSimCompute.isEnabled();
	else return waterSimFragment.isEnabled();
}

void ofApp::waterSimToggleEnabled() {
	if (useComputeWaterSim) waterSimCompute.setEnabled(!waterSimCompute.isEnabled());
	else waterSimFragment.setEnabled(!waterSimFragment.isEnabled());
}

int ofApp::waterSimGetSimWidth() const {
	if (useComputeWaterSim) return waterSimCompute.getSimWidth();
	else return waterSimFragment.getSimWidth();
}

int ofApp::waterSimGetSimHeight() const {
	if (useComputeWaterSim) return waterSimCompute.getSimHeight();
	else return waterSimFragment.getSimHeight();
}

void ofApp::waterSimSetLavaMode(bool enabled) {
	if (useComputeWaterSim) {
		waterSimCompute.setFluidType(enabled
			? ComputeWaterSimulation::FLUID_LAVA
			: ComputeWaterSimulation::FLUID_WATER);
	} else {
		waterSimFragment.setLavaMode(enabled);
	}
}

void ofApp::waterSimClear() {
	if (useComputeWaterSim) waterSimCompute.clearWater();
	else waterSimFragment.clearWater();
}

void ofApp::waterSimSetEvaporation(float rate) {
	if (useComputeWaterSim) waterSimCompute.setEvaporationRate(rate);
	else waterSimFragment.setEvaporationRate(rate);
}

void ofApp::waterSimSetErosion(bool on) {
	if (useComputeWaterSim) waterSimCompute.setErosionEnabled(on);
	else waterSimFragment.setErosionEnabled(on);
}

bool ofApp::waterSimIsErosion() const {
	return useComputeWaterSim ? waterSimCompute.isErosionEnabled() : waterSimFragment.isErosionEnabled();
}

float ofApp::waterSimGetEvaporation() const {
	return useComputeWaterSim ? waterSimCompute.getEvaporationRate() : waterSimFragment.getEvaporationRate();
}

bool ofApp::waterSimIsLavaMode() const {
	if (useComputeWaterSim) return waterSimCompute.getFluidType() == ComputeWaterSimulation::FLUID_LAVA;
	else return waterSimFragment.isLavaMode();
}
