#pragma once
#include "AppFolder.h"
#include "StudioPlayer.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "../song/Song.h"
#include "../song/SongFile.h"
#include "../song/Chords.h"
#include "../song/Hook.h"
#include "../song/Arrange.h"
#include "../song/Library.h"
#include "../engine/Limiter.h"

// Runs song mode in the background so the music (and your DAW) never stutters:
// listening to a reference song, then writing and rendering new songs.
class SongJob : private juce::Thread
{
public:
    enum class Stage { Idle, Listening, Writing, Rendering, Done, Failed };

    enum class EditOp { Swap, Reverse, PitchUp, PitchDown, Add, Region, Grains, Key, Like, Dislike, Notes, SwapDrum, Feel, AddHook, AddChops, FromTake, Arrange, Add808 }; // Add: the "track" is the LoopKind to add; Key: track = key + 12 * minor
    struct GrainSettings { float amount = 0, sizeMs = 120, chaos = 0, position = 0; int rate = 2; };

    struct TrackInfo
    {
        juce::String name, source, remix;
        juce::File stem;
        bool canReverse = false, canSwap = false;
        int pitch = 0;
        bool reversed = false;
        int candidates = 0, candidate = 0;
        bool sample = false; // from your library (the knobs' effects apply live)
        int group = 0;       // 1 chords, 2 melody, 3 bass, 4 drums
        float space = 0, echo = 0, warmth = 0, nostalgia = 0; // its own effects to start with
        // for the editor: the sample it plays and which part of it
        juce::String samplePath;
        int loopBars = 0, regionStart = 0, regionBars = 0;
        bool isLoop = false;
        GrainSettings grains;
        juce::String keyText; // e.g. "G# major to C minor, down 5 st"
    };

    struct Status
    {
        Stage stage = Stage::Idle;
        float progress = 0.0f;
        juce::String message;
        bool haveReference = false;
        juce::String referenceName;
        lp::song::Profile profile;
        bool haveSong = false;
        lp::song::Song song;
        lp::song::Rendered files;
        juce::File abletonSet;
        // your sample library
        bool scanning = false;
        int librarySounds = 0, libraryLoops = 0;
        juce::String usedSamples; // what this song took from your library
        std::vector<TrackInfo> tracks;
        int version = 0;          // goes up whenever a track's audio file changes
        std::vector<int> kindCounts; // loops of each LoopKind that fit this song's tempo (for "add instrument")
        bool editing = false;
        juce::Array<juce::File> takes;  // the takes written together (empty = just this one)
        int take = -1;                  // which of them is open
    };

    SongJob() : juce::Thread ("Live Planet song mode", 16 * 1024 * 1024) { startThread (juce::Thread::Priority::low); } // loads your sample library right away
    ~SongJob() override { signalThreadShouldExit(); notify(); stopThread (20000); }

    // open a song written earlier (its folder), e.g. when your Ableton project is reopened
    void openSong (const juce::File& folder)
    {
        {
            const juce::ScopedLock sl (lock);
            if (status.haveSong && currentFolder == folder) return; // already open
            pendingReference = folder;
            task = Task::Open;
        }
        notify();
    }

    void setReference (const juce::File& file)
    {
        {
            const juce::ScopedLock sl (lock);
            pendingReference = file;
            task = Task::Listen;
        }
        startIfNeeded();
    }

    void clearReference()
    {
        const juce::ScopedLock sl (lock);
        status.haveReference = false;
        status.referenceName = {};
        status.profile = {};
    }

    // looks through your sample folders (Splice and any you add) in the background
    void scanLibrary (const juce::Array<juce::File>& folders)
    {
        {
            const juce::ScopedLock sl (lock);
            scanFolders = folders;
            pendingScan = true;
            status.scanning = true;
        }
        startIfNeeded();
    }

    static juce::File libraryCache()
    {
        return lp::appFolder().getChildFile ("library-v4.json"); // v4: melodic one-shots too
    }

    void makeSong (const lp::song::Options& o, const lp::song::RenderSettings& rs, const lp::Weather& sky, bool useLibrary, bool onlySamples = false)
    {
        {
            const juce::ScopedLock sl (lock);
            useSamples = useLibrary;
            samplesOnly = onlySamples;
            options = o;
            settings = rs;
            weather = sky;
            task = Task::Write;
        }
        startIfNeeded();
    }

    // move the whole song to another key: every melodic sample is re-tuned (drums aren't), played parts transposed
    void setSongKey (int key, bool minor)
    {
        {
            const juce::ScopedLock sl (lock);
            if (current.parts.empty()) return;
            edits.push_back ({ key + (minor ? 12 : 0), EditOp::Key });
            status.editing = true;
        }
        startIfNeeded();
    }

    // play only part of a track's sample (in the song's bars; bars 0 = all of it)
    void setRegion (int track, int startBar, int bars)
    {
        {
            const juce::ScopedLock sl (lock);
            pendingRegion[track] = { startBar, bars };
            edits.push_back ({ track, EditOp::Region });
            status.editing = true;
        }
        startIfNeeded();
    }

    // how much of the groove the drums use (0 = straight, 1 = as heard, 1.5 = more): re-records the drums
    void setFeel (int track, float amount)
    {
        {
            const juce::ScopedLock sl (lock);
            pendingFeel = amount;
            edits.erase (std::remove_if (edits.begin(), edits.end(), [] (const std::pair<int, EditOp>& e) { return e.second == EditOp::Feel; }), edits.end());
            edits.push_back ({ track, EditOp::Feel });
            status.editing = true;
        }
        startIfNeeded();
    }
    lp::song::Groove groove() const { const juce::ScopedLock sl (lock); return current.groove; }

    // your own drum pattern (drawn in the editor): replaces the track's hits, re-recorded in the background
    void setNotes (int track, std::vector<lp::song::Note> notes)
    {
        {
            const juce::ScopedLock sl (lock);
            pendingNotes[track] = std::move (notes);
            // only the newest drawing matters: drop an older one still waiting
            edits.erase (std::remove_if (edits.begin(), edits.end(), [track] (const std::pair<int, EditOp>& e)
                                         { return e.first == track && e.second == EditOp::Notes; }), edits.end());
            edits.push_back ({ track, EditOp::Notes });
            status.editing = true;
        }
        startIfNeeded();
    }

    // swap just one drum sound of the kit (the kick, the snare...) for another from your library.
    // dir +1 = next, -1 = back
    void swapDrum (int track, int note, int dir)
    {
        {
            const juce::ScopedLock sl (lock);
            pendingDrumSwap[track] = { note, dir };
            edits.push_back ({ track, EditOp::SwapDrum });
            status.editing = true;
        }
        startIfNeeded();
    }

    // re-play a track as grains (re-recorded in the background)
    void setGrains (int track, GrainSettings g)
    {
        {
            const juce::ScopedLock sl (lock);
            pendingGrains[track] = g;
            edits.push_back ({ track, EditOp::Grains });
            status.editing = true;
        }
        startIfNeeded();
    }

    // everything except the notes themselves: cheap enough to call while drawing
    // studio edits: each one redoes just that track's audio
    void editTrack (int track, EditOp op)
    {
        {
            const juce::ScopedLock sl (lock);
            edits.push_back ({ track, op });
            status.editing = true;
        }
        startIfNeeded();
    }

    // ---- your taste: "love it" (+1) or "not this" (-1) on a track. Remembered on disk, and every
    // later pick leans toward what you like: that sample, its pack, and the overall sound you go for.
    void rateTrack (int track, int rating) { editTrack (track, rating > 0 ? EditOp::Like : EditOp::Dislike); }

    // job thread (it reads what was heard in each sample)
    void applyRating (int track, int rating)
    {
        juce::StringArray paths;
        juce::String key;
        {
            const juce::ScopedLock sl (lock);
            if (track < 0 || track >= (int) current.parts.size()) return;
            const auto& p = current.parts[(size_t) track];
            if (p.samplePath.isNotEmpty()) paths.add (p.samplePath);
            if (p.kind == lp::song::PartKind::Drums) for (auto& [n, f] : current.drumSamples) paths.add (f);
            for (auto& h : p.fxHits) paths.addIfNotAlreadyThere (h.path);
            key = trackKey (p, current);
        }
        {
            const juce::ScopedLock tl (tasteLock);
            const int before = trackRatings.count (key) ? trackRatings[key] : 0;
            const int now = before == rating ? 0 : rating; // pressing it again takes it back
            trackRatings[key] = now;
            for (auto& path : paths)
            {
                if (now == 0) sampleTaste.erase (path); else sampleTaste[path] = now;
                if (const auto* e = library.find (path))
                {
                    auto& pk = packTaste[e->pack];
                    pk = juce::jlimit (-6.0f, 6.0f, pk + (float) (now - before) / (float) std::max (1, paths.size()));
                }
                // the sound you like: an average of the tones of loved samples
                auto it = keyCache.find (path);
                if (it != keyCache.end() && it->second.feel.valid && now != before)
                {
                    const auto& bands = it->second.feel.tone.bandsDb;
                    auto change = [&] (int r, float sign)
                    {
                        auto& sum = r > 0 ? likedBands : dislikedBands;
                        (r > 0 ? likedCount : dislikedCount) += sign;
                        for (int b = 0; b < 10; ++b) sum[(size_t) b] += sign * bands[(size_t) b];
                    };
                    if (before != 0) change (before, -1.0f); // taken back
                    if (now != 0) change (now, 1.0f);
                }
            }
            saveTaste();
        }
        if (rating < 0 && ratingOf (track) < 0) applyEdit (track, EditOp::Swap); // not this: try another right away
    }

    int ratingOf (int track) const
    {
        juce::String key;
        {
            const juce::ScopedLock sl (lock);
            if (track < 0 || track >= (int) current.parts.size()) return 0;
            key = trackKey (current.parts[(size_t) track], current);
        }
        const juce::ScopedLock tl (tasteLock);
        auto it = trackRatings.find (key);
        return it != trackRatings.end() ? it->second : 0;
    }

    // one track in full (its notes), plus the song's sections and drum sounds: for the drum editor
    struct PartView
    {
        bool valid = false;
        lp::song::PartKind kind = lp::song::PartKind::Loop;
        std::vector<lp::song::Note> notes;
        std::vector<lp::song::Section> sections;
        std::map<int, juce::String> drumSamples;
        juce::String title;
        int totalBars = 0;
        juce::String kitKey; // changes when the drum sounds change (swap, pitch)
    };
    PartView partView (int track) const
    {
        const juce::ScopedLock sl (lock);
        PartView v;
        if (! status.haveSong || track < 0 || track >= (int) current.parts.size()) return v;
        const auto& p = current.parts[(size_t) track];
        v.valid = true; v.kind = p.kind; v.notes = p.notes;
        v.sections = current.sections; v.drumSamples = current.drumSamples;
        v.title = current.title; v.totalBars = current.totalBars();
        v.kitKey << p.variant << "|" << p.pitchSemis;
        for (auto& [n, f] : current.drumSamples) v.kitKey << "|" << n << f;
        return v;
    }
    // every drum sound of a track's kit, one hit each, at the level they have in the track's file
    // (any thread; reads the track's file once, so not on the audio thread)
    std::map<int, std::shared_ptr<juce::AudioBuffer<float>>> drumHits (int track) const
    {
        lp::song::Song song;
        juce::File stem;
        {
            const juce::ScopedLock sl (lock);
            if (! status.haveSong || track < 0 || track >= (int) current.parts.size()) return {};
            song = current;
            if (track < (int) status.tracks.size()) stem = status.tracks[(size_t) track].stem;
        }
        const auto& part = song.parts[(size_t) track];
        std::set<int> notes;
        for (auto& n : part.notes) notes.insert (n.pitch);
        for (auto& [n, f] : song.drumSamples) notes.insert (n);
        for (int must : { 36, 38, 39, 42, 46, 45, 37, 70, 49 }) notes.insert (must);
        std::map<int, std::shared_ptr<juce::AudioBuffer<float>>> hits;
        float loudest = 0.0f;
        std::map<int, float> maxVel;
        for (auto& n : part.notes) maxVel[n.pitch] = std::max (maxVel[n.pitch], n.velocity);
        for (int note : notes)
        {
            auto b = lp::song::renderDrumHit (song, part, note, StudioPlayer::fileRate);
            if (b.getNumSamples() < 2) continue;
            const float pk = std::max (b.getMagnitude (0, 0, b.getNumSamples()), b.getMagnitude (1, 0, b.getNumSamples()));
            if (maxVel.count (note)) loudest = std::max (loudest, pk * maxVel[note]);
            hits[note] = std::make_shared<juce::AudioBuffer<float>> (std::move (b));
        }
        // the same loudness as in the track: its loudest moment vs. the loudest hit
        float stemPeak = 0.0f;
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        if (std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (stem)); rd != nullptr)
        {
            juce::Range<float> lr[2];
            rd->readMaxLevels (0, rd->lengthInSamples, lr, 2);
            for (auto& r : lr) stemPeak = std::max ({ stemPeak, std::abs (r.getStart()), std::abs (r.getEnd()) });
        }
        const float g = loudest > 1e-4f && stemPeak > 1e-4f ? juce::jlimit (0.05f, 8.0f, stemPeak / loudest) : 0.6f;
        for (auto& [n, b] : hits) b->applyGain (g);
        return hits;
    }

    // the name of the last drum sound swapped in, and a counter that goes up with each swap
    std::pair<juce::String, int> lastDrumSwap() const { const juce::ScopedLock sl (lock); return { drumSwapName, drumSwapCount }; }
    bool kitIsSamples() const { const juce::ScopedLock sl (lock); return ! current.drumSamples.empty(); }

    // the chords the song's tracks really play (two a bar at most), heard from their files.
    // (a written hook is left out: it follows the chords, it doesn't make them)
    static std::vector<lp::song::chords::Chord> songChords (const lp::song::Song& song, const std::vector<TrackInfo>& tracks)
    {
        const double barSamples = 4.0 * 60.0 / song.bpm * StudioPlayer::fileRate;
        const int bars = song.totalBars(), slots = bars * 2;
        const int len = (int) (bars * barSamples);
        std::vector<float> music ((size_t) len, 0.0f), bassLine ((size_t) len, 0.0f);
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        bool anyBass = false;
        for (size_t i = 0; i < tracks.size() && i < song.parts.size(); ++i)
        {
            const auto& p = song.parts[i];
            const juce::String n (p.name);
            if (p.kind == lp::song::PartKind::Drums || p.kind == lp::song::PartKind::Fx || n.containsIgnoreCase ("drum")
                || n.containsIgnoreCase ("perc") || n.containsIgnoreCase ("transition") || n == "hook" || n == "vocal chops") continue;
            const bool isBass = p.kind == lp::song::PartKind::Bass || n.containsIgnoreCase ("bass");
            std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (tracks[i].stem));
            if (rd == nullptr) continue;
            juce::AudioBuffer<float> b (2, len);
            b.clear();
            rd->read (&b, 0, (int) std::min<juce::int64> (len, rd->lengthInSamples), 0, true, true);
            auto& dst = isBass ? bassLine : music;
            for (int k = 0; k < len; ++k) dst[(size_t) k] += 0.5f * (b.getSample (0, k) + b.getSample (1, k));
            anyBass = anyBass || isBass;
        }
        const auto mc = lp::song::barChroma (music.data(), len, StudioPlayer::fileRate, slots);
        const auto bc = anyBass ? lp::song::barChroma (bassLine.data(), len, StudioPlayer::fileRate, slots) : std::vector<std::array<float, 12>> {};
        return lp::song::chords::read (mc, bc, song.key, song.minor, 2.0);
    }

    // the song's chords and bass as MIDI ("drag chords"): read from what its tracks really play, so it
    // matches the song even after swaps and key changes. Made once per version of the song.
    juce::File chordsMidi (juce::String* summary = nullptr)
    {
        lp::song::Song song;
        std::vector<TrackInfo> tracks;
        juce::File folder;
        int version = 0;
        {
            const juce::ScopedLock sl (lock);
            if (! status.haveSong) return {};
            song = current; tracks = status.tracks; folder = currentFolder; version = status.version;
        }
        const auto file = folder.getChildFile (song.title + " - chords and bass.mid");
        if (chordsVersion == version && file.existsAsFile()) { if (summary) *summary = chordsSummary; return file; }
        const auto chords = songChords (song, tracks);
        lp::song::chords::writeMidi (chords, song.bpm, song.title, file);
        juce::StringArray names;
        for (auto& c : chords) if (c.valid && (names.isEmpty() || names[names.size() - 1] != lp::song::chords::name (c))) names.add (lp::song::chords::name (c));
        chordsSummary = names.joinIntoString ("  ");
        chordsVersion = version;
        if (summary) *summary = chordsSummary;
        return file;
    }

    // melodic one-shots in your library (keys, bass, 808s, synth hits, vocal shots), by pack: for sample instruments
    std::map<juce::String, juce::StringArray> oneShots() const
    {
        const juce::ScopedLock sl (lock);
        std::map<juce::String, juce::StringArray> out;
        using SK = lp::song::SampleKind;
        for (auto& e : library.all())
        {
            if (! (e.kind == SK::OneShot || e.kind == SK::Bass808)) continue;
            if (! packOn ((int) lp::song::Library::sourceOf (e.path), e.pack)) continue; // only packs you've switched on
            if (e.bpm > 0.0 || e.loop != lp::song::LoopKind::None) continue; // loops are for songs
            const auto lower = e.path.toLowerCase();
            if (lower.contains ("loop") || lower.contains ("fill") || lower.contains ("drum")) continue;
            out[e.pack].add (e.path);
        }
        for (auto& [k, v] : out) v.sort (true);
        return out;
    }

    lp::song::PartKind kindOf (int track) const
    {
        const juce::ScopedLock sl (lock);
        return status.haveSong && track >= 0 && track < (int) current.parts.size() ? current.parts[(size_t) track].kind : lp::song::PartKind::Loop;
    }

    Status getStatus() const
    {
        const juce::ScopedLock sl (lock);
        Status s;
        s.scanning = status.scanning;
        s.librarySounds = status.librarySounds;
        s.libraryLoops = status.libraryLoops;
        s.kindCounts = status.kindCounts;
        s.usedSamples = status.usedSamples;
        s.tracks = status.tracks;
        s.version = status.version;
        s.editing = status.editing;
        s.takes = status.takes;
        s.take = status.take;
        s.stage = status.stage;
        s.progress = status.progress;
        s.message = status.message;
        s.haveReference = status.haveReference;
        s.referenceName = status.referenceName;
        s.profile.valid = status.profile.valid;
        s.profile.bpm = status.profile.bpm;
        s.profile.key = status.profile.key;
        s.profile.minor = status.profile.minor;
        s.profile.lengthSeconds = status.profile.lengthSeconds;
        s.profile.density = status.profile.density;
        s.profile.sustain = status.profile.sustain;
        s.profile.lowSustain = status.profile.lowSustain;
        s.profile.tone = status.profile.tone;
        s.profile.brightness = status.profile.brightness;
        s.profile.hasStems = status.profile.hasStems;
        s.profile.vocalsHeard = status.profile.vocalsHeard;
        s.profile.stemDb = status.profile.stemDb;
        for (auto& sec : status.profile.sections)
            s.profile.sections.push_back ({ sec.label, sec.startBar, sec.bars, sec.energy, {}, {} });
        s.haveSong = status.haveSong;
        s.song.title = status.song.title;
        s.song.bpm = status.song.bpm;
        s.song.playbackMakeupDb = status.song.playbackMakeupDb;
        s.song.key = status.song.key;
        s.song.minor = status.song.minor;
        s.song.sections = status.song.sections;
        s.files = status.files;
        s.abletonSet = status.abletonSet;
        return s;
    }
    bool isBusy() const { const juce::ScopedLock sl (lock); return status.stage == Stage::Listening || status.stage == Stage::Writing || status.stage == Stage::Rendering; }

    // where songs are saved: ~/Music/Live Planet/Songs
    static juce::File songsFolder()
    {
        return lp::appFolder().getChildFile ("Songs");
    }

    // the Ableton helper device reads this to find the newest song
    static juce::File latestPointer()
    {
        return lp::appFolder().getChildFile ("latest-song.json");
    }

    std::function<juce::File (const lp::song::Song&, const lp::song::Rendered&)> writeAbletonSet; // set by the processor

