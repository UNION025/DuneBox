/***********************************************************************
WaterSimulation.h - GPU-based shallow water simulation using adapted
SARndbox GLSL shaders via ofFbo ping-pong rendering.

Implements the Saint-Venant (shallow-water) equations with:
- Kurganov-Petrova central-upwind scheme for numerical fluxes
- Minmod flux limiter for slope reconstruction
- Two-stage Heun (modified Euler) Runge-Kutta time integration
- Desingularized velocity to handle dry areas

Original shader algorithms: Oliver Kreylos, 2012-2018 (GPL v2)
Port to OpenFrameworks: DuneBox project, 2025

This file is part of DuneBox, a fork of Magic Sand.
***********************************************************************/

#pragma once

#include "ofMain.h"

class WaterSimulation {
public:
    WaterSimulation();
    ~WaterSimulation();

    /// Initialize FBOs and load adapted shaders.
    /// @param width  Simulation grid width in cells
    /// @param height Simulation grid height in cells
    void setup(int width, int height);

    /// Run one simulation step (RK2 integration).
    /// @param depthTexture  Current terrain depth/elevation texture
    /// @param dt            Frame delta time in seconds
    void update(ofTexture& depthTexture, float dt);

    /// Draw the water overlay to the current framebuffer (at sim resolution).
    void draw();

    /// Draw the water overlay scaled to the given dimensions.
    void draw(float w, float h);

    /// Add water at a point (rain gesture). Coordinates in simulation grid space.
    void addWater(float x, float y, float radius, float amount);

    /// Get the water render output texture for compositing.
    ofTexture& getOutputTexture();

    /// Get the current water quantity texture (for integration with surface renderer).
    ofTexture& getQuantityTexture();

    /// Get the current bathymetry texture.
    ofTexture& getBathymetryTexture();
    void resetQuantityToTerrain(int bathymetryIndex);
    ofTexture& getWaterAddTexture() { return waterAddFbo.getTexture(); } // last frame's sources (diagnostics)

    // --- Configuration ---
    void setGravity(float g);
    float getGravity() const { return gravity; }

    void setAttenuation(float a);
    float getAttenuation() const { return attenuation; }

    void setTheta(float t);
    float getTheta() const { return theta; }

    void setEpsilon(float e);
    float getEpsilon() const { return epsilon; }

    void setCellSize(float cs);
    float getCellSize() const { return cellSize; }

    void setWaterOpacity(float opacity);
    float getWaterOpacity() const { return waterOpacity; }

    void setMaxStepsPerFrame(int steps);
    int getMaxStepsPerFrame() const { return maxStepsPerFrame; }

    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled; }

    void setLavaMode(bool enabled);
    bool isLavaMode() const { return lavaActive; }

    /// Water removed per second everywhere (0 = none). Not applied to lava.
    void setEvaporationRate(float rate) { evaporationRate = std::max(0.0f, rate); }
    float getEvaporationRate() const { return evaporationRate; }

    /// Remove all water (and lava) from the grid.
    void clearWater();

    /// Show where flowing water would carve the sand and where sediment
    /// would settle. Drawn only; the real sand is untouched. Not applied to lava.
    void setErosionEnabled(bool on);
    bool isErosionEnabled() const { return erosionEnabled; }
    void clearErosion();

    void loadSettings(const std::string& path = "settings/waterSettings.xml");
    void saveSettings(const std::string& path = "settings/waterSettings.xml");

    int getSimWidth() const { return simWidth; }
    int getSimHeight() const { return simHeight; }

private:
    // --- Shader pipeline passes ---
    void updateBathymetry(ofTexture& depthTexture);
    void calcDerivative(int quantityIndex);
    void eulerStep(int srcIndex, float stepSize);
    void rungeKuttaStep(int srcIndex, int starIndex, float stepSize);
    void applyBoundary(int targetIndex);
    void applyWaterAdditions(float stepSize);
    void applyWaterUpdate(int quantityIndex);
    void applyErosion(float dt);

    // --- Fullscreen quad drawing ---
    void drawFullscreenQuad();

    // --- FBOs ---

    // Water state: (w, hu, hv) in RGB channels.
    // Triple-buffered: [0] and [1] for ping-pong, [2] for Euler scratch.
    ofFbo quantityFbo[3];

    // Terrain elevation (vertex-centered, R channel).
    // Double-buffered for old/new comparison.
    ofFbo bathymetryFbo[2];

    // Temporal derivative dq/dt (RGB channels).
    ofFbo derivativeFbo;

    // Additive water source/sink texture (R channel).
    ofFbo waterAddFbo;

    // Final rendered water overlay.
    ofFbo waterRenderFbo;

    // Erosion layer: R = bed change, G = suspended sediment. Ping-pong.
    ofFbo sedimentFbo[2];
    int currentSediment = 0;

    // --- Shaders ---
    ofShader bathymetryUpdateShader;
    ofShader slopeFluxDerivShader;
    ofShader eulerStepShader;
    ofShader rungeKuttaStepShader;
    ofShader boundaryShader;
    ofShader waterAddShader;
    ofShader waterUpdateShader;
    ofShader waterRenderShader;
    ofShader erosionShader;

    // --- Fullscreen quad mesh ---
    ofVboMesh quadMesh;

    // --- Cached boundary edge meshes (built once in setup, drawn every frame) ---
    ofVboMesh boundaryEdges[4]; // bottom, top, left, right
    struct WaterAddition {
        float x, y, radius, amount;
    };
    std::vector<WaterAddition> pendingWaterAdds;

    // --- State ---
    int currentQuantity;    // Ping-pong index for quantity FBOs (0 or 1)
    int currentBathymetry;  // Ping-pong index for bathymetry FBOs (0 or 1)
    bool bathymetryDirty;   // Whether bathymetry needs updating

    // --- Parameters ---
    float gravity;          // Gravitational acceleration (default 9.81)
    float attenuation;      // Momentum damping per second (default 0.99)
    float theta;            // Minmod limiter parameter, 1.0-2.0 (default 1.5)
    float epsilon;          // Desingularization coefficient (default 0.01)
    float cellSize;         // Physical cell size in world units (default 1.0)
    float waterOpacity;     // Rendering opacity scale (default 5.0)
    float fixedDt;          // Fixed simulation timestep (default 0.01)
    int maxStepsPerFrame;   // Max simulation substeps per frame (default 5)
    bool enabled;
    bool initialized;
    bool lavaActive;
    float evaporationRate = 0.0f;
    bool erosionEnabled = false;

    // Saved base values for toggling lava mode
    float baseAttenuation;
    float baseWaterOpacity;

    int simWidth, simHeight;
};
