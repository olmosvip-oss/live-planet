#include "Song.h"
#include "../engine/Engine.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace lp::song
{
namespace
{
    constexpr float kTwoPi = 6.2831853f;
    inline float mtof (float n) { return 440.0f * std::pow (2.0f, (n - 69.0f) / 12.0f); }

    // ---------- drums, made from math (no samples)
    struct DrumVoice
    {
        int type = 0; // 0 kick, 1 snare, 2 closed hat, 3 open hat, 4 clap, 5 tom
        int age = 0;
        float vel = 1, phase = 0;
        bool active = false;
    };

    struct DrumKit
    {
        double sr = 48000;
        std::array<DrumVoice, 32> voices {};
        lp::dsp::Noise noise;
        lp::dsp::SVF snareBand, hatHigh, clapBand;
        float lp = 0;

        void prepare (double rate)
        {
            sr = rate;
            snareBand.setup (1800.0f, 0.8f, (float) sr);
            hatHigh.setup (7500.0f, 0.7f, (float) sr);
            clapBand.setup (1200.0f, 1.2f, (float) sr);
        }

        void hit (int note, float vel)
        {
            int type = note == 36 ? 0 : note == 38 ? 1 : note == 42 ? 2 : note == 46 ? 3 : note == 39 ? 4 : 5;
            if (type == 2 || type == 3) // a closed hat chokes the open one
                for (auto& v : voices) if (v.active && v.type == 3) v.active = false;
            for (auto& v : voices)
                if (! v.active) { v = { type, 0, vel, 0, true }; return; }
        }

        void render (float* L, float* R, int n)
        {
            const float fsr = (float) sr;
            for (int i = 0; i < n; ++i)
            {
                const float nz = noise.next();
                snareBand.tick (nz);
                hatHigh.tick (nz);
                clapBand.tick (nz);
                float outL = 0, outR = 0;
                for (auto& v : voices)
                {
                    if (! v.active) continue;
                    const float t = (float) v.age / fsr;
                    float s = 0, pan = 0;
                    switch (v.type)
                    {
                        case 0: { // kick: a falling sine with a soft click
                            const float f = 48.0f + 110.0f * std::exp (-t * 30.0f);
                            v.phase += kTwoPi * f / fsr;
                            s = std::sin (v.phase) * std::exp (-t * 7.0f) * 1.1f + nz * std::exp (-t * 400.0f) * 0.15f;
                            if (t > 0.6f) v.active = false;
                            break; }
                        case 1: { // snare: tone plus filtered noise
                            v.phase += kTwoPi * 185.0f / fsr;
                            s = std::sin (v.phase) * std::exp (-t * 30.0f) * 0.45f + snareBand.bp * std::exp (-t * 16.0f) * 1.6f;
                            if (t > 0.35f) v.active = false;
                            break; }
                        case 2: s = hatHigh.hp * std::exp (-t * 70.0f) * 0.55f; pan = 0.3f; if (t > 0.1f) v.active = false; break;
                        case 3: s = hatHigh.hp * std::exp (-t * 7.0f) * 0.4f; pan = 0.3f; if (t > 0.6f) v.active = false; break;
                        case 4: { // clap: three quick bursts and a tail
                            const float bursts = (t < 0.01f || (t > 0.012f && t < 0.02f) || (t > 0.023f && t < 0.031f)) ? 1.0f : 0.0f;
                            s = clapBand.bp * (bursts * 1.6f + std::exp (-t * 14.0f) * 1.1f) * (t > 0.023f ? 1.0f : 0.8f);
                            pan = -0.1f;
                            if (t > 0.4f) v.active = false;
                            break; }
                        default: { // tom
                            const float f = 110.0f + 60.0f * std::exp (-t * 20.0f);
                            v.phase += kTwoPi * f / fsr;
                            s = std::sin (v.phase) * std::exp (-t * 9.0f) * 0.8f;
                            pan = -0.25f;
                            if (t > 0.5f) v.active = false;
                        }
                    }
                    s *= v.vel;
                    outL += s * (1.0f - pan);
                    outR += s * (1.0f + pan);
                    ++v.age;
                }
                // a touch of warmth
                lp += 0.6f * (outL - lp);
                L[i] = std::tanh (outL * 0.8f);
                R[i] = std::tanh (outR * 0.8f);
            }
        }
    };

    // ---------- a soft, round bass (sine plus a little harmonic), also pure math
    struct BassSynth
    {
        double sr = 48000;
        struct V { int note = 0; float vel = 0, env = 0; bool on = false, active = false; double phase = 0; };
        std::array<V, 4> voices {};
        float lowpass = 0;

        void noteOn (int note, float vel)
        {
            for (auto& v : voices) if (v.active && v.on) v.on = false; // one note at a time
            for (auto& v : voices) if (! v.active) { v = { note, vel, 0.0f, true, true, 0.0 }; return; }
        }
        void noteOff (int note) { for (auto& v : voices) if (v.note == note) v.on = false; }

        void render (float* L, float* R, int n)
        {
            const float atk = 1.0f - std::exp (-1.0f / (0.006f * (float) sr));
            const float rel = std::exp (-1.0f / (0.07f * (float) sr));
            for (int i = 0; i < n; ++i)
            {
                float s = 0;
                for (auto& v : voices)
                {
                    if (! v.active) continue;
                    v.env = v.on ? v.env + (1.0f - v.env) * atk : v.env * rel;
                    if (! v.on && v.env < 0.0005f) { v.active = false; continue; }
                    v.phase += 2.0 * juce::MathConstants<double>::pi * mtof ((float) v.note) / sr;
                    const float ph = (float) v.phase;
                    s += (std::sin (ph) + 0.25f * std::sin (2.0f * ph) + 0.08f * std::sin (3.0f * ph)) * v.env * v.vel;
                }
                lowpass += 0.25f * (s - lowpass);
                L[i] = R[i] = std::tanh (lowpass * 0.9f);
            }
        }
    };

    struct Event { long sample; int note; float vel; bool on; };

    std::vector<Event> eventsFor (const Part& part, double samplesPerBeat)
    {
        std::vector<Event> ev;
        for (auto& n : part.notes)
        {
            ev.push_back ({ (long) (n.beat * samplesPerBeat), n.pitch, n.velocity, true });
            ev.push_back ({ (long) ((n.beat + n.length) * samplesPerBeat), n.pitch, 0.0f, false });
        }
        // note-offs first at the same moment, so a repeated note retriggers cleanly
        std::sort (ev.begin(), ev.end(), [] (const Event& a, const Event& b)
                   { return a.sample != b.sample ? a.sample < b.sample : (! a.on && b.on); });
        return ev;
    }

    bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buf, double sr)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (file.createOutputStream().release());
        if (os == nullptr) return false;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
        if (w == nullptr) return false;
        os.release();
        return w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    }

    juce::String keyName (int key, bool minor) { return juce::String (lp::noteName (key)) + (minor ? " minor" : " major"); }
}