private:
    enum class Task { None, Listen, Write, Open };

    void startIfNeeded()
    {
        if (! isThreadRunning()) startThread (juce::Thread::Priority::low);
        notify();
    }

    static void mark (const char* what)
    {
        if (! std::getenv ("LP_TIMING")) return;
        static double last = juce::Time::getMillisecondCounterHiRes();
        const double now = juce::Time::getMillisecondCounterHiRes();
        std::fprintf (stderr, "[timing] %-28s %.1f s\n", what, (now - last) / 1000.0);
        last = now;
    }

    void setStage (Stage s, float p, const juce::String& msg)
    {
        if (std::getenv ("LP_TIMING")) // for tuning speed: how long each step took
        {
            static double last = juce::Time::getMillisecondCounterHiRes();
            const double now = juce::Time::getMillisecondCounterHiRes();
            std::fprintf (stderr, "[timing] +%.1f s  -> %s\n", (now - last) / 1000.0, msg.toRawUTF8());
            last = now;
        }
        const juce::ScopedLock sl (lock);
        status.stage = s;
        status.progress = p;
        status.message = msg;
    }

    void run() override
    {
        juce::ScopedNoDenormals noDenormals; // fading filter tails are very slow on CPUs otherwise
        loadTaste();
        // start with the library found last time, so songs can use it straight away
        loadFeatures();
        if (library.load (libraryCache()))
        {
            const juce::ScopedLock sl (lock);
            status.librarySounds = library.size();
            status.libraryLoops = library.loopCount();
        }
        while (! threadShouldExit())
        {
            bool scanNow = false;
            juce::Array<juce::File> folders;
            {
                const juce::ScopedLock sl (lock);
                scanNow = pendingScan;
                pendingScan = false;
                folders = scanFolders;
            }
            std::vector<std::pair<int, EditOp>> todo;
            {
                const juce::ScopedLock sl (lock);
                std::swap (todo, edits);
            }
            if (! todo.empty())
            {
                for (auto& [tr, op] : todo) applyEdit (tr, op);
                const juce::ScopedLock sl (lock);
                status.editing = ! edits.empty();
                continue;
            }
            if (scanNow)
            {
                lp::song::Library fresh;
                fresh.scan (folders);
                fresh.save (libraryCache());
                const juce::ScopedLock sl (lock);
                library = fresh;
                status.scanning = false;
                status.librarySounds = library.size();
                status.libraryLoops = library.loopCount();
                continue;
            }
            Task t;
            juce::File ref;
            lp::song::Options o;
            lp::song::RenderSettings rs;
            lp::Weather sky;
            {
                const juce::ScopedLock sl (lock);
                t = task;
                task = Task::None;
                ref = pendingReference;
                o = options; rs = settings; sky = weather;
            }
            try
            {
                if (t == Task::Listen) listen (ref);
                else if (t == Task::Write) writeTakes (o, rs, sky);
                else if (t == Task::Open) openSaved (ref);
                else wait (-1);
            }
            catch (const std::bad_alloc&)
            {
                setStage (Stage::Failed, 0.0f, "not enough memory for a song this long. try sketch, or a shorter reference.");
            }
            catch (const std::exception& e)
            {
                setStage (Stage::Failed, 0.0f, juce::String ("something went wrong: ") + e.what());
            }
            catch (...)
            {
                setStage (Stage::Failed, 0.0f, "something went wrong writing the song.");
            }
        }
    }

    void listen (const juce::File& file)
    {
        setStage (Stage::Listening, 0.1f, "listening to " + file.getFileNameWithoutExtension() + "...");
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        if (reader == nullptr)
        {
            setStage (Stage::Failed, 0.0f, "couldn't open that file. try a wav, aiff, flac or mp3.");
            return;
        }
        const auto maxLen = (juce::int64) (reader->sampleRate * 60.0 * 8.0); // up to 8 minutes
        const int len = (int) std::min (reader->lengthInSamples, maxLen);
        juce::AudioBuffer<float> buf ((int) reader->numChannels, len);
        reader->read (&buf, 0, len, 0, true, true);
        std::vector<float> mono ((size_t) len), side;
        for (int i = 0; i < len; ++i)
        {
            float v = 0;
            for (int ch = 0; ch < buf.getNumChannels(); ++ch) v += buf.getSample (ch, i);
            mono[(size_t) i] = v / (float) buf.getNumChannels();
        }
        if (buf.getNumChannels() >= 2) // the stereo picture tells the vocals (in the middle) from the rest
        {
            side.resize ((size_t) len);
            for (int i = 0; i < len; ++i) side[(size_t) i] = 0.5f * (buf.getSample (0, i) - buf.getSample (1, i));
        }
        setStage (Stage::Listening, 0.5f, "finding tempo, key and sections, splitting it into parts...");
        // producers often put the tempo and key in the name ("todavia-140-cm"): trust that over listening,
        // and measure the groove and sections on that tempo's grid
        const auto hint = lp::song::Library::parse (file, file.getParentDirectory());
        const double knownBpm = hint.bpm >= 60.0 && hint.bpm <= 200.0 ? hint.bpm : 0.0;
        auto profile = lp::song::analyse (mono.data(), len, reader->sampleRate, knownBpm, side.empty() ? nullptr : side.data());
        profile.sourceName = file.getFileNameWithoutExtension();
        if (profile.valid && hint.key >= 0) { profile.key = hint.key; profile.minor = hint.minor; }
        const juce::ScopedLock sl (lock);
        if (! profile.valid)
        {
            status.stage = Stage::Failed;
            status.message = "that song is too short or too quiet to learn from (it needs at least 8 seconds).";
            return;
        }
        status.profile = profile;
        status.haveReference = true;
        status.referenceName = profile.sourceName;
        status.stage = Stage::Idle;
        status.progress = 0.0f;
        status.message = {};
    }

    // one song, or (takes = 3) three versions of it to pick from: the same sections, tempo and key, so
    // any track of one fits into another; only the sounds differ
    void writeTakes (const lp::song::Options& o, const lp::song::RenderSettings& rs, const lp::Weather& sky)
    {
        { const juce::ScopedLock sl (lock); status.takes.clear(); }
        write (o, rs, sky);
        if (o.takes <= 1) return;
        juce::File first;
        lp::song::Song song1;
        {
            const juce::ScopedLock sl (lock);
            if (! status.haveSong || status.stage != Stage::Done) return;
            first = currentFolder; song1 = current;
        }
        juce::Array<juce::File> takes { first };
        juce::StringArray used; // what the takes so far used: the next take reaches for other sounds
        auto noteUsed = [&]
        {
            const juce::ScopedLock sl (lock);
            for (auto& p : current.parts) if (p.samplePath.isNotEmpty()) used.addIfNotAlreadyThere (p.samplePath);
            for (auto& [n, f] : current.drumSamples) if (const auto* e = library.find (f)) used.addIfNotAlreadyThere ("pack:" + e->pack);
        };
        noteUsed();
        for (int k = 2; k <= o.takes && ! threadShouldExit(); ++k)
        {
            auto o2 = o;
            o2.avoid = used;
            o2.pickSeed = o.seed + 7919u * (juce::uint32) k;
            o2.titleSuffix = " (take " + juce::String (k) + ")";
            o2.dawBpm = song1.bpm;                       // the same tempo...
            o2.promptKey = song1.key; o2.promptMinor = song1.minor; o2.shiftKey = false; // ...and key
            o2.style = (int) song1.style;
            setStage (Stage::Writing, 0.02f, "writing take " + juce::String (k) + " of " + juce::String (o.takes) + "...");
            write (o2, rs, sky);
            noteUsed();
            const juce::ScopedLock sl (lock);
            if (status.haveSong && currentFolder != first && ! takes.contains (currentFolder)) takes.add (currentFolder);
        }
        // every take knows the others (saved in each, so they come back with the project)
        for (auto& f : takes)
        {
            auto v = juce::JSON::parse (f.getChildFile ("song.json"));
            if (auto* obj = v.getDynamicObject())
            {
                juce::StringArray paths;
                for (auto& t : takes) paths.add (t.getFullPathName());
                obj->setProperty ("takes", lp::song::file::strings (paths));
                f.getChildFile ("song.json").replaceWithText (juce::JSON::toString (v));
            }
        }
        openSaved (first);
        const juce::ScopedLock sl (lock);
        status.message = juce::String (takes.size()) + " takes written: click Take 1, 2, 3 to compare, and mix takes to combine them";
    }

public:
    // a new structure for the song (sections moved, resized, added, removed, duplicated): every track
    // follows it, and the song is recorded again (your mixer, master and takes stay)
    void rearrange (std::vector<lp::song::arrange::Item> plan)
    {
        {
            const juce::ScopedLock sl (lock);
            pendingPlan = std::move (plan);
            edits.push_back ({ 0, EditOp::Arrange });
            status.editing = true;
        }
        startIfNeeded();
    }

    // a catchy vocal-chop hook for the song, sung with your own vocal's syllables (a new track)
    void addChopHookFrom (const juce::File& vocal)
    {
        {
            const juce::ScopedLock sl (lock);
            pendingOwnVocal = vocal;
            edits.push_back ({ 0, EditOp::AddChops });
            status.editing = true;
        }
        startIfNeeded();
    }
    bool hasSong() const { const juce::ScopedLock sl (lock); return status.haveSong; }

    // switch to take i (A/B while it plays: the studio reloads the tracks, the position stays)
    void openTake (int i)
    {
        juce::File f;
        {
            const juce::ScopedLock sl (lock);
            if (i < 0 || i >= status.takes.size()) return;
            f = status.takes[i];
        }
        openSong (f);
    }

    // the tracks of another take (for "mix takes")
    std::vector<std::pair<juce::String, juce::String>> takeTracks (int i) const
    {
        juce::File f;
        {
            const juce::ScopedLock sl (lock);
            if (i < 0 || i >= status.takes.size()) return {};
            f = status.takes[i];
        }
        std::vector<std::pair<juce::String, juce::String>> out;
        const auto v = juce::JSON::parse (f.getChildFile ("song.json"));
        if (auto* parts = v["song"]["parts"].getArray())
            for (auto& p : *parts) out.push_back ({ p["name"].toString(), niceSource (p["source"].toString()) });
        return out;
    }

    // put track `src` of take `take` in place of track `dst` here (dst = -1: add it)
    void useFromTake (int dst, int take, int src)
    {
        {
            const juce::ScopedLock sl (lock);
            pendingFromTake = { take, src };
            edits.push_back ({ dst, EditOp::FromTake });
            status.editing = true;
        }
        startIfNeeded();
    }
