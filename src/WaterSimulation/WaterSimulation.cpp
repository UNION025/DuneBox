/***********************************************************************
WaterSimulation.cpp - GPU-based shallow water simulation using adapted
SARndbox GLSL shaders via ofFbo ping-pong rendering.

Implements the per-frame simulation loop:
  1. Update bathymetry from new depth data
  2. RK2 integration loop (predictor + corrector):
     a. Compute derivative from current state
     b. Euler half-step -> intermediate state q*
     c. Compute derivative from q*
     d. RK2 corrector -> final state
     e. Apply boundary conditions
  3. Apply rain/drain additions
  4. Render water overlay

Original shader algorithms: Oliver Kreylos, 2012-2018 (GPL v2)
Port to OpenFrameworks: DuneBox project, 2025

This file is part of DuneBox, a fork of Magic Sand.
***********************************************************************/

#include "WaterSimulation.h"
#include "ErosionParams.h"
#include "ofXml.h"

static const string SHADER_PATH = "shaders/water/adapted/";

// --- Construction ---------------------------------------------------

WaterSimulation::WaterSimulation()
    : currentQuantity(0)
    , currentBathymetry(0)
    , bathymetryDirty(true)
    , gravity(9.81f)
    , attenuation(0.99f)
    , theta(1.5f)
    , epsilon(0.01f)
    , cellSize(1.0f)
    , waterOpacity(5.0f)
    , fixedDt(0.01f)
    , maxStepsPerFrame(5)
    , enabled(false)
    , initialized(false)
    , lavaActive(false)
    , baseAttenuation(0.99f)
    , baseWaterOpacity(5.0f)
    , simWidth(0)
    , simHeight(0)
{
}

WaterSimulation::~WaterSimulation() {
}

// --- Setup ----------------------------------------------------------

