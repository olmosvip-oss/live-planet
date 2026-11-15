#!/bin/bash
# Signs every bundle in dist/ (run in CI on a Mac, before packaging).
# With the Apple secrets (see build-pkg.sh): Developer ID + hardened runtime, then Apple notarizes them
# and the ticket is stapled to each bundle, so the by-hand plugins and the app open with no warning too.
# Without them: ad-hoc signing (people click "Open Anyway" the first time).
set -euo pipefail
cd dist
if [ -n "${MACOS_CERT_P12:-}" ] && [ -n "${MACOS_DEV_ID_APP:-}" ]; then
  KC=build.keychain
  if ! security list-keychains | grep -q "$KC"; then
    security create-keychain -p ci "$KC"
    security list-keychains -d user -s "$KC" $(security list-keychains -d user | tr -d '"')
    security default-keychain -s "$KC"
    security unlock-keychain -p ci "$KC"
    security set-keychain-settings -lut 7200 "$KC"
    echo "$MACOS_CERT_P12" | base64 --decode > ../cert.p12
    security import ../cert.p12 -k "$KC" -P "$MACOS_CERT_PASSWORD" -T /usr/bin/codesign -T /usr/bin/productsign -T /usr/bin/pkgbuild
    security set-key-partition-list -S apple-tool:,apple: -s -k ci "$KC" >/dev/null
    rm ../cert.p12
  fi
  for b in *.vst3 *.component *.clap; do
    [ -e "$b" ] && codesign --force --deep --options runtime --timestamp -s "$MACOS_DEV_ID_APP" "$b"
  done
  [ -e Loopwright.app ] && codesign --force --deep --options runtime --timestamp \
      --entitlements ../packaging/mac/app.entitlements -s "$MACOS_DEV_ID_APP" Loopwright.app
  if [ -n "${NOTARY_APPLE_ID:-}" ]; then
    ditto -c -k --keepParent . ../notarize.zip
    xcrun notarytool submit ../notarize.zip --apple-id "$NOTARY_APPLE_ID" --team-id "$NOTARY_TEAM_ID" --password "$NOTARY_PASSWORD" --wait
    for b in *.vst3 *.component *.clap *.app; do [ -e "$b" ] && xcrun stapler staple "$b"; done
    rm ../notarize.zip
    echo "bundles signed and notarized"
  fi
  touch ../.signed
else
  for b in *.vst3 *.component *.clap *.app; do [ -e "$b" ] && codesign --force --deep -s - "$b"; done
  echo "ad-hoc signed (add the Apple secrets to sign properly)"
fi