void writeMidi (const Song& song, const juce::File& file)
{
    constexpr int tpq = 960;
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);

    // conductor track: tempo, metre, key and the section markers (most DAWs show these as markers)
    juce::MidiMessageSequence conductor;
    conductor.addEvent (juce::MidiMessage::textMetaEvent (3, song.title), 0);
    conductor.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / song.bpm)), 0);
    conductor.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    conductor.addEvent (juce::MidiMessage::keySignatureMetaEvent (0, song.minor), 0);
    for (auto& s : song.sections)
        conductor.addEvent (juce::MidiMessage::textMetaEvent (6, s.name), s.startBar * 4.0 * tpq);
    conductor.addEvent (juce::MidiMessage::endOfTrack(), song.totalBars() * 4.0 * tpq);
    mf.addTrack (conductor);

    for (auto& part : song.parts)
    {
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, part.name), 0);
        const int ch = part.kind == PartKind::Drums ? 10 : 1;
        for (auto& n : part.notes)
        {
            seq.addEvent (juce::MidiMessage::noteOn (ch, n.pitch, juce::jlimit (0.05f, 1.0f, n.velocity)), n.beat * tpq);
            seq.addEvent (juce::MidiMessage::noteOff (ch, n.pitch), (n.beat + n.length) * tpq - 1);
        }
        seq.sort();
        seq.updateMatchedPairs();
        mf.addTrack (seq);
    }
    file.deleteFile();
    juce::FileOutputStream os (file);
    if (os.openedOk()) mf.writeTo (os, 1);
}

