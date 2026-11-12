# Selling Loopwright: checklist

## 1. The name
A US trademark application for **LOOPWRIGHT** (serial **99942826**) was filed in 2026, owner and goods not checked yet.
Look it up at https://tsdr.uspto.gov (enter the serial number):
- If it's for software/music (class 9 or 42) and not yours, pick another name before selling. The name lives in one place
  (`lp::ui::productName()` and `PRODUCT_NAME` in `CMakeLists.txt`).
- Otherwise, consider filing your own in class 9 (downloadable audio software). A trademark lawyer can do a full clearance search.

## 2. Licenses for the code inside
See `THIRD_PARTY_NOTICES.md`. The one that costs money: **JUCE** is free only for open-source (AGPL) use, so selling
closed-source needs a JUCE commercial license (juce.com). Signalsmith Stretch, its FFT and the fonts are free (MIT / OFL):
just include their notices.

## 3. Apple signing (so Macs open it without warnings)
1. Join the Apple Developer Program.
2. Make **Developer ID Application** and **Developer ID Installer** certificates and export them together as one `.p12`.
3. In GitHub: Settings > Secrets and variables > Actions, add:
   `MACOS_CERT_P12` (the .p12 as base64: `base64 -i cert.p12 | pbcopy`), `MACOS_CERT_PASSWORD`,
   `MACOS_DEV_ID_APP`, `MACOS_DEV_ID_INSTALLER` (the certificate names), `NOTARY_APPLE_ID`, `NOTARY_TEAM_ID`,
   `NOTARY_PASSWORD` (an app-specific password from appleid.apple.com).
4. The next build's **Loopwright-macOS-Installer** is signed and notarized.

Windows: an EV / OV code-signing certificate stops SmartScreen warnings (later).

## 4. License codes (offline, no server)
Licensing is off until you switch it on, so test builds stay free.
1. Make your key pair once, on your own computer, and **back up `private.key` somewhere safe** (never in the repo):
   `build/Keygen_artefacts/Release/Keygen --new my-keys`
2. Build the plugin with your public key (or put it in a GitHub secret and pass it in the workflow):
   `cmake -B build -DLOOPWRIGHT_PUBLIC_KEY="$(cat my-keys/public.key)"`
   Buyers then get a 14-day trial; after that, **Write a song** asks for a code.
3. For each sale: `Keygen --make "Buyer Name" my-keys/private.key` and email them the `LW1-...` code.
   They paste it via the license link in Settings. A store like Gumroad, Lemon Squeezy or Paddle can send codes for you.
