#!/bin/bash
# Builds Loopwright on your own Mac and installs it (VST3 + Audio Unit).
# Use it when the GitHub build is slow or down. Run from the live-planet folder:
#   bash tools/build-mac.sh
set -e
cd "$(dirname "$0")/.."

echo "== 1/4 checking Apple's build tools"
if ! xcode-select -p >/dev/null 2>&1; then
    echo "Apple's Command Line Tools are needed. A window will open: click Install, wait for it, then run this again."
    xcode-select --install || true
    exit 1
fi

echo "== 2/4 checking CMake"
CMAKE=$(command -v cmake || true)
if [ -z "$CMAKE" ]; then
    for d in "$HOME"/Library/Python/*/bin; do [ -x "$d/cmake" ] && CMAKE="$d/cmake"; done
fi
if [ -z "$CMAKE" ]; then
    echo "installing CMake (just for you, no admin password needed)..."
    python3 -m pip install --user --quiet cmake
    for d in "$HOME"/Library/Python/*/bin; do [ -x "$d/cmake" ] && CMAKE="$d/cmake"; done
fi
[ -n "$CMAKE" ] || { echo "couldn't install CMake. Download it from cmake.org, then run this again."; exit 1; }
echo "using $CMAKE"

if [ ! -f libs/JUCE/CMakeLists.txt ]; then
    echo "getting JUCE..."
    git submodule update --init --recursive
fi

echo "== 3/4 building (10-20 minutes the first time)"
ARCH=$(uname -m)   # just this Mac's chip, which is faster to build
"$CMAKE" -B build-mac -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$ARCH" >/dev/null
"$CMAKE" --build build-mac --config Release --target LivePlanet_VST3 LivePlanet_AU -j "$(sysctl -n hw.ncpu)"

echo "== 4/4 installing"
ART=build-mac/LivePlanet_artefacts/Release
mkdir -p "$HOME/Library/Audio/Plug-Ins/VST3" "$HOME/Library/Audio/Plug-Ins/Components"
rm -rf "$HOME/Library/Audio/Plug-Ins/VST3/Loopwright.vst3" "$HOME/Library/Audio/Plug-Ins/Components/Loopwright.component"
cp -R "$ART/VST3/Loopwright.vst3" "$HOME/Library/Audio/Plug-Ins/VST3/"
cp -R "$ART/AU/Loopwright.component" "$HOME/Library/Audio/Plug-Ins/Components/"
xattr -cr "$HOME/Library/Audio/Plug-Ins/VST3/Loopwright.vst3" "$HOME/Library/Audio/Plug-Ins/Components/Loopwright.component"
codesign --force --deep -s - "$HOME/Library/Audio/Plug-Ins/VST3/Loopwright.vst3" "$HOME/Library/Audio/Plug-Ins/Components/Loopwright.component"
echo "installed. open Ableton (Settings > Plug-Ins > Rescan if it doesn't show up)."
