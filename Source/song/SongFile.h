#pragma once
#include "Song.h"

// A written song saved as JSON (song.json in its folder), so it can be opened again later:
// when your Ableton project is reopened, the plugin finds its song and you can keep editing it.
namespace lp::song::file
{

inline juce::var floats (const float* v, size_t n) { juce::Array<juce::var> a; for (size_t i = 0; i < n; ++i) a.add (v[i]); return a; }
inline void readFloats (const juce::var& v, float* out, size_t n)
{
    if (auto* a = v.getArray()) for (size_t i = 0; i < n && i < (size_t) a->size(); ++i) out[i] = (float) (*a)[(int) i];
}
template <class C> juce::var ints (const C& c) { juce::Array<juce::var> a; for (int x : c) a.add (x); return a; }
inline std::vector<int> readInts (const juce::var& v) { std::vector<int> r; if (auto* a = v.getArray()) for (auto& x : *a) r.push_back ((int) x); return r; }
inline juce::var strings (const juce::StringArray& s) { juce::Array<juce::var> a; for (auto& x : s) a.add (x); return a; }
inline juce::StringArray readStrings (const juce::var& v) { juce::StringArray r; if (auto* a = v.getArray()) for (auto& x : *a) r.add (x.toString()); return r; }

inline juce::var toneToVar (const Tone& t)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("valid", t.valid); o->setProperty ("bands", floats (t.bandsDb.data(), 10));
    o->setProperty ("rolloff", t.rolloffHz); o->setProperty ("air", t.airDb); o->setProperty ("rms", t.rmsDb); o->setProperty ("crest", t.crestDb);
    return o;
}
inline Tone toneFromVar (const juce::var& v)
{
    Tone t;
    t.valid = v["valid"]; readFloats (v["bands"], t.bandsDb.data(), 10);
    t.rolloffHz = v.getProperty ("rolloff", t.rolloffHz); t.airDb = v.getProperty ("air", t.airDb);
    t.rmsDb = v.getProperty ("rms", t.rmsDb); t.crestDb = v.getProperty ("crest", t.crestDb);
    return t;
}

inline juce::var partToVar (const Part& p)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", juce::String (p.name)); o->setProperty ("kind", (int) p.kind); o->setProperty ("sound", (int) p.sound);
    o->setProperty ("gainDb", p.gainDb); o->setProperty ("variant", p.variant);
    juce::Array<juce::var> notes;
    for (auto& n : p.notes)
        notes.add (n.glide ? juce::Array<juce::var> { n.beat, n.length, n.pitch, n.velocity, 1 } : juce::Array<juce::var> { n.beat, n.length, n.pitch, n.velocity });
    o->setProperty ("notes", notes);
    o->setProperty ("samplePath", p.samplePath); o->setProperty ("loopBars", p.loopBars); o->setProperty ("loopStartBars", ints (p.loopStartBars));
    o->setProperty ("source", p.source); o->setProperty ("remix", p.remix); o->setProperty ("candidates", strings (p.candidates));
    o->setProperty ("candidate", p.candidate); o->setProperty ("pitchSemis", p.pitchSemis); o->setProperty ("reversed", p.reversed);
    o->setProperty ("autoTune", p.autoTune);
    if (p.locked) o->setProperty ("locked", true);
    { std::vector<int> ct; for (int x : p.candidateTunes) ct.push_back (x); o->setProperty ("candidateTunes", ints (ct)); }
    o->setProperty ("barSrc", ints (p.barSrc));
    juce::Array<juce::var> bf;
    for (auto& [bar, open] : p.barFilter) bf.add (juce::Array<juce::var> { bar, open });
    o->setProperty ("barFilter", bf);
    o->setProperty ("chop", ints (p.chopBars)); o->setProperty ("stutter", ints (p.stutterBars));
    o->setProperty ("gap", ints (p.gapBars)); o->setProperty ("swell", ints (p.swellBars));
    juce::Array<juce::var> fx;
    for (auto& h : p.fxHits) fx.add (juce::Array<juce::var> { h.path, h.beat, h.peakOnBeat, h.gainDb });
    o->setProperty ("fxHits", fx);
    o->setProperty ("mixDb", p.mixDb); o->setProperty ("regionStart", p.regionStart); o->setProperty ("regionBars", p.regionBars);
    o->setProperty ("grainAmount", p.grainAmount); o->setProperty ("grainSizeMs", p.grainSizeMs); o->setProperty ("grainChaos", p.grainChaos);
    o->setProperty ("grainPosition", p.grainPosition); o->setProperty ("grainRate", p.grainRate); o->setProperty ("pan", p.pan);
    if (! p.kit.empty())
    {
        juce::Array<juce::var> kit;
        for (auto& [note, path] : p.kit) kit.add (juce::Array<juce::var> { note, path });
        o->setProperty ("kit", kit);
    }
    return o;
}

