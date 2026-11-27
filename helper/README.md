# Loopwright Voice (the voice helper), packaged

The app buyers download: `Loopwright Voice.app`, **Macs with Apple Silicon (M1 or later) only**, macOS 14+, 16 GB memory.
It refuses to run on Intel Macs (it checks `hw.optional.arm64`, so it works under Rosetta too). There's no Windows version.

    bash helper/build-mac.sh 0.1.0      ->  helper/dist/Loopwright-Voice-0.1.0-mac.zip  (~40 MB)

## What's in it
- `src/`: the helper (`app/server.py` + the scripts it runs), copied from `~/song-maker` on 2026-11-27.
- Jordan_Sing and Jordan_Rap, taken at build time from `VOICES_DIR` (default `~/song-maker/my_voice/clean`). They're sold
  separately on loop-wright.com, so they're never committed here: this repo is public.
- The code of ACE-Step 1.5 (MIT) and SoulX-Singer (Apache 2.0) at pinned commits, fetched by the build. No weights.
- `uv`, which installs Python 3.11 and the packages in `requirements-*.txt` (frozen from the working Mac).

## First launch (`launcher.sh` -> `setup.py`)
Checks the Mac, then sets up in `~/Library/Application Support/Loopwright/Voice` with a progress page on
http://127.0.0.1:8792: Python, two environments (~3 GB), then the models from Hugging Face (~20 GB; 25 GB free needed).
Every step is remembered, so opening the app again after a failure carries on. Then the helper starts in the background
and writes `voice-helper.json`; Loopwright finds it. Opening the app while it runs offers to stop it.
Logs: `~/Library/Logs/Loopwright Voice setup.log` and `Loopwright Voice.log`.

## Left out on purpose
becruily's karaoke model and anvuew's dereverb model (licenses not cleared yet). Without them the helper skips
"Removing background noise" and reports `clean: false` in `/api/options`. To bring it back once cleared: add their
folders to the "Pitch tracker" download's `allow` list in `setup.py`.

## Before selling
- Notarize: `xcrun notarytool store-credentials loopwright --apple-id <you> --team-id AUV484UBU7`, then
  `NOTARY_PROFILE=loopwright bash helper/build-mac.sh <version>`.
- The helper has no license check of its own: anyone with the zip can run it. Keep the download link private.
- Not tested yet: a full singing job from a fresh install, a Mac with 16 GB, macOS 14.
