/***********************************************************************
Localization.cpp - UI translation and font substitution

This file is part of DuneBox, a fork of Magic Sand.
***********************************************************************/

#include "Localization.h"

#include <map>
#include <memory>
#include <set>

namespace dunebox {
namespace i18n {

namespace {

struct State {
	bool loaded = false;
	std::string language = "ja";
	std::string fontFile;        // empty: keep the requested font
	float fontScale = 1.0f;      // CJK glyphs need a little more size to read
	std::map<std::string, std::string> strings;
	std::vector<ofUnicode::range> ranges;
	std::map<int, std::unique_ptr<ofTrueTypeFont>> uiFonts;
};

State& state() {
	static State s;
	return s;
}

// Latin + punctuation for numbers, file names and mixed text, plus kana and
// full-width forms, plus every code point the table uses (its kanji). Loading
// all of CJKUnified instead would rasterise ~20k glyphs per font size.
std::vector<ofUnicode::range> buildRanges(const std::map<std::string, std::string>& strings) {
	std::set<uint32_t> used;
	for (const auto& kv : strings)
		for (uint32_t c : ofUTF8Iterator(kv.second))
			used.insert(c);

	std::vector<ofUnicode::range> ranges = {
		ofUnicode::Latin1Supplement,
		ofUnicode::GeneralPunctuation,
		{0x2190, 0x21FF},             // arrows
		{0x2460, 0x24FF},             // enclosed numbers
		{0x25A0, 0x25FF},             // geometric shapes
		ofUnicode::CJKSymbolAndPunctuation,
		ofUnicode::Hiragana,
		ofUnicode::Katakana,
		ofUnicode::KatakanaHalfAndFullwidthForms,
	};
	auto covered = [&ranges](uint32_t c) {
		for (const auto& r : ranges)
			if (c >= r.begin && c <= r.end) return true;
		return false;
	};
	// Merge consecutive code points into one range each.
	uint32_t begin = 0, end = 0;
	bool open = false;
	for (uint32_t c : used) {
		if (covered(c)) continue;
		if (open && c == end + 1) { end = c; continue; }
		if (open) ranges.push_back({begin, end});
		begin = end = c;
		open = true;
	}
	if (open) ranges.push_back({begin, end});
	return ranges;
}

void ensureLoaded() {
	State& s = state();
	if (s.loaded) return;
	s.loaded = true;

	ofBuffer langFile = ofBufferFromFile("settings/language.txt");
	std::string lang = ofTrim(langFile.getText());
	if (!lang.empty()) s.language = lang;
	if (s.language == "en") return;

	std::string path = "lang/" + s.language + ".json";
	if (!ofFile::doesFileExist(path)) {
		ofLogWarning("i18n") << path << " not found - using English UI";
		s.language = "en";
		return;
	}
	ofJson json = ofLoadJson(path);
	if (json.contains("font")) s.fontFile = json["font"].get<std::string>();
	if (json.contains("fontScale")) s.fontScale = json["fontScale"].get<float>();
	if (json.contains("strings"))
		for (auto it = json["strings"].begin(); it != json["strings"].end(); ++it)
			s.strings[it.key()] = it.value().get<std::string>();
	s.ranges = buildRanges(s.strings);
	ofLogNotice("i18n") << "language " << s.language << ": " << s.strings.size()
		<< " strings, " << s.ranges.size() << " glyph ranges, font " << s.fontFile;
}

} // namespace

const std::string& language() {
	ensureLoaded();
	return state().language;
}

std::string tr(const std::string& english) {
	ensureLoaded();
	const auto& strings = state().strings;
	auto it = strings.find(english);
	return it == strings.end() ? english : it->second;
}

ofTrueTypeFontSettings fontSettings(const std::string& file, int size) {
	ensureLoaded();
	const State& s = state();
	if (s.language == "en" || s.fontFile.empty()) {
		ofTrueTypeFontSettings settings(file, size);
		settings.addRange(ofUnicode::Latin1Supplement);
		return settings;
	}
	ofTrueTypeFontSettings settings(s.fontFile, (int)std::round(size * s.fontScale));
	settings.ranges = s.ranges;
	return settings;
}

bool loadFont(ofTrueTypeFont& font, const std::string& file, int size) {
	return font.load(fontSettings(file, size));
}

ofTrueTypeFont& uiFont(int size) {
	auto& fonts = state().uiFonts;
	auto& font = fonts[size];
	if (!font) {
		font = std::make_unique<ofTrueTypeFont>();
		loadFont(*font, "fonts/Geist-Medium.ttf", size);
		font->setLineHeight(font->getLineHeight() * 1.3f);
	}
	return *font;
}

} // namespace i18n
} // namespace dunebox

ofTrueTypeFontSettings ofxSmartFontSettings(const std::string& file, int size) {
	return dunebox::i18n::fontSettings(file, size);
}