void WaterSimulation::setup(int width, int height) {
    simWidth = width;
    simHeight = height;

    ofLogNotice("WaterSimulation") << "Setting up " << simWidth << "x" << simHeight << " simulation grid";

    // --- Load shaders ---
    string vp = SHADER_PATH + "passthrough.vert";

    bathymetryUpdateShader.load(vp, SHADER_PATH + "BathymetryUpdate.frag");
    slopeFluxDerivShader.load(vp, SHADER_PATH + "SlopeFluxDeriv.frag");
    eulerStepShader.load(vp, SHADER_PATH + "EulerStep.frag");
    rungeKuttaStepShader.load(vp, SHADER_PATH + "RungeKuttaStep.frag");
    boundaryShader.load(vp, SHADER_PATH + "Boundary.frag");
    waterAddShader.load(SHADER_PATH + "WaterAdd.vert", SHADER_PATH + "WaterAdd.frag");
    waterUpdateShader.load(vp, SHADER_PATH + "WaterUpdate.frag");
    waterRenderShader.load(SHADER_PATH + "WaterRender.vert", SHADER_PATH + "WaterRender.frag");
    if (!erosionShader.load(vp, SHADER_PATH + "Erosion.frag")) {
        ofLogWarning("WaterSimulation") << "Erosion shader failed to load - erosion view unavailable";
    }

    // Verify all shaders loaded
    bool allLoaded = bathymetryUpdateShader.isLoaded()
        && slopeFluxDerivShader.isLoaded()
        && eulerStepShader.isLoaded()
        && rungeKuttaStepShader.isLoaded()
        && boundaryShader.isLoaded()
        && waterAddShader.isLoaded()
        && waterUpdateShader.isLoaded()
        && waterRenderShader.isLoaded();

    if (!allLoaded) {
        ofLogError("WaterSimulation") << "One or more shaders failed to load - water simulation disabled";
        if (!bathymetryUpdateShader.isLoaded()) ofLogError("WaterSimulation") << "  Missing: BathymetryUpdate";
        if (!slopeFluxDerivShader.isLoaded())   ofLogError("WaterSimulation") << "  Missing: SlopeFluxDeriv";
        if (!eulerStepShader.isLoaded())         ofLogError("WaterSimulation") << "  Missing: EulerStep";
        if (!rungeKuttaStepShader.isLoaded())    ofLogError("WaterSimulation") << "  Missing: RungeKuttaStep";
        if (!boundaryShader.isLoaded())          ofLogError("WaterSimulation") << "  Missing: Boundary";
        if (!waterAddShader.isLoaded())          ofLogError("WaterSimulation") << "  Missing: WaterAdd";
        if (!waterUpdateShader.isLoaded())       ofLogError("WaterSimulation") << "  Missing: WaterUpdate";
        if (!waterRenderShader.isLoaded())       ofLogError("WaterSimulation") << "  Missing: WaterRender";
        return;
    }

    // --- Allocate FBOs ---

    // Quantity FBOs: RGB32F for (w, hu, hv)
    ofFboSettings qSettings;
    qSettings.width = simWidth;
    qSettings.height = simHeight;
    qSettings.internalformat = GL_RGBA32F;
    qSettings.numColorbuffers = 1;
    qSettings.useDepth = false;
    qSettings.textureTarget = GL_TEXTURE_2D;
    qSettings.minFilter = GL_NEAREST;
    qSettings.maxFilter = GL_NEAREST;
    qSettings.wrapModeHorizontal = GL_CLAMP_TO_EDGE;
    qSettings.wrapModeVertical = GL_CLAMP_TO_EDGE;

    for (int i = 0; i < 3; i++) {
        quantityFbo[i].allocate(qSettings);
        quantityFbo[i].begin();
        ofClear(0, 0, 0, 0);
        quantityFbo[i].end();
    }

    // Bathymetry FBOs: stores terrain elevation.
    // In SARndbox this is vertex-centered at (size-1)x(size-1), but for the
    // POC we use the same grid size and average 4 neighbors in the shaders
    // to get cell-centered values (matching the original shader convention).
    ofFboSettings bSettings;
    bSettings.width = simWidth;
    bSettings.height = simHeight;
    bSettings.internalformat = GL_RGBA32F;
    bSettings.numColorbuffers = 1;
    bSettings.useDepth = false;
    bSettings.textureTarget = GL_TEXTURE_2D;
    bSettings.minFilter = GL_NEAREST;
    bSettings.maxFilter = GL_NEAREST;
    bSettings.wrapModeHorizontal = GL_CLAMP_TO_EDGE;
    bSettings.wrapModeVertical = GL_CLAMP_TO_EDGE;

    for (int i = 0; i < 2; i++) {
        bathymetryFbo[i].allocate(bSettings);
        bathymetryFbo[i].begin();
        ofClear(0, 0, 0, 0);
        bathymetryFbo[i].end();
    }

    // Derivative FBO: RGB32F
    ofFboSettings dSettings = qSettings;
    derivativeFbo.allocate(dSettings);
    derivativeFbo.begin();
    ofClear(0, 0, 0, 0);
    derivativeFbo.end();

    // Water addition FBO: single channel accumulator
    ofFboSettings wSettings = qSettings;
    waterAddFbo.allocate(wSettings);
    waterAddFbo.begin();
    ofClear(0, 0, 0, 0);
    waterAddFbo.end();

    // Water render output FBO: RGBA for overlay compositing
    ofFboSettings rSettings;
    rSettings.width = simWidth;
    rSettings.height = simHeight;
    rSettings.internalformat = GL_RGBA8;
    rSettings.numColorbuffers = 1;
    rSettings.useDepth = false;
    rSettings.textureTarget = GL_TEXTURE_2D;
    rSettings.minFilter = GL_LINEAR;
    rSettings.maxFilter = GL_LINEAR;
    rSettings.wrapModeHorizontal = GL_CLAMP_TO_EDGE;
    rSettings.wrapModeVertical = GL_CLAMP_TO_EDGE;

    waterRenderFbo.allocate(rSettings);
    waterRenderFbo.begin();
    ofClear(0, 0, 0, 0);
    waterRenderFbo.end();

    for (int i = 0; i < 2; i++) {
        sedimentFbo[i].allocate(qSettings);
    }
    currentSediment = 0;
    clearErosion();

    // --- Build fullscreen quad mesh ---
    quadMesh.clear();
    quadMesh.setMode(OF_PRIMITIVE_TRIANGLE_STRIP);
    // NDC fullscreen quad with texcoords [0,1]
    quadMesh.addVertex(ofVec3f(-1, -1, 0));
    quadMesh.addTexCoord(ofVec2f(0, 0));
    quadMesh.addVertex(ofVec3f(1, -1, 0));
    quadMesh.addTexCoord(ofVec2f(1, 0));
    quadMesh.addVertex(ofVec3f(-1, 1, 0));
    quadMesh.addTexCoord(ofVec2f(0, 1));
    quadMesh.addVertex(ofVec3f(1, 1, 0));
    quadMesh.addTexCoord(ofVec2f(1, 1));

    // --- Build cached boundary edge meshes (4 edges, drawn every frame) ---
    float hpw = 0.5f / simWidth;
    float hph = 0.5f / simHeight;
    float pw = 1.0f / simWidth;
    float ph = 1.0f / simHeight;

    // Bottom edge (row 0)
    boundaryEdges[0].clear();
    boundaryEdges[0].setMode(OF_PRIMITIVE_TRIANGLE_STRIP);
    boundaryEdges[0].addVertex(ofVec3f(-1.0, -1.0, 0));
    boundaryEdges[0].addTexCoord(ofVec2f(hpw, hph));
    boundaryEdges[0].addVertex(ofVec3f(1.0, -1.0, 0));
    boundaryEdges[0].addTexCoord(ofVec2f(1.0 - hpw, hph));
    boundaryEdges[0].addVertex(ofVec3f(-1.0, -1.0 + 2.0 * ph, 0));
    boundaryEdges[0].addTexCoord(ofVec2f(hpw, hph));
    boundaryEdges[0].addVertex(ofVec3f(1.0, -1.0 + 2.0 * ph, 0));
    boundaryEdges[0].addTexCoord(ofVec2f(1.0 - hpw, hph));

    // Top edge (row simHeight-1)
    boundaryEdges[1].clear();
    boundaryEdges[1].setMode(OF_PRIMITIVE_TRIANGLE_STRIP);
    boundaryEdges[1].addVertex(ofVec3f(-1.0, 1.0 - 2.0 * ph, 0));
    boundaryEdges[1].addTexCoord(ofVec2f(hpw, 1.0 - hph));
    boundaryEdges[1].addVertex(ofVec3f(1.0, 1.0 - 2.0 * ph, 0));
    boundaryEdges[1].addTexCoord(ofVec2f(1.0 - hpw, 1.0 - hph));
    boundaryEdges[1].addVertex(ofVec3f(-1.0, 1.0, 0));
    boundaryEdges[1].addTexCoord(ofVec2f(hpw, 1.0 - hph));
    boundaryEdges[1].addVertex(ofVec3f(1.0, 1.0, 0));
    boundaryEdges[1].addTexCoord(ofVec2f(1.0 - hpw, 1.0 - hph));

    // Left edge (column 0)
    boundaryEdges[2].clear();
    boundaryEdges[2].setMode(OF_PRIMITIVE_TRIANGLE_STRIP);
    boundaryEdges[2].addVertex(ofVec3f(-1.0, -1.0, 0));
    boundaryEdges[2].addTexCoord(ofVec2f(hpw, hph));
    boundaryEdges[2].addVertex(ofVec3f(-1.0 + 2.0 * pw, -1.0, 0));
    boundaryEdges[2].addTexCoord(ofVec2f(hpw, hph));
    boundaryEdges[2].addVertex(ofVec3f(-1.0, 1.0, 0));
    boundaryEdges[2].addTexCoord(ofVec2f(hpw, 1.0 - hph));
    boundaryEdges[2].addVertex(ofVec3f(-1.0 + 2.0 * pw, 1.0, 0));
    boundaryEdges[2].addTexCoord(ofVec2f(hpw, 1.0 - hph));

    // Right edge (column simWidth-1)
    boundaryEdges[3].clear();
    boundaryEdges[3].setMode(OF_PRIMITIVE_TRIANGLE_STRIP);
    boundaryEdges[3].addVertex(ofVec3f(1.0 - 2.0 * pw, -1.0, 0));
    boundaryEdges[3].addTexCoord(ofVec2f(1.0 - hpw, hph));
    boundaryEdges[3].addVertex(ofVec3f(1.0, -1.0, 0));
    boundaryEdges[3].addTexCoord(ofVec2f(1.0 - hpw, hph));
    boundaryEdges[3].addVertex(ofVec3f(1.0 - 2.0 * pw, 1.0, 0));
    boundaryEdges[3].addTexCoord(ofVec2f(1.0 - hpw, 1.0 - hph));
    boundaryEdges[3].addVertex(ofVec3f(1.0, 1.0, 0));
    boundaryEdges[3].addTexCoord(ofVec2f(1.0 - hpw, 1.0 - hph));

    currentQuantity = 0;
    currentBathymetry = 0;
    bathymetryDirty = true;
    initialized = true;

    ofLogNotice("WaterSimulation") << "Setup complete. FBOs: "
        << simWidth << "x" << simHeight << " RGBA32F";

    loadSettings();
}

