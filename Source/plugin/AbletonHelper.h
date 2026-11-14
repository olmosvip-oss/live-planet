#pragma once
#include "AppFolder.h"
#include <juce_core/juce_core.h>

// Writes the "Live Planet Song Helper" Max for Live device (needs Ableton Live Suite).
// Drop it on any track once; pressing its button sets the tempo and places named locators
// for the newest song Live Planet wrote. It reads ~/Music/Live Planet/latest-song.json.
namespace lp::ableton
{

inline juce::File helperFolder()
{
    return lp::appFolder().getChildFile ("Ableton Helper");
}

inline juce::String helperScript (const juce::File& latest)
{
    const auto posix = latest.getFullPathName();
    juce::String js = R"JS(// Loopwright Song Helper: the song's tempo, its sections as named locators, and (Live 12) its tracks.
autowatch = 1;
inlets = 1;
outlets = 1;

var PATHS = ["@POSIX@", "Macintosh HD:@POSIX@"];
// names Loopwright gives its sections: locators with these names are its own, and get moved when the song changes
var OURS = /^(intro|verse|pre-chorus|pre chorus|prechorus|chorus|hook|drop|bridge|break|breakdown|build|outro|interlude|post-chorus)( \d+)?$/i;

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
function say (t) { outlet (0, "set", t); }

function readPlan()
{
    var txt = null;
    for (var i = 0; i < PATHS.length && ! txt; i++) txt = readText (PATHS[i]);
    if (! txt) { say ("no song yet: make one in Loopwright first"); return null; }
    return JSON.parse (txt);
}

// ---- steps, one at a time: Live needs a moment after the playhead moves before a locator lands there
var steps = [], stepTask = null, afterSteps = null;
function runSteps (then)
{
    afterSteps = then;
    if (stepTask) stepTask.cancel();
    stepTask = new Task (nextStep, this);
    stepTask.interval = 60;
    stepTask.repeat (-1);
}
function nextStep()
{
    if (steps.length == 0)
    {
        stepTask.cancel();
        stepTask = null;
        if (afterSteps) { var f = afterSteps; afterSteps = null; f(); }
        return;
    }
    var s = steps.shift();
    try { s(); } catch (e) { post ("loopwright helper: " + e + "\n"); }
}

function cues()
{
    var song = new LiveAPI ("live_set");
    var out = [];
    var n = song.getcount ("cue_points");
    for (var c = 0; c < n; c++)
    {
        var cue = new LiveAPI ("live_set cue_points " + c);
        out.push ({ api: cue, time: first (cue.get ("time")), name: String (first (cue.get ("name"))) });
    }
    return out;
}

// the song's sections as named locators (old Loopwright locators that aren't sections any more are taken away)
function bang() { place (null); watch(); }

// keeps the locators in step with the plugin: when you change the song's sections there, they move here too
// (only while Live is stopped, so it never jumps the playhead while you play)
var watchTask = null, lastSeen = null;
function planKey()
{
    var txt = null;
    for (var i = 0; i < PATHS.length && ! txt; i++) txt = readText (PATHS[i]);
    if (! txt) return null;
    var p = JSON.parse (txt);
    return p.title + "|" + p.bpm + "|" + JSON.stringify (p.sections);
}
function watch()
{
    if (watchTask) return;
    lastSeen = planKey();
    watchTask = new Task (function()
    {
        var k = planKey();
        if (k === null || k === lastSeen || stepTask) return;
        if (first (new LiveAPI ("live_set").get ("is_playing")) == 1) return;
        lastSeen = k;
        place (null);
    }, this);
    watchTask.interval = 2000;
    watchTask.repeat (-1);
}
function place (then)
{
    var plan = readPlan();
    if (! plan) return;
    var song = new LiveAPI ("live_set");
    song.set ("tempo", plan.bpm);
    if (first (song.get ("is_playing")) == 1) song.call ("stop_playing"); // the playhead has to stand still
    var keep = first (song.get ("current_song_time"));

    var want = {};
    for (var s = 0; s < plan.sections.length; s++) want[Math.round (plan.sections[s].beat * 4)] = plan.sections[s].name;
    var have = {};
    var existing = cues();
    steps = [];
    for (var c = 0; c < existing.length; c++)
    {
        var key = Math.round (existing[c].time * 4);
        if (want[key] !== undefined) { have[key] = true; continue; }
        if (OURS.test (existing[c].name)) toggleAt (existing[c].time);   // an old section of ours: away with it
    }
    for (var k in want) if (! have[k]) toggleAt (k / 4);
    steps.push (function()
    {
        // name them, and count what's really there
        var placed = 0, all = cues();
        for (var i = 0; i < all.length; i++)
        {
            var nm = want[Math.round (all[i].time * 4)];
            if (nm !== undefined) { all[i].api.set ("name", nm); placed++; }
        }
        new LiveAPI ("live_set").set ("current_song_time", keep);
        say (placed + " of " + plan.sections.length + " locators for '" + plan.title + "' at " + plan.bpm + " bpm"
             + (placed < plan.sections.length ? ". press again to finish" : ""));
        lastSeen = planKey();
        if (then) then (plan);
    });
    say ("placing " + plan.sections.length + " locators...");
    runSteps (null);
}
function toggleAt (beat)
{
    steps.push (function() { new LiveAPI ("live_set").set ("current_song_time", beat); });
    steps.push (function() { new LiveAPI ("live_set").call ("set_or_delete_cue"); });
}

// the whole song in Live: a track for every part with its file at bar 1, its notes as MIDI where it has some (Live 12)
function build()
{
    var plan = readPlan();
    if (! plan) return;
    var major = first (new LiveAPI ("live_app").call ("get_major_version"));
    if (major < 12) { say ("building tracks needs Live 12. drag the stems in instead"); return; }
    var song = new LiveAPI ("live_set");
    song.set ("tempo", plan.bpm);
    var tracks = plan.tracks || [];
    if (tracks.length == 0) for (var s = 0; s < plan.stems.length; s++) tracks.push ({ name: "", file: plan.stems[s] });
    steps = [];
    for (var i = 0; i < tracks.length; i++) (function (t)
    {
        steps.push (function()
        {
            song.call ("create_audio_track", -1);
            var count = song.getcount ("tracks");
            var track = new LiveAPI ("live_set tracks " + (count - 1));
            track.set ("name", t.name || t.file.split ("/").pop().replace (/\.wav$/, "").replace (/^\d+ /, ""));
            if (t.color !== undefined) track.set ("color", t.color);
            track.call ("create_audio_clip", t.file, 0);
            if (t.mute) track.set ("mute", 1);
        });
        if (t.notes && t.notes.length > 0) steps.push (function()
        {
            // the same part as MIDI, muted, to play with your own instruments
            try
            {
                song.call ("create_midi_track", -1);
                var count = song.getcount ("tracks");
                var track = new LiveAPI ("live_set tracks " + (count - 1));
                track.set ("name", (t.name || "part") + " midi");
                if (t.color !== undefined) track.set ("color", t.color);
                track.set ("mute", 1);
                var end = 0;
                for (var n = 0; n < t.notes.length; n++) end = Math.max (end, t.notes[n][0] + t.notes[n][1]);
                track.call ("create_midi_clip", 0, Math.ceil (end / 4) * 4);
                var clip = new LiveAPI ("live_set tracks " + (count - 1) + " arrangement_clips 0");
                var list = [];
                for (var m = 0; m < t.notes.length; m++)
                    list.push ({ pitch: t.notes[m][2], start_time: t.notes[m][0], duration: t.notes[m][1], velocity: Math.round (t.notes[m][3] * 127) });
                clip.call ("add_new_notes", { notes: list });
            }
            catch (e) { post ("loopwright helper (midi): " + e + "\n"); }
        });
    }) (tracks[i]);
    say ("building " + tracks.length + " tracks...");
    runSteps (function() { place (function (p) { say (tracks.length + " tracks + locators for '" + p.title + "'. turn off 'play along with my DAW' in loopwright."); }); });
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
   { "box": { "id": "obj-8", "maxclass": "newobj", "text": "live.thisdevice", "numinlets": 1, "numoutlets": 3,
              "outlettype": [ "bang", "int", "int" ], "patching_rect": [ 380.0, 20.0, 100.0, 22.0 ] } },
   { "box": { "id": "obj-9", "maxclass": "newobj", "text": "deferlow", "numinlets": 1, "numoutlets": 1,
              "outlettype": [ "" ], "patching_rect": [ 380.0, 60.0, 70.0, 22.0 ] } },
   { "box": { "id": "obj-10", "maxclass": "newobj", "text": "plugin~", "numinlets": 2, "numoutlets": 2,
              "outlettype": [ "signal", "signal" ], "patching_rect": [ 380.0, 120.0, 60.0, 22.0 ] } },
   { "box": { "id": "obj-11", "maxclass": "newobj", "text": "plugout~", "numinlets": 2, "numoutlets": 0,
              "patching_rect": [ 380.0, 170.0, 60.0, 22.0 ] } },
   { "box": { "id": "obj-3", "maxclass": "message", "text": "drop me on any track: tempo + song locators (kept in step with loopwright)", "numinlets": 2, "numoutlets": 1,
              "outlettype": [ "" ], "patching_rect": [ 20.0, 130.0, 330.0, 22.0 ],
              "presentation": 1, "presentation_rect": [ 8.0, 48.0, 348.0, 22.0 ] } }
  ],
  "lines": [
   { "patchline": { "source": [ "obj-1", 0 ], "destination": [ "obj-4", 0 ] } },
   { "patchline": { "source": [ "obj-4", 0 ], "destination": [ "obj-2", 0 ] } },
   { "patchline": { "source": [ "obj-2", 0 ], "destination": [ "obj-3", 0 ] } },
   { "patchline": { "source": [ "obj-5", 0 ], "destination": [ "obj-6", 0 ] } },
   { "patchline": { "source": [ "obj-6", 0 ], "destination": [ "obj-7", 0 ] } },
   { "patchline": { "source": [ "obj-7", 0 ], "destination": [ "obj-2", 0 ] } },
   { "patchline": { "source": [ "obj-8", 0 ], "destination": [ "obj-9", 0 ] } },
   { "patchline": { "source": [ "obj-9", 0 ], "destination": [ "obj-2", 0 ] } },
   { "patchline": { "source": [ "obj-10", 0 ], "destination": [ "obj-11", 0 ] } },
   { "patchline": { "source": [ "obj-10", 1 ], "destination": [ "obj-11", 1 ] } }
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
    const auto script = helperScript (latestPointer);
    dir.getChildFile ("lp_helper.js").replaceWithText (script);
    // also where Max always looks, so the device finds its script whichever Max version Live uses
    const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    for (auto v : { "Max 8", "Max 9" })
    {
        auto lib = docs.getChildFile (v).getChildFile ("Library");
        if (lib.createDirectory()) lib.getChildFile ("lp_helper.js").replaceWithText (script);
    }

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
    auto device = dir.getChildFile ("Loopwright Song Helper.amxd");
    device.replaceWithData (out.getData(), out.getDataSize());
    return device;
}

} // namespace lp::ableton
