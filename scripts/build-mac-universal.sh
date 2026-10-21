#!/usr/bin/env bash
# Build a universal (Intel + Apple Silicon) DuneBox and a folder ready to copy
# to another Mac:  dist/DuneBox/{DuneBox.app, data/, はじめにお読みください.html}
#
# The host architecture builds natively against ../openFrameworks. The other
# one is cross-built against ../openFrameworks-<arch> (an APFS clone made on
# the first run) from a scratch copy of the project in ../DuneBox-build-<arch>:
# OF keeps one libopenFrameworks.a and addons/obj per tree, and obj/ and bin/
# per project directory, so the two builds can't share either.
#
# data/ is copied from bin/data, including bin/data/settings: the package
# carries this rig's current calibration.
#
#   ./scripts/setup-mac.sh             # once
#   ./scripts/build-mac-universal.sh   # -> dist/DuneBox
#
# After re-running setup-mac.sh (new addon pins or patches), delete
# ../openFrameworks-<arch> so the clone is made again.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
OF_ROOT="$(cd "${OF_ROOT:-$REPO/../openFrameworks}" && pwd)"
HOST=$(uname -m)
case "$HOST" in
	x86_64) OTHER=arm64 ;;
	arm64)  OTHER=x86_64 ;;
	*) echo "unsupported host arch: $HOST"; exit 1 ;;
esac
OF_OTHER="$OF_ROOT-$OTHER"
# Not a dot-directory: OF drops source paths containing "/." from the build.
WORK="$(cd "$REPO/.." && pwd)/DuneBox-build-$OTHER"
JOBS=$(sysctl -n hw.ncpu)

echo "==> $HOST (native)"
# OF derives the project root from $PWD, so cd rather than make -C.
(cd "$REPO" && make -j"$JOBS" OF_ROOT="$OF_ROOT")

echo "==> $OTHER (cross)"
if [ ! -d "$OF_OTHER" ]; then
	cp -cR "$OF_ROOT" "$OF_OTHER"   # APFS clone: shares blocks until changed
	rm -rf "$OF_OTHER/libs/openFrameworksCompiled/lib/osx/libopenFrameworks.a" \
	       "$OF_OTHER/libs/openFrameworksCompiled/lib/osx/obj" \
	       "$OF_OTHER/addons/obj"
fi
# -mtune=native names the host CPU, which the other architecture's compiler rejects.
sed -i '' 's/-mtune=native//' "$OF_OTHER/libs/openFrameworksCompiled/project/osx/config.osx.default.mk"
mkdir -p "$WORK"
rsync -a --delete "$REPO/src/" "$WORK/src/"
rsync -a --delete --exclude 'DebugFiles/*' "$REPO/bin/data/" "$WORK/bin/data/"
cp "$REPO/Makefile" "$REPO/config.make" "$REPO/addons.make" "$WORK/"
(cd "$WORK" && make -j"$JOBS" OF_ROOT="$OF_OTHER" \
	CC="clang -arch $OTHER" CXX="clang++ -arch $OTHER" USER_LDFLAGS="-arch $OTHER")

echo "==> universal package"
DIST="$REPO/dist/DuneBox"
rm -rf "$REPO/dist"
mkdir -p "$DIST"
cp -R "$REPO/bin/DuneBox.app" "$DIST/"
lipo -create "$REPO/bin/DuneBox.app/Contents/MacOS/DuneBox" \
             "$WORK/bin/DuneBox.app/Contents/MacOS/DuneBox" \
     -output "$DIST/DuneBox.app/Contents/MacOS/DuneBox"
rsync -a --exclude 'DebugFiles/*' "$REPO/bin/data/" "$DIST/data/"
mkdir -p "$DIST/data/DebugFiles"
cp "$REPO/docs/manual-ja.html" "$DIST/はじめにお読みください.html"
{
	echo "commit: $(git -C "$REPO" rev-parse --short HEAD)$(git -C "$REPO" diff --quiet -- src || echo ' (+ uncommitted src changes)')"
	echo "built:  $(date '+%Y-%m-%d %H:%M') on $HOST"
	echo "archs:  $(lipo -archs "$DIST/DuneBox.app/Contents/MacOS/DuneBox")"
} > "$DIST/data/BUILD.txt"
# Ad-hoc signature: Apple Silicon refuses unsigned arm64 code.
codesign --force --deep -s - "$DIST/DuneBox.app"
codesign --verify --deep --strict "$DIST/DuneBox.app"

cat "$DIST/data/BUILD.txt"
echo "==> $DIST  (copy the whole folder; DuneBox.app needs data/ beside it)"
