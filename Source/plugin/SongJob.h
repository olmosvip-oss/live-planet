#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "../song/Song.h"
#include "../song/Library.h"

// Runs song mode in the background so the music (and your DAW) never stutters:
// listening to a reference song, then writing and rendering new songs.
class SongJob : private juce::Thread
{
public:
    enum class Stage { Idle, Listening, Writing, Rendering, Done, Failed };

    enum class EditOp { Swap, Reverse, PitchUp, PitchDown };

    struct TrackInfo
    {
        juce::String name, source, remix;
        juce::File stem;
        bool canReverse = false, canSwap = false;
        int pitch = 0;
        bool reversed = false;
        int candidates = 0, candidate = 0;
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
        bool editing = false;
    };

    SongJob() : juce::Thread ("Live Planet song mode", 16 * 1024 * 1024) { startThread (juce::Thread::Priority::low); } // loads your sample library right away
    ~SongJob() override { signalThreadShouldExit(); notify(); stopThread (20000); }

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
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Live Planet").getChildFile ("library.json");
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

    Status getStatus() const
    {
        const juce::ScopedLock sl (lock);
        Status s;
        s.scanning = status.scanning;
        s.librarySounds = status.librarySounds;
        s.libraryLoops = status.libraryLoops;
        s.usedSamples = status.usedSamples;
        s.tracks = status.tracks;
        s.version = status.version;
        s.editing = status.editing;
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
        for (auto& sec : status.profile.sections)
            s.profile.sections.push_back ({ sec.label, sec.startBar, sec.bars, sec.energy, {}, {} });
        s.haveSong = status.haveSong;
        s.song.title = status.song.title;
        s.song.bpm = status.song.bpm;
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
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Live Planet").getChildFile ("Songs");
    }

    // the Ableton helper device reads this to find the newest song
    static juce::File latestPointer()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Live Planet").getChildFile ("latest-song.json");
    }

    std::function<juce::File (const lp::song::Song&, const lp::song::Rendered&)> writeAbletonSet; // set by the processor

