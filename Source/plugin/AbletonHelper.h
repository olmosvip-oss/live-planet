#pragma once
#include <juce_core/juce_core.h>

// Writes the "Live Planet Song Helper" Max for Live device (needs Ableton Live Suite).
// Drop it on any track once; pressing its button sets the tempo and places named locators
// for the newest song Live Planet wrote. It reads ~/Music/Live Planet/latest-song.json.
namespace lp::ableton
{

inline juce::File helperFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Live Planet").getChildFile ("Ableton Helper");
}

inline juce::String helperScript (const juce::File& latest)
{
    const auto posix = latest.getFullPathName();
    juce::String js = R"JS(// Live Planet Song Helper: tempo + named locators for the newest Live Planet song.
autowatch = 1;
inlets = 1;
outlets = 1;

var PATHS = ["@POSIX@", "Macintosh HD:@POSIX@"];

function readText (p)
{
    var f = new File (p, "read");
    if (! f.isopen) return null;
    var s = "";
    while (f.position < f.eof) s += f.readstring (4096);
    f.close();
    return s;
}

function first (v) { return (v instanceof Array) ? v[0] : v; }

function readPlan()
{
    var txt = null;
    for (var i = 0; i < PATHS.length && ! txt; i++) txt = readText (PATHS[i]);
    if (! txt) { outlet (0, "set", "no song yet: make one in Live Planet first"); return null; }
    return JSON.parse (txt);
}

// one audio track per part, each clip starting at bar 1 (needs Live 12)
function build()
{
    var plan = readPlan();
    if (! plan) return;
    var major = first (new LiveAPI ("live_app").call ("get_major_version"));
    if (major < 12) { outlet (0, "set", "building tracks needs Live 12. drag the stems in instead (hold cmd)."); return; }
    var song = new LiveAPI ("live_set");
    song.set ("tempo", plan.bpm);
    for (var i = 0; i < plan.stems.length; i++)
    {
        song.call ("create_audio_track", -1);
        var count = song.getcount ("tracks");
        var track = new LiveAPI ("live_set tracks " + (count - 1));
        var file = plan.stems[i];
        track.set ("name", file.split ("/").pop().replace (/\.wav$/, "").replace (/^\d+ /, ""));
        track.call ("create_audio_clip", file, 0);
    }
    bang();
    outlet (0, "set", plan.stems.length + " tracks + locators for '" + plan.title + "'. turn off 'play with my song' in live planet.");
}

function bang()
{
    var plan = readPlan();
    if (! plan) return;
    var song = new LiveAPI ("live_set");
    song.set ("tempo", plan.bpm);

    // only add locators that aren't there yet (the call toggles them)
    var have = {};
    var n = song.getcount ("cue_points");
    for (var c = 0; c < n; c++)
        have[Math.round (first (new LiveAPI ("live_set cue_points " + c).get ("time")))] = true;

    var keep = first (song.get ("current_song_time"));
    for (var s = 0; s < plan.sections.length; s++)
    {
        var t = plan.sections[s].beat;
        if (! have[t]) { song.set ("current_song_time", t); song.call ("set_or_delete_cue"); }
    }

    // name them after the song's sections
    n = song.getcount ("cue_points");
    for (var c2 = 0; c2 < n; c2++)
    {
        var cue = new LiveAPI ("live_set cue_points " + c2);
        var time = Math.round (first (cue.get ("time")));
        for (var s2 = 0; s2 < plan.sections.length; s2++)
            if (plan.sections[s2].beat == time) cue.set ("name", plan.sections[s2].name);
    }
    song.set ("current_song_time", keep);
    outlet (0, "set", plan.sections.length + " locators for '" + plan.title + "' at " + plan.bpm + " bpm");
}
)JS";
    return js.replace ("@POSIX@", posix);
}