// --- Fullscreen quad helper -----------------------------------------

void WaterSimulation::drawFullscreenQuad() {
    quadMesh.draw();
}

// --- Update: run simulation step ------------------------------------

void WaterSimulation::update(ofTexture& depthTexture, float dt) {
    if (!enabled || !initialized) return;

    // The passes write state, not colour: their outputs carry alpha 0, so with
    // OF's default alpha blending every write was discarded and the quantity
    // grid stayed at zero (no water ever existed). Blend only where a pass asks.
    ofPushStyle();
    ofDisableBlendMode();

    // Step 0: Update bathymetry if terrain changed
    updateBathymetry(depthTexture);

    // Fixed timestep simulation with multiple substeps
    float timeRemaining = dt;
    int numSteps = 0;

    while (timeRemaining > 1e-8f && numSteps < maxStepsPerFrame) {
        float stepDt = min(fixedDt, timeRemaining);

        // -- Step 1: Compute derivative from current state --
        calcDerivative(currentQuantity);

        // -- Step 2: Euler predictor -> q* (stored in quantityFbo[2]) --
        eulerStep(currentQuantity, stepDt);

        // -- Step 3: Compute derivative from q* --
        calcDerivative(2); // derivative of intermediate state

        // -- Step 4: RK2 corrector -> q_new --
        rungeKuttaStep(currentQuantity, 2, stepDt);

        // -- Step 5: Boundary conditions --
        applyBoundary(1 - currentQuantity);

        // Flip ping-pong
        currentQuantity = 1 - currentQuantity;
        timeRemaining -= stepDt;
        numSteps++;
    }

    // Step 6: Apply water additions (rain gestures) and evaporation
    if (!pendingWaterAdds.empty() || (evaporationRate > 0.0f && !lavaActive)) {
        applyWaterAdditions(fixedDt);
        applyWaterUpdate(currentQuantity);
        currentQuantity = 1 - currentQuantity;
        pendingWaterAdds.clear();
    }

    // Step 7: Carry sediment with the flow
    bool showErosion = erosionEnabled && !lavaActive && erosionShader.isLoaded();
    if (showErosion) {
        applyErosion(dt);
    }

    // Step 8: Render water overlay
    waterRenderFbo.begin();
    ofClear(0, 0, 0, 0);

    waterRenderShader.begin();
    waterRenderShader.setUniformTexture("sedimentSampler", sedimentFbo[currentSediment].getTexture(), 2);
    waterRenderShader.setUniform1i("uErosion", showErosion ? 1 : 0);
    waterRenderShader.setUniform1f("erosionGain", ErosionParams::displayGain);
    waterRenderShader.setUniformTexture("quantitySampler", quantityFbo[currentQuantity].getTexture(), 0);
    waterRenderShader.setUniformTexture("bathymetrySampler", bathymetryFbo[currentBathymetry].getTexture(), 1);
    waterRenderShader.setUniform2f("texelSize", 1.0f / simWidth, 1.0f / simHeight);
    waterRenderShader.setUniform1f("waterOpacity", waterOpacity);
    waterRenderShader.setUniform1f("time", ofGetElapsedTimef());
    waterRenderShader.setUniform1i("uLavaMode", lavaActive ? 1 : 0);
    drawFullscreenQuad();
    waterRenderShader.end();

    waterRenderFbo.end();
    ofPopStyle();
}

