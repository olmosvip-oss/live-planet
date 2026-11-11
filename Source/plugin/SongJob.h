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

    enum class EditOp { Swap, Reverse, PitchUp, PitchDown, Add }; // Add: the "track" is the LoopKind to add

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
        s.kindCounts = status.kindCounts;
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
        // producers often put the tempo and key in the name ("todavia-140-cm"): trust that over listening,
        // and measure the groove and sections on that tempo's grid
        const auto hint = lp::song::Library::parse (file, file.getParentDirectory());
        const double knownBpm = hint.bpm >= 60.0 && hint.bpm <= 200.0 ? hint.bpm : 0.0;
        auto profile = lp::song::analyse (mono.data(), len, reader->sampleRate, knownBpm);
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
        if (useLib || onlySamples)
        {
            setStage (Stage::Writing, 0.03f, "looking through your samples...");
            picks = lp::song::pickFromLibrary (library, target, o.seed, [] (const juce::String& path, double bpm) { return loopBars (path, bpm); },
                                               {}, [this, &target] (const juce::String& path) { return tuneFor (path, target.key, target.minor); },
                                               [this, &ref] (const juce::String& path) { return soundsLike (path, ref.tone); });
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
        song.drumSamples = picks.drums;
        song.refTone = ref.valid ? ref.tone : lp::song::Tone {};

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
            loop.source = "splice: " + juce::File (loop.samplePath).getFileNameWithoutExtension();
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
            const lp::song::LoopChoice *drumC = nullptr, *percC = nullptr, *bassC = nullptr, *vocalC = nullptr, *padC = nullptr, *fxC = nullptr;
            std::vector<const lp::song::LoopChoice*> melodic;
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
            juce::Random mix ((juce::int64) o.seed * 13 + 5);
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
            if (drumC != nullptr)
            {
                if (kit) addLoop (*drumC, "drum loop", { "chorus", "bridge" });                      // layered on the kit
                else     addLoop (*drumC, "drum loop", { "verse", "pre-chorus", "chorus", "bridge" });
            }
            if (percC != nullptr)  addLoop (*percC, "percussion", { "pre-chorus", "chorus" });
            if (bassC != nullptr)  addLoop (*bassC, "bass loop", { "verse", "pre-chorus", "chorus" });
            if (melodic.size() > 0) addLoop (*melodic[0], lp::song::loopKindName (melodic[0]->kind), { "intro", "verse", "pre-chorus", "chorus", "outro" });
            if (melodic.size() > 1) addLoop (*melodic[1], lp::song::loopKindName (melodic[1]->kind), { "chorus", "bridge" });
            if (melodic.size() > 2) addLoop (*melodic[2], lp::song::loopKindName (melodic[2]->kind), { "pre-chorus", "chorus", "outro" });
            if (melodic.size() > 3 && wanted > 3) addLoop (*melodic[3], lp::song::loopKindName (melodic[3]->kind), { "verse", "bridge" });
            if (padC != nullptr)   addLoop (*padC, "pad", { "intro", "bridge", "outro" });
            if (vocalC != nullptr) addLoop (*vocalC, "vocal", { "chorus", "bridge" });
            if (fxC != nullptr)    addLoop (*fxC, "fx", { "pre-chorus" });
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

        song.matchEqDb = files.matchEqDb;  // edited tracks later get the same tone and level
        song.matchGainDb = files.matchGainDb;
        song.mixShaped = true;
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
        status.kindCounts.assign (lp::song::kNumLoopKinds, 0);
        for (int k = 1; k < lp::song::kNumLoopKinds; ++k)
            status.kindCounts[(size_t) k] = (int) library.loopsOfKind ((lp::song::LoopKind) k, song.bpm).size();
        status.haveSong = true;
        status.stage = Stage::Done;
        status.progress = 1.0f;
        status.message = "\"" + song.title + "\" is ready. locators: drag the locators button onto any track in ableton";
        if (o.dawBpm > 0.0 && std::abs (song.bpm - o.dawBpm) > 0.5)
            status.message = "\"" + song.title + "\" is at " + juce::String (song.bpm, 0) + " bpm: set your project to " + juce::String (song.bpm, 0)
                           + " (the locators helper does it for you)";
        if (noteForYou.isNotEmpty()) status.message << ". " << noteForYou;
    }

    // semitones to put a sample in the song's key (99 = it has no clear key). Listens once per file.
    struct Heard { lp::song::KeyReading reading; bool clear = false; int key = -1; bool minor = false; lp::song::Tone tone; };

    // how much a sample sounds like your reference (0..1)
    float soundsLike (const juce::String& path, const lp::song::Tone& ref)
    {
        if (! ref.valid) return 0.5f;
        tuneFor (path, 0, false); // listens once (key and sound)
        auto it = keyCache.find (path);
        return it != keyCache.end() ? lp::song::toneLikeness (it->second.tone, ref) : 0.5f;
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
                h.tone = lp::song::measureTone (mono.data(), len, rd->sampleRate);
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
        const juce::String was = e != nullptr && e->key >= 0 ? juce::String (" (it was ") + lp::noteName (e->key) + (e->minor ? " minor)" : " major)") : juce::String();
        if (loop.autoTune != 0)
            t << ", tuned " << (loop.autoTune > 0 ? "+" : "") << loop.autoTune << " st into " << songKey << was;
        else
            t << ", already in " << songKey;
        t << ", arranged into this song's sections";
        return t;
    }

    std::map<juce::String, Heard> keyCache;

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
        t.sample = p.source.startsWith ("splice");
        const juce::String n (p.name);
        if (p.kind == lp::song::PartKind::Drums || n.contains ("drum") || n == "percussion") t.group = 4;
        else if (p.kind == lp::song::PartKind::Bass || n.contains ("bass"))                t.group = 3;
        else if (n == "keys" || n == "guitar" || n == "pad" || n == "strings" || n == "loop" || p.kind == lp::song::PartKind::Chords || p.kind == lp::song::PartKind::Pad)
            t.group = 1;
        else t.group = 2;
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
        for (auto* e : library.loopsOfKind (kind, song.bpm))
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
            found.push_back ({ e, b, tune, soundsLike (e->path, song.refTone) });
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
        part.source = "splice: " + juce::File (part.samplePath).getFileNameWithoutExtension();
        part.remix = remixText (part, 0.0, song) + " (added by you)";
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
        status.tracks.push_back (trackInfo (part, stem));
        status.message = "added " + juce::String (part.name) + ": " + juce::File (part.samplePath).getFileNameWithoutExtension();
        ++status.version;
    }

    void applyEdit (int tr, EditOp op)
    {
        if (op == EditOp::Add) { addInstrument ((lp::song::LoopKind) tr); return; }
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
                part.autoTune = part.candidate < part.candidateTunes.size() ? part.candidateTunes[part.candidate] : 0;
                const auto* entry = library.find (part.samplePath);
                const int bars = songBars (part.samplePath, entry != nullptr ? entry->bpm : song.bpm, song.bpm);
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
                part.remix = remixText (part, 0.0, song);
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
    juce::uint32 currentSeed = 1;
    std::vector<std::pair<int, EditOp>> edits;
    Task task = Task::None;
    juce::File pendingReference;
    lp::song::Options options;
    lp::song::RenderSettings settings;
    lp::Weather weather;
};
