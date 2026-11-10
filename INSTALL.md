# Installing Live Planet

Copy the plugin file into your plugin folder, then restart your music app and rescan plugins.

| File | Mac | Windows |
|---|---|---|
| `Live Planet.vst3` | `/Library/Audio/Plug-Ins/VST3/` | `C:\Program Files\Common Files\VST3\` |
| `Live Planet.component` (Logic, GarageBand) | `/Library/Audio/Plug-Ins/Components/` | n/a |
| `Live Planet.clap` (Bitwig, Reaper, FL Studio 2024+) | `/Library/Audio/Plug-Ins/CLAP/` | `C:\Program Files\Common Files\CLAP\` |

On Linux, use `~/.vst3/` and `~/.clap/`.

The **Live Planet** app next to the plugins works on its own, with no music app needed.

## Mac: plugin doesn't show up, or "can't be opened"
The Mac download is a zip inside a zip: unzip **LivePlanet-macOS.zip** again to get the plugins.
Test builds are not signed by Apple yet, so macOS blocks them. After copying them into place, open Terminal and run:

```
cd "/Library/Audio/Plug-Ins"
for p in "VST3/Live Planet.vst3" "Components/Live Planet.component"; do
  [ -e "$p" ] && sudo xattr -dr com.apple.quarantine "$p" && sudo codesign --force --deep -s - "$p"
done
```

In Ableton: Settings > Plug-Ins, turn on "Use VST3 Plug-In System Folders" and "Use Audio Units v2",
then hold Option and click Rescan. Live Planet appears under the maker name **Live Planet Audio**.

## Using it
1. Add Live Planet to an **instrument** track.
2. Type a city in the search box and press Go. It starts playing that city's weather.
3. Hold a few keys on your MIDI keyboard to choose the chord yourself. The weather keeps playing it.
4. To play another instrument with Live Planet's notes, route its MIDI output to that track.

## Song mode (Ableton)
1. In Live Planet open the **song** tab. Drop a song you love on the plugin (optional), then press **write a song**.
2. Songs are saved in `~/Music/Live Planet/Songs/`.
3. **Locators:** the first song also creates `~/Music/Live Planet/Ableton Helper/Live Planet Song Helper.amxd`
   (needs Live Suite / Max for Live). Drag the **locators** pill (or that file) onto any track once, then press
   **place song locators**: it sets the tempo and adds a named locator at every section.
4. **Parts:** drag **stems** into the arrangement at bar 1 (hold Cmd while dropping so each part gets its own track),
   or drag **midi** to get the notes, to play them with any instrument.