// --- Pipeline passes ------------------------------------------------

void WaterSimulation::updateBathymetry(ofTexture& depthTexture) {
    int newBathy = 1 - currentBathymetry;

    // Render the depth texture into the new bathymetry FBO
    bathymetryFbo[newBathy].begin();
    ofClear(0, 0, 0, 0);
    depthTexture.draw(0, 0, simWidth, simHeight);
    bathymetryFbo[newBathy].end();

    if (bathymetryDirty) {
        // First frame (or after clearWater): start dry on this terrain.
        bathymetryDirty = false;
        currentBathymetry = newBathy;
        resetQuantityToTerrain(newBathy);
        return;
    }

    // Run BathymetryUpdateShader to adjust water heights
    int newQuantity = 1 - currentQuantity;
    quantityFbo[newQuantity].begin();

    bathymetryUpdateShader.begin();
    bathymetryUpdateShader.setUniformTexture("oldBathymetrySampler",
        bathymetryFbo[currentBathymetry].getTexture(), 0);
    bathymetryUpdateShader.setUniformTexture("newBathymetrySampler",
        bathymetryFbo[newBathy].getTexture(), 1);
    bathymetryUpdateShader.setUniformTexture("quantitySampler",
        quantityFbo[currentQuantity].getTexture(), 2);
    bathymetryUpdateShader.setUniform2f("texelSize", 1.0f / simWidth, 1.0f / simHeight);
    drawFullscreenQuad();
    bathymetryUpdateShader.end();

    quantityFbo[newQuantity].end();

    currentBathymetry = newBathy;
    currentQuantity = newQuantity;
}

