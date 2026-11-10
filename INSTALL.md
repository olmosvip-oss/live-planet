# Installing Live Planet

Copy the plugin file into your plugin folder, then restart your music app and rescan plugins.

| File | Mac | Windows |
|---|---|---|
| `Live Planet.vst3` | `/Library/Audio/Plug-Ins/VST3/` | `C:\Program Files\Common Files\VST3\` |
| `Live Planet.component` (Logic, GarageBand) | `/Library/Audio/Plug-Ins/Components/` | n/a |
| `Live Planet.clap` (Bitwig, Reaper, FL Studio 2024+) | `/Library/Audio/Plug-Ins/CLAP/` | `C:\Program Files\Common Files\CLAP\` |

On Linux, use `~/.vst3/` and `~/.clap/`.

The **Live Planet** app next to the plugins works on its own, with no music app needed.

## Mac: "can't be opened" warning
Test builds are not signed by Apple yet. To allow them, open Terminal and run:

```
sudo xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/Live Planet.vst3" "/Library/Audio/Plug-Ins/Components/Live Planet.component"
```

## Using it
1. Add Live Planet to an **instrument** track.
2. Type a city in the search box and press Go. It starts playing that city's weather.
3. Hold a few keys on your MIDI keyboard to choose the chord yourself. The weather keeps playing it.
4. To play another instrument with Live Planet's notes, route its MIDI output to that track.
