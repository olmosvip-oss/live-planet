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

    void makeSong (const lp::song::Options& o, const lp::song::RenderSettings& rs, const lp::Weather& sky, bool useLibrary)
    {
        {
            const juce::ScopedLock sl (lock);
            useSamples = useLibrary;
            options = o;
            settings = rs;
            weather = sky;
            task = Task::Write;
        }
        startIfNeeded();
    }

    // everything except the notes themselves: cheap enough to call while drawing
    Status getStatus() const
    {
        const juce::ScopedLock sl (lock);
        Status s;
        s.scanning = status.scanning;
        s.librarySounds = status.librarySounds;
        s.libraryLoops = status.libraryLoops;
        s.usedSamples = status.usedSamples;
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
        bool useLib;
        {
            const juce::ScopedLock sl (lock);
            useLib = useSamples && library.size() > 0;
        }
        auto opts = o;
        lp::song::LibraryPicks picks;
        juce::String used;
        if (useLib)
        {
            setStage (Stage::Writing, 0.03f, "looking through your samples...");
            const auto target = lp::song::chooseTarget (ref, sky, o);
            picks = lp::song::pickFromLibrary (library, target, o.seed, [] (const juce::String& path, double bpm) { return loopBars (path, bpm); });
            if (picks.loop != nullptr)
            {
                // the loop sets the exact tempo (at the nearest of half / normal / double speed)
                double best = picks.loop->bpm, bestDiff = 1e9;
                for (double m : { 0.5, 1.0, 2.0 })
                    if (std::abs (picks.loop->bpm * m - target.bpm) < bestDiff) { bestDiff = std::abs (picks.loop->bpm * m - target.bpm); best = picks.loop->bpm * m; }
                opts.forceBpm = best;
                opts.forceKey = target.key;          // same key or its relative: the notes match either way
                opts.forceMinor = target.minor;
                used << "loop: " << juce::File (picks.loop->path).getFileNameWithoutExtension();
            }
            if (! picks.drums.empty())
                used << (used.isEmpty() ? "" : "  -  ") << "drums: " << picks.drumPack;
        }
        auto song = lp::song::generate (ref, sky, opts);
        song.drumSamples = picks.drums;
        if (picks.loop != nullptr)
        {
            lp::song::Part loop;
            loop.name = "loop";
            loop.kind = lp::song::PartKind::Loop;
            loop.samplePath = picks.loop->path;
            loop.loopBars = loopBars (picks.loop->path, song.bpm);
            if (loop.loopBars > 0)
            {
                for (auto& sec : song.sections)
                    if (sec.name.rfind ("chorus", 0) == 0 || sec.name.rfind ("bridge", 0) == 0)
                        for (int b = sec.startBar; b + loop.loopBars <= sec.startBar + sec.bars; b += loop.loopBars)
                            loop.loopStartBars.push_back (b);
                if (! loop.loopStartBars.empty()) song.parts.push_back (loop);
            }
        }

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
    bool pendingScan = false, useSamples = true;
    Task task = Task::None;
    juce::File pendingReference;
    lp::song::Options options;
    lp::song::RenderSettings settings;
    lp::Weather weather;
};