void WaterSimulation::calcDerivative(int quantityIndex) {
    derivativeFbo.begin();

    slopeFluxDerivShader.begin();
    slopeFluxDerivShader.setUniform2f("cellSize", cellSize, cellSize);
    slopeFluxDerivShader.setUniform1f("theta", theta);
    slopeFluxDerivShader.setUniform1f("g", gravity);
    slopeFluxDerivShader.setUniform1f("epsilon", epsilon);
    slopeFluxDerivShader.setUniformTexture("bathymetrySampler",
        bathymetryFbo[currentBathymetry].getTexture(), 0);
    slopeFluxDerivShader.setUniformTexture("quantitySampler",
        quantityFbo[quantityIndex].getTexture(), 1);
    slopeFluxDerivShader.setUniform2f("texelSize", 1.0f / simWidth, 1.0f / simHeight);
    drawFullscreenQuad();
    slopeFluxDerivShader.end();

    derivativeFbo.end();
}

void WaterSimulation::eulerStep(int srcIndex, float stepSize) {
    float atten = pow(attenuation, stepSize);

    // Euler step always writes to quantityFbo[2] (scratch buffer)
    quantityFbo[2].begin();

    eulerStepShader.begin();
    eulerStepShader.setUniform1f("stepSize", stepSize);
    eulerStepShader.setUniform1f("attenuation", atten);
    eulerStepShader.setUniformTexture("quantitySampler",
        quantityFbo[srcIndex].getTexture(), 0);
    eulerStepShader.setUniformTexture("derivativeSampler",
        derivativeFbo.getTexture(), 1);
    drawFullscreenQuad();
    eulerStepShader.end();

    quantityFbo[2].end();
}

