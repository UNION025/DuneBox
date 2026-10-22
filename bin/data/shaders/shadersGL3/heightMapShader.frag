/***********************************************************************
heightMapShader - Shader fragment to display color and contourlines.
Copyright (c) 2016 Thomas Wolf

-- adapted from SurfaceAddContourLines by Oliver Kreylos
Copyright (c) 2012 Oliver Kreylos

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
with the Magic Sand; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
***********************************************************************/

#version 150

out vec4 outputColor;

in float depthfrag;

uniform sampler2DRect heightColorMapSampler;
uniform sampler2DRect pixelCornerElevationSampler; // Sampler for the half pixel texture
uniform float contourLineFactor;
uniform int drawContourLines;
uniform float timeOfDay;
uniform int dayNightEnabled;
uniform vec2 heightColorMapTransformation; // elevation -> colour-map coordinate (same as the vertex shader)
uniform float time;
uniform int seaAnimation;

// Below sea level: drifting ripples and foam lapping at the shore. depthfrag
// is the colour-map coordinate; heightColorMapTransformation.y is where
// elevation 0 (sea level) lands, and 1/|x| is millimetres per unit.
vec3 animateSea(vec3 color) {
    float seaCoord = heightColorMapTransformation.y;
    float depthMm = (seaCoord - depthfrag) / abs(heightColorMapTransformation.x);
    if (depthMm <= 0.0) return color;
    vec2 p = gl_FragCoord.xy;
    float waves = sin(dot(p, vec2(0.11, 0.07)) + time * 1.6)
                + sin(dot(p, vec2(-0.06, 0.13)) + time * 1.1)
                + 0.5 * sin(dot(p, vec2(0.17, -0.12)) - time * 2.3);
    float calm = clamp(depthMm / 120.0, 0.0, 1.0);          // deeper water moves less
    color *= 1.0 + (waves / 2.5) * mix(0.12, 0.05, calm);
    float foamMm = 5.0 + 4.0 * sin(time * 1.3 + dot(p, vec2(0.02, 0.015)));
    float foam = 1.0 - smoothstep(0.0, foamMm, depthMm);
    return mix(color, vec3(1.0), foam * 0.5);
}

// Compute day/night tint from timeOfDay (0=midnight, 0.5=noon)
vec3 getDayNightTint() {
    // Phase boundaries matching Python: night 0-0.2, dawn 0.2-0.3, day 0.3-0.7, dusk 0.7-0.8, night 0.8-1.0
    vec3 nightTint = vec3(0.3, 0.3, 0.5);
    vec3 dawnTint  = vec3(0.9, 0.7, 0.5);
    vec3 dayTint   = vec3(1.0, 1.0, 1.0);
    vec3 duskTint  = vec3(0.8, 0.6, 0.7);

    float t = timeOfDay;
    if (t < 0.2) return nightTint;
    if (t < 0.3) return mix(nightTint, dawnTint, (t - 0.2) / 0.1);
    if (t < 0.5) return mix(dawnTint, dayTint, (t - 0.3) / 0.2);
    if (t < 0.7) return dayTint;
    if (t < 0.8) return mix(dayTint, duskTint, (t - 0.7) / 0.1);
    if (t < 0.9) return mix(duskTint, nightTint, (t - 0.8) / 0.1);
    return nightTint;
}

void main()
{
    vec2 depthPos = vec2(depthfrag, 0.5);//depthvalue*texsize, 0.5);
    vec4 color =  texture(heightColorMapSampler, depthPos);	//colormap converted depth
    if (seaAnimation == 1)
        color.rgb = animateSea(color.rgb);

    if (drawContourLines == 1)
    {
        // Contour line computation
        /* Calculate the contour line interval containing each pixel corner by evaluating the half-pixel offset elevation texture: */
        float corner0=floor(texture(pixelCornerElevationSampler,vec2(gl_FragCoord.x,gl_FragCoord.y)).r*contourLineFactor);
        float corner1=floor(texture(pixelCornerElevationSampler,vec2(gl_FragCoord.x+1.0,gl_FragCoord.y)).r*contourLineFactor);
        float corner2=floor(texture(pixelCornerElevationSampler,vec2(gl_FragCoord.x,gl_FragCoord.y+1.0)).r*contourLineFactor);
        float corner3=floor(texture(pixelCornerElevationSampler,vec2(gl_FragCoord.x+1.0,gl_FragCoord.y+1.0)).r*contourLineFactor);
        
        /* Find all pixel edges that cross at least one contour line: */
        int edgeMask=0;
        int numEdges=0;
        if(corner0!=corner1)
        {
            edgeMask+=1;
            ++numEdges;
        }
        if(corner2!=corner3)
        {
            edgeMask+=2;
            ++numEdges;
        }
        if(corner0!=corner2)
        {
            edgeMask+=4;
            ++numEdges;
        }
        if(corner1!=corner3)
        {
            edgeMask+=8;
            ++numEdges;
        }
        
        /* Check for all cases in which the pixel should be colored as a topographic contour line: */
        if(numEdges>2||edgeMask==3||edgeMask==12||(numEdges==2&&mod(floor(gl_FragCoord.x)+floor(gl_FragCoord.y),2.0)==0.0))
        {
            /* Topographic contour lines are rendered in black: */
            color=vec4(0.0,0.0,0.0,1.0);
        }
    }

    outputColor = color;

    // Apply day/night tint
    if (dayNightEnabled == 1) {
        vec3 tint = getDayNightTint();
        outputColor.rgb *= tint;
    }
}
