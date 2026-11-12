# Installing Loopwright

## Mac (easiest): the installer
Open **Loopwright.pkg** and click through. It puts the VST3, Audio Unit and CLAP plugins and the
Loopwright app in the right folders, and removes the old Live Planet plugins.
If macOS says it can't check the developer (test builds aren't signed by Apple yet):
right-click the .pkg > **Open** > **Open**.

## By hand
| File | Mac | Windows |
|---|---|---|
| `Loopwright.vst3` | `/Library/Audio/Plug-Ins/VST3/` | `C:\Program Files\Common Files\VST3\` |
| `Loopwright.component` (Logic, GarageBand, Ableton AU) | `/Library/Audio/Plug-Ins/Components/` | n/a |
| `Loopwright.clap` (Bitwig, Reaper, FL Studio 2024+) | `/Library/Audio/Plug-Ins/CLAP/` | `C:\Program Files\Common Files\CLAP\` |

On Linux, use `~/.vst3/` and `~/.clap/`.

Mac, copied by hand from the zip: test builds aren't signed by Apple yet, so run this once in Terminal:

```
cd "/Library/Audio/Plug-Ins"
for p in "VST3/Loopwright.vst3" "Components/Loopwright.component" "CLAP/Loopwright.clap"; do
  [ -e "$p" ] && sudo xattr -cr "$p" && sudo codesign --force --deep -s - "$p"
done
```

Then rescan plugins in your DAW (Ableton: Settings > Plug-Ins, hold Option and click Rescan).

## Using it
1. Put Loopwright on a track. Drop a song you love on it, and/or type a description ("dark trap in F minor with flute").
2. Press **Write a song**. It's made from your Splice / WAVS samples, at your project's tempo, all in one key.
3. **STUDIO** and **EDIT**: mix, swap, re-pitch, grain and add effects to each track. **Love it / Not this** teaches it your taste.
4. Drag **stems** (or one track, with **Print FX** on to keep its effects) into your DAW. Drag **locators** onto a track in
   Ableton Live Suite to set the tempo and a locator at every section.
5. Save your project: Loopwright remembers the song and every track's mix, in any DAW.

Songs are saved in `~/Music/Loopwright/Songs/`.