void WaterSimulation::rungeKuttaStep(int srcIndex, int starIndex, float stepSize) {
    float atten = pow(attenuation, stepSize);
    int targetIndex = 1 - srcIndex;

    quantityFbo[targetIndex].begin();

    rungeKuttaStepShader.begin();
    rungeKuttaStepShader.setUniform1f("stepSize", stepSize);
    rungeKuttaStepShader.setUniform1f("attenuation", atten);
    rungeKuttaStepShader.setUniformTexture("quantitySampler",
        quantityFbo[srcIndex].getTexture(), 0);
    rungeKuttaStepShader.setUniformTexture("quantityStarSampler",
        quantityFbo[starIndex].getTexture(), 1);
    rungeKuttaStepShader.setUniformTexture("derivativeSampler",
        derivativeFbo.getTexture(), 2);
    drawFullscreenQuad();
    rungeKuttaStepShader.end();

    quantityFbo[targetIndex].end();
}

void WaterSimulation::applyBoundary(int targetIndex) {
    quantityFbo[targetIndex].begin();

    boundaryShader.begin();
    boundaryShader.setUniformTexture("bathymetrySampler",
        bathymetryFbo[currentBathymetry].getTexture(), 0);
    boundaryShader.setUniform2f("texelSize", 1.0f / simWidth, 1.0f / simHeight);

    for (int i = 0; i < 4; i++) {
        boundaryEdges[i].draw();
    }

    boundaryShader.end();
    quantityFbo[targetIndex].end();
}

void WaterSimulation::applyWaterAdditions(float stepSize) {
    // Clear the water addition FBO
    waterAddFbo.begin();
    ofClear(0, 0, 0, 0);

    // Plain additive blending (GL_ONE, GL_ONE) as in SARndbox, so overlapping
    // sources sum. OF_BLENDMODE_ADD is (SRC_ALPHA, ONE): with the amount also in
    // alpha it added amount^2 (~1e-6), and no rain or river water ever appeared.
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE);

    waterAddShader.begin();
    waterAddShader.setUniform1f("stepSize", stepSize);

    for (const auto& wa : pendingWaterAdds) {
        // Convert grid coordinates to NDC [-1,1]
        float cx = (wa.x / simWidth) * 2.0f - 1.0f;
        float cy = (wa.y / simHeight) * 2.0f - 1.0f;
        float rx = (wa.radius / simWidth) * 2.0f;
        float ry = (wa.radius / simHeight) * 2.0f;

        waterAddShader.setUniform1f("waterAmount", wa.amount);

        // Build a polygon disk (32-sided, matching SARndbox)
        ofMesh disk;
        disk.setMode(OF_PRIMITIVE_TRIANGLE_FAN);

        // Center vertex
        disk.addVertex(ofVec3f(cx, cy, 0));

        int segments = 32;
        for (int i = 0; i <= segments; i++) {
            float angle = (float)i / segments * TWO_PI;
            float px = cx + cos(angle) * rx;
            float py = cy + sin(angle) * ry;
            disk.addVertex(ofVec3f(px, py, 0));
        }

        disk.draw();
    }

    // Evaporation: a negative source over the whole grid. WaterUpdate clamps
    // depth at zero, so dry cells stay dry.
    if (evaporationRate > 0.0f && !lavaActive) {
        waterAddShader.setUniform1f("waterAmount", -evaporationRate);
        quadMesh.draw();
    }

    waterAddShader.end();
    ofDisableBlendMode();
    waterAddFbo.end();
}

void WaterSimulation::applyWaterUpdate(int quantityIndex) {
    int targetIndex = 1 - quantityIndex;

    quantityFbo[targetIndex].begin();

    waterUpdateShader.begin();
    waterUpdateShader.setUniformTexture("bathymetrySampler",
        bathymetryFbo[currentBathymetry].getTexture(), 0);
    waterUpdateShader.setUniformTexture("quantitySampler",
        quantityFbo[quantityIndex].getTexture(), 1);
    waterUpdateShader.setUniformTexture("waterSampler",
        waterAddFbo.getTexture(), 2);
    waterUpdateShader.setUniform2f("texelSize", 1.0f / simWidth, 1.0f / simHeight);
    drawFullscreenQuad();
    waterUpdateShader.end();

    quantityFbo[targetIndex].end();
}