private:
    enum class Task { None, Listen, Write };

    void startIfNeeded()
    {
        if (! isThreadRunning()) startThread (juce::Thread::Priority::low);
        notify();
    }

    void setStage (Stage s, float p, const juce::String& msg)
    {
        const juce::ScopedLock sl (lock);
        status.stage = s;
        status.progress = p;
        status.message = msg;
    }

    void run() override
    {
        // start with the library found last time, so songs can use it straight away
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
                else if (t == Task::Write) write (o, rs, sky);
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
        std::vector<float> mono ((size_t) len);
        for (int i = 0; i < len; ++i)
        {
            float v = 0;
            for (int ch = 0; ch < buf.getNumChannels(); ++ch) v += buf.getSample (ch, i);
            mono[(size_t) i] = v / (float) buf.getNumChannels();
        }
        setStage (Stage::Listening, 0.5f, "finding tempo, key and sections...");
        auto profile = lp::song::analyse (mono.data(), len, reader->sampleRate);
        profile.sourceName = file.getFileNameWithoutExtension();
        // producers often put the tempo and key in the name ("todavia-140-cm"): trust that over listening
        const auto hint = lp::song::Library::parse (file, file.getParentDirectory());
        if (profile.valid && hint.bpm >= 60.0 && hint.bpm <= 200.0) profile.bpm = hint.bpm;
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
        lp::song::LibraryPicks picks;
        juce::String used;
        const auto target = lp::song::chooseTarget (ref, sky, o);
        if (useLib || onlySamples)
        {
            setStage (Stage::Writing, 0.03f, "looking through your samples...");
            picks = lp::song::pickFromLibrary (library, target, o.seed, [] (const juce::String& path, double bpm) { return loopBars (path, bpm); });
            if (picks.loop != nullptr)
            {
                opts.forceBpm = picks.loopBpm > 0 ? picks.loopBpm : picks.loop->bpm;
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
        song.drumSamples = picks.drums;

        // where each sound came from, and how it was changed, for the studio
        for (auto& part : song.parts)
        {
            using PK = lp::song::PartKind;
            if (part.kind == PK::Drums)
            {
                part.source = song.drumSamples.empty() ? juce::String ("live planet: ") + lp::song::styleName (song.style) + " kit"
                                                       : "splice: " + picks.drumPack + " kit";
                part.remix = song.drumSamples.empty() ? "a new groove, played on coded drums"
                                                      : juce::String ("one-shots re-sequenced into a new ") + lp::song::styleName (song.style) + " groove";
                part.candidates = picks.drumPacks;
            }
            else if (part.kind == PK::Bass)
            {
                part.source = juce::String ("live planet: ") + (part.variant == 1 ? "808" : "bass");
                part.remix = "new bass line on the song's chords";
            }
            else
            {
                part.source = juce::String ("live planet: ") + lp::profileFor (part.sound).name;
                part.remix = "new notes written for this song";
            }
        }

        // loops from your library, placed by section
        auto addLoop = [&] (const lp::song::LoopChoice& choice, const juce::String& name, std::initializer_list<const char*> sections)
        {
            lp::song::Part loop;
            loop.name = name.toStdString();
            loop.kind = lp::song::PartKind::Loop;
            for (auto& opt : choice.options) loop.candidates.add (opt.first->path);
            loop.samplePath = loop.candidates[0];
            loop.loopBars = loopBars (loop.samplePath, song.bpm);
            if (loop.loopBars <= 0) return;
            for (auto& sec : song.sections)
                for (auto want : sections)
                    if (sec.name.rfind (want, 0) == 0)
                        for (int b = sec.startBar; b + loop.loopBars <= sec.startBar + sec.bars; b += loop.loopBars)
                            loop.loopStartBars.push_back (b);
            if (loop.loopStartBars.empty()) return;
            const double fileBpm = choice.options[0].first->bpm;
            double m = 1.0;
            for (double mm : { 0.5, 1.0, 2.0 }) if (std::abs (fileBpm * mm - song.bpm) < std::abs (fileBpm * m - song.bpm)) m = mm;
            const double fit = (song.bpm / (fileBpm * m) - 1.0) * 100.0;
            loop.source = "splice: " + juce::File (loop.samplePath).getFileNameWithoutExtension();
            loop.remix = juce::String ("plays as recorded") + (std::abs (fit) > 0.05 ? ", fitted to " + juce::String (juce::roundToInt (song.bpm)) + " bpm (" + (fit > 0 ? "+" : "") + juce::String (fit, 1) + "%)" : "")
                       + ", arranged into this song's sections";
            song.parts.push_back (loop);
        };

        if (onlySamples)
        {
            // samples only: no coded instruments, the song is built from your loops and one-shots
            song.parts.erase (std::remove_if (song.parts.begin(), song.parts.end(), [&] (const lp::song::Part& p2)
                { return p2.kind != lp::song::PartKind::Drums || song.drumSamples.empty(); }), song.parts.end());
            int melodic = 0;
            for (auto& choice : picks.loopSet)
            {
                using LK = lp::song::LoopKind;
                if (choice.kind == LK::Drums)        { if (song.drumSamples.empty()) addLoop (choice, "drum loop", { "verse", "pre-chorus", "chorus", "bridge" }); }
                else if (choice.kind == LK::Bass)    addLoop (choice, "bass loop", { "verse", "chorus" });
                else if (choice.kind == LK::Vocal)   addLoop (choice, "vocal", { "chorus", "bridge" });
                else if (melodic == 0)               { addLoop (choice, lp::song::loopKindName (choice.kind), { "intro", "verse", "pre-chorus", "chorus", "outro" }); ++melodic; }
                else if (melodic == 1)               { addLoop (choice, lp::song::loopKindName (choice.kind), { "chorus", "bridge" }); ++melodic; }
            }
        }
        else if (picks.loop != nullptr && ! picks.loopSet.empty())
            addLoop (picks.loopSet.front(), "loop", { "chorus", "bridge" });

        for (auto& part : song.parts)
            if (part.kind == lp::song::PartKind::Loop || (part.kind == lp::song::PartKind::Drums && ! song.drumSamples.empty()))
                used << (used.isEmpty() ? "" : "  -  ") << juce::String (part.name) << ": " << part.source.fromFirstOccurrenceOf (": ", false, false);

        auto folder = songsFolder().getChildFile (juce::File::createLegalFileName (song.title));
        for (int n = 2; folder.exists(); ++n)
            folder = songsFolder().getChildFile (juce::File::createLegalFileName (song.title + " " + juce::String (n)));

        setStage (Stage::Rendering, 0.05f, "recording \"" + song.title + "\"...");
        lp::song::Rendered files;
        const bool ok = lp::song::render (song, rs, folder, files,
                                          [this] (float p) { const juce::ScopedLock sl (lock); status.progress = 0.05f + 0.9f * p; },
                                          [this] { return threadShouldExit(); });
        if (! ok)
        {
            setStage (Stage::Failed, 0.0f, "couldn't save the song. is your Music folder writable?");
            return;
        }
        juce::File als;
        if (writeAbletonSet) als = writeAbletonSet (song, files);
        latestPointer().getParentDirectory().createDirectory();
        latestPointer().replaceWithText (files.planFile.loadFileAsString());

        const juce::ScopedLock sl (lock);
        current = song;
        currentRs = rs;
        currentFolder = folder;
        currentPicksDrumPacks = picks.drumPacks;
        currentTarget = target;
        currentSeed = o.seed;
        status.tracks.clear();
        for (int i = 0; i < (int) song.parts.size(); ++i)
            status.tracks.push_back (trackInfo (song.parts[(size_t) i], files.stems[i]));
        ++status.version;
        status.song = song;
        status.usedSamples = used;
        finishedMix = files.mixAudio;
        finishedBpm = song.bpm;
        files.mixAudio.reset();
        status.files = files;  // (without the audio itself)
        status.abletonSet = als;
        status.haveSong = true;
        status.stage = Stage::Done;
        status.progress = 1.0f;
        status.message = "\"" + song.title + "\" is ready";
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
        t.canReverse = p.kind == lp::song::PartKind::Loop || (p.kind == lp::song::PartKind::Drums && p.source.startsWith ("splice"));
        t.canSwap = true;
        t.candidates = p.candidates.size();
        t.candidate = p.candidate;
        return t;
    }

    void applyEdit (int tr, EditOp op)
    {
        lp::song::Song song;
        {
            const juce::ScopedLock sl (lock);
            if (tr < 0 || tr >= (int) current.parts.size()) return;
            song = current;
        }
        auto& part = song.parts[(size_t) tr];
        using PK = lp::song::PartKind;
        if (op == EditOp::Reverse) part.reversed = ! part.reversed;
        if (op == EditOp::PitchUp) part.pitchSemis = std::min (12, part.pitchSemis + 1);
        if (op == EditOp::PitchDown) part.pitchSemis = std::max (-12, part.pitchSemis - 1);
        if (op == EditOp::Swap)
        {
            if (part.kind == PK::Loop && part.candidates.size() > 1)
            {
                part.candidate = (part.candidate + 1) % part.candidates.size();
                part.samplePath = part.candidates[part.candidate];
                const int bars = loopBars (part.samplePath, song.bpm);
                if (bars > 0)
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
                part.source = "splice: " + juce::File (part.samplePath).getFileNameWithoutExtension();
            }
            else if (part.kind == PK::Drums && ! song.drumSamples.empty() && part.candidates.size() > 1)
            {
                part.candidate = (part.candidate + 1) % part.candidates.size();
                auto picks = lp::song::pickFromLibrary (library, currentTarget, currentSeed, {}, part.candidates[part.candidate]);
                song.drumSamples = picks.drums;
                part.source = "splice: " + picks.drumPack + " kit";
            }
            else if (part.kind == PK::Drums)
            {
                part.variant = (part.variant + 1) % lp::song::kNumStyles; // another coded kit
                part.source = juce::String ("live planet: ") + lp::song::styleName ((lp::song::Style) part.variant) + " kit";
            }
            else if (part.kind != PK::Bass)
            {
                part.sound = (lp::SoundId) (((int) part.sound + 1) % lp::kNumBuiltIns); // another instrument
                part.source = juce::String ("live planet: ") + lp::profileFor (part.sound).name;
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
        status.tracks[(size_t) tr] = trackInfo (part, stem);
        ++status.version;
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
    juce::uint32 currentSeed = 1;
    std::vector<std::pair<int, EditOp>> edits;
    Task task = Task::None;
    juce::File pendingReference;
    lp::song::Options options;
    lp::song::RenderSettings settings;
    lp::Weather weather;
};
