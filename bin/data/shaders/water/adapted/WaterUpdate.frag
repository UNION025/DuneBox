// Adapted from SARndbox Water2WaterUpdateShader.fs
// Original: Oliver Kreylos, 2012. GPL v2.
// Applies accumulated water additions/removals to the quantity grid.
#version 150

uniform sampler2D bathymetrySampler;
uniform sampler2D quantitySampler;
uniform sampler2D waterSampler;
uniform vec2 texelSize;
uniform float seaLevel;  // terrain value of sea level
uniform float seaKeep;   // fraction of water kept over the sea this update
uniform float landRain;  // depth added this update to every cell above sea level

in vec2 vTexCoord;
out vec4 fragColor;

void main() {
    vec2 fc = vTexCoord;

    float b = (texture(bathymetrySampler, fc + vec2(-texelSize.x, -texelSize.y)).r +
               texture(bathymetrySampler, fc + vec2(0.0, -texelSize.y)).r +
               texture(bathymetrySampler, fc + vec2(-texelSize.x, 0.0)).r +
               texture(bathymetrySampler, fc).r) * 0.25;

    vec3 q = texture(quantitySampler, fc).rgb;

    float hOld = q.x - b;
    float hNew = max(hOld + texture(waterSampler, fc).r + (b > seaLevel ? landRain : 0.0), 0.0);
    // Rivers end in the sea: water there drains away instead of filling the box.
    if (b < seaLevel) hNew *= seaKeep;

    q.x = hNew + b;
    q.yz = hNew == 0.0 ? vec2(0.0, 0.0) : (hNew < hOld ? q.yz * (hNew / hOld) : q.yz);

    fragColor = vec4(q, 0.0);
}