private:

    void write (const lp::song::Options& o, const lp::song::RenderSettings& rs, const lp::Weather& sky)
    {
        setStage (Stage::Writing, 0.02f, "writing a new song...");
        lp::song::Profile ref;
        {
            const juce::ScopedLock sl (lock);
            if (status.haveReference) ref = status.profile;
        }
        bool useLib, onlySamples;
        {
            const juce::ScopedLock sl (lock);
            useLib = useSamples && library.size() > 0;
            onlySamples = samplesOnly && library.size() > 0;
        }
        auto opts = o;
        juce::String noteForYou;
        lp::song::LibraryPicks picks;
        juce::String used;
        const auto target = lp::song::chooseTarget (ref, sky, o);
        const juce::uint32 pickSeed = o.pickSeed != 0 ? o.pickSeed : o.seed; // takes: same song, other sounds
        if (useLib || onlySamples)
        {
            setStage (Stage::Writing, 0.03f, "looking through your samples...");
            refreshUsable();
            picks = lp::song::pickFromLibrary (usableLib, target, pickSeed, [] (const juce::String& path, double bpm) { return loopBars (path, bpm); },
                                               {}, [this, &target] (const juce::String& path) { return tuneFor (path, target.key, target.minor); },
                                               [this, &ref, &target] (const juce::String& path) { return soundsLike (path, ref, target.wish); });
            // a take: other sounds than the takes before it (they come first among the good matches)
            if (! o.avoid.isEmpty())
            {
                for (auto& c : picks.loopSet)
                    std::stable_partition (c.options.begin(), c.options.end(), [&] (const lp::song::LoopOption& op) { return ! o.avoid.contains (op.entry->path); });
                for (auto& pk : picks.drumPacks)
                    if (! o.avoid.contains ("pack:" + pk) && pk != picks.drumPack)
                    {
                        const auto other = lp::song::pickFromLibrary (usableLib, target, pickSeed, [] (const juce::String& path, double bpm) { return loopBars (path, bpm); },
                                                                     pk, {}, {});
                        if (! other.drums.empty()) { picks.drums = other.drums; picks.drumPack = other.drumPack; }
                        break;
                    }
            }
            if (picks.loop != nullptr)
            {
                const bool tempoSet = o.dawBpm > 0.0 || o.promptBpm > 0.0 || o.genreBpm > 0.0;
                opts.forceBpm = tempoSet ? target.bpm : (picks.loopBpm > 0 ? picks.loopBpm : picks.loop->bpm);
                opts.forceKey = target.key;          // same key or its relative: the notes match either way
                opts.forceMinor = target.minor;
            }
        }
        if (onlySamples && picks.loopSet.empty())
        {
            setStage (Stage::Failed, 0.0f, "no loops in your library match this key and tempo. try \"write another\", or turn off samples only.");
            return;
        }
        auto song = lp::song::generate (ref, sky, opts);
        song.title += o.titleSuffix;
        // the drums' feel: the reference's own swing and timing, or one that suits the style
        song.groove = ref.valid && ref.groove.valid ? ref.groove : lp::song::styleGroove (song.style);
        song.groove.valid = true;
        song.drumSamples = picks.drums;
        if (ref.valid && ref.hasStems)
        {
            const auto adj = balanceFromRef (ref);
            for (size_t si = 0; si < song.sections.size(); ++si)
            {
                const auto& sec = song.sections[si];
                const auto present = refPresence (ref, sec.name);
                const bool nextIsChorus = si + 1 < song.sections.size() && baseName (song.sections[si + 1].name) == "chorus";
                const double b0 = sec.startBar * 4.0, b1 = (sec.startBar + sec.bars) * 4.0, fillFrom = nextIsChorus ? b1 - 4.0 : b1;
                for (auto& part : song.parts)
                {
                    const bool drums = part.kind == lp::song::PartKind::Drums, bass = part.kind == lp::song::PartKind::Bass;
                    if ((drums && ! present[lp::song::StemDrums]) || (bass && ! present[lp::song::StemBass]))
                        part.notes.erase (std::remove_if (part.notes.begin(), part.notes.end(), [&] (const lp::song::Note& nt)
                            { return nt.beat >= b0 && nt.beat < (drums ? fillFrom : b1); }), part.notes.end()); // (the fill into a chorus stays)
                }
            }
            for (auto& part : song.parts)
            {
                using PK = lp::song::PartKind;
                if (part.kind == PK::Bass) part.mixDb += adj[lp::song::StemBass];
                else if (part.kind == PK::Melody) part.mixDb += adj[lp::song::StemVocals];
                else if (part.kind == PK::Chords || part.kind == PK::Pad || part.kind == PK::Counter) part.mixDb += adj[lp::song::StemMusic];
            }
        }
        song.refTone = ref.valid ? ref.tone : lp::song::Tone {};

        // where each sound came from, and how it was changed, for the studio
        for (auto& part : song.parts)
        {
            using PK = lp::song::PartKind;
            if (part.kind == PK::Drums)
            {
                part.source = song.drumSamples.empty() ? juce::String ("loopwright: ") + lp::song::styleName (song.style) + " kit"
                                                       : kitLabel (picks) + picks.drumPack + " kit";
                part.remix = song.drumSamples.empty() ? "a new groove, played on coded drums"
                                                      : juce::String ("one-shots re-sequenced into a new ") + lp::song::styleName (song.style) + " groove";
                part.candidates = picks.drumPacks;
            }
            else if (part.kind == PK::Bass)
            {
                part.source = juce::String ("loopwright: ") + (part.variant == 1 ? "808" : "bass");
                part.remix = "new bass line on the song's chords";
            }
            else
            {
                part.source = juce::String ("loopwright: ") + lp::profileFor (part.sound).name;
                part.remix = "new notes written for this song";
            }
        }

        // loops from your library, placed by section
        auto addLoop = [&] (const lp::song::LoopChoice& choice, const juce::String& name, std::initializer_list<const char*> sections)
        {
            lp::song::Part loop;
            loop.name = name.toStdString();
            loop.kind = lp::song::PartKind::Loop;
            for (auto& opt : choice.options) { loop.candidates.add (opt.entry->path); loop.candidateTunes.add (opt.tune); }
            loop.samplePath = loop.candidates[0];
            loop.autoTune = loop.candidateTunes[0];
            loop.loopBars = songBars (loop.samplePath, choice.options[0].entry->bpm, song.bpm);
            if (loop.loopBars <= 0) return;
            for (auto& sec : song.sections)
                for (auto want : sections)
                    if (sec.name.rfind (want, 0) == 0)
                        for (int b = sec.startBar; b + loop.loopBars <= sec.startBar + sec.bars; b += loop.loopBars)
                            loop.loopStartBars.push_back (b);
            if (loop.loopStartBars.empty()) return;
            const double fileBpm = choice.options[0].entry->bpm;
            double m = 1.0;
            for (double mm : { 0.5, 1.0, 2.0 }) if (std::abs (fileBpm * mm - song.bpm) < std::abs (fileBpm * m - song.bpm)) m = mm;
            const double fit = (song.bpm / (fileBpm * m) - 1.0) * 100.0;
            loop.source = sourceLabel (loop.samplePath) + juce::File (loop.samplePath).getFileNameWithoutExtension();
            loop.remix = remixText (loop, fit, song);
            song.parts.push_back (loop);
        };

        if (onlySamples)
        {
            // samples only: no coded instruments, the song is built from your loops and one-shots
            song.parts.erase (std::remove_if (song.parts.begin(), song.parts.end(), [&] (const lp::song::Part& p2)
                { return p2.kind != lp::song::PartKind::Drums || song.drumSamples.empty(); }), song.parts.end());
            // a full band from your library: drums, percussion, bass, up to three melodic instruments
            // (a different mix each time), a pad, a vocal and an fx
            using LK = lp::song::LoopKind;
            lp::song::LoopChoice *drumC = nullptr, *percC = nullptr, *bassC = nullptr, *vocalC = nullptr, *padC = nullptr, *fxC = nullptr;
            std::vector<lp::song::LoopChoice*> melodic;
            for (auto& c : picks.loopSet)
                switch (c.kind)
                {
                    case LK::Drums: drumC = &c; break;
                    case LK::Perc:  percC = &c; break;
                    case LK::Bass:  bassC = &c; break;
                    case LK::Vocal: vocalC = &c; break;
                    case LK::Pad:   padC = &c; break;
                    case LK::FX:    fxC = &c; break;
                    default:        melodic.push_back (&c); break;
                }
            // the main loop's instrument leads; the others are shuffled by the song's seed
            juce::Random mix ((juce::int64) pickSeed * 13 + 5);
            for (size_t i = melodic.size(); i > 2; --i) std::swap (melodic[i - 1], melodic[1 + (size_t) mix.nextInt ((int) i - 1)]);
            // instruments you asked for go first (and all of them play)
            size_t wanted = 0;
            for (int want : o.wantKinds)
                for (size_t i = wanted; i < melodic.size(); ++i)
                    if ((int) melodic[i]->kind == want) { std::rotate (melodic.begin() + (long) wanted, melodic.begin() + (long) i, melodic.begin() + (long) i + 1); ++wanted; break; }
            juce::StringArray missing;
            for (int want : o.wantKinds)
            {
                bool found = false;
                for (auto& c : picks.loopSet) found = found || (int) c.kind == want;
                if (! found) missing.add (lp::song::loopKindName ((lp::song::LoopKind) want));
            }
            if (! missing.isEmpty()) noteForYou = "no " + missing.joinIntoString (" / ") + " loops in your library fit this song";
            const bool kit = ! song.drumSamples.empty();
            // chords that agree: everything that plays with the main loop is picked (from its options) for
            // chords that match it bar by bar; if none match well, that sound only plays where the main loop doesn't
            std::map<const lp::song::LoopChoice*, bool> clashes;
            if (! melodic.empty() && ! melodic[0]->options.empty())
            {
                const auto& lead = melodic[0]->options[0];
                const int leadBars = songBars (lead.entry->path, lead.entry->bpm, song.bpm);
                const auto& leadCh = chordsOf (lead.entry->path);
                for (auto* c : { bassC, padC, vocalC })
                    if (c != nullptr) melodic.push_back (c);
                for (size_t i = 1; i < melodic.size(); ++i)
                {
                    auto* c = melodic[i];
                    float best = -1.0f;
                    std::vector<std::pair<float, lp::song::LoopOption>> scored;
                    for (auto& opt : c->options)
                    {
                        const int bars = songBars (opt.entry->path, opt.entry->bpm, song.bpm);
                        const float agree = lp::song::chordAgreement (leadCh, lead.tune, leadBars, chordsOf (opt.entry->path), opt.tune, bars);
                        best = std::max (best, agree);
                        scored.push_back ({ agree * 2.0f + opt.like - (float) std::abs (opt.tune) * 0.05f - (o.avoid.contains (opt.entry->path) ? 1.5f : 0.0f), opt });
                    }
                    std::stable_sort (scored.begin(), scored.end(), [] (auto& a, auto& b) { return a.first > b.first; });
                    c->options.clear();
                    for (auto& [sc, opt] : scored) c->options.push_back (opt);
                    clashes[c] = best >= 0.0f && best < 0.75f;
                }
                melodic.resize (melodic.size() - (size_t) ((bassC != nullptr) + (padC != nullptr) + (vocalC != nullptr)));
            }
            // a produced arrangement: each loop has a role, and plays (and is treated) section by section
            auto addArranged = [&] (const lp::song::LoopChoice& choice, const juce::String& name, Role role)
            {
                lp::song::Part loop;
                loop.name = name.toStdString();
                loop.kind = lp::song::PartKind::Loop;
                for (auto& opt : choice.options) { loop.candidates.add (opt.entry->path); loop.candidateTunes.add (opt.tune); }
                loop.samplePath = loop.candidates[0];
                loop.autoTune = loop.candidateTunes[0];
                loop.loopBars = songBars (loop.samplePath, choice.options[0].entry->bpm, song.bpm);
                if (loop.loopBars <= 0) return;
                const bool clash = clashes.count (&choice) && clashes[&choice] && role != Role::Bass;
                arrange (loop, song, clash ? Role::Fourth : role, kit, ref, clash);
                // mixed by role: the lead and the low end up front and centred, the rest a little back and to the sides
                switch (role)
                {
                    case Role::Main:     loop.mixDb = +5.5f; loop.pan = 0.0f;   break;
                    case Role::Second:   loop.mixDb = -2.0f; loop.pan = -0.3f;  break;
                    case Role::Third:    loop.mixDb = -5.0f; loop.pan = 0.35f;  break;
                    case Role::Fourth:   loop.mixDb = -6.0f; loop.pan = -0.45f; break;
                    case Role::Bass:     loop.mixDb = +1.0f; loop.pan = 0.0f;   break;
                    case Role::DrumLoop: loop.mixDb = kit ? -4.0f : +3.0f; loop.pan = 0.0f; break;
                    case Role::Perc:     loop.mixDb = -6.0f; loop.pan = 0.25f;  break;
                    case Role::Pad:      loop.mixDb = -7.0f; loop.pan = 0.0f;   break;
                    case Role::Vocal:    loop.mixDb = +7.0f; loop.pan = 0.0f;   break;
                    case Role::Fx:       loop.mixDb = -3.0f; loop.pan = 0.0f;   break;
                }
                const auto adj = balanceFromRef (ref);
                const int stem = stemFor (role);
                if (stem >= 0) loop.mixDb += adj[(size_t) stem];
                if (std::none_of (loop.barSrc.begin(), loop.barSrc.end(), [] (int v) { return v >= 0; }) && loop.swellBars.empty()) return;
                loop.source = sourceLabel (loop.samplePath) + juce::File (loop.samplePath).getFileNameWithoutExtension();
                loop.remix = remixText (loop, 0.0, song) + arrangementText (loop)
                           + (clash ? ". plays on its own (its chords don't match the main loop's)" : juce::String());
                song.parts.push_back (loop);
            };
            if (drumC != nullptr)  addArranged (*drumC, "drum loop", Role::DrumLoop);
            if (percC != nullptr)  addArranged (*percC, "percussion", Role::Perc);
            if (bassC != nullptr)  addArranged (*bassC, "bass loop", Role::Bass);
            const Role melodicRoles[4] = { Role::Main, Role::Second, Role::Third, Role::Fourth };
            for (size_t i = 0; i < melodic.size() && i < (wanted > 3 ? 4u : 3u); ++i)
                addArranged (*melodic[i], lp::song::loopKindName (melodic[i]->kind), melodicRoles[i]);
            if (padC != nullptr)   addArranged (*padC, "pad", Role::Pad);
            if (vocalC != nullptr) addArranged (*vocalC, "vocal", Role::Vocal);
            if (fxC != nullptr && usableLib.fxOfKind (lp::song::SampleKind::Riser).empty()) addArranged (*fxC, "fx", Role::Fx);
        }
        else if (picks.loop != nullptr && ! picks.loopSet.empty())
            addLoop (picks.loopSet.front(), "loop", { "chorus", "bridge" });

        // transition FX from your library at every change of section
        if (useLib || onlySamples)
        {
            juce::StringArray packs;
            packs.add (picks.drumPack);
            for (auto& part : song.parts)
                if (const auto* e = library.find (part.samplePath)) packs.addIfNotAlreadyThere (e->pack);
            auto tr = makeTransitions (song, pickSeed, packs, target.genreWords);
            if (! tr.fxHits.empty())
            {
                // a crash sample takes over from the kit's stand-in crash on the chorus "one"
                bool hasCrash = false;
                for (auto& h : tr.fxHits) hasCrash = hasCrash || library.find (h.path) != nullptr && library.find (h.path)->kind == lp::song::SampleKind::Crash;
                if (hasCrash)
                    for (auto& part : song.parts)
                        if (part.kind == lp::song::PartKind::Drums)
                            for (auto& sec : song.sections)
                                if (baseName (sec.name) == "chorus")
                                    part.notes.erase (std::remove_if (part.notes.begin(), part.notes.end(), [&] (const lp::song::Note& n)
                                        { return n.pitch == 46 && std::abs (n.beat - sec.startBar * 4.0) < 0.01; }), part.notes.end());
                song.parts.push_back (tr);
            }
        }

        for (auto& part : song.parts)
            if (part.kind == lp::song::PartKind::Loop || part.kind == lp::song::PartKind::Fx || (part.kind == lp::song::PartKind::Drums && ! song.drumSamples.empty()))
                used << (used.isEmpty() ? "" : "  -  ") << juce::String (part.name) << ": " << part.source.fromFirstOccurrenceOf (": ", false, false);

        auto folder = songsFolder().getChildFile (juce::File::createLegalFileName (song.title));
        for (int n = 2; folder.exists(); ++n)
            folder = songsFolder().getChildFile (juce::File::createLegalFileName (song.title + " " + juce::String (n)));

        mark ("ready to record");
        setStage (Stage::Rendering, 0.05f, "recording \"" + song.title + "\"...");
        saveFeatures(); // what was heard while choosing, remembered for next time
        lp::song::Rendered files;
        const bool ok = lp::song::render (song, rs, folder, files,
                                          [this] (float p) { const juce::ScopedLock sl (lock); status.progress = 0.05f + 0.9f * p; },
                                          [this] { return threadShouldExit(); });
        if (! ok)
        {
            setStage (Stage::Failed, 0.0f, "couldn't save the song. is your Music folder writable?");
            return;
        }
        mark ("recorded");
        juce::File als;
        if (writeAbletonSet) als = writeAbletonSet (song, files);
        latestPointer().getParentDirectory().createDirectory();
        latestPointer().replaceWithText (files.planFile.loadFileAsString());

        song.matchEqDb = files.matchEqDb;  // edited tracks later get the same tone and level
        song.matchGainDb = files.matchGainDb;
        song.playbackMakeupDb = files.playbackMakeupDb;
        song.mixShaped = true;
        const juce::ScopedLock sl (lock);
        current = song;
        currentRs = rs;
        currentFolder = folder;
        currentPicksDrumPacks = picks.drumPacks;
        currentTarget = target;
        currentRef = ref;
        currentSeed = o.seed;
        status.tracks.clear();
        for (int i = 0; i < (int) song.parts.size(); ++i)
            status.tracks.push_back (info (song.parts[(size_t) i], files.stems[i], song));
        ++status.version;
        status.song = song;
        status.usedSamples = used;
        finishedMix = files.mixAudio;
        finishedBpm = song.bpm;
        files.mixAudio.reset();
        status.files = files;  // (without the audio itself)
        status.take = status.takes.indexOf (folder);
        status.abletonSet = als;
        status.kindCounts.assign (lp::song::kNumLoopKinds, 0);
        for (int k = 1; k < lp::song::kNumLoopKinds; ++k)
            status.kindCounts[(size_t) k] = (int) usableLib.loopsOfKind ((lp::song::LoopKind) k, song.bpm).size();
        status.haveSong = true;
        status.stage = Stage::Done;
        status.progress = 1.0f;
        status.message = "\"" + song.title + "\" is ready. locators: drag the locators button onto any track in ableton";
        if (o.dawBpm > 0.0 && std::abs (song.bpm - o.dawBpm) > 0.5)
            status.message = "\"" + song.title + "\" is at " + juce::String (song.bpm, 0) + " bpm: set your project to " + juce::String (song.bpm, 0)
                           + " (the locators helper does it for you)";
        if (noteForYou.isNotEmpty()) status.message << ". " << noteForYou;
        saveSession();
    }

    // semitones to put a sample in the song's key (99 = it has no clear key). Listens once per file.
    struct Heard { lp::song::KeyReading reading; bool clear = false; int key = -1; bool minor = false; lp::song::Tone tone; lp::song::Feel feel;
                   std::vector<std::array<float, 12>> chords; bool chordsRead = false; };

    // the notes in each bar of a loop (read once, remembered)
    const std::vector<std::array<float, 12>>& chordsOf (const juce::String& path)
    {
        tuneFor (path, 0, false); // make sure it has been listened to (key and sound) first
        auto& h = keyCache[path];
        if (h.chordsRead) return h.chords;
        h.chordsRead = true;
        featuresDirty = true;
        const auto* e = library.find (path);
        if (e == nullptr || e->bpm <= 0) return h.chords;
        const int bars = loopBars (path, e->bpm);
        if (bars <= 0) return h.chords;
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File (path)));
        if (rd == nullptr) return h.chords;
        const int len = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 90.0));
        juce::AudioBuffer<float> b ((int) rd->numChannels, len);
        rd->read (&b, 0, len, 0, true, true);
        std::vector<float> mono ((size_t) len);
        for (int i = 0; i < len; ++i) { float v = 0; for (int ch = 0; ch < b.getNumChannels(); ++ch) v += b.getSample (ch, i); mono[(size_t) i] = v; }
        h.chords = lp::song::barChroma (mono.data(), len, rd->sampleRate, bars);
        return h.chords;
    }

    // how much a sample sounds like your reference (0..1)
    // how well a sample fits, 0..1: its tone against the matching part of the reference (a bass loop against
    // the reference's bass...), held vs. plucky and busy against the reference, and the words you typed
    float soundsLike (const juce::String& path, const lp::song::Profile& ref, const lp::song::SoundWish& wish)
    {
        tuneFor (path, 0, false); // listens once (key and sound), remembered on disk
        auto it = keyCache.find (path);
        if (it == keyCache.end() || ! it->second.feel.valid)
        {
            double sum = 0.5, weight = 1;
            addTaste (path, nullptr, [&] (double v, double w) { sum += juce::jlimit (0.0, 1.0, v) * w; weight += w; });
            return (float) (sum / weight);
        }
        const auto& f = it->second.feel;
        const auto* e = library.find (path);
        using LK = lp::song::LoopKind;
        const LK kind = e != nullptr ? e->loop : LK::Other;
        double sum = 0, weight = 0;
        auto add = [&] (double v, double w) { sum += juce::jlimit (0.0, 1.0, v) * w; weight += w; };

        // tone: shaped like the matching part of the reference
        if (ref.valid && ref.hasStems)
        {
            const int stem = kind == LK::Bass ? lp::song::StemBass : (kind == LK::Drums || kind == LK::Perc) ? lp::song::StemDrums
                           : kind == LK::Vocal ? lp::song::StemVocals : lp::song::StemMusic;
            const int b0 = stem == lp::song::StemBass ? 0 : (stem == lp::song::StemDrums ? 1 : 3), b1 = stem == lp::song::StemBass ? 5 : 9;
            double ma = 0, mb = 0; int n = 0;
            for (int b = b0; b <= b1; ++b) { ma += f.tone.bandsDb[(size_t) b]; mb += ref.stemBands[(size_t) stem][(size_t) b]; ++n; }
            ma /= n; mb /= n;
            double num = 0, da = 0, db = 0;
            for (int b = b0; b <= b1; ++b)
            {
                const double x = f.tone.bandsDb[(size_t) b] - ma, y = ref.stemBands[(size_t) stem][(size_t) b] - mb;
                num += x * y; da += x * x; db += y * y;
            }
            const double corr = num / std::sqrt (std::max (1e-9, da * db));
            add ((corr + 1.0) * 0.5, 3.0);
        }
        else if (ref.valid && ref.tone.valid)
            add (lp::song::toneLikeness (f.tone, ref.tone), 2.0);
        // held vs. plucky (melodic sounds), busy (drums)
        const bool melodic = kind != LK::Drums && kind != LK::Perc && kind != LK::FX && kind != LK::Bass;
        if (ref.valid && melodic) add (1.0 - std::abs (f.sustain - (ref.sustain > 0.83f ? 0.75f : 0.4f)) * 2.0, 1.5);
        if (ref.valid && (kind == LK::Drums || kind == LK::Perc)) add (1.0 - std::abs (f.busy - ref.density) * 1.5, 1.5);
        // the words you typed
        const double octaves = std::log2 (std::max (200.0f, f.tone.rolloffHz) / 1500.0); // < 0 dark, > 0 bright
        if (wish.dark)   add (0.5 - wish.dark * octaves * 0.35, 2.0);
        if (wish.lofi)   add (0.5 - wish.lofi * (octaves * 0.25 + (f.tone.airDb + 30.0f) / 40.0), 2.0);
        if (wish.airy)   add (0.5 + wish.airy * (f.tone.airDb + 30.0f) / 30.0, 1.5);
        if (wish.punchy) add (0.5 + wish.punchy * (f.tone.crestDb - 12.0f) / 10.0, 1.5);
        if (wish.plucky && melodic) add (0.5 + wish.plucky * (0.5f - f.sustain) * 1.5, 2.0);
        if (wish.warm)   add (0.5 + wish.warm * ((f.tone.bandsDb[3] + f.tone.bandsDb[4]) * 0.5f - f.tone.bandsDb[7]) / 20.0, 1.5);
        addTaste (path, &f, add);
        return weight > 0 ? (float) (sum / weight) : 0.5f;
    }

    // ---- taste, remembered in taste.json
    juce::CriticalSection tasteLock;
    std::map<juce::String, int> sampleTaste;   // sample file -> +1 / -1
    std::map<juce::String, float> packTaste;   // pack -> running score
    std::map<juce::String, int> trackRatings;  // what a track's buttons show (by what it plays)
    std::array<float, 10> likedBands {}, dislikedBands {}; // summed tone of loved / not-this samples
    float likedCount = 0.0f, dislikedCount = 0.0f;

    // a rating belongs to the sounds a track plays (so it follows them, not the track's name)
    static juce::String trackKey (const lp::song::Part& p, const lp::song::Song& song)
    {
        if (p.samplePath.isNotEmpty()) return p.samplePath;
        juce::String k (p.name);
        if (p.kind == lp::song::PartKind::Drums) for (auto& [n, f] : song.drumSamples) k << "|" << f;
        for (auto& h : p.fxHits) k << "|" << h.path;
        return k;
    }
    static juce::File tasteFile() { return lp::appFolder().getChildFile ("taste.json"); }

    void saveTaste() // tasteLock held
    {
        auto* o = new juce::DynamicObject();
        auto* sm = new juce::DynamicObject();
        for (auto& [k, v] : sampleTaste) sm->setProperty (k, v);
        auto* pm = new juce::DynamicObject();
        for (auto& [k, v] : packTaste) pm->setProperty (k, v);
        auto* tm = new juce::DynamicObject();
        for (auto& [k, v] : trackRatings) if (v != 0) tm->setProperty (k, v);
        o->setProperty ("samples", sm); o->setProperty ("packs", pm); o->setProperty ("tracks", tm);
        o->setProperty ("likedBands", lp::song::file::floats (likedBands.data(), 10));
        o->setProperty ("likedCount", likedCount);
        o->setProperty ("dislikedBands", lp::song::file::floats (dislikedBands.data(), 10));
        o->setProperty ("dislikedCount", dislikedCount);
        tasteFile().replaceWithText (juce::JSON::toString (juce::var (o)));
    }

    void loadTaste()
    {
        const auto v = juce::JSON::parse (tasteFile());
        const juce::ScopedLock tl (tasteLock);
        if (auto* o = v["samples"].getDynamicObject()) for (auto& p : o->getProperties()) sampleTaste[p.name.toString()] = (int) p.value;
        if (auto* o = v["packs"].getDynamicObject()) for (auto& p : o->getProperties()) packTaste[p.name.toString()] = (float) p.value;
        if (auto* o = v["tracks"].getDynamicObject()) for (auto& p : o->getProperties()) trackRatings[p.name.toString()] = (int) p.value;
        lp::song::file::readFloats (v["likedBands"], likedBands.data(), 10);
        lp::song::file::readFloats (v["dislikedBands"], dislikedBands.data(), 10);
        likedCount = v["likedCount"]; dislikedCount = v["dislikedCount"];
    }

    // how much a sample suits what you've told it you like (adds to soundsLike's score)
    void addTaste (const juce::String& path, const lp::song::Feel* feel, const std::function<void (double, double)>& add)
    {
        const juce::ScopedLock tl (tasteLock);
        auto s = sampleTaste.find (path);
        if (s != sampleTaste.end()) add (s->second > 0 ? 1.0 : 0.0, s->second > 0 ? 3.0 : 8.0); // "not this" nearly rules it out
        if (const auto* e = library.find (path))
        {
            auto pk = packTaste.find (e->pack);
            if (pk != packTaste.end() && std::abs (pk->second) > 0.01f) add (0.5 + 0.12 * pk->second, 1.5);
        }
        const float rated = likedCount + dislikedCount;
        if (feel != nullptr && feel->valid && rated >= 2.0f)
        {
            // the sound you like: toward what you loved, away from what you didn't
            // (correlation of the sample's tone with that direction)
            std::array<float, 10> want {};
            double ma = 0, mb = 0;
            for (int b = 1; b < 10; ++b)
            {
                const float liked = likedCount > 0.5f ? likedBands[(size_t) b] / likedCount : 0.0f;
                const float notLiked = dislikedCount > 0.5f ? dislikedBands[(size_t) b] / dislikedCount : 0.0f;
                want[(size_t) b] = likedCount > 0.5f && dislikedCount > 0.5f ? liked - notLiked : (likedCount > 0.5f ? liked : -notLiked);
                ma += feel->tone.bandsDb[(size_t) b]; mb += want[(size_t) b];
            }
            ma /= 9; mb /= 9;
            double num = 0, da = 0, db = 0;
            for (int b = 1; b < 10; ++b)
            {
                const double x = feel->tone.bandsDb[(size_t) b] - ma, y = want[(size_t) b] - mb;
                num += x * y; da += x * x; db += y * y;
            }
            add ((num / std::sqrt (std::max (1e-9, da * db)) + 1.0) * 0.5, std::min (2.0, 0.5 + (double) rated * 0.15));
        }
    }

    // what was heard in each sample, kept on disk so your library is only listened to once
    static juce::File featuresFile() { return lp::appFolder().getChildFile ("sample-features.json"); }
    bool featuresDirty = false;

    void loadFeatures()
    {
        auto v = juce::JSON::parse (featuresFile());
        if (auto* obj = v.getDynamicObject())
            for (auto& prop : obj->getProperties())
            {
                const auto& a = prop.value;
                if (! a.isArray() || a.size() < 38) continue;
                Heard h;
                h.key = (int) a[0]; h.minor = (bool) a[1]; h.clear = (bool) a[2];
                for (int i = 0; i < 24; ++i) h.reading.scores[(size_t) i] = (float) (double) a[3 + i];
                h.reading.best = (float) (double) a[27]; h.reading.hasThird = (bool) a[28];
                auto& t = h.feel.tone;
                for (int i = 0; i < 10; ++i) t.bandsDb[(size_t) i] = (float) (double) a[29 + i];
                if (a.size() >= 45)
                {
                    t.rolloffHz = (float) (double) a[39]; t.airDb = (float) (double) a[40]; t.rmsDb = (float) (double) a[41];
                    t.crestDb = (float) (double) a[42]; h.feel.sustain = (float) (double) a[43]; h.feel.busy = (float) (double) a[44];
                    t.valid = h.feel.valid = true;
                }
                if (a.size() >= 46 && (int) a[45] >= 0)
                {
                    const int nb = (int) a[45];
                    h.chordsRead = a.size() >= 46 + nb * 12;
                    for (int bi = 0; h.chordsRead && bi < nb; ++bi)
                    {
                        std::array<float, 12> bar {};
                        for (int k = 0; k < 12; ++k) bar[(size_t) k] = (float) (double) a[46 + bi * 12 + k];
                        h.chords.push_back (bar);
                    }
                }
                h.tone = t;
                keyCache[prop.name.toString()] = h;
            }
    }

    void saveFeatures()
    {
        if (! featuresDirty) return;
        auto obj = std::make_unique<juce::DynamicObject>();
        for (auto& [path, h] : keyCache)
        {
            juce::Array<juce::var> a { h.key, h.minor, h.clear };
            for (float v : h.reading.scores) a.add (v);
            a.add (h.reading.best); a.add (h.reading.hasThird);
            for (float v : h.feel.tone.bandsDb) a.add (v);
            a.add (h.feel.tone.rolloffHz); a.add (h.feel.tone.airDb); a.add (h.feel.tone.rmsDb); a.add (h.feel.tone.crestDb);
            a.add (h.feel.sustain); a.add (h.feel.busy);
            a.add (h.chordsRead ? (int) h.chords.size() : -1);
            for (auto& bar : h.chords) for (float v : bar) a.add (v);
            obj->setProperty (juce::Identifier (path), a);
        }
        featuresFile().getParentDirectory().createDirectory();
        featuresFile().replaceWithText (juce::JSON::toString (juce::var (obj.release()), true));
        featuresDirty = false;
    }

    // semitones to put a loop into the song's key (99 = too unsure): listens to it, and checks the file name
    int tuneFor (const juce::String& path, int songKey, bool songMinor)
    {
        auto it = keyCache.find (path);
        if (it == keyCache.end())
        {
            Heard h;
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File (path)));
            if (rd != nullptr && rd->lengthInSamples > 0)
            {
                const int len = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 20.0));
                juce::AudioBuffer<float> b ((int) rd->numChannels, len);
                rd->read (&b, 0, len, 0, true, true);
                std::vector<float> mono ((size_t) len);
                for (int i = 0; i < len; ++i)
                {
                    float v = 0;
                    for (int ch = 0; ch < b.getNumChannels(); ++ch) v += b.getSample (ch, i);
                    mono[(size_t) i] = v;
                }
                h.clear = lp::song::detectKey (mono.data(), len, rd->sampleRate, h.key, h.minor, &h.reading);
                h.feel = lp::song::measureFeel (mono.data(), len, rd->sampleRate);
                h.tone = h.feel.tone;
                featuresDirty = true;
            }
            it = keyCache.emplace (path, h).first;
        }
        const auto* e = library.find (path);
        return lp::song::loopTune (it->second.reading, it->second.clear, e != nullptr ? e->key : -1, e != nullptr && e->minor, songKey, songMinor);
    }

    juce::String remixText (const lp::song::Part& loop, double fitPercent, const lp::song::Song& song) const
    {
        juce::String t = "plays as recorded";
        if (const auto* e = library.find (loop.samplePath); e != nullptr && e->bpm > 0)
        {
            double m = 1.0;
            for (double mm : { 0.5, 1.0, 2.0 }) if (std::abs (e->bpm * mm - song.bpm) < std::abs (e->bpm * m - song.bpm)) m = mm;
            fitPercent = (song.bpm / (e->bpm * m) - 1.0) * 100.0;
            if (std::abs (fitPercent) > 1.2)
                t = "stretched " + juce::String (juce::roundToInt (e->bpm)) + " > " + juce::String (song.bpm, 0) + " bpm, pitch kept"
                    + (m != 1.0 ? juce::String (m > 1.0 ? " (half-time loop)" : " (double-time loop)") : juce::String());
        }
        if (t == "plays as recorded" && std::abs (fitPercent) > 0.05)
            t << ", fitted to " << juce::roundToInt (song.bpm) << " bpm (" << (fitPercent > 0 ? "+" : "") << juce::String (fitPercent, 1) << "%)";
        const juce::String songKey = juce::String (lp::noteName (song.key)) + (song.minor ? " minor" : " major");
        const auto* e = library.find (loop.samplePath);
        juce::String was;
        if (auto h = keyCache.find (loop.samplePath); h != keyCache.end())
        {
            bool usedName = false;
            lp::song::loopTune (h->second.reading, h->second.clear, e != nullptr ? e->key : -1, e != nullptr && e->minor, song.key, song.minor, &usedName);
            if (usedName && e != nullptr) was = juce::String (" (labelled ") + lp::noteName (e->key) + (e->minor ? " minor)" : " major)");
            else if (h->second.key >= 0) was = juce::String (" (heard in ") + lp::noteName (h->second.key) + (h->second.minor ? " minor)" : " major)");
        }
        if (loop.autoTune != 0)
            t << ", tuned " << (loop.autoTune > 0 ? "+" : "") << loop.autoTune << " st into " << songKey << was;
        else
            t << ", already in " << songKey;
        t << ", arranged into this song's sections";
        return t;
    }

    std::map<juce::String, Heard> keyCache;

    // the song in a new key: melodic samples re-tuned from their own key, played parts moved, the mix rebuilt
    void retuneSong (int key, bool minor)
    {
        lp::song::Song song;
        std::vector<TrackInfo> tracks;
        {
            const juce::ScopedLock sl (lock);
            song = current;
            tracks = status.tracks;
        }
        if (song.parts.empty() || (song.key == key && song.minor == minor)) return;
        using PK = lp::song::PartKind;
        using LK = lp::song::LoopKind;
        // played parts move by the shortest way between the two keys' home notes (relative keys count as the same)
        const int shift = lp::song::tuneShift (song.key, song.minor, key, minor);
        song.key = key;
        song.minor = minor;
        for (size_t i = 0; i < song.parts.size() && i < tracks.size(); ++i)
        {
            auto& part = song.parts[i];
            bool changed = false;
            if (part.kind == PK::Loop)
            {
                const auto* e = library.find (part.samplePath);
                const bool untuned = e != nullptr && (e->loop == LK::Drums || e->loop == LK::Perc || e->loop == LK::FX);
                if (untuned) continue;
                for (int c = 0; c < part.candidates.size(); ++c)
                {
                    const int t = tuneFor (part.candidates[c], key, minor);
                    if (c < part.candidateTunes.size()) part.candidateTunes.set (c, t == 99 ? 0 : t);
                }
                const int t = tuneFor (part.samplePath, key, minor);
                part.autoTune = t == 99 ? 0 : t;
                part.remix = remixText (part, 0.0, song) + arrangementText (part);
                changed = true;
            }
            else if (part.kind != PK::Drums && part.kind != PK::Fx && shift != 0)
            {
                for (auto& nt : part.notes) nt.pitch += shift;
                changed = true;
            }
            if (! changed) continue;
            {
                const juce::ScopedLock sl (lock);
                status.message = "moving \"" + juce::String (part.name) + "\" to " + lp::noteName (key) + (minor ? " minor" : " major") + "...";
            }
            juce::AudioBuffer<float> buf;
            if (! lp::song::renderPart (song, part, currentRs, buf, [this] { return threadShouldExit(); })) return;
            const auto stem = currentFolder.getChildFile ("stems").getChildFile (
                juce::String ((int) i + 1).paddedLeft ('0', 2) + " " + juce::String (part.name) + " (" + lp::noteName (key) + (minor ? "m" : "") + ").wav");
            if (! writeStem (stem, buf, currentRs.sampleRate)) return;
            tracks[i] = info (part, stem, song);
        }
        rebuildMix (song, tracks);
        const juce::ScopedLock sl (lock);
        current = song;
        status.song = song;
        status.tracks = tracks;
        status.message = "\"" + song.title + "\" is now in " + lp::noteName (key) + (minor ? " minor" : " major");
        ++status.version;
        saveSession();
    }

    // the full mix again from the tracks (after a key change), through the same limiter
    void rebuildMix (const lp::song::Song& song, const std::vector<TrackInfo>& tracks)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        juce::AudioBuffer<float> mix;
        for (auto& t : tracks)
        {
            std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (t.stem));
            if (rd == nullptr) continue;
            const int n = (int) rd->lengthInSamples;
            if (mix.getNumSamples() < n) mix.setSize (2, n, true, true);
            juce::AudioBuffer<float> b (2, n);
            rd->read (&b, 0, n, 0, true, true);
            for (int ch = 0; ch < 2; ++ch) mix.addFrom (ch, 0, b, ch, 0, n);
        }
        if (mix.getNumSamples() == 0) return;
        lp::Limiter lim;
        lim.prepare (currentRs.sampleRate, -1.0f);
        lim.process (mix.getWritePointer (0), mix.getWritePointer (1), mix.getNumSamples(), song.playbackMakeupDb);
        writeStem (currentFolder.getChildFile (song.title + " - full mix.wav"), mix, currentRs.sampleRate);
    }

    // a track's info, with what happened to its key
    TrackInfo info (const lp::song::Part& p, const juce::File& stem, const lp::song::Song& song) const
    {
        auto t = trackInfo (p, stem);
        if (p.kind == lp::song::PartKind::Loop)
            if (const auto* e = library.find (p.samplePath); e != nullptr && e->loop != lp::song::LoopKind::Drums
                                                                && e->loop != lp::song::LoopKind::Perc && e->loop != lp::song::LoopKind::FX)
            {
                const juce::String songKey = juce::String (lp::noteName (song.key)) + (song.minor ? " minor" : " major");
                const int tune = p.autoTune;
                // spelled out ("A# minor", not "A#m", which reads as major once it's in capitals)
                juce::String from = e->key >= 0 ? juce::String (lp::noteName (e->key)) + (e->minor ? " minor" : " major") : juce::String ("its own key");
                t.keyText = tune == 0 ? "in " + songKey + " already"
                                      : from + " to " + songKey + ", " + (tune > 0 ? "up " : "down ") + juce::String (std::abs (tune)) + " st";
            }
        return t;
    }

