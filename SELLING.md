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

## 4. Selling and license codes (Stripe + the website)
Price: **$39 one-time** (the launch price), yours forever, free updates, with a **7-day free trial**.

How a sale works:
1. "buy now" on the website goes to `/api/checkout`, which opens a Stripe checkout for $39.
2. After paying, Stripe sends the buyer to `/thanks`: the site checks the payment with Stripe and shows their
   license code (made from their email, so the same buyer always gets the same code). It's emailed to them too.
3. In the plugin they click **TRIAL** at the top of Settings, paste the code and press **Activate**. It's checked
   offline from then on. Lost codes: `/license` emails the code again to the address that bought.

The key pair: the website holds `private.key` (it makes codes), the plugin holds `packaging/license-public-key.txt`
(it only checks them). **Back up `private.key` somewhere safe and never put it in a repo.** Lose it and you can't
make codes that the shipped plugins accept; leak it and anyone can.

Setup, once:
- **Website secrets** (Lovable > Secrets): `STRIPE_SECRET_KEY`, `STRIPE_WEBHOOK_SECRET`, `LOOPWRIGHT_PRIVATE_KEY`
  (the whole contents of private.key), and for the emails `RESEND_API_KEY` + `LICENSE_EMAIL_FROM`.
- **Stripe webhook**: Developers > Webhooks > add `https://your-site/api/stripe-webhook`, event
  `checkout.session.completed`; its signing secret is `STRIPE_WEBHOOK_SECRET`.
- **GitHub variable** (Settings > Secrets and variables > Actions > Variables): `LOOPWRIGHT_BUY_URL` =
  `https://your-site/api/checkout`, for the plugin's **Buy $39** button.
- **The version you sell**: push a tag like `v1.0.0` (or run the workflow by hand with "licensed" ticked). Those builds
  have the trial and need a code; every other build stays unlocked for testing.

By hand, if you ever need to: `Keygen --make "buyer@email.com" private.key` prints a code (use their email in lowercase,
the same as the website, so both give the same code).
