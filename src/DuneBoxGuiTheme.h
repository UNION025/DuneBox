#pragma once

// Operator-panel styling shared with the sandcam dashboard and sidebar
// (tokens in DESIGN.md). Geist is bundled in bin/data/fonts under the OFL.

#include "ofxDatGui.h"
#include <memory>

namespace dunebox {

namespace tok {
	inline ofColor bg()           { return ofColor::fromHex(0x0A0A0A); }
	inline ofColor surface()      { return ofColor::fromHex(0x111111); }
	inline ofColor surface2()     { return ofColor::fromHex(0x171717); }
	inline ofColor surface3()     { return ofColor::fromHex(0x1F1F1F); }
	inline ofColor border()       { return ofColor::fromHex(0x262626); }
	inline ofColor borderStrong() { return ofColor::fromHex(0x3A3A3A); }
	inline ofColor text()         { return ofColor::fromHex(0xEDEDED); }
	inline ofColor text2()        { return ofColor::fromHex(0xA1A1A1); }
	inline ofColor textOff()      { return ofColor::fromHex(0x5C5C5C); } // disabled controls
	inline ofColor accent()       { return ofColor::fromHex(0x52A8FF); }
	inline ofColor ok()           { return ofColor::fromHex(0x3FCF8E); }
	inline ofColor warn()         { return ofColor::fromHex(0xF5A524); }
	inline ofColor err()          { return ofColor::fromHex(0xFF6166); }
}

// ofxDatGuiComponent keeps its default theme in a protected static; deriving
// is the only way to replace it. Call install() before any ofxDatGui is built
// so every panel is laid out with these metrics from the start.
struct GuiTheme : public ofxDatGuiComponent {
	static void install() {
		auto t = std::make_unique<ofxDatGuiTheme>(false);

		t->color.guiBackground = tok::border();
		t->color.label = tok::text();
		t->color.icons = tok::text2();
		t->color.background = tok::surface();
		t->color.backgroundOnMouseOver = tok::surface3();
		t->color.backgroundOnMouseDown = tok::surface2();
		t->color.inputAreaBackground = tok::surface3();
		t->color.slider.fill = tok::accent();
		t->color.slider.text = tok::text();
		t->color.textInput.text = tok::text();
		t->color.textInput.highlight = tok::accent();
		t->color.textInput.backgroundOnActive = tok::surface2();
		t->color.colorPicker.border = tok::borderStrong();
		t->color.pad2d.line = tok::text2();
		t->color.pad2d.ball = tok::accent();
		t->color.graph.lines = tok::accent();
		t->color.graph.fills = tok::accent();
		t->color.matrix.normal.label = tok::text2();
		t->color.matrix.normal.button = tok::surface3();
		t->color.matrix.hover.label = tok::text();
		t->color.matrix.hover.button = tok::borderStrong();
		t->color.matrix.selected.label = tok::bg();
		t->color.matrix.selected.button = tok::accent();

		t->stripe.visible = false;

		t->layout.width = 300.0f;
		t->layout.height = 30.0f;
		t->layout.padding = 2.0f;
		t->layout.vMargin = 1.0f;
		t->layout.labelWidth = 120.0f;
		t->layout.labelMargin = 12.0f;
		t->layout.upperCaseLabels = false;
		t->layout.textInput.forceUpperCase = false;

		t->font.file = "fonts/Geist-Medium.ttf";
		t->font.size = 8;

		t->init();
		theme = std::move(t);
	}
};

}