inline juce::String helperPatcher()
{
    return R"JSON({
 "patcher": {
  "fileversion": 1,
  "appversion": { "major": 8, "minor": 5, "revision": 5, "architecture": "x64", "modernui": 1 },
  "classnamespace": "box",
  "rect": [ 100.0, 100.0, 420.0, 220.0 ],
  "openinpresentation": 1,
  "default_fontsize": 12.0,
  "gridsize": [ 15.0, 15.0 ],
  "boxes": [
   { "box": { "id": "obj-1", "maxclass": "live.text", "numinlets": 1, "numoutlets": 2, "outlettype": [ "", "" ],
              "parameter_enable": 1, "mode": 0, "text": "place song locators", "texton": "place song locators",
              "patching_rect": [ 20.0, 20.0, 160.0, 30.0 ], "presentation": 1, "presentation_rect": [ 8.0, 8.0, 180.0, 34.0 ],
              "varname": "place",
              "saved_attribute_attributes": { "valueof": { "parameter_longname": "place", "parameter_shortname": "place",
                                                           "parameter_type": 2, "parameter_mmax": 1, "parameter_enum": [ "val1", "val2" ] } } } },
   { "box": { "id": "obj-4", "maxclass": "newobj", "text": "sel 1", "numinlets": 2, "numoutlets": 2,
              "outlettype": [ "bang", "" ], "patching_rect": [ 20.0, 60.0, 50.0, 22.0 ] } },
   { "box": { "id": "obj-2", "maxclass": "newobj", "text": "js lp_helper.js", "numinlets": 1, "numoutlets": 1,
              "outlettype": [ "" ], "patching_rect": [ 20.0, 95.0, 120.0, 22.0 ] } },
   { "box": { "id": "obj-5", "maxclass": "live.text", "numinlets": 1, "numoutlets": 2, "outlettype": [ "", "" ],
              "parameter_enable": 1, "mode": 0, "text": "build song tracks", "texton": "build song tracks",
              "patching_rect": [ 200.0, 20.0, 160.0, 30.0 ], "presentation": 1, "presentation_rect": [ 196.0, 8.0, 160.0, 34.0 ],
              "varname": "build",
              "saved_attribute_attributes": { "valueof": { "parameter_longname": "build", "parameter_shortname": "build",
                                                           "parameter_type": 2, "parameter_mmax": 1, "parameter_enum": [ "val1", "val2" ] } } } },
   { "box": { "id": "obj-6", "maxclass": "newobj", "text": "sel 1", "numinlets": 2, "numoutlets": 2,
              "outlettype": [ "bang", "" ], "patching_rect": [ 200.0, 60.0, 50.0, 22.0 ] } },
   { "box": { "id": "obj-7", "maxclass": "message", "text": "build", "numinlets": 2, "numoutlets": 1,
              "outlettype": [ "" ], "patching_rect": [ 200.0, 95.0, 50.0, 22.0 ] } },
   { "box": { "id": "obj-3", "maxclass": "message", "text": "make a song in Live Planet, then press", "numinlets": 2, "numoutlets": 1,
              "outlettype": [ "" ], "patching_rect": [ 20.0, 130.0, 330.0, 22.0 ],
              "presentation": 1, "presentation_rect": [ 8.0, 48.0, 348.0, 22.0 ] } }
  ],
  "lines": [
   { "patchline": { "source": [ "obj-1", 0 ], "destination": [ "obj-4", 0 ] } },
   { "patchline": { "source": [ "obj-4", 0 ], "destination": [ "obj-2", 0 ] } },
   { "patchline": { "source": [ "obj-2", 0 ], "destination": [ "obj-3", 0 ] } },
   { "patchline": { "source": [ "obj-5", 0 ], "destination": [ "obj-6", 0 ] } },
   { "patchline": { "source": [ "obj-6", 0 ], "destination": [ "obj-7", 0 ] } },
   { "patchline": { "source": [ "obj-7", 0 ], "destination": [ "obj-2", 0 ] } }
  ],
  "dependency_cache": [ { "name": "lp_helper.js", "type": "TEXT", "implicit": 1 } ],
  "latency": 0,
  "is_mpe": 0,
  "project": { "version": 1, "creationdate": 0, "modificationdate": 0, "viewrect": [ 0.0, 0.0, 300.0, 500.0 ],
               "autoorganize": 1, "hideprojectwindow": 1, "showdependencies": 1, "autolocalize": 0,
               "contents": { "patchers": { } }, "layout": { }, "searchpath": { }, "detailsvisible": 0,
               "amxdtype": 1633771873, "readonly": 0, "devpathtype": 0, "devpath": ".", "sortmode": 0, "viewmode": 0 }
 }
})JSON";
}

// Writes the device (.amxd) and its script next to it. Returns the device file.
inline juce::File installHelper (const juce::File& latestPointer)
{
    auto dir = helperFolder();
    dir.createDirectory();
    dir.getChildFile ("lp_helper.js").replaceWithText (helperScript (latestPointer));

    // .amxd = a small binary header, then the patcher as JSON
    const auto json = helperPatcher();
    juce::MemoryOutputStream out;
    auto u32 = [&out] (juce::uint32 v) { out.writeInt ((int) v); }; // little-endian
    out.write ("ampf", 4); u32 (4);
    out.write ("aaaa", 4);                     // an audio effect, so it fits on any track
    out.write ("meta", 4); u32 (4); u32 (0);
    out.write ("ptch", 4); u32 ((juce::uint32) json.getNumBytesAsUTF8() + 1);
    out.write (json.toRawUTF8(), json.getNumBytesAsUTF8());
    out.writeByte (0);
    auto device = dir.getChildFile ("Live Planet Song Helper.amxd");
    device.replaceWithData (out.getData(), out.getDataSize());
    return device;
}

} // namespace lp::ableton
