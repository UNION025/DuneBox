/***********************************************************************
FitText.h - draw a string centred in a box, shrunk to fit

Game texts are laid out in the projector sand region (getProjectorActiveROI),
whose size depends on the rig; the fonts are fixed sizes (up to 128 pt), so a
long or Japanese string could run off the sand.

This file is part of DuneBox, a fork of Magic Sand.
***********************************************************************/

#pragma once

#include "ofMain.h"

inline void drawStringFit(ofTrueTypeFont& font, const std::string& text, const ofRectangle& box) {
	if (!font.isLoaded() || text.empty() || box.width <= 0 || box.height <= 0) return;
	ofRectangle bb = font.getStringBoundingBox(text, 0, 0);
	if (bb.width <= 0 || bb.height <= 0) return;
	float scale = std::min(1.0f, std::min(box.width * 0.9f / bb.width, box.height * 0.9f / bb.height));
	ofPushMatrix();
	ofTranslate(box.getCenter());
	ofScale(scale, scale);
	font.drawString(text, -bb.x - bb.width / 2, -bb.y - bb.height / 2);
	ofPopMatrix();
}