public:
    // a sample's name as people read it: "CK_118_pluck_arp_01_Asmin" -> "CK 118 pluck arp 01 · A# minor"
    // (sample makers write a sharp as "s": Asmin = A# minor, Fsmaj = F# major)
    static juce::String niceName (const juce::String& raw)
    {
        auto words = juce::StringArray::fromTokens (raw.replaceCharacters ("_-", "  "), " ", "");
        words.removeEmptyStrings();
        for (int i = 0; i < words.size(); ++i)
        {
            const auto w = words[i];
            const auto lw = w.toLowerCase();
            if (w.isEmpty() || juce::String ("ABCDEFG").indexOfChar (juce::CharacterFunctions::toUpperCase (w[0])) < 0) continue;
            juce::String rest = lw.substring (1), acc;
            if (rest.startsWith ("#")) { acc = "#"; rest = rest.substring (1); }
            else if (rest.startsWith ("s") && ! rest.startsWith ("sus")) { acc = "#"; rest = rest.substring (1); }
            else if (rest.startsWith ("b") && rest != "b") { acc = "b"; rest = rest.substring (1); }
            juce::String quality;
            if (rest == "maj" || rest == "major") quality = " major";
            else if (rest == "min" || rest == "minor" || rest == "m") quality = " minor";
            else if (! (rest.isEmpty() && i == words.size() - 1 && w.length() <= 2)) continue; // not a key ("bass", "arp"...)
            words.set (i, juce::String::fromUTF8 ("\xc2\xb7 ") + w.substring (0, 1).toUpperCase() + acc + quality);
        }
        return words.joinIntoString (" ");
    }
    // the source line with the sample's name made readable
    static juce::String niceSource (const juce::String& source)
    {
        const int colon = source.indexOf (": ");
        if (colon < 0) return source;
        return source.substring (0, colon + 2) + niceName (source.substring (colon + 2));
    }