void WaterSimulation::applyErosion(float dt) {
    int target = 1 - currentSediment;

    sedimentFbo[target].begin();
    erosionShader.begin();
    erosionShader.setUniformTexture("quantitySampler", quantityFbo[currentQuantity].getTexture(), 0);
    erosionShader.setUniformTexture("bathymetrySampler", bathymetryFbo[currentBathymetry].getTexture(), 1);
    erosionShader.setUniformTexture("oldBathymetrySampler", bathymetryFbo[1 - currentBathymetry].getTexture(), 2);
    erosionShader.setUniformTexture("sedimentSampler", sedimentFbo[currentSediment].getTexture(), 3);
    erosionShader.setUniform1f("dt", std::min(dt, 0.1f));
    erosionShader.setUniform1f("cellSize", cellSize);
    erosionShader.setUniform1f("capacity", ErosionParams::capacity);
    erosionShader.setUniform1f("erodeRate", ErosionParams::erodeRate);
    erosionShader.setUniform1f("depositRate", ErosionParams::depositRate);
    erosionShader.setUniform1f("fade", ErosionParams::fade);
    erosionShader.setUniform1f("resetDelta", ErosionParams::resetDelta);
    erosionShader.setUniform1f("maxBed", ErosionParams::maxBed);
    drawFullscreenQuad();
    erosionShader.end();
    sedimentFbo[target].end();

    currentSediment = target;
}

// --- Draw -----------------------------------------------------------

void WaterSimulation::draw() {
    if (!enabled || !initialized) return;

    ofEnableAlphaBlending();
    waterRenderFbo.getTexture().draw(0, 0, simWidth, simHeight);
    ofDisableAlphaBlending();
}

void WaterSimulation::draw(float w, float h) {
    if (!enabled || !initialized) return;

    ofEnableAlphaBlending();
    waterRenderFbo.getTexture().draw(0, 0, w, h);
    ofDisableAlphaBlending();
}

// --- Public API -----------------------------------------------------

void WaterSimulation::addWater(float x, float y, float radius, float amount) {
    pendingWaterAdds.push_back({x, y, radius, amount});
}

ofTexture& WaterSimulation::getOutputTexture() {
    return waterRenderFbo.getTexture();
}

ofTexture& WaterSimulation::getQuantityTexture() {
    return quantityFbo[currentQuantity].getTexture();
}

ofTexture& WaterSimulation::getBathymetryTexture() {
    return bathymetryFbo[currentBathymetry].getTexture();
}

// --- Settings persistence -------------------------------------------

void WaterSimulation::loadSettings(const std::string& path) {
    ofXml xml;
    if (!xml.load(path)) {
        ofLogNotice("WaterSimulation") << "No settings file at " << path << ", using defaults";
        return;
    }
    auto root = xml.getChild("WATERSIMULATION");
    if (auto c = root.getChild("gravity")) gravity = c.getFloatValue();
    if (auto c = root.getChild("attenuation")) attenuation = c.getFloatValue();
    if (auto c = root.getChild("theta")) theta = c.getFloatValue();
    if (auto c = root.getChild("epsilon")) epsilon = c.getFloatValue();
    if (auto c = root.getChild("cellSize")) cellSize = c.getFloatValue();
    if (auto c = root.getChild("waterOpacity")) waterOpacity = c.getFloatValue();
    if (auto c = root.getChild("fixedDt")) fixedDt = c.getFloatValue();
    if (auto c = root.getChild("maxStepsPerFrame")) maxStepsPerFrame = c.getIntValue();
    if (auto c = root.getChild("enabled")) enabled = c.getBoolValue();

    if (auto c = root.getChild("lavaMode")) {
        bool savedLavaMode = c.getBoolValue();
        if (savedLavaMode != lavaActive) {
            setLavaMode(savedLavaMode);
        }
    }

    ofLogNotice("WaterSimulation") << "Settings loaded from " << path;
}

