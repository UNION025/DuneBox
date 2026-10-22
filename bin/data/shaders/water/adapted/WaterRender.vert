// Adapted from SARndbox WaterRenderingShader.vs
// Original: Oliver Kreylos, 2014. GPL v2.
// Simplified for 2D overlay rendering (no 3D mesh / Gouraud lighting).
// Passes through water depth as alpha for transparency.
#version 150

// Water depth is computed per fragment (WaterRender.frag): this shader runs on
// the 4-vertex fullscreen quad, so per-vertex depth was the corners' depth
// stretched over two triangles (straight-edged water polygons).

in vec4 position;
in vec2 texcoord;

out vec2 vTexCoord;

void main() {
    vTexCoord = texcoord;
    gl_Position = position;
}
