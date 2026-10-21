/***********************************************************************
Localization.h - UI translation and font substitution

English source strings are the keys: bin/data/lang/<lang>.json maps them to
the target language, and a key that is missing from the table renders as the
English text. The language comes from bin/data/settings/language.txt
("ja" or "en"; "ja" when the file is absent).

The GUI fonts (Geist, Helvetica, Roboto, Verdana) have no Japanese glyphs, so
in a non-English mode every UI font load is redirected to the table's font,
with glyph ranges built from the characters the table actually uses.

This file is part of DuneBox, a fork of Magic Sand.
***********************************************************************/

#pragma once

#include "ofMain.h"
#include <string>

namespace dunebox {
namespace i18n {

const std::string& language();

// Translated text for an English UI string (or the string itself).
std::string tr(const std::string& english);

// Translate a freshly added ofxDatGui component's label in place. The
// component keeps its English name, which the event handlers and getSlider()
// lookups match on, so only the displayed text changes.
template <class Component>
Component* trLabel(Component* c) {
	c->setLabel(tr(c->getLabel()));
	return c;
}

// Settings for loading `file` at `size` as a UI font in the current language.
ofTrueTypeFontSettings fontSettings(const std::string& file, int size);
bool loadFont(ofTrueTypeFont& font, const std::string& file, int size);

// Shared app-owned UI font (replaces ofDrawBitmapString, which is ASCII only).
ofTrueTypeFont& uiFont(int size);

} // namespace i18n
} // namespace dunebox

// Called by the patched ofxSmartFont constructor (see scripts/setup-mac.sh and
// .github/workflows/build.yml) so ofxDatGui / ofxModal fonts follow the language.
ofTrueTypeFontSettings ofxSmartFontSettings(const std::string& file, int size);
