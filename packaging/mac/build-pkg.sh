#!/bin/bash
# Builds Loopwright.pkg from the plugins in dist/ (run in CI after the build, on a Mac).
# Signing and Apple notarization happen only when these are set (GitHub repository secrets):
#   MACOS_CERT_P12 (base64 of your Developer ID .p12, holding both the Application and Installer certificates)
#   MACOS_CERT_PASSWORD, MACOS_DEV_ID_APP ("Developer ID Application: Your Name (TEAMID)"),
#   MACOS_DEV_ID_INSTALLER ("Developer ID Installer: Your Name (TEAMID)"),
#   NOTARY_APPLE_ID, NOTARY_TEAM_ID, NOTARY_PASSWORD (an app-specific password)
# Without them it makes an unsigned installer (right-click > Open the first time).
set -euo pipefail
VERSION="${1:-0.1.0}"
ROOT=pkgroot
rm -rf "$ROOT" package/Loopwright.pkg
mkdir -p "$ROOT/Library/Audio/Plug-Ins/VST3" "$ROOT/Library/Audio/Plug-Ins/Components" "$ROOT/Library/Audio/Plug-Ins/CLAP" "$ROOT/Applications" package
cp -R dist/Loopwright.vst3 "$ROOT/Library/Audio/Plug-Ins/VST3/"
cp -R dist/Loopwright.component "$ROOT/Library/Audio/Plug-Ins/Components/"
cp -R dist/Loopwright.clap "$ROOT/Library/Audio/Plug-Ins/CLAP/"
[ -d dist/Loopwright.app ] && cp -R dist/Loopwright.app "$ROOT/Applications/"

SIGN=0
if [ -n "${MACOS_CERT_P12:-}" ] && [ -n "${MACOS_DEV_ID_APP:-}" ]; then
  SIGN=1
  KC=build.keychain
  security create-keychain -p ci "$KC"
  security default-keychain -s "$KC"
  security unlock-keychain -p ci "$KC"
  echo "$MACOS_CERT_P12" | base64 --decode > cert.p12
  security import cert.p12 -k "$KC" -P "$MACOS_CERT_PASSWORD" -T /usr/bin/codesign -T /usr/bin/productsign -T /usr/bin/pkgbuild
  security set-key-partition-list -S apple-tool:,apple: -s -k ci "$KC" >/dev/null
  rm cert.p12
fi

# every bundle signed: properly (hardened runtime, for notarization) or ad-hoc
for b in "$ROOT"/Library/Audio/Plug-Ins/*/Loopwright.* "$ROOT"/Applications/Loopwright.app; do
  [ -e "$b" ] || continue
  if [ $SIGN = 1 ]; then codesign --force --deep --options runtime --timestamp -s "$MACOS_DEV_ID_APP" "$b"
  else codesign --force --deep -s - "$b"; fi
done

# install exactly where they belong (never "relocated" to an old copy somewhere else)
pkgbuild --analyze --root "$ROOT" components.plist
i=0
while /usr/libexec/PlistBuddy -c "Print :$i" components.plist >/dev/null 2>&1; do
  /usr/libexec/PlistBuddy -c "Delete :$i:BundleIsRelocatable" components.plist 2>/dev/null || true
  /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" components.plist
  i=$((i + 1))
done

SIGNARGS=()
[ $SIGN = 1 ] && [ -n "${MACOS_DEV_ID_INSTALLER:-}" ] && SIGNARGS=(--sign "$MACOS_DEV_ID_INSTALLER" --timestamp)
mkdir -p scripts && cp packaging/mac/postinstall scripts/postinstall
pkgbuild --root "$ROOT" --component-plist components.plist --scripts scripts \
         --identifier com.loopwright.loopwright.pkg --version "$VERSION" --install-location / \
         ${SIGNARGS[@]+"${SIGNARGS[@]}"} package/Loopwright.pkg

if [ $SIGN = 1 ] && [ -n "${NOTARY_APPLE_ID:-}" ]; then
  xcrun notarytool submit package/Loopwright.pkg --apple-id "$NOTARY_APPLE_ID" --team-id "$NOTARY_TEAM_ID" --password "$NOTARY_PASSWORD" --wait
  xcrun stapler staple package/Loopwright.pkg
  echo "signed and notarized"
else
  echo "unsigned installer (add the Apple secrets to sign and notarize)"
fi
