#!/usr/bin/env bash
# One-time macOS dev setup: openFrameworks 0.12.0 + pinned addons + Git LFS assets.
# Mirrors the Windows CI (.github/workflows/build.yml) so both platforms build
# from the same addon versions and patches.
#
#   ./scripts/setup-mac.sh          # OF goes to ../openFrameworks
#   OF_ROOT=/path ./scripts/setup-mac.sh
#   make -j8 && open bin/DuneBox.app
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
OF_ROOT="${OF_ROOT:-$REPO/../openFrameworks}"
OF_VERSION=0.12.0
TOOLS="$REPO/../.tools"

# --- Git LFS (reference maps / GUI images are LFS objects) ---------------
if ! command -v git-lfs >/dev/null; then
	mkdir -p "$TOOLS"
	if [ ! -x "$TOOLS/git-lfs" ]; then
		echo "==> downloading git-lfs"
		arch=$(uname -m); [ "$arch" = x86_64 ] && arch=amd64
		v=$(curl -sL https://api.github.com/repos/git-lfs/git-lfs/releases/latest | awk -F'"' '/"tag_name"/ && !v {v=$4} END {print v}')
		tmp=$(mktemp -d)
		curl -sL -o "$tmp/lfs.zip" "https://github.com/git-lfs/git-lfs/releases/download/$v/git-lfs-darwin-$arch-$v.zip"
		unzip -qo "$tmp/lfs.zip" -d "$tmp"
		cp "$tmp"/git-lfs-*/git-lfs "$TOOLS/git-lfs"
		rm -rf "$tmp"
	fi
	export PATH="$TOOLS:$PATH"
	echo "    (add to your shell: export PATH=\"$(cd "$TOOLS" && pwd):\$PATH\")"
fi
git -C "$REPO" lfs install --local >/dev/null
git -C "$REPO" lfs pull

# --- openFrameworks -------------------------------------------------------
if [ ! -d "$OF_ROOT/libs/openFrameworks" ]; then
	echo "==> downloading openFrameworks $OF_VERSION (~430MB)"
	tmp=$(mktemp -d)
	curl -L -o "$tmp/of.zip" "https://github.com/openframeworks/openFrameworks/releases/download/$OF_VERSION/of_v${OF_VERSION}_osx_release.zip"
	unzip -q "$tmp/of.zip" -d "$tmp"
	mv "$tmp"/of_v*_osx_release "$OF_ROOT"
	rm -rf "$tmp"
fi
# Recent macOS SDKs no longer ship AGL.framework; OF 0.12.0 still links it.
sed -i '' '/PLATFORM_FRAMEWORKS += AGL/d' "$OF_ROOT/libs/openFrameworksCompiled/project/osx/config.osx.default.mk"

# --- Addons (same pins as CI) ----------------------------------------------
clone_pin() { # repo dir sha
	local dir="$OF_ROOT/addons/$2"
	if [ ! -d "$dir/.git" ]; then rm -rf "$dir"; git clone -q "https://github.com/$1" "$dir"; fi
	git -C "$dir" fetch -q origin "$3" 2>/dev/null || true
	git -C "$dir" checkout -q -f "$3"
}
clone_pin kylemcdonald/ofxCv ofxCv        209a3986a8f8e1928c7a11f6e1c5d8ea5406e639
clone_pin braitsch/ofxParagraph ofxParagraph 41dbfd2e07e00e358629d470dd1b6f986dee2b70
clone_pin braitsch/ofxModal ofxModal     71bfaa283407bbe58b921fd581d8665f21cf5eb8
clone_pin thomwolf/ofxDatGui ofxDatGui   0c2230afa68fe5dea822932f6b4c178778af9440

# Same ofxDatGui patches as CI (see build.yml for the reasoning).
DG="$OF_ROOT/addons/ofxDatGui/src"
find "$DG" -name '*.h' -print0 | xargs -0 perl -0pi -e 's/#pragma once\n(?!using namespace std;)/#pragma once\nusing namespace std;\n/'
SF="$DG/libs/ofxSmartFont/ofxSmartFont.cpp"
grep -q 'ofVboMesh _m = ttf.getStringMesh' "$SF" || {
	perl -pi -e 's|#include "ofxSmartFont.h"|#include "ofxSmartFont.h"\n#include "ofVboMesh.h"|' "$SF"
	perl -pi -e 's/ttf\.drawString\(s, x, y\);/ofVboMesh _m = ttf.getStringMesh(s, x, y); const ofTexture& _t = ttf.getFontTexture(); _t.bind(); _m.draw(OF_MESH_FILL); _t.unbind();/' "$SF"
}
grep -q 'ofVboMesh _m = ttf.getStringMesh' "$SF" || { echo "ERROR: ofxSmartFont patch did not apply"; exit 1; }

echo "==> done. Build with:  cd \"$REPO\" && make -j8 OF_ROOT=\"$(cd "$OF_ROOT" && pwd)\""
