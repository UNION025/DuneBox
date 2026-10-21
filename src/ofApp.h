/***********************************************************************
ofApp.h - main openframeworks app
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

#pragma once

#include "ofMain.h"
#include "ofxDatGui.h"
#include "KinectProjector/KinectProjector.h"
#include "SandSurfaceRenderer/SandSurfaceRenderer.h"
#include "Games/MapGameController.h"
#include "Games/BoidGameController.h"
#include "WaterSimulation/WaterSimulation.h"
#include "WaterSimulation/ComputeWaterSimulation.h"
#include "Diagnostics.h"
#include "Bridge/Bridge.h"
#include <memory>

class ofApp : public ofBaseApp {

public:
	void setup();
	~ofApp();
	void update();

	void draw();
	void drawProjWindow(ofEventArgs& args);

	void keyPressed(int key);
	void keyReleased(int key);
	void mouseMoved(int x, int y);
	void mouseDragged(int x, int y, int button);
	void mousePressed(int x, int y, int button);
	void mouseReleased(int x, int y, int button);
	void mouseEntered(int x, int y);
	void mouseExited(int x, int y);
	void windowResized(int w, int h);
	void dragEvent(ofDragInfo dragInfo);
	void gotMessage(ofMessage msg);

	std::shared_ptr<ofAppBaseWindow> projWindow;

private:
	std::shared_ptr<KinectProjector> kinectProjector;
	std::unique_ptr<SandSurfaceRenderer> sandSurfaceRenderer;
	CMapGameController mapGameController;
	CBoidGameController boidGameController;

	// Water simulation - auto-selects compute (GL 4.3+) or fragment (GL 3.2) path
	WaterSimulation waterSimFragment;
	ComputeWaterSimulation waterSimCompute;
	bool useComputeWaterSim;       // True if compute shader path is active

	// Unified accessors that delegate to the active simulation
	void waterSimSetup(int w, int h);
	void waterSimUpdate(ofTexture& depthTex, float dt);
	void waterSimDraw(float w, float h);
	void waterSimAddWater(float x, float y, float radius, float amount);
	bool waterSimIsEnabled() const;
	void waterSimToggleEnabled();
	void waterSimSetLavaMode(bool enabled);
	bool waterSimIsLavaMode() const;
	int  waterSimGetSimWidth() const;
	int  waterSimGetSimHeight() const;
	void waterSimClear();
	void waterSimSetEvaporation(float rate);
	float waterSimGetEvaporation() const;
	void waterSimSetErosion(bool on);
	bool waterSimIsErosion() const;

	// Dashboard commands relayed by sandcam over the bridge
	void handleDashboardCommand(const ofJson& msg);
	bool handRainEnabled = true;   // rain where hands hover over the sand

	ofFbo testTerrainFbo;          // Fallback terrain when no Kinect
	bool useTestTerrain;           // True when Kinect is unavailable
	void generateTestTerrain();    // Fill testTerrainFbo with sine-wave heightfield
	void detectRainGesture();      // Threshold-based hand detection for rain

	// Main window ROI 
	ofRectangle mainWindowROI;

	// Theme display overlay
	std::string themeDisplayName;
	float themeDisplayTimer;

	// Lava mode state
	int preLavaThemeIndex;  // theme to restore when lava deactivates

	// Volcano eruption state (bridge-triggered or manual)
	bool volcanoEruptionActive = false;
	float volcanoEruptionTimer = 0.0f;
	float volcanoSourceX = 0.0f;
	float volcanoSourceY = 0.0f;

	// Inter-app bridge (Python <-> C++)
	Bridge bridge;
	int bridgeFrameCounter = 0;

	// Startup diagnostics
	Diagnostics diagnostics;

	// Remote screen monitor: with DUNEBOX_MONITOR_DIR set, both windows are
	// saved there as main.png / projector.png every few seconds (for checking
	// a rig without screen-recording access).
	std::string monitorDir;
	uint64_t monitorLastMain = 0;
	uint64_t monitorLastProj = 0;
	void onMonitorDrawMain(ofEventArgs& args);
	void onMonitorDrawProj(ofEventArgs& args);
};