inline Part partFromVar (const juce::var& v)
{
    Part p;
    p.name = v["name"].toString().toStdString(); p.kind = (PartKind) (int) v["kind"]; p.sound = (lp::SoundId) (int) v["sound"];
    p.gainDb = v["gainDb"]; p.variant = v["variant"];
    if (auto* a = v["notes"].getArray())
        for (auto& n : *a) p.notes.push_back ({ (double) n[0], (double) n[1], (int) n[2], (float) n[3], n.size() > 4 && (int) n[4] != 0 });
    p.samplePath = v["samplePath"].toString(); p.loopBars = v["loopBars"]; p.loopStartBars = readInts (v["loopStartBars"]);
    p.source = v["source"].toString(); p.remix = v["remix"].toString(); p.candidates = readStrings (v["candidates"]);
    p.candidate = v["candidate"]; p.pitchSemis = v["pitchSemis"]; p.reversed = v["reversed"]; p.autoTune = v["autoTune"]; p.locked = (bool) v.getProperty ("locked", false);
    for (int x : readInts (v["candidateTunes"])) p.candidateTunes.add (x);
    p.barSrc = readInts (v["barSrc"]);
    if (auto* a = v["barFilter"].getArray()) for (auto& e : *a) p.barFilter[(int) e[0]] = (float) e[1];
    for (int x : readInts (v["chop"])) p.chopBars.insert (x);
    for (int x : readInts (v["stutter"])) p.stutterBars.insert (x);
    for (int x : readInts (v["gap"])) p.gapBars.insert (x);
    for (int x : readInts (v["swell"])) p.swellBars.insert (x);
    if (auto* a = v["fxHits"].getArray()) for (auto& e : *a) p.fxHits.push_back ({ e[0].toString(), (double) e[1], (bool) e[2], (float) e[3] });
    p.mixDb = v["mixDb"]; p.regionStart = v["regionStart"]; p.regionBars = v["regionBars"];
    p.grainAmount = v["grainAmount"]; p.grainSizeMs = v.getProperty ("grainSizeMs", 120.0f); p.grainChaos = v["grainChaos"];
    p.grainPosition = v["grainPosition"]; p.grainRate = v.getProperty ("grainRate", 2); p.pan = v["pan"];
    if (auto* a = v["kit"].getArray()) for (auto& e : *a) p.kit[(int) e[0]] = e[1].toString();
    return p;
}

inline juce::var songToVar (const Song& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("title", s.title); o->setProperty ("bpm", s.bpm); o->setProperty ("key", s.key); o->setProperty ("minor", s.minor);
    o->setProperty ("style", (int) s.style);
    juce::Array<juce::var> secs;
    for (auto& sec : s.sections) secs.add (juce::Array<juce::var> { juce::String (sec.name), sec.startBar, sec.bars, sec.energy });
    o->setProperty ("sections", secs);
    juce::Array<juce::var> parts;
    for (auto& p : s.parts) parts.add (partToVar (p));
    o->setProperty ("parts", parts);
    juce::Array<juce::var> drums;
    for (auto& [note, path] : s.drumSamples) drums.add (juce::Array<juce::var> { note, path });
    o->setProperty ("drumSamples", drums);
    o->setProperty ("refTone", toneToVar (s.refTone));
    o->setProperty ("matchEqDb", floats (s.matchEqDb.data(), 10)); o->setProperty ("matchGainDb", s.matchGainDb);
    o->setProperty ("playbackMakeupDb", s.playbackMakeupDb); o->setProperty ("mixShaped", s.mixShaped);
    auto* g = new juce::DynamicObject();
    g->setProperty ("valid", s.groove.valid); g->setProperty ("swing16", s.groove.swing16); g->setProperty ("swing8", s.groove.swing8);
    g->setProperty ("snareLate", s.groove.snareLate); g->setProperty ("kickLate", s.groove.kickLate);
    g->setProperty ("accent", floats (s.groove.accent.data(), 16)); g->setProperty ("humanizeMs", s.groove.humanizeMs);
    g->setProperty ("amount", s.groove.amount);
    o->setProperty ("groove", g);
    return o;
}

inline bool songFromVar (const juce::var& v, Song& s)
{
    if (! v.isObject() || v["parts"].getArray() == nullptr) return false;
    s = {};
    s.title = v["title"].toString(); s.bpm = v["bpm"]; s.key = v["key"]; s.minor = v["minor"]; s.style = (Style) (int) v["style"];
    if (auto* a = v["sections"].getArray()) for (auto& e : *a) s.sections.push_back ({ e[0].toString().toStdString(), (int) e[1], (int) e[2], (float) e[3] });
    for (auto& p : *v["parts"].getArray()) s.parts.push_back (partFromVar (p));
    if (auto* a = v["drumSamples"].getArray()) for (auto& e : *a) s.drumSamples[(int) e[0]] = e[1].toString();
    s.refTone = toneFromVar (v["refTone"]);
    readFloats (v["matchEqDb"], s.matchEqDb.data(), 10);
    s.matchGainDb = v["matchGainDb"]; s.playbackMakeupDb = v["playbackMakeupDb"]; s.mixShaped = v["mixShaped"];
    if (const auto g = v["groove"]; g.isObject())
    {
        s.groove.valid = g["valid"]; s.groove.swing16 = g["swing16"]; s.groove.swing8 = g["swing8"];
        s.groove.snareLate = g["snareLate"]; s.groove.kickLate = g["kickLate"];
        readFloats (g["accent"], s.groove.accent.data(), 16);
        s.groove.humanizeMs = g.getProperty ("humanizeMs", 4.0f); s.groove.amount = g.getProperty ("amount", 1.0f);
    }
    return s.bpm > 0 && ! s.sections.empty();
}

} // namespace lp::song::file