void WaterSimulation::saveSettings(const std::string& path) {
    ofXml xml;
    auto root = xml.appendChild("WATERSIMULATION");
    root.appendChild("gravity").set(gravity);
    root.appendChild("attenuation").set(attenuation);
    root.appendChild("theta").set(theta);
    root.appendChild("epsilon").set(epsilon);
    root.appendChild("cellSize").set(cellSize);
    root.appendChild("waterOpacity").set(waterOpacity);
    root.appendChild("fixedDt").set(fixedDt);
    root.appendChild("maxStepsPerFrame").set(maxStepsPerFrame);
    root.appendChild("enabled").set(enabled);
    root.appendChild("lavaMode").set(lavaActive);

    if (xml.save(path)) {
        ofLogNotice("WaterSimulation") << "Settings saved to " << path;
    } else {
        ofLogError("WaterSimulation") << "Failed to save settings to " << path;
    }
}

void WaterSimulation::setGravity(float g) { gravity = g; }
void WaterSimulation::setAttenuation(float a) { attenuation = a; }
void WaterSimulation::setTheta(float t) { theta = t; }
void WaterSimulation::setEpsilon(float e) { epsilon = e; }
void WaterSimulation::setCellSize(float cs) { cellSize = cs; }
void WaterSimulation::setWaterOpacity(float opacity) { waterOpacity = opacity; }
void WaterSimulation::setMaxStepsPerFrame(int steps) { maxStepsPerFrame = steps; }
void WaterSimulation::setEnabled(bool e) { enabled = e; }

void WaterSimulation::clearWater() {
    if (!initialized) return;
    pendingWaterAdds.clear();
    // Zeroing the grid would mean negative depth wherever the terrain is above
    // 0; re-start dry from the next frame's terrain instead.
    bathymetryDirty = true;
}

// Surface = terrain, no flow. The quantity grid stores the water *surface*, so
// a grid of zeros reads as negative depth, and the solver's sqrt(g*h) turns
// that into NaN that spreads one cell per step (the blocky water fronts).
void WaterSimulation::resetQuantityToTerrain(int bathymetryIndex) {
    // BathymetryUpdate with old = new terrain and a zero quantity gives
    // max(0 - b, 0) + b = b, already in the solver's cell-centred layout.
    quantityFbo[2].begin();
    ofClear(0, 0, 0, 0);
    quantityFbo[2].end();
    for (int i = 0; i < 2; i++) {
        quantityFbo[i].begin();
        bathymetryUpdateShader.begin();
        bathymetryUpdateShader.setUniformTexture("oldBathymetrySampler", bathymetryFbo[bathymetryIndex].getTexture(), 0);
        bathymetryUpdateShader.setUniformTexture("newBathymetrySampler", bathymetryFbo[bathymetryIndex].getTexture(), 1);
        bathymetryUpdateShader.setUniformTexture("quantitySampler", quantityFbo[2].getTexture(), 2);
        bathymetryUpdateShader.setUniform2f("texelSize", 1.0f / simWidth, 1.0f / simHeight);
        drawFullscreenQuad();
        bathymetryUpdateShader.end();
        quantityFbo[i].end();
    }
}

void WaterSimulation::setErosionEnabled(bool on) {
    if (on && !erosionEnabled) clearErosion();
    erosionEnabled = on;
}

void WaterSimulation::clearErosion() {
    for (int i = 0; i < 2; i++) {
        if (!sedimentFbo[i].isAllocated()) continue;
        sedimentFbo[i].begin();
        ofClear(0, 0, 0, 0);
        sedimentFbo[i].end();
    }
}

void WaterSimulation::setLavaMode(bool enabled) {
    if (enabled == lavaActive) return;

    if (enabled) {
        // Save current water parameters before overriding
        baseAttenuation = attenuation;
        baseWaterOpacity = waterOpacity;
        // Lava: viscous flow, more opaque
        attenuation = 0.85f;
        waterOpacity = 8.0f;
    } else {
        // Restore water parameters
        attenuation = baseAttenuation;
        waterOpacity = baseWaterOpacity;
    }
    lavaActive = enabled;
    ofLogNotice("WaterSimulation") << "Lava mode: " << (lavaActive ? "ON" : "OFF");
}