bool render (const Song& song, const RenderSettings& rs, const juce::File& folder, Rendered& out,
             std::function<void (float)> progress, std::function<bool()> shouldStop)
{
    folder.createDirectory();
    const auto stemDir = folder.getChildFile ("stems");
    stemDir.createDirectory();
    out = {};
    out.folder = folder;

    const double sr = rs.sampleRate;
    const double spb = sr * 60.0 / song.bpm;
    const int total = (int) ((song.totalBars() * 4.0 + 8.0) * spb); // a few extra beats for tails
    constexpr int block = 256;
    juce::AudioBuffer<float> mix (2, total);
    mix.clear();

    lp::Weather calm; // a gentle, neutral sky for the song's sounds
    calm.rainMm = 0; calm.windKmh = 6; calm.cloudPct = 30; calm.temperatureC = 18; calm.humidityPct = 60;

    for (size_t pi = 0; pi < song.parts.size(); ++pi)
    {
        const auto& part = song.parts[pi];
        juce::AudioBuffer<float> buf (2, total);
        buf.clear();
        const auto events = eventsFor (part, spb);
        size_t next = 0;
        auto* L = buf.getWritePointer (0);
        auto* R = buf.getWritePointer (1);

        if (part.kind == PartKind::Drums)
        {
            DrumKit kit;
            kit.prepare (sr);
            for (int pos = 0; pos < total; pos += block)
            {
                const int n = std::min (block, total - pos);
                int done = 0;
                while (next < events.size() && events[next].sample < pos + n)
                {
                    const int at = (int) std::max<long> (0, events[next].sample - pos);
                    if (at > done) { kit.render (L + pos + done, R + pos + done, at - done); done = at; }
                    if (events[next].on) kit.hit (events[next].note, events[next].vel);
                    ++next;
                }
                kit.render (L + pos + done, R + pos + done, n - done);
            }
        }
        else if (part.kind == PartKind::Bass)
        {
            BassSynth bass;
            bass.sr = sr;
            for (int pos = 0; pos < total; pos += block)
            {
                const int n = std::min (block, total - pos);
                int done = 0;
                while (next < events.size() && events[next].sample < pos + n)
                {
                    const int at = (int) std::max<long> (0, events[next].sample - pos);
                    if (at > done) { bass.render (L + pos + done, R + pos + done, at - done); done = at; }
                    if (events[next].on) bass.noteOn (events[next].note, events[next].vel); else bass.noteOff (events[next].note);
                    ++next;
                }
                bass.render (L + pos + done, R + pos + done, n - done);
            }
        }
        else
        {
            // tonal parts are played by Live Planet's own instruments, exactly like keys on a keyboard
            lp::Engine engine;
            engine.prepare (sr);
            engine.jumpToWeather (calm);
            lp::EngineParams p;
            p.sound = part.sound;
            p.nostalgia = rs.nostalgia;
            p.space = part.kind == PartKind::Pad ? std::min (1.0f, rs.space + 0.2f) : rs.space;
            p.warmth = rs.warmth;
            p.echo = part.kind == PartKind::Melody || part.kind == PartKind::Counter ? rs.echo : rs.echo * 0.3f;
            p.atmos = 0.0f;
            p.position = 0.1f;
            lp::MidiOut midi;
            std::bitset<128> held;
            std::vector<lp::KeyEvent> keys;
            for (int pos = 0; pos < total; pos += block)
            {
                const int n = std::min (block, total - pos);
                keys.clear();
                while (next < events.size() && events[next].sample < pos + n)
                {
                    const auto& e = events[next++];
                    keys.push_back ({ (int) std::max<long> (0, e.sample - pos), e.note, e.vel, e.on });
                }
                lp::TransportInfo t;
                t.shouldPlay = false;
                t.keys = keys.data();
                t.numKeys = (int) keys.size();
                midi.clear();
                engine.process (L + pos, R + pos, n, p, t, held, midi);
                lp::VisualEvent ve;
                while (engine.popVisual (ve)) {}
                if (shouldStop && (pos & 0xffff) == 0 && shouldStop()) return false;
            }
        }

        // level each part to where it sits in a mix (measured only where it actually plays)
        {
            double sum = 0;
            long count = 0;
            for (int i = 0; i < total; i += 4)
            {
                const float v = std::abs (L[i]) + std::abs (R[i]);
                if (v > 2e-4f) { sum += (double) L[i] * L[i] + (double) R[i] * R[i]; ++count; }
            }
            const double rms = count > 0 ? std::sqrt (sum / (2.0 * (double) count)) : 0.0;
            float targetDb = -16.0f;
            switch (part.kind)
            {
                case PartKind::Chords:  targetDb = -17.0f; break;
                case PartKind::Melody:  targetDb = -15.5f; break;
                case PartKind::Counter: targetDb = -23.0f; break;
                case PartKind::Pad:     targetDb = -24.0f; break;
                case PartKind::Bass:    targetDb = -16.0f; break;
                case PartKind::Drums:   targetDb = -17.0f; break;
            }
            if (rms > 1e-6)
            {
                const float g = juce::jlimit (0.05f, 12.0f, juce::Decibels::decibelsToGain (targetDb) / (float) rms);
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto* d = buf.getWritePointer (ch);
                    for (int i = 0; i < total; ++i) d[i] = std::tanh (d[i] * g * 1.05f) / 1.05f;
                }
            }
        }
        for (int ch = 0; ch < 2; ++ch)
            mix.addFrom (ch, 0, buf, ch, 0, total, 1.6f);

        const auto stem = stemDir.getChildFile (juce::String ((int) pi + 1).paddedLeft ('0', 2) + " " + part.name + ".wav");
        if (! writeWav (stem, buf, sr)) return false;
        out.stems.add (stem);
        if (progress) progress ((float) (pi + 1) / (float) (song.parts.size() + 1));
        if (shouldStop && shouldStop()) return false;
    }

    // the full mix: gentle glue, then never above -1 dB
    float peak = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = mix.getWritePointer (ch);
        for (int i = 0; i < total; ++i) { d[i] = std::tanh (d[i] * 0.9f); peak = std::max (peak, std::abs (d[i])); }
    }
    if (peak > 0.0f) mix.applyGain (0.89f / peak);
    out.mixFile = folder.getChildFile (song.title + " - full mix.wav");
    if (! writeWav (out.mixFile, mix, sr)) return false;

    out.midiFile = folder.getChildFile (song.title + ".mid");
    writeMidi (song, out.midiFile);

    // the plan the Ableton helper reads: tempo, sections (for locators) and the files
    auto plan = std::make_unique<juce::DynamicObject>();
    plan->setProperty ("title", song.title);
    plan->setProperty ("bpm", song.bpm);
    plan->setProperty ("key", keyName (song.key, song.minor));
    juce::Array<juce::var> secs;
    for (auto& s : song.sections)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("name", juce::String (s.name));
        o->setProperty ("beat", s.startBar * 4);
        o->setProperty ("bars", s.bars);
        secs.add (juce::var (o.release()));
    }
    plan->setProperty ("sections", secs);
    juce::Array<juce::var> stems;
    for (auto& f : out.stems) stems.add (f.getFullPathName());
    plan->setProperty ("stems", stems);
    plan->setProperty ("midi", out.midiFile.getFullPathName());
    plan->setProperty ("mix", out.mixFile.getFullPathName());
    out.planFile = folder.getChildFile ("plan.json");
    out.planFile.replaceWithText (juce::JSON::toString (juce::var (plan.release())));
    if (progress) progress (1.0f);
    return true;
}

} // namespace lp::song
