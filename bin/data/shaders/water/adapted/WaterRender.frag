// Adapted from SARndbox WaterRenderingShader.fs + SurfaceAddWaterColor.fs
// Original: Oliver Kreylos, 2014. GPL v2.
// Simplified water visualization as a blue-tinted overlay.
// Uses water depth for opacity and adds simple specular highlight.
// Supports lava mode with emissive volcanic gradient.
#version 150

uniform sampler2D quantitySampler;
uniform sampler2D bathymetrySampler;
uniform vec2 texelSize;
uniform float waterOpacity;
uniform float time;
uniform int uLavaMode;
uniform sampler2D sedimentSampler;
uniform int uErosion;
uniform float erosionGain;

in vec2 vTexCoord;
out vec4 fragColor;

const vec3 SCAR_COLOR = vec3(0.30, 0.17, 0.08);
const vec3 DEPOSIT_COLOR = vec3(0.96, 0.86, 0.62);
const vec3 MUD_COLOR = vec3(0.45, 0.33, 0.18);

void main() {
    // Surface height minus cell-centred terrain, per pixel.
    float w = texture(quantitySampler, vTexCoord).r;
    float b = (texture(bathymetrySampler, vTexCoord + vec2(-texelSize.x, -texelSize.y)).r +
               texture(bathymetrySampler, vTexCoord + vec2(0.0, -texelSize.y)).r +
               texture(bathymetrySampler, vTexCoord + vec2(-texelSize.x, 0.0)).r +
               texture(bathymetrySampler, vTexCoord).r) * 0.25;
    float waterDepth = max(w - b, 0.0);

    vec2 sediment = uErosion == 1 ? texture(sedimentSampler, vTexCoord).rg : vec2(0.0);

    if (waterDepth < 0.001) {
        float mark = clamp(abs(sediment.r) * erosionGain, 0.0, 0.7);
        if (mark < 0.03) discard;
        fragColor = vec4(sediment.r < 0.0 ? SCAR_COLOR : DEPOSIT_COLOR, mark);
        return;
    }

    // Compute simple normal from water surface height differences
    float wL = texture(quantitySampler, vTexCoord + vec2(-texelSize.x, 0.0)).r;
    float wR = texture(quantitySampler, vTexCoord + vec2(texelSize.x, 0.0)).r;
    float wD = texture(quantitySampler, vTexCoord + vec2(0.0, -texelSize.y)).r;
    float wU = texture(quantitySampler, vTexCoord + vec2(0.0, texelSize.y)).r;

    vec3 normal = normalize(vec3(wL - wR, wD - wU, 2.0));

    // Simple specular highlight from overhead light
    vec3 lightDir = normalize(vec3(0.3, 0.3, 1.0));
    vec3 viewDir = vec3(0.0, 0.0, 1.0);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), 64.0);

    if (uLavaMode == 1) {
        // Lava gradient based on depth
        float d = clamp(waterDepth * waterOpacity * 0.125, 0.0, 1.0);

        // Cooling crust: very shallow lava darkens to near-black
        vec3 crustColor = vec3(0.08, 0.02, 0.01);
        // Shallow: bright orange-yellow with glow
        vec3 shallowColor = vec3(1.0, 0.784, 0.196);
        // Medium: deep orange-red
        vec3 mediumColor = vec3(0.863, 0.314, 0.078);
        // Deep: dark red-black (cooling lava)
        vec3 deepColor = vec3(0.314, 0.039, 0.02);

        vec3 lavaColor;
        if (d < 0.08) {
            // Cooling crust at the very edges
            lavaColor = mix(crustColor, shallowColor, d / 0.08);
        } else if (d < 0.4) {
            // Shallow → medium
            lavaColor = mix(shallowColor, mediumColor, (d - 0.08) / 0.32);
        } else {
            // Medium → deep
            lavaColor = mix(mediumColor, deepColor, (d - 0.4) / 0.6);
        }

        // Emissive glow: brighten based on depth (hotter interior)
        float glow = smoothstep(0.0, 0.5, d) * 0.4;
        lavaColor += vec3(glow * 0.6, glow * 0.15, glow * 0.02);

        // Reduce specular for lava (molten surface is diffuse)
        float lavaSpec = spec * 0.15;
        float alpha = clamp(waterDepth * waterOpacity, 0.0, 0.95);

        fragColor = vec4(lavaColor + vec3(lavaSpec), alpha);
    } else {
        // Water color: deep blue with depth-based opacity
        vec3 waterColor = mix(vec3(0.1, 0.3, 0.7), MUD_COLOR,
                              clamp(sediment.g * erosionGain, 0.0, 0.75));
        float alpha = clamp(waterDepth * waterOpacity, 0.0, 0.85);

        fragColor = vec4(waterColor + vec3(spec * 0.4), alpha);
    }
}
