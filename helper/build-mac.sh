#!/bin/bash
# Builds "Loopwright Voice.app" (Apple Silicon only) and zips it for the website:
#   bash helper/build-mac.sh [version]          -> helper/dist/Loopwright-Voice-<version>-mac.zip
#
# The app is small: the helper's code, the two model projects' code (ACE-Step 1.5, SoulX-Singer; no weights),
# Jordan's two voices and uv. Python, the packages and the models (~20 GB) download on first launch (setup.py).
#
# Signing (optional, for no Gatekeeper warning):
#   SIGN_ID="Developer ID Application: Your Name (TEAMID)"   (default: the first one in your keychain)
#   NOTARY_PROFILE=<name>   made once with: xcrun notarytool store-credentials <name> --apple-id ... --team-id ...
# Without NOTARY_PROFILE the app is signed but not notarized (people right-click > Open the first time).
set -euo pipefail
cd "$(dirname "$0")"
VERSION="${1:-$(date +%Y.%m.%d)}"

# the model projects' code, at the versions the helper was tested with
ACE_REPO=https://github.com/ace-step/ACE-Step-1.5.git;  ACE_COMMIT=ca1e85f
SOULX_REPO=https://github.com/Soul-AILab/SoulX-Singer.git; SOULX_COMMIT=81aeb3a
UV_VERSION=0.13.9
# Jordan's voices are sold, so they're never in this (public) repo: the build takes them from a private folder
VOICES_DIR="${VOICES_DIR:-$HOME/song-maker/my_voice/clean}"
for v in Jordan_Sing Jordan_Rap; do
  [ -f "$VOICES_DIR/$v.wav" ] || { echo "missing $VOICES_DIR/$v.wav (set VOICES_DIR to the folder with Jordan's voices)"; exit 1; }
done

OUT=dist; WORK=build
APP="$OUT/Loopwright Voice.app"
rm -rf "$WORK" "$APP"; mkdir -p "$WORK" "$OUT"
RES="$APP/Contents/Resources"; PAY="$RES/payload"
mkdir -p "$APP/Contents/MacOS" "$PAY/vendor" "$PAY/voices"

echo "== 1/5 the model projects' code"
fetch() { # repo commit dest
  if [ ! -d "$WORK/$3.git" ]; then git clone --quiet --filter=blob:none --no-checkout "$1" "$WORK/$3.git"; fi
  mkdir -p "$PAY/vendor/$3"
  git -C "$WORK/$3.git" archive "$2" | tar -x -C "$PAY/vendor/$3"
  rm -rf "$PAY/vendor/$3/checkpoints" "$PAY/vendor/$3/pretrained_models" "$PAY/vendor/$3/.github"
}
fetch "$ACE_REPO" "$ACE_COMMIT" ACE-Step-1.5
fetch "$SOULX_REPO" "$SOULX_COMMIT" SoulX-Singer

echo "== 2/5 the helper"
cp -R src "$PAY/src"
cp "$VOICES_DIR/Jordan_Sing.wav" "$VOICES_DIR/Jordan_Rap.wav" "$PAY/voices/"
cp requirements-soulx.txt requirements-ace.txt "$PAY/"
echo "$VERSION" > "$PAY/VERSION"
cp setup.py "$RES/setup.py"
cp launcher.sh "$APP/Contents/MacOS/Loopwright Voice"
chmod +x "$APP/Contents/MacOS/Loopwright Voice"
cp ../THIRD_PARTY_NOTICES.md "$RES/" 2>/dev/null || true
cp "$PAY/vendor/ACE-Step-1.5/LICENSE" "$RES/LICENSE-ACE-Step.txt"
cp "$PAY/vendor/SoulX-Singer/LICENSE" "$RES/LICENSE-SoulX-Singer.txt"
find "$PAY" -name __pycache__ -prune -exec rm -rf {} +

echo "== 3/5 uv $UV_VERSION (Apple Silicon)"
if [ ! -x "$WORK/uv" ]; then
  curl -fsSL "https://github.com/astral-sh/uv/releases/download/$UV_VERSION/uv-aarch64-apple-darwin.tar.gz" | tar -xz -C "$WORK"
  mv "$WORK/uv-aarch64-apple-darwin/uv" "$WORK/uv"
fi
cp "$WORK/uv" "$RES/uv"

# the icon: Loopwright's own (Resources/icon/icon-1024.png), as an .icns
ICONSET="$WORK/AppIcon.iconset"; rm -rf "$ICONSET"; mkdir -p "$ICONSET"
for sz in 16 32 128 256 512; do
  sips -z $sz $sz ../Resources/icon/icon-1024.png --out "$ICONSET/icon_${sz}x${sz}.png" >/dev/null
  sips -z $((sz * 2)) $((sz * 2)) ../Resources/icon/icon-1024.png --out "$ICONSET/icon_${sz}x${sz}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$RES/AppIcon.icns"

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>Loopwright Voice</string>
  <key>CFBundleDisplayName</key><string>Loopwright Voice</string>
  <key>CFBundleIdentifier</key><string>com.loopwright.voice</string>
  <key>CFBundleExecutable</key><string>Loopwright Voice</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>LSMinimumSystemVersion</key><string>14.0</string>
  <key>LSArchitecturePriority</key><array><string>arm64</string></array>
  <key>LSRequiresNativeExecution</key><true/>
  <key>LSUIElement</key><true/>
  <key>NSHumanReadableCopyright</key><string>Loopwright</string>
</dict></plist>
PLIST

echo "== 4/5 signing"
SIGN_ID="${SIGN_ID:-$(security find-identity -v -p codesigning | sed -n 's/.*"\(Developer ID Application:[^"]*\)".*/\1/p' | head -1)}"
if [ -n "$SIGN_ID" ]; then
  codesign --force --options runtime --timestamp -s "$SIGN_ID" "$RES/uv"
  codesign --force --options runtime --timestamp -s "$SIGN_ID" "$APP"
  codesign --verify --strict --verbose=1 "$APP"
else
  codesign --force --deep -s - "$APP"
  echo "(no Developer ID found: ad-hoc signed)"
fi

echo "== 5/5 zip (and notarize)"
ZIP="$OUT/Loopwright-Voice-$VERSION-mac.zip"
rm -f "$ZIP"
ditto -c -k --keepParent "$APP" "$ZIP"
if [ -n "${NOTARY_PROFILE:-}" ] && [ -n "$SIGN_ID" ]; then
  xcrun notarytool submit "$ZIP" --keychain-profile "$NOTARY_PROFILE" --wait
  xcrun stapler staple "$APP"
  rm -f "$ZIP"; ditto -c -k --keepParent "$APP" "$ZIP"
  echo "notarized"
else
  echo "(not notarized: set NOTARY_PROFILE to notarize)"
fi
ls -lh "$ZIP"