private:

    // where a sound came from, as shown in the studio ("splice: ...", "wavs: ...", "folder: ...")
    static juce::String sourceLabel (const juce::String& path)
    {
        switch (lp::song::Library::sourceOf (path))
        {
            case lp::song::Library::Source::Splice: return "splice: ";
            case lp::song::Library::Source::Wavs:   return "wavs: ";
            default:                                return "folder: ";
        }
    }
    static juce::String kitLabel (const lp::song::LibraryPicks& picks)
    {
        return picks.drums.empty() ? juce::String ("splice: ") : sourceLabel (picks.drums.begin()->second);
    }
    static bool isLibrarySource (const juce::String& s)
    {
        return s.startsWith ("splice") || s.startsWith ("wavs") || s.startsWith ("folder") || s.startsWith ("library");
    }

    static TrackInfo trackInfo (const lp::song::Part& p, const juce::File& stem)
    {
        TrackInfo t;
        t.name = p.name;
        t.source = p.source;
        t.remix = p.remix;
        t.stem = stem;
        t.pitch = p.pitchSemis;
        t.reversed = p.reversed;
        t.canReverse = p.kind == lp::song::PartKind::Loop || (p.kind == lp::song::PartKind::Drums && isLibrarySource (p.source));
        t.canSwap = true;
        t.candidates = p.candidates.size();
        t.candidate = p.candidate;
        t.sample = isLibrarySource (p.source);
        const juce::String n (p.name);
        if (p.kind == lp::song::PartKind::Drums || p.kind == lp::song::PartKind::Fx || n.contains ("drum") || n == "percussion") t.group = 4;
        else if (p.kind == lp::song::PartKind::Bass || n.contains ("bass"))                t.group = 3;
        else if (n == "keys" || n == "guitar" || n == "pad" || n == "strings" || n == "loop" || p.kind == lp::song::PartKind::Chords || p.kind == lp::song::PartKind::Pad)
            t.group = 1;
        else t.group = 2;
        // its own effects to start with, like a mixer would set them
        const juce::String nm (p.name);
        if (p.kind == lp::song::PartKind::Fx)                 { t.space = 0.25f; }
        else if (nm == "vocal")                               { t.space = 0.32f; t.echo = 0.16f; t.warmth = 0.1f; }
        else if (nm == "pad" || p.kind == lp::song::PartKind::Pad) { t.space = 0.4f; t.warmth = 0.15f; }
        else if (t.group == 4)                                { t.space = nm == "percussion" ? 0.14f : 0.06f; t.echo = nm == "percussion" ? 0.06f : 0.0f; }
        else if (t.group == 3)                                { t.warmth = 0.12f; }
        else if (nm == "bells" || nm == "pluck" || nm == "lead" || p.kind == lp::song::PartKind::Melody)
                                                              { t.space = 0.24f; t.echo = 0.12f; t.warmth = 0.1f; }
        else                                                  { t.space = 0.2f; t.echo = 0.05f; t.warmth = 0.12f; }
        t.samplePath = p.samplePath;
        t.loopBars = p.loopBars;
        t.regionStart = p.regionStart;
        t.regionBars = p.regionBars;
        t.isLoop = p.kind == lp::song::PartKind::Loop;
        t.grains = { p.grainAmount, p.grainSizeMs, p.grainChaos, p.grainPosition, p.grainRate };
        return t;
    }

    // adds a track: a loop of one kind from your library, in tempo and in key, placed where that kind fits
    void addInstrument (lp::song::LoopKind kind)
    {
        using LK = lp::song::LoopKind;
        lp::song::Song song;
        {
            const juce::ScopedLock sl (lock);
            if (current.parts.empty() || (int) current.parts.size() >= 12) return;
            song = current;
        }
        // favour packs already in the song (same vibe), then the genre, shuffled so adding twice gives something new
        juce::StringArray packsInSong;
        for (auto& p : song.parts)
            if (const auto* e = library.find (p.samplePath)) packsInSong.addIfNotAlreadyThere (e->pack);
        const auto styleBit = 1u << (int) song.style;
        juce::Random rng;
        std::vector<std::pair<float, const lp::song::LibraryEntry*>> pool;
        refreshUsable();
        for (auto* e : usableLib.loopsOfKind (kind, song.bpm))
        {
            bool used = false;
            for (auto& p : song.parts) used = used || p.samplePath == e->path;
            if (used) continue;
            pool.push_back ({ (packsInSong.contains (e->pack) ? 2.5f : 0.0f) + ((e->styles & styleBit) ? 1.5f : 0.0f) + rng.nextFloat() * 2.0f, e });
        }
        std::sort (pool.begin(), pool.end(), [] (auto& a, auto& b) { return a.first > b.first; });
        const bool tonal = kind != LK::Drums && kind != LK::Perc && kind != LK::FX;
        lp::song::Part part;
        part.name = lp::song::loopKindName (kind);
        part.kind = lp::song::PartKind::Loop;
        int bars = 0;
        struct Found { const lp::song::LibraryEntry* e; int bars, tune; float like; };
        std::vector<Found> found;
        for (size_t i = 0; i < pool.size() && i < 40 && found.size() < 8; ++i)
        {
            const auto* e = pool[i].second;
            const int b = songBars (e->path, e->bpm, song.bpm);
            if (b <= 0) continue;
            const int tune = tonal ? tuneFor (e->path, song.key, song.minor) : 0;
            if (tune == 99) continue;
            found.push_back ({ e, b, tune, soundsLike (e->path, currentRef, currentTarget.wish) });
        }
        // the ones that sound most like your reference first
        std::stable_sort (found.begin(), found.end(), [] (const Found& a, const Found& b)
            { return a.like * 3.0f - (float) std::abs (a.tune) * 0.35f > b.like * 3.0f - (float) std::abs (b.tune) * 0.35f; });
        for (size_t i = 0; i < found.size() && i < 5; ++i)
        {
            if (i == 0) bars = found[i].bars;
            part.candidates.add (found[i].e->path);
            part.candidateTunes.add (found[i].tune);
        }
        if (part.candidates.isEmpty())
        {
            const juce::ScopedLock sl (lock);
            status.message = juce::String ("no ") + part.name + " loops in your library fit this song's key and tempo";
            return;
        }
        part.samplePath = part.candidates[0];
        part.autoTune = part.candidateTunes[0];
        part.loopBars = bars;
        std::vector<std::string> where { "verse", "chorus" };
        if (kind == LK::Pad)                            where = { "intro", "bridge", "outro" };
        else if (kind == LK::FX)                        where = { "pre-chorus" };
        else if (kind == LK::Perc || kind == LK::Drums) where = { "pre-chorus", "chorus" };
        else if (kind == LK::Vocal)                     where = { "chorus", "bridge" };
        for (auto& sec : song.sections)
            for (auto& want : where)
                if (sec.name.rfind (want, 0) == 0)
                    for (int b = sec.startBar; b + bars <= sec.startBar + sec.bars; b += bars)
                        part.loopStartBars.push_back (b);
        if (part.loopStartBars.empty())
            for (int b = 0; b + bars <= song.totalBars(); b += bars) part.loopStartBars.push_back (b);
        part.source = sourceLabel (part.samplePath) + juce::File (part.samplePath).getFileNameWithoutExtension();
        part.remix = remixText (part, 0.0, song) + " (added by you)";
        saveFeatures();
        song.parts.push_back (part);
        juce::AudioBuffer<float> buf;
        if (! lp::song::renderPart (song, part, currentRs, buf, [this] { return threadShouldExit(); })) return;
        const int tr = (int) song.parts.size() - 1;
        const auto stem = currentFolder.getChildFile ("stems").getChildFile (
            juce::String (tr + 1).paddedLeft ('0', 2) + " " + juce::String (part.name) + " (added " + juce::String (++editCounter) + ").wav");
        if (! writeStem (stem, buf, currentRs.sampleRate)) return;
        const juce::ScopedLock sl (lock);
        current = song;
        status.song = song;
        status.tracks.push_back (info (part, stem, song));
        status.message = "added " + juce::String (part.name) + ": " + juce::File (part.samplePath).getFileNameWithoutExtension();
        ++status.version;
        saveSession();
    }

    void applyArrange()
    {
        std::vector<lp::song::arrange::Item> plan;
        lp::song::Song old;
        {
            const juce::ScopedLock sl (lock);
            plan = pendingPlan;
            old = current;
        }
        if (plan.empty() || old.sections.empty()) return;
        auto song = lp::song::arrange::rebuild (old, plan);
        setStage (Stage::Rendering, 0.05f, "rebuilding \"" + song.title + "\" in its new shape...");
        const auto sub = currentFolder.getChildFile ("arrangement " + juce::String (++editCounter));
        lp::song::Rendered files;
        const bool ok = lp::song::render (song, currentRs, sub, files,
                                          [this] (float p) { const juce::ScopedLock sl (lock); status.progress = 0.05f + 0.9f * p; },
                                          [this] { return threadShouldExit(); });
        if (! ok) { setStage (Stage::Failed, 0.0f, "couldn't record the new arrangement"); return; }
        song.matchEqDb = files.matchEqDb; song.matchGainDb = files.matchGainDb;
        song.playbackMakeupDb = files.playbackMakeupDb; song.mixShaped = true;
        juce::File als;
        if (writeAbletonSet) als = writeAbletonSet (song, files);
        files.folder = currentFolder; // the same song (so your mixer and master stay), in its new shape
        files.mixAudio.reset();
        const juce::ScopedLock sl (lock);
        current = song;
        status.song = song;
        status.tracks.clear();
        for (int i = 0; i < (int) song.parts.size(); ++i)
            status.tracks.push_back (info (song.parts[(size_t) i], files.stems[i], song));
        status.files = files;
        if (als.existsAsFile()) status.abletonSet = als;
        ++status.version;
        status.stage = Stage::Done;
        status.progress = 1.0f;
        status.message = "\"" + song.title + "\" rebuilt: " + juce::String (song.totalBars()) + " bars, " + juce::String ((int) song.sections.size()) + " sections";
        saveSession();
    }

    void add808()
    {
        lp::song::Song song;
        std::vector<TrackInfo> tracks;
        {
            const juce::ScopedLock sl (lock);
            if (current.parts.empty() || (int) current.parts.size() >= 12) return;
            song = current; tracks = status.tracks;
        }
        lp::song::Part part;
        part.kind = lp::song::PartKind::Bass;
        part.name = "808";
        part.variant = 1;
        const auto seed = currentSeed * 17u + (juce::uint32) song.parts.size();
        part.notes = write808 (song, songChords (song, tracks), seed);
        if (part.notes.empty()) { const juce::ScopedLock sl (lock); status.message = "no beat to follow yet: add drums first"; return; }
        part.candidates = oneShotsFor (song, true, seed);
        if (! part.candidates.isEmpty())
        {
            part.samplePath = part.candidates[0];
            part.source = sourceLabel (part.samplePath) + juce::File (part.samplePath).getFileNameWithoutExtension();
        }
        else part.source = "loopwright: 808";
        int glides = 0;
        for (auto& n : part.notes) glides += n.glide ? 1 : 0;
        part.remix = "follows your chords and kick, with " + juce::String (glides) + " slides. swap: another 808";
        song.parts.push_back (part);
        juce::AudioBuffer<float> buf;
        if (! lp::song::renderPart (song, part, currentRs, buf, [this] { return threadShouldExit(); })) return;
        const int tr = (int) song.parts.size() - 1;
        const auto stem = currentFolder.getChildFile ("stems").getChildFile (
            juce::String (tr + 1).paddedLeft ('0', 2) + " 808 (added " + juce::String (++editCounter) + ").wav");
        if (! writeStem (stem, buf, currentRs.sampleRate)) return;
        const juce::ScopedLock sl (lock);
        current = song;
        status.song = song;
        status.tracks.push_back (info (part, stem, song));
        status.message = "808 added: it follows your chords and kick (mute the bass loop if you like it better)";
        ++status.version;
        saveSession();
    }

    void applyFromTake (int dst)
    {
        std::pair<int, int> req;
        juce::File f;
        lp::song::Song song;
        {
            const juce::ScopedLock sl (lock);
            req = pendingFromTake;
            if (req.first < 0 || req.first >= status.takes.size()) return;
            f = status.takes[req.first];
            song = current;
        }
        const auto v = juce::JSON::parse (f.getChildFile ("song.json"));
        lp::song::Song other;
        if (! lp::song::file::songFromVar (v["song"], other)) return;
        const auto stems = lp::song::file::readStrings (v["stems"]);
        if (req.second < 0 || req.second >= (int) other.parts.size() || req.second >= stems.size()) return;
        auto part = other.parts[(size_t) req.second];
        const juce::File stem (stems[req.second]);
        if (! stem.existsAsFile()) return;
        if (part.kind == lp::song::PartKind::Drums) song.drumSamples = other.drumSamples; // its kit comes with it
        part.remix = "from " + other.title.fromFirstOccurrenceOf ("(", false, false).upToFirstOccurrenceOf (")", false, false)
                   + (other.title.contains ("(take") ? "" : "take 1") + ". " + part.remix;
        const juce::ScopedLock sl (lock);
        if (dst >= 0 && dst < (int) song.parts.size())
        {
            song.parts[(size_t) dst] = part;
            status.tracks[(size_t) dst] = info (part, stem, song);
        }
        else if ((int) song.parts.size() < 12)
        {
            song.parts.push_back (part);
            status.tracks.push_back (info (part, stem, song));
        }
        current = song;
        status.song = song;
        status.message = juce::String (part.name) + " is now from another take";
        ++status.version;
        saveSession();
    }

    // vocals to chop: vocal loops and vocal one-shots in your library (the song's own packs first)
    juce::StringArray vocalsToChop (const lp::song::Song& song, juce::uint32 seed)
    {
        refreshUsable();
        juce::StringArray packsInSong;
        for (auto& p : song.parts) if (const auto* e = library.find (p.samplePath)) packsInSong.addIfNotAlreadyThere (e->pack);
        std::vector<std::pair<float, juce::String>> pool;
        juce::Random rng ((juce::int64) seed);
        for (auto& e : usableLib.all())
        {
            const auto lower = e.path.toLowerCase();
            const bool vocal = e.loop == lp::song::LoopKind::Vocal || lower.contains ("vocal") || lower.contains ("vox") || lower.contains ("voice") || lower.contains ("chop");
            if (! vocal || e.kind == lp::song::SampleKind::Riser || e.kind == lp::song::SampleKind::Impact) continue;
            pool.push_back ({ (packsInSong.contains (e.pack) ? 2.0f : 0.0f) + rng.nextFloat() * 1.5f, e.path });
        }
        std::sort (pool.begin(), pool.end(), [] (auto& a, auto& b) { return a.first > b.first; });
        juce::StringArray out;
        for (size_t i = 0; i < pool.size() && i < 8; ++i) out.add (pool[i].second);
        return out;
    }

    // sounds from your library for written parts: melodic one-shots (plucks, keys, bells, leads...) for a hook,
    // 808s for a bass line. The song's own packs first; only sounds with a clear note (or a note in their name).
    juce::StringArray oneShotsFor (const lp::song::Song& song, bool bass, juce::uint32 seed)
    {
        refreshUsable();
        juce::StringArray packsInSong;
        for (auto& p : song.parts) if (const auto* e = library.find (p.samplePath)) packsInSong.addIfNotAlreadyThere (e->pack);
        for (auto& [nn, f] : song.drumSamples) if (const auto* e = library.find (f)) packsInSong.addIfNotAlreadyThere (e->pack);
        std::vector<std::pair<float, juce::String>> pool;
        juce::Random rng ((juce::int64) seed);
        const juce::StringArray melodicWords { "pluck", "bell", "keys", "key", "piano", "lead", "synth", "flute", "guitar", "marimba", "mallet", "stab", "chord", "rhodes", "organ", "string", "brass", "kalimba", "harp" };
        const juce::StringArray notWords { "vocal", "vox", "fx", "foley", "asmr", "riser", "impact", "noise", "kick", "snare", "hat", "perc", "crash", "sweep", "drum" };
        for (auto& e : usableLib.all())
        {
            const auto lower = juce::File (e.path).getFileNameWithoutExtension().toLowerCase();
            bool skip = false;
            for (auto& w : notWords) skip = skip || lower.contains (w);
            if (skip) continue;
            float score = rng.nextFloat();
            if (bass)
            {
                if (! (e.kind == lp::song::SampleKind::Bass808 || (e.kind == lp::song::SampleKind::OneShot && (lower.contains ("808") || lower.contains ("bass"))))) continue;
            }
            else
            {
                if (e.kind != lp::song::SampleKind::OneShot) continue;
                for (auto& w : melodicWords) if (lower.contains (w)) { score += 1.5f; break; }
            }
            if (packsInSong.contains (e.pack)) score += 2.0f;
            pool.push_back ({ score, e.path });
        }
        std::sort (pool.begin(), pool.end(), [] (auto& x, auto& y) { return x.first > y.first; });
        juce::StringArray out;
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        for (size_t i = 0; i < pool.size() && out.size() < 8 && i < 40; ++i)
        {
            // a melodic sound needs a note: listen for one (808s pass: their note is often too deep to hear, and in the name)
            if (! bass)
            {
                std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File (pool[i].second)));
                if (rd == nullptr) continue;
                const int len = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) rd->sampleRate);
                juce::AudioBuffer<float> b (2, len);
                rd->read (&b, 0, len, 0, true, true);
                std::vector<float> mono ((size_t) len);
                for (int k = 0; k < len; ++k) mono[(size_t) k] = 0.5f * (b.getSample (0, k) + b.getSample (1, k));
                if (lp::sources::detectPitch (mono, rd->sampleRate) <= 0.0) continue;
            }
            out.add (pool[i].second);
        }
        return out;
    }

    // an 808 line that follows the chords: it plays with the kick, on each chord's bass note, sliding when
    // the chord moves on quickly (and the odd octave slide at the end of a phrase, like a trap 808)
    static std::vector<lp::song::Note> write808 (const lp::song::Song& song, const std::vector<lp::song::chords::Chord>& chordList, juce::uint32 seed)
    {
        std::vector<double> kicks;
        for (auto& p : song.parts)
            if (p.kind == lp::song::PartKind::Drums)
                for (auto& n : p.notes) if (n.pitch == 36) kicks.push_back (n.beat);
        std::sort (kicks.begin(), kicks.end());
        kicks.erase (std::unique (kicks.begin(), kicks.end(), [] (double a, double b) { return std::abs (a - b) < 0.05; }), kicks.end());
        if (kicks.empty()) // no drawn kick: the downbeats and the "and" of 3, where the music plays
            for (auto& sec : song.sections)
                if (sec.energy >= 0.35f)
                    for (int b = sec.startBar; b < sec.startBar + sec.bars; ++b) { kicks.push_back (b * 4.0); kicks.push_back (b * 4.0 + 2.5); }
        auto chordAt = [&] (double beat) -> const lp::song::chords::Chord*
        {
            for (auto& c : chordList) if (c.valid && beat >= c.beat - 1e-6 && beat < c.beat + c.length) return &c;
            return nullptr;
        };
        juce::Random rng ((juce::int64) seed);
        std::vector<lp::song::Note> out;
        int prevPitch = -1;
        for (size_t i = 0; i < kicks.size(); ++i)
        {
            const double t = kicks[i];
            const auto* c = chordAt (t + 0.01);
            const int pc = c != nullptr ? c->bass : song.key;
            int pitch = 24 + pc;                 // C1..B1 ...
            if (pitch < 28) pitch += 12;         // ...kept between E1 and D#2, where 808s sit
            const double next = i + 1 < kicks.size() ? kicks[i + 1] : t + 2.0;
            const double len = std::min (next - t, 2.0);
            const bool glide = prevPitch >= 0 && pitch != prevPitch && ! out.empty() && out.back().beat + out.back().length >= t - 0.05 && rng.nextFloat() < 0.6f;
            out.push_back ({ t, std::max (0.25, len), pitch, 0.9f, glide });
            // the end of every 4 bars: a slide up an octave on the last eighth (if there's room)
            const double barEnd = std::floor (t / 16.0) * 16.0 + 16.0;
            if (next >= barEnd && t < barEnd - 0.5 && rng.nextFloat() < 0.5f)
            {
                out.back().length = std::max (0.25, barEnd - 0.5 - t);
                out.push_back ({ barEnd - 0.5, 0.45, pitch + 12, 0.85f, true });
            }
            prevPitch = pitch;
        }
        return out;
    }

    // the instrument a written hook is played on, by style (Swap changes the melody, not the sound)
    static lp::SoundId hookSound (lp::song::Style st)
    {
        using S = lp::song::Style;
        switch (st)
        {
            case S::Trap: case S::Electronic: return lp::SoundId::Vapor;
            case S::Dusty: case S::BoomBap:   return lp::SoundId::Rhodes;
            case S::House:                    return lp::SoundId::Glass;
            default:                          return lp::SoundId::Celesta;
        }
    }

    // a hook, written over the song's chords, added as a new track: a melody, or chopped vocals
    void addHook (bool chops)
    {
        lp::song::Song song;
        std::vector<TrackInfo> tracks;
        {
            const juce::ScopedLock sl (lock);
            if (current.parts.empty() || (int) current.parts.size() >= 12) return;
            song = current; tracks = status.tracks;
        }
        setStage (Stage::Done, 1.0f, chops ? "writing vocal chops over your chords..." : "writing a hook over your chords...");
        const auto chordList = songChords (song, tracks);
        lp::song::Part part;
        part.kind = lp::song::PartKind::Melody;
        part.name = chops ? "vocal chops" : "hook";
        part.variant = 1;
        const auto seed = currentSeed * 31u + (juce::uint32) song.parts.size() * 7919u;
        juce::String where;
        part.notes = lp::song::hook::write (song, chordList, seed + (juce::uint32) part.variant, chops, where);
        if (chops)
        {
            // your own vocal (dropped or picked), or vocals from your sample folders
            juce::File own;
            { const juce::ScopedLock sl (lock); own = pendingOwnVocal; pendingOwnVocal = juce::File(); }
            part.candidates = own.existsAsFile() ? juce::StringArray (own.getFullPathName()) : vocalsToChop (song, seed);
            if (part.candidates.isEmpty())
            {
                const juce::ScopedLock sl (lock);
                status.message = "no vocals in your sample folders to chop: try the melody hook";
                return;
            }
            part.samplePath = part.candidates[0];
            part.source = (own.existsAsFile() ? juce::String ("your vocal: ") : sourceLabel (part.samplePath)) + juce::File (part.samplePath).getFileNameWithoutExtension();
        }
        else
        {
            // played by a sound from your library (so it sounds like the rest of the record), or a built-in one
            part.candidates = oneShotsFor (song, false, seed);
            if (! part.candidates.isEmpty())
            {
                part.samplePath = part.candidates[0];
                part.source = sourceLabel (part.samplePath) + juce::File (part.samplePath).getFileNameWithoutExtension();
            }
            else
            {
                part.sound = hookSound (song.style);
                part.source = juce::String ("loopwright: written hook, ") + lp::profileFor (part.sound).name;
            }
        }
        part.remix = "an original hook over your chords, in the " + where + (part.samplePath.isNotEmpty() && ! chops ? ". swap: another sound, then another hook" : ". swap: another hook");
        song.parts.push_back (part);
        juce::AudioBuffer<float> buf;
        if (! lp::song::renderPart (song, part, currentRs, buf, [this] { return threadShouldExit(); })) return;
        const int tr = (int) song.parts.size() - 1;
        const auto stem = currentFolder.getChildFile ("stems").getChildFile (
            juce::String (tr + 1).paddedLeft ('0', 2) + " " + juce::String (part.name) + " (added " + juce::String (++editCounter) + ").wav");
        if (! writeStem (stem, buf, currentRs.sampleRate)) return;
        const juce::ScopedLock sl (lock);
        current = song;
        status.song = song;
        status.tracks.push_back (info (part, stem, song));
        status.message = juce::String (chops ? "vocal chops" : "a hook") + " written over your chords, in the " + where;
        ++status.version;
        saveSession();
    }

    void applyEdit (int tr, EditOp op)
    {
        if (op == EditOp::Add) { addInstrument ((lp::song::LoopKind) tr); return; }
        if (op == EditOp::AddHook || op == EditOp::AddChops) { addHook (op == EditOp::AddChops); return; }
        if (op == EditOp::FromTake) { applyFromTake (tr); return; }
        if (op == EditOp::Add808) { add808(); return; }
        if (op == EditOp::Arrange) { applyArrange(); return; }
        if (op == EditOp::Like || op == EditOp::Dislike) { applyRating (tr, op == EditOp::Like ? 1 : -1); return; }
        if (op == EditOp::Key) { retuneSong (tr % 12, tr >= 12); return; }
        lp::song::Song song;
        {
            const juce::ScopedLock sl (lock);
            if (tr < 0 || tr >= (int) current.parts.size()) return;
            song = current;
        }
        auto& part = song.parts[(size_t) tr];
        using PK = lp::song::PartKind;
        if (op == EditOp::Reverse) part.reversed = ! part.reversed;
        if (op == EditOp::Grains)
        {
            const juce::ScopedLock sl (lock);
            const auto gs = pendingGrains[tr];
            part.grainAmount = gs.amount; part.grainSizeMs = gs.sizeMs; part.grainChaos = gs.chaos;
            part.grainPosition = gs.position; part.grainRate = gs.rate;
        }
        if (op == EditOp::SwapDrum && ! song.drumSamples.empty())
        {
            std::pair<int, int> req;
            { const juce::ScopedLock sl (lock); req = pendingDrumSwap[tr]; }
            const int note = req.first;
            using SK = lp::song::SampleKind;
            const SK want = note == 36 ? SK::Kick : note == 38 ? SK::Snare : note == 39 ? SK::Clap : note == 42 ? SK::Hat
                          : note == 46 ? SK::OpenHat : note == 49 ? SK::Crash : SK::Perc;
            refreshUsable();
            // every sound of that kind: the kit's own pack first, then the rest of your library (in a set order,
            // so next / back step through them), leaving out anything you said "not this" to
            const auto cur = song.drumSamples.count (note) ? song.drumSamples[note] : juce::String();
            const auto* curEntry = library.find (cur);
            const juce::String pack = curEntry != nullptr ? curEntry->pack : juce::String();
            std::vector<const lp::song::LibraryEntry*> same, other;
            for (auto& e : usableLib.all())
            {
                if (e.kind != want) continue;
                {
                    const juce::ScopedLock tl (tasteLock);
                    auto t = sampleTaste.find (e.path);
                    if (t != sampleTaste.end() && t->second < 0) continue;
                }
                (e.pack == pack ? same : other).push_back (&e);
            }
            auto byName = [] (const lp::song::LibraryEntry* a, const lp::song::LibraryEntry* b) { return a->path < b->path; };
            std::sort (same.begin(), same.end(), byName);
            std::sort (other.begin(), other.end(), byName);
            juce::Random shuffle ((juce::int64) currentSeed * 31 + note);
            for (int i = (int) other.size() - 1; i > 0; --i) std::swap (other[(size_t) i], other[(size_t) shuffle.nextInt (i + 1)]);
            std::vector<const lp::song::LibraryEntry*> all (same);
            all.insert (all.end(), other.begin(), other.end());
            if (! all.empty())
            {
                int at = -1;
                for (int i = 0; i < (int) all.size(); ++i) if (all[(size_t) i]->path == cur) at = i;
                const int nextIndex = at < 0 ? 0 : ((at + (req.second < 0 ? -1 : 1)) % (int) all.size() + (int) all.size()) % (int) all.size();
                song.drumSamples[note] = all[(size_t) nextIndex]->path;
                {
                    const juce::ScopedLock sl (lock);
                    drumSwapName = juce::File (all[(size_t) nextIndex]->path).getFileNameWithoutExtension();
                    ++drumSwapCount;
                }
                part.source = sourceLabel (all[(size_t) nextIndex]->path) + "your own kit (mixed)";
            }
        }
        if (op == EditOp::Feel)
        {
            const juce::ScopedLock sl (lock);
            song.groove.amount = pendingFeel;
            if (! song.groove.valid) { song.groove = lp::song::styleGroove (song.style); song.groove.amount = pendingFeel; }
        }
        if (op == EditOp::Notes)
        {
            const juce::ScopedLock sl (lock);
            auto notes = pendingNotes[tr];
            std::sort (notes.begin(), notes.end(), [] (const lp::song::Note& a, const lp::song::Note& b) { return a.beat < b.beat; });
            part.notes = std::move (notes);
            part.remix = "your own pattern";
        }
        if (op == EditOp::Region)
        {
            const juce::ScopedLock sl (lock);
            const auto r = pendingRegion[tr];
            part.regionStart = r.first;
            part.regionBars = r.second >= part.loopBars ? 0 : r.second;
        }
        if (op == EditOp::PitchUp) part.pitchSemis = std::min (12, part.pitchSemis + 1);
        if (op == EditOp::PitchDown) part.pitchSemis = std::max (-12, part.pitchSemis - 1);
        if (op == EditOp::Swap && ((part.kind == PK::Melody && part.name == "hook" && part.candidates.size() > 1 && part.variant % 2 == 1)
                                   || (part.kind == PK::Bass && part.name == "808" && part.candidates.size() > 1)))
        {
            // another sound playing the same notes
            ++part.variant;
            part.candidate = (part.candidate + 1) % std::max (1, part.candidates.size());
            part.samplePath = part.candidates[part.candidate];
            part.source = sourceLabel (part.samplePath) + juce::File (part.samplePath).getFileNameWithoutExtension();
        }
        else if (op == EditOp::Swap && part.kind == PK::Melody && (part.name == "hook" || part.name == "vocal chops"))
        {
            // another hook: a new melody over the same chords (chops: every other time, another vocal too)
            std::vector<TrackInfo> tracks;
            { const juce::ScopedLock sl (lock); tracks = status.tracks; }
            const bool chops = part.name == "vocal chops";
            ++part.variant;
            juce::String where;
            part.notes = lp::song::hook::write (song, songChords (song, tracks), currentSeed * 31u + (juce::uint32) tr * 7919u + (juce::uint32) part.variant, chops, where);
            if (chops && part.candidates.size() > 1 && part.variant % 2 == 0)
            {
                part.candidate = (part.candidate + 1) % part.candidates.size();
                part.samplePath = part.candidates[part.candidate];
                part.source = sourceLabel (part.samplePath) + juce::File (part.samplePath).getFileNameWithoutExtension();
            }
            part.remix = "an original hook over your chords (take " + juce::String (part.variant) + "), in the " + where + ". swap: another hook";
        }
        else if (op == EditOp::Swap)
        {
            if (part.kind == PK::Loop && part.candidates.size() > 1)
            {
                // the next one, skipping any you said "not this" to
                for (int tries = 0; tries < part.candidates.size(); ++tries)
                {
                    part.candidate = (part.candidate + 1) % part.candidates.size();
                    const juce::ScopedLock tl (tasteLock);
                    auto t = sampleTaste.find (part.candidates[part.candidate]);
                    if (t == sampleTaste.end() || t->second > 0) break;
                }
                part.samplePath = part.candidates[part.candidate];
                part.regionStart = 0; part.regionBars = 0;
                part.autoTune = part.candidate < part.candidateTunes.size() ? part.candidateTunes[part.candidate] : 0;
                const auto* entry = library.find (part.samplePath);
                const int bars = songBars (part.samplePath, entry != nullptr ? entry->bpm : song.bpm, song.bpm);
                if (bars > 0 && ! part.barSrc.empty())
                {
                    part.loopBars = bars;
                    recut (part, song);
                }
                else if (bars > 0)
                {
                    // keep the same placements, re-cut to the new loop's length
                    std::vector<int> starts;
                    for (auto& sec : song.sections)
                        for (int b : part.loopStartBars)
                            if (b == sec.startBar)
                                for (int x = sec.startBar; x + bars <= sec.startBar + sec.bars; x += bars) starts.push_back (x);
                    part.loopBars = bars;
                    if (! starts.empty()) part.loopStartBars = starts;
                }
                part.source = sourceLabel (part.samplePath) + juce::File (part.samplePath).getFileNameWithoutExtension();
                part.remix = remixText (part, 0.0, song) + arrangementText (part);
            }
            else if (part.kind == PK::Fx)
            {
                juce::StringArray packs;
                for (auto& other : song.parts)
                    if (const auto* e = library.find (other.samplePath)) packs.addIfNotAlreadyThere (e->pack);
                const int next = part.candidate + 1;
                part = makeTransitions (song, currentSeed + 1000u * (juce::uint32) next, packs, currentTarget.genreWords);
                part.candidate = next;
            }
            else if (part.kind == PK::Drums && ! song.drumSamples.empty() && part.candidates.size() > 1)
            {
                part.candidate = (part.candidate + 1) % part.candidates.size();
                refreshUsable();
                auto picks = lp::song::pickFromLibrary (usableLib, currentTarget, currentSeed, {}, part.candidates[part.candidate]);
                song.drumSamples = picks.drums;
                part.source = kitLabel (picks) + picks.drumPack + " kit";
            }
            else if (part.kind == PK::Drums)
            {
                part.variant = (part.variant + 1) % lp::song::kNumStyles; // another coded kit
                part.source = juce::String ("loopwright: ") + lp::song::styleName ((lp::song::Style) part.variant) + " kit";
            }
            else if (part.kind != PK::Bass)
            {
                part.sound = (lp::SoundId) (((int) part.sound + 1) % lp::kNumBuiltIns); // another instrument
                part.source = juce::String ("loopwright: ") + lp::profileFor (part.sound).name;
            }
        }
        juce::AudioBuffer<float> buf;
        if (! lp::song::renderPart (song, part, currentRs, buf, [this] { return threadShouldExit(); })) return;
        // a new file each time, so the one playing right now is never overwritten
        const auto stem = currentFolder.getChildFile ("stems").getChildFile (
            juce::String (tr + 1).paddedLeft ('0', 2) + " " + juce::String (part.name) + " (edit " + juce::String (++editCounter) + ").wav");
        if (! writeStem (stem, buf, currentRs.sampleRate)) return;
        const juce::ScopedLock sl (lock);
        current = song;
        status.song = song;
        status.tracks[(size_t) tr] = info (part, stem, song);
        if (op == EditOp::Notes && status.files.midiFile != juce::File())
            lp::song::writeMidi (song, status.files.midiFile); // "drag midi" gets your pattern too
        ++status.version;
        saveSession();
    }

    // everything needed to open this song again and keep editing it (called with `lock` held)
    void saveSession()
    {
        if (! currentFolder.isDirectory()) return;
        namespace F = lp::song::file;
        auto* o = new juce::DynamicObject();
        o->setProperty ("version", 1);
        o->setProperty ("song", F::songToVar (current));
        auto* r = new juce::DynamicObject();
        r->setProperty ("sampleRate", currentRs.sampleRate); r->setProperty ("nostalgia", currentRs.nostalgia);
        r->setProperty ("space", currentRs.space); r->setProperty ("warmth", currentRs.warmth); r->setProperty ("echo", currentRs.echo);
        o->setProperty ("render", r);
        auto* tg = new juce::DynamicObject();
        tg->setProperty ("bpm", currentTarget.bpm); tg->setProperty ("key", currentTarget.key); tg->setProperty ("minor", currentTarget.minor);
        tg->setProperty ("style", (int) currentTarget.style); tg->setProperty ("genreWords", F::strings (currentTarget.genreWords));
        const auto& w = currentTarget.wish;
        tg->setProperty ("wish", F::ints (std::vector<int> { w.dark, w.plucky, w.punchy, w.lofi, w.warm, w.airy }));
        o->setProperty ("target", tg);
        o->setProperty ("seed", (juce::int64) currentSeed);
        o->setProperty ("drumPacks", F::strings (currentPicksDrumPacks));
        auto* rf = new juce::DynamicObject();
        rf->setProperty ("valid", currentRef.valid); rf->setProperty ("hasStems", currentRef.hasStems);
        juce::Array<juce::var> bands;
        for (auto& b : currentRef.stemBands) bands.add (F::floats (b.data(), b.size()));
        rf->setProperty ("stemBands", bands);
        o->setProperty ("ref", rf);
        juce::StringArray stems;
        for (auto& t : status.tracks) stems.add (t.stem.getFullPathName());
        o->setProperty ("stems", F::strings (stems));
        o->setProperty ("midi", status.files.midiFile.getFullPathName());
        o->setProperty ("mix", status.files.mixFile.getFullPathName());
        o->setProperty ("plan", status.files.planFile.getFullPathName());
        o->setProperty ("ableton", status.abletonSet.getFullPathName());
        o->setProperty ("used", status.usedSamples);
        {
            juce::StringArray tk;
            for (auto& t : status.takes) tk.add (t.getFullPathName());
            o->setProperty ("takes", F::strings (tk));
        }
        currentFolder.getChildFile ("song.json").replaceWithText (juce::JSON::toString (juce::var (o)));
    }

    // job thread: bring a saved song back (no re-recording: its tracks are already on disk)
    void openSaved (const juce::File& folder)
    {
        namespace F = lp::song::file;
        const auto v = juce::JSON::parse (folder.getChildFile ("song.json"));
        lp::song::Song song;
        if (! F::songFromVar (v["song"], song)) return;
        const auto stems = F::readStrings (v["stems"]);
        if (stems.size() != (int) song.parts.size()) return;
        for (auto& p : stems) if (! juce::File (p).existsAsFile()) return; // moved or deleted: leave it
        lp::song::RenderSettings rs;
        rs.sampleRate = v["render"].getProperty ("sampleRate", 48000.0); rs.nostalgia = v["render"]["nostalgia"];
        rs.space = v["render"]["space"]; rs.warmth = v["render"]["warmth"]; rs.echo = v["render"]["echo"];
        lp::song::Target tg {};
        tg.bpm = v["target"]["bpm"]; tg.key = v["target"]["key"]; tg.minor = v["target"]["minor"];
        tg.style = (lp::song::Style) (int) v["target"]["style"]; tg.genreWords = F::readStrings (v["target"]["genreWords"]);
        const auto w = F::readInts (v["target"]["wish"]);
        if (w.size() == 6) tg.wish = { w[0], w[1], w[2], w[3], w[4], w[5] };
        lp::song::Profile ref;
        ref.valid = v["ref"]["valid"]; ref.hasStems = v["ref"]["hasStems"];
        if (auto* a = v["ref"]["stemBands"].getArray())
            for (size_t i = 0; i < ref.stemBands.size() && i < (size_t) a->size(); ++i) F::readFloats ((*a)[(int) i], ref.stemBands[i].data(), 10);
        lp::song::Rendered files;
        files.folder = folder;
        for (auto& p : stems) files.stems.add (juce::File (p));
        files.midiFile = juce::File (v["midi"].toString()); files.mixFile = juce::File (v["mix"].toString());
        files.planFile = juce::File (v["plan"].toString());
        files.matchEqDb = song.matchEqDb; files.matchGainDb = song.matchGainDb; files.playbackMakeupDb = song.playbackMakeupDb;
        refreshUsable();
        const juce::ScopedLock sl (lock);
        current = song;
        currentRs = rs;
        currentFolder = folder;
        currentPicksDrumPacks = F::readStrings (v["drumPacks"]);
        currentTarget = tg;
        currentRef = ref;
        currentSeed = (juce::uint32) (juce::int64) v["seed"];
        status.tracks.clear();
        for (int i = 0; i < (int) song.parts.size(); ++i)
            status.tracks.push_back (info (song.parts[(size_t) i], files.stems[i], song));
        ++status.version;
        status.song = song;
        status.usedSamples = v["used"].toString();
        status.takes.clear();
        for (auto& t : F::readStrings (v["takes"])) if (juce::File (t).isDirectory()) status.takes.add (juce::File (t));
        status.take = status.takes.indexOf (folder);
        status.files = files;
        status.abletonSet = juce::File (v["ableton"].toString());
        status.kindCounts.assign (lp::song::kNumLoopKinds, 0);
        for (int k = 1; k < lp::song::kNumLoopKinds; ++k)
            status.kindCounts[(size_t) k] = (int) usableLib.loopsOfKind ((lp::song::LoopKind) k, song.bpm).size();
        status.haveSong = true;
        status.stage = Stage::Done;
        status.progress = 1.0f;
        status.message = "\"" + song.title + "\" is back, as you left it";
    }

    static bool writeStem (const juce::File& file, const juce::AudioBuffer<float>& buf, double sr)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (file.createOutputStream().release());
        if (os == nullptr) return false;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
        if (w == nullptr) return false;
        os.release();
        return w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    }

    int editCounter = 0;

    // ---------- the producer: which loop plays where, and what happens to it
    enum class Role { Main, Second, Third, Fourth, Bass, DrumLoop, Perc, Pad, Vocal, Fx };

    static std::string baseName (const std::string& n) { const auto sp = n.find (' '); return sp == std::string::npos ? n : n.substr (0, sp); }
    static int sectionNumber (const std::string& n) { const auto sp = n.find (' '); return sp == std::string::npos ? 1 : std::atoi (n.c_str() + sp + 1); }

    static bool plays (Role r, const std::string& base, int n, bool lastChorus, bool kit)
    {
        switch (r)
        {
            case Role::Main:     return base == "intro" || base == "verse" || base == "pre-chorus" || base == "chorus" || base == "outro";
            case Role::Second:   return base == "chorus" || base == "bridge" || (base == "verse" && n >= 2);
            case Role::Third:    return (base == "chorus" && n >= 2) || (base == "pre-chorus" && n >= 2);
            case Role::Fourth:   return base == "verse" || base == "bridge";
            case Role::Bass:     return base == "verse" || base == "pre-chorus" || base == "chorus";
            case Role::DrumLoop: return kit ? (base == "chorus" || (base == "verse" && n >= 2)) : (base == "verse" || base == "pre-chorus" || base == "chorus");
            case Role::Perc:     return base == "pre-chorus" || base == "chorus";
            case Role::Pad:      return base == "intro" || base == "pre-chorus" || base == "bridge" || base == "outro" || lastChorus;
            case Role::Vocal:    return base == "chorus" || base == "bridge";
            case Role::Fx:       return false; // placed to land on each chorus instead
        }
        return false;
    }

    // the matching section of the reference (same kind, same turn), and which of its parts play there
    static std::array<bool, 4> refPresence (const lp::song::Profile& ref, const std::string& sectionName)
    {
        std::array<bool, 4> all { true, true, true, true };
        if (! ref.valid || ! ref.hasStems) return all;
        const auto base = baseName (sectionName);
        std::vector<const lp::song::RefSection*> same;
        for (auto& r : ref.sections) if (r.label == base) same.push_back (&r);
        if (same.empty()) return all;
        auto pr = same[(size_t) ((sectionNumber (sectionName) - 1) % (int) same.size())]->present;
        if (! pr[lp::song::StemMusic] && ! pr[lp::song::StemVocals] && ! pr[lp::song::StemDrums]) return all; // never silence a whole section
        return pr;
    }

    // how the reference balances its parts vs. a typical mix (bass ~3 dB over the drums, music ~2, the lead level with them)
    static std::array<float, 4> balanceFromRef (const lp::song::Profile& ref)
    {
        std::array<float, 4> a { 0, 0, 0, 0 };
        if (! ref.valid || ! ref.hasStems) return a;
        const float d = ref.stemDb[lp::song::StemDrums];
        a[lp::song::StemBass]  = juce::jlimit (-5.0f, 5.0f, (ref.stemDb[lp::song::StemBass] - d) - 3.0f);
        a[lp::song::StemMusic] = juce::jlimit (-5.0f, 5.0f, (ref.stemDb[lp::song::StemMusic] - d) - 2.0f);
        if (ref.vocalsHeard) a[lp::song::StemVocals] = juce::jlimit (-5.0f, 5.0f, ref.stemDb[lp::song::StemVocals] - d);
        return a;
    }

    static int stemFor (Role r)
    {
        switch (r)
        {
            case Role::Bass:     return lp::song::StemBass;
            case Role::DrumLoop: case Role::Perc: return lp::song::StemDrums;
            case Role::Vocal:    return lp::song::StemVocals;
            case Role::Fx:       return -1;
            default:             return lp::song::StemMusic;
        }
    }

    static void arrange (lp::song::Part& p, const lp::song::Song& song, Role role, bool kit, const lp::song::Profile& ref = {}, bool alone = false)
    {
        const int total = song.totalBars();
        p.barSrc.assign ((size_t) total, -1);
        p.barFilter.clear(); p.chopBars.clear(); p.stutterBars.clear(); p.gapBars.clear(); p.swellBars.clear();
        const int L = std::max (1, p.loopBars);
        int lastChorusIdx = -1;
        for (size_t i = 0; i < song.sections.size(); ++i) if (baseName (song.sections[i].name) == "chorus") lastChorusIdx = (int) i;
        for (size_t si = 0; si < song.sections.size(); ++si)
        {
            const auto& sec = song.sections[si];
            const auto base = baseName (sec.name);
            const int n = sectionNumber (sec.name);
            const bool lastChorus = (int) si == lastChorusIdx;
            const bool nextIsChorus = si + 1 < song.sections.size() && baseName (song.sections[si + 1].name) == "chorus";
            const bool nextIsPre = si + 1 < song.sections.size() && baseName (song.sections[si + 1].name) == "pre-chorus";
            const int s0 = sec.startBar, s1 = sec.startBar + sec.bars;
            if (role == Role::Fx)
            {
                // a riser that ends right on the chorus
                if (nextIsChorus)
                    for (int k = 0; k < L; ++k)
                        if (s1 - L + k >= 0 && s1 - L + k < total) p.barSrc[(size_t) (s1 - L + k)] = k;
                continue;
            }
            const auto present = refPresence (ref, sec.name);
            const int stem = stemFor (role);
            // the reference leaves this part out here: so do we (the lead stays if there's anything up front)
            const bool refSaysNo = stem >= 0 && ! present[(size_t) stem]
                                   && ! (role == Role::Main && (present[lp::song::StemMusic] || present[lp::song::StemVocals]));
            // its chords don't agree with the main loop's: it only plays where the main loop is silent
            const bool mainHere = plays (Role::Main, base, n, lastChorus, kit);
            if (plays (role, base, n, lastChorus, kit) && ! refSaysNo && ! (alone && mainHere))
            {
                for (int b = s0; b < s1 && b < total; ++b) p.barSrc[(size_t) b] = (b - s0) % L;
                // the build: the last beat before a chorus drops out
                if (nextIsChorus) p.gapBars.insert (s1 - 1);
                if (role == Role::Main)
                {
                    if (base == "intro")  // a muffled intro that opens up
                        for (int b = s0; b <= s1; ++b) p.barFilter[b] = b == s1 ? 1.0f : 0.12f + 0.88f * (float) (b - s0) / (float) sec.bars;
                    if (base == "outro")  // and closes down at the end
                        for (int b = s0; b <= s1; ++b) p.barFilter[b] = 1.0f - 0.85f * (float) (b - s0) / (float) sec.bars;
                    if (base == "verse" && nextIsPre) p.stutterBars.insert (s1 - 1);
                    if (base == "verse" && n >= 2 && sec.bars >= 8)  // verse 2: one chopped variation near its end
                        p.chopBars.insert (s1 - 2);
                    if (lastChorus && sec.bars >= 16)               // the last chorus: a variation half way
                        p.chopBars.insert (s0 + 7);
                }
                if (role == Role::Pad && base == "outro")
                    for (int b = s0; b <= s1; ++b) p.barFilter[b] = 1.0f - 0.8f * (float) (b - s0) / (float) sec.bars;
                if (role == Role::Second && base == "bridge") // the bridge: the second sound, muffled and far away
                    for (int b = s0; b <= s1; ++b) p.barFilter[b] = b == s1 ? 1.0f : 0.4f;
            }
            else if (nextIsChorus && (role == Role::Second || (role == Role::Pad)) && s1 - 1 >= 0)
                p.swellBars.insert (s1 - 1); // a reversed swell rising into the chorus
        }
        // gaps and swells only where the part has something to do
        for (auto it = p.gapBars.begin(); it != p.gapBars.end();)
            it = (*it >= 0 && *it < total && p.barSrc[(size_t) *it] >= 0) ? std::next (it) : p.gapBars.erase (it);
    }

    // ---------- which packs songs may use. WAVS packs start switched off (their licenses differ:
    // switch on the royalty-free ones); everything else starts on
public:
    struct PackRow { juce::String name; int source = 0; int count = 0; bool on = true; };

    std::vector<PackRow> packRows()
    {
        const juce::ScopedLock sl (lock);
        loadSources();
        std::map<std::pair<int, juce::String>, int> counts;
        for (auto& e : library.all()) ++counts[{ (int) lp::song::Library::sourceOf (e.path), e.pack }];
        std::vector<PackRow> rows;
        for (auto& [k, n] : counts) rows.push_back ({ k.second, k.first, n, packOn (k.first, k.second) });
        return rows;
    }

    void setPackOn (int source, const juce::String& pack, bool on)
    {
        const juce::ScopedLock sl (lock);
        loadSources();
        const auto id = juce::String (source) + "|" + pack;
        packsOn.removeString (id); packsOff.removeString (id);
        (on ? packsOn : packsOff).add (id);
        auto obj = std::make_unique<juce::DynamicObject>();
        juce::Array<juce::var> a, b;
        for (auto& x : packsOn) a.add (x);
        for (auto& x : packsOff) b.add (x);
        obj->setProperty ("on", a);
        obj->setProperty ("off", b);
        sourcesFile().replaceWithText (juce::JSON::toString (juce::var (obj.release()), true));
    }

private:
    static juce::File sourcesFile() { return lp::appFolder().getChildFile ("sources.json"); }
    juce::StringArray packsOn, packsOff;
    bool sourcesLoaded = false;
    lp::song::Library usableLib;

    void loadSources()
    {
        if (sourcesLoaded) return;
        sourcesLoaded = true;
        const auto v = juce::JSON::parse (sourcesFile());
        if (auto* a = v["on"].getArray()) for (auto& x : *a) packsOn.add (x.toString());
        if (auto* a = v["off"].getArray()) for (auto& x : *a) packsOff.add (x.toString());
    }

    bool packOn (int source, const juce::String& pack) const
    {
        const auto id = juce::String (source) + "|" + pack;
        if (packsOff.contains (id)) return false;
        if (packsOn.contains (id)) return true;
        return source != (int) lp::song::Library::Source::Wavs;
    }

    void refreshUsable()
    {
        const juce::ScopedLock sl (lock);
        loadSources();
        usableLib = library.onlyWhere ([this] (const lp::song::LibraryEntry& e) { return packOn ((int) lp::song::Library::sourceOf (e.path), e.pack); });
    }

    // ---------- transitions: FX from your library at the right spots
    lp::song::Part makeTransitions (const lp::song::Song& song, juce::uint32 seed, const juce::StringArray& preferPacks,
                                    const juce::StringArray& genreWords)
    {
        using SK = lp::song::SampleKind;
        lp::song::Part p;
        p.name = "transitions";
        p.kind = lp::song::PartKind::Fx;
        juce::Random rng ((juce::int64) seed * 97 + 11);
        // a couple of choices per kind, best fits first (same packs as the song, its genre), so it varies
        auto choose = [&] (SK k, int howMany)
        {
            std::vector<std::pair<float, const lp::song::LibraryEntry*>> pool;
            for (auto* e : usableLib.fxOfKind (k))
            {
                float sc = rng.nextFloat() * 2.0f + (preferPacks.contains (e->pack) ? 3.0f : 0.0f);
                for (auto& w : genreWords) if (e->path.containsIgnoreCase (w)) { sc += 2.0f; break; }
                pool.push_back ({ sc, e });
            }
            std::sort (pool.begin(), pool.end(), [] (auto& a, auto& b) { return a.first > b.first; });
            juce::StringArray out;
            for (size_t i = 0; i < pool.size() && out.size() < howMany; ++i) out.add (pool[i].second->path);
            return out;
        };
        const auto risers = choose (SK::Riser, 2), reverses = choose (SK::ReverseFx, 2), impacts = choose (SK::Impact, 2),
                   crashes = choose (SK::Crash, 1), downs = choose (SK::Downlifter, 2), subs = choose (SK::SubDrop, 1);
        auto pick = [] (const juce::StringArray& a, int i) { return a.isEmpty() ? juce::String() : a[i % a.size()]; };
        auto add = [&] (const juce::String& path, double beat, bool peak, float db)
        {
            if (path.isNotEmpty() && beat >= 0) p.fxHits.push_back ({ path, beat, peak, db });
        };
        int chorusNo = 0, chorusCount = 0;
        for (auto& sec : song.sections) chorusCount += baseName (sec.name) == "chorus";
        for (size_t si = 0; si < song.sections.size(); ++si)
        {
            const auto& sec = song.sections[si];
            const auto base = baseName (sec.name);
            const double at = sec.startBar * 4.0;
            if (base == "chorus")
            {
                const bool last = ++chorusNo == chorusCount;
                // into it: a riser (or a reverse swell, taking turns); the last chorus gets both
                const bool useReverse = ! reverses.isEmpty() && (chorusNo % 2 == 0 || risers.isEmpty());
                // (levels: how loud each one's loudest moment is, under the music)
                if (useReverse) add (pick (reverses, chorusNo), at, true, -30.0f);
                else            add (pick (risers, chorusNo), at, true, -29.0f);
                if (last && ! useReverse) add (pick (reverses, chorusNo + 1), at, true, -33.0f);
                // on the "one": impact, crash, sub drop
                add (pick (impacts, chorusNo), at, false, -27.0f);
                add (pick (crashes, 0), at, false, -32.0f);
                if (last || chorusNo == 1) add (pick (subs, 0), at, false, -28.0f);
            }
            else if (base == "bridge" || base == "outro" || (base == "verse" && sectionNumber (sec.name) >= 2))
                add (pick (downs, (int) si), at, false, -33.0f); // the energy drops: a downlifter
            else if (base == "verse" && si > 0)
                add (! reverses.isEmpty() ? pick (reverses, 0) : pick (crashes, 0), at, ! reverses.isEmpty(), -34.0f); // a small lift out of the intro
        }
        int n = (int) p.fxHits.size();
        p.source = n > 0 ? "library: " + juce::String (n) + " transition fx" : juce::String();
        juce::StringArray kinds;
        if (! risers.isEmpty()) kinds.add ("risers landing on each chorus");
        if (! reverses.isEmpty()) kinds.add ("reverse swells");
        if (! impacts.isEmpty() || ! crashes.isEmpty()) kinds.add ("impacts / crashes on the downbeat");
        if (! subs.isEmpty()) kinds.add ("sub drops");
        if (! downs.isEmpty()) kinds.add ("downlifters where the energy drops");
        p.remix = "placed by Loopwright: " + kinds.joinIntoString (", ") + ". swap for a different set";
        p.candidate = 0;
        return p;
    }

    // after a swap: the same plan, re-cut to the new loop's length
    static void recut (lp::song::Part& p, const lp::song::Song& song)
    {
        const int L = std::max (1, p.loopBars);
        for (auto& sec : song.sections)
            for (int b = sec.startBar; b < sec.startBar + sec.bars && b < (int) p.barSrc.size(); ++b)
                if (p.barSrc[(size_t) b] >= 0) p.barSrc[(size_t) b] = (b - sec.startBar) % L;
    }

    static juce::String arrangementText (const lp::song::Part& p)
    {
        juce::StringArray t;
        if (! p.barFilter.empty())   t.add ("filter sweeps");
        if (! p.chopBars.empty())    t.add ("chopped variations");
        if (! p.stutterBars.empty()) t.add ("stutters");
        if (! p.swellBars.empty())   t.add ("reversed swells into the chorus");
        if (! p.gapBars.empty())     t.add ("drop-outs before the chorus");
        return t.isEmpty() ? juce::String() : ". produced: " + t.joinIntoString (", ");
    }

    // a loop's length in the SONG's bars: whole bars at its own tempo, at the nearest of half / normal / double speed
    static int songBars (const juce::String& path, double fileBpm, double songBpm)
    {
        const int fileBars = loopBars (path, fileBpm);
        if (fileBars <= 0 || fileBpm <= 0) return 0;
        double m = 1.0;
        for (double mm : { 0.5, 1.0, 2.0 }) if (std::abs (fileBpm * mm - songBpm) < std::abs (fileBpm * m - songBpm)) m = mm;
        const double bars = fileBars * m;
        return bars >= 1.0 && std::abs (bars - std::round (bars)) < 1e-6 ? (int) bars : 0;
    }

    // how many whole bars a loop lasts at a tempo (0 if it isn't whole bars, within 1.5%)
    static int loopBars (const juce::String& path, double bpm)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File (path)));
        if (rd == nullptr || rd->sampleRate <= 0) return 0;
        const double bars = (double) rd->lengthInSamples / rd->sampleRate * bpm / 240.0;
        for (int b : { 1, 2, 4, 8, 16 })
            if (std::abs (bars - b) / b < 0.015) return b;
        return 0;
    }

public:
    // the newest finished song, for playing along with the DAW (taken once by the processor)
    std::shared_ptr<juce::AudioBuffer<float>> takeFinishedMix (double& bpm)
    {
        const juce::ScopedLock sl (lock);
        bpm = finishedBpm;
        return std::move (finishedMix);
    }

private:
    std::shared_ptr<juce::AudioBuffer<float>> finishedMix;
    double finishedBpm = 90.0;
    juce::CriticalSection lock;
    Status status;
    lp::song::Library library;
    juce::Array<juce::File> scanFolders;
    bool pendingScan = false, useSamples = true, samplesOnly = false;
    lp::song::Song current;
    lp::song::RenderSettings currentRs;
    juce::File currentFolder;
    juce::StringArray currentPicksDrumPacks;
    lp::song::Target currentTarget {};
    lp::song::Profile currentRef;
    juce::uint32 currentSeed = 1;
    std::vector<std::pair<int, EditOp>> edits;
    std::map<int, std::pair<int, int>> pendingRegion;
    std::map<int, GrainSettings> pendingGrains;
    std::map<int, std::vector<lp::song::Note>> pendingNotes;
    std::map<int, std::pair<int, int>> pendingDrumSwap;
    int chordsVersion = -1;
    std::pair<int, int> pendingFromTake { -1, -1 };
    std::vector<lp::song::arrange::Item> pendingPlan;
    float pendingFeel = 1.0f;
    juce::File pendingOwnVocal;
    juce::String chordsSummary;
    juce::String drumSwapName; // the drum sound just swapped in (for the editor to show)
    int drumSwapCount = 0;
    Task task = Task::None;
    juce::File pendingReference;
    lp::song::Options options;
    lp::song::RenderSettings settings;
    lp::Weather weather;
};
