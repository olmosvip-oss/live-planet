#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <deque>
#include <map>
#include <mutex>
#include <vector>

// The Voice page's link to the local voice helper: a separate app on this computer that runs the singing
// models (nothing of them is inside the plugin). Everything here happens on its own background thread,
// never on the audio thread; when the helper isn't running, it just says so.
//
// The helper writes where it listens (port, a per-run token) to a small file only you can read:
//   macOS:  ~/Library/Application Support/Loopwright/voice-helper.json
//   other:  <app data>/Loopwright/voice-helper.json      (LP_VOICE_HELPER_FILE overrides, for tests)
namespace lp::voice
{
struct Result { int n = 0; juce::String vocal, mix, heard; int words = 0, key = 0; };
struct Job
{
    juce::String id, state, stage, note, title, error;
    double created = 0.0;
    bool clean = true;
    std::vector<Result> results;
    bool live() const { return state == "queued" || state == "running"; }
};
struct Snapshot
{
    bool connected = false;          // the helper answered just now
    juce::String problem;            // why not, in plain words
    juce::StringArray voices, tracks;
    std::vector<std::pair<juce::String, juce::String>> stages; // (key, what it says)
    std::vector<Job> jobs;           // newest first
    juce::String message;            // the last thing that happened (sent, failed...)
    bool messageBad = false;
    bool sending = false;            // a song section or a voice is on its way to the helper
    std::map<juce::String, juce::File> files; // "jobid/n" -> the vocal, here, once fetched
    std::map<juce::String, juce::String> tags; // job id -> the tag of the request that started it
    int serial = 0;                  // goes up with every change
};

// what the helper's answers may contain (they're checked, never trusted)
inline bool validJobId (const juce::String& s)
{
    return s.length() == 12 && s.containsOnly ("0123456789abcdef");
}

class Client : private juce::Thread
{
public:
    Client() : juce::Thread ("Loopwright voice") {}
    ~Client() override { stopThread (8000); }

    static juce::File connectionFile()
    {
        if (auto* p = std::getenv ("LP_VOICE_HELPER_FILE")) return juce::File (juce::String::fromUTF8 (p));
       #if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Application Support/Loopwright/voice-helper.json");
       #else
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Loopwright/voice-helper.json");
       #endif
    }

    // the page shows (or a song is being sung): keep up with the helper. Starts the thread the first time.
    void setWanted (bool w)
    {
        wanted = w;
        if (w && ! isThreadRunning()) startThread (juce::Thread::Priority::low);
        if (w) notify();
    }

    Snapshot snapshot() const { const std::lock_guard<std::mutex> l (lock); return snap; }
    int serial() const { const std::lock_guard<std::mutex> l (lock); return snap.serial; }

    // ---- what you ask for (each runs in the background, in order)
    struct Request
    {
        juce::String lyrics, style, voice, key;
        double bpm = 120.0;
        int takes = 4, results = 2, runs = 3;
        float tune = 0.95f;
        bool clean = true, consent = false;
        std::function<juce::File()> renderTrack; // makes the song section's WAV (called on the background thread)
        juce::String tag; // remembered with the job it starts (where its section is in the song)
    };
    void create (Request r) { push ({ Cmd::Create, std::move (r), {}, {}, 0, {} }); }
    void cancel (const juce::String& id) { if (validJobId (id)) push ({ Cmd::Cancel, {}, id, {}, 0, {} }); }
    void addVoice (const juce::File& wav) { push ({ Cmd::AddVoice, {}, {}, wav, 0, {} }); }
    // the version's vocal, fetched into `dest` (then it's in snapshot().files)
    void fetch (const juce::String& id, int n, const juce::File& dest) { if (validJobId (id) && n > 0 && n < 10) push ({ Cmd::Fetch, {}, id, {}, n, dest }); }

private:
    struct Cmd
    {
        enum Kind { Create, Cancel, AddVoice, Fetch } kind;
        Request req;
        juce::String id;
        juce::File file;
        int n = 0;
        juce::File dest;
    };
    void push (Cmd c)
    {
        { const std::lock_guard<std::mutex> l (lock); cmds.push_back (std::move (c)); }
        if (! isThreadRunning()) startThread (juce::Thread::Priority::low);
        notify();
    }
    void say (const juce::String& m, bool bad)
    {
        const std::lock_guard<std::mutex> l (lock);
        snap.message = m; snap.messageBad = bad; ++snap.serial;
    }

    // ---- talking to it
    bool readConnection()
    {
        const auto f = connectionFile();
        if (! f.existsAsFile()) { problem ("not started (no connection file)"); return false; }
        const auto v = juce::JSON::parse (f.loadFileAsString().substring (0, 4000));
        const int p = (int) v.getProperty ("port", 0);
        const auto t = v.getProperty ("token", "").toString();
        if (p < 1024 || p > 65535 || t.length() < 8 || t.length() > 200 || ! t.containsOnly ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"))
        { problem ("its connection file isn't readable"); return false; }
        port = p; token = t;
        return true;
    }
    void problem (const juce::String& why)
    {
        const std::lock_guard<std::mutex> l (lock);
        if (snap.connected || snap.problem != why) { snap.connected = false; snap.problem = why; ++snap.serial; }
    }
    juce::URL url (const juce::String& path) const { return juce::URL ("http://127.0.0.1:" + juce::String (port) + path); }

    // a request; the answer's body as text (empty and code 0 if it didn't answer)
    juce::String call (const juce::String& path, const juce::MemoryBlock* post, const juce::String& contentType, int& code, int timeoutMs = 4000)
    {
        code = 0;
        auto u = url (path);
        if (post != nullptr) u = u.withPOSTData (*post);
        // (the query stays in the address; a POST's body is ours, exactly as given)
        const auto o = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                           .withConnectionTimeoutMs (timeoutMs)
                           .withStatusCode (&code)
                           .withHttpRequestCmd (post != nullptr ? "POST" : "GET")
                           .withExtraHeaders ("X-Token: " + token + (contentType.isNotEmpty() ? "\r\nContent-Type: " + contentType : juce::String()));
        std::unique_ptr<juce::InputStream> in (u.createInputStream (o));
        if (in == nullptr) return {};
        juce::MemoryOutputStream m;
        m.writeFromInputStream (*in, 2 * 1024 * 1024); // (answers are small; more is not the helper)
        return m.toString();
    }
    juce::var callJson (const juce::String& path, const juce::var* body, int& code)
    {
        juce::MemoryBlock mb;
        if (body != nullptr) { const auto s = juce::JSON::toString (*body, true); mb.append (s.toRawUTF8(), s.getNumBytesAsUTF8()); }
        return juce::JSON::parse (call (path, body != nullptr ? &mb : nullptr, body != nullptr ? "application/json" : juce::String(), code));
    }
    static juce::String errorOf (const juce::var& v, int code)
    {
        const auto e = v.getProperty ("error", "").toString().substring (0, 300);
        return e.isNotEmpty() ? e : code == 0 ? juce::String ("the voice helper didn't answer") : "the voice helper said no (" + juce::String (code) + ")";
    }

    void poll()
    {
        if (! readConnection()) return;
        int code = 0;
        const auto opts = callJson ("/api/options", nullptr, code);
        if (code != 200 || ! opts.isObject()) { problem (code == 0 ? "not running (nothing answered on its port)" : "it answered oddly (" + juce::String (code) + ")"); return; }
        const auto jobsV = callJson ("/api/jobs", nullptr, code);
        Snapshot fresh;
        {
            const std::lock_guard<std::mutex> l (lock);
            fresh = snap;
        }
        fresh.connected = true;
        fresh.problem = {};
        auto strings = [] (const juce::var& a, int max)
        {
            juce::StringArray out;
            if (auto* arr = a.getArray())
                for (auto& x : *arr) if (out.size() < max) out.add (x.toString().substring (0, 120));
            return out;
        };
        fresh.voices = strings (opts["voices"], 200);
        fresh.tracks = strings (opts["tracks"], 500);
        fresh.stages.clear();
        if (auto* arr = opts["stages"].getArray())
            for (auto& s : *arr)
                if (s.isArray() && s.size() >= 2 && fresh.stages.size() < 12)
                    fresh.stages.push_back ({ s[0].toString().substring (0, 20), s[1].toString().substring (0, 60) });
        fresh.jobs.clear();
        if (auto* arr = jobsV.getArray())
            for (auto& jv : *arr)
            {
                Job j;
                j.id = jv["id"].toString();
                if (! validJobId (j.id)) continue;
                j.state = jv["state"].toString().substring (0, 16);
                j.stage = jv["stage"].toString().substring (0, 20);
                j.note = jv["note"].toString().substring (0, 80);
                j.title = jv["title"].toString().substring (0, 60);
                j.error = jv["error"].toString().substring (0, 400);
                j.created = (double) jv["created"];
                j.clean = jv["opts"].isObject() ? (bool) jv["opts"].getProperty ("clean", true) : true;
                if (auto* rs = jv["results"].getArray())
                    for (auto& rv : *rs)
                    {
                        Result r;
                        r.n = juce::jlimit (0, 9, (int) rv["n"]);
                        const auto want = "/audio/" + j.id + "/vocal_" + juce::String (r.n) + ".wav";
                        if (r.n < 1 || rv["vocal"].toString() != want) continue;
                        r.vocal = want;
                        r.words = juce::jlimit (0, 100, (int) rv.getProperty ("words", 0));
                        r.key = juce::jlimit (0, 100, (int) rv.getProperty ("key", 0));
                        r.heard = rv.getProperty ("heard", "").toString().substring (0, 200);
                        j.results.push_back (r);
                    }
                fresh.jobs.push_back (j);
                if (fresh.jobs.size() >= 20) break;
            }
        anyLive = ! fresh.jobs.empty() && fresh.jobs.front().live();
        const std::lock_guard<std::mutex> l (lock);
        fresh.serial = snap.serial + 1;
        fresh.message = snap.message; fresh.messageBad = snap.messageBad; fresh.sending = snap.sending; fresh.files = snap.files; fresh.tags = snap.tags;
        snap = fresh;
    }

    // a WAV to the helper: the song's section, or a voice
    juce::String upload (const juce::File& wav, const juce::String& kind, juce::String& err)
    {
        juce::MemoryBlock mb;
        if (! wav.existsAsFile() || wav.getSize() > 300LL * 1024 * 1024 || ! wav.loadFileAsData (mb) || mb.getSize() < 44)
        { err = "that file can't be sent (a WAV under 300 MB)"; return {}; }
        const auto name = juce::URL::addEscapeChars (wav.getFileName().substring (0, 60), true);
        int code = 0;
        const auto v = juce::JSON::parse (call ("/api/upload?kind=" + kind + "&name=" + name, &mb, "audio/wav", code, 30000));
        if (code != 200) { err = errorOf (v, code); return {}; }
        return v.getProperty ("name", "").toString().substring (0, 120);
    }

    void handle (Cmd& c)
    {
        if (! readConnection()) { say ("The voice helper isn't running", true); return; }
        int code = 0;
        if (c.kind == Cmd::Cancel)
        {
            juce::var body (new juce::DynamicObject()); body.getDynamicObject()->setProperty ("id", c.id);
            const auto v = callJson ("/api/cancel", &body, code);
            say (code == 200 ? juce::String ("Stopping...") : errorOf (v, code), code != 200);
        }
        else if (c.kind == Cmd::AddVoice)
        {
            juce::String err;
            setSending (true);
            const auto name = upload (c.file, "voice", err);
            setSending (false);
            say (name.isNotEmpty() ? "Added the voice \"" + name + "\"" : err, name.isEmpty());
        }
        else if (c.kind == Cmd::Fetch)
        {
            auto o = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs (8000).withStatusCode (&code);
            std::unique_ptr<juce::InputStream> in (url ("/audio/" + c.id + "/vocal_" + juce::String (c.n) + ".wav").createInputStream (o));
            if (in == nullptr || code != 200) { say ("Couldn't fetch version " + juce::String (c.n) + " from the voice helper", true); return; }
            c.dest.getParentDirectory().createDirectory();
            const auto tmp = c.dest.getSiblingFile (c.dest.getFileName() + ".part");
            {
                juce::FileOutputStream out (tmp);
                if (! out.openedOk()) { say ("Couldn't save the vocal", true); return; }
                out.setPosition (0); out.truncate();
                out.writeFromInputStream (*in, 300LL * 1024 * 1024);
            }
            juce::MemoryBlock head;
            tmp.loadFileAsData (head);
            if (head.getSize() < 44 || std::memcmp (head.getData(), "RIFF", 4) != 0) { tmp.deleteFile(); say ("The voice helper sent something that isn't audio", true); return; }
            tmp.moveFileTo (c.dest);
            const std::lock_guard<std::mutex> l (lock);
            snap.files[c.id + "/" + juce::String (c.n)] = c.dest;
            ++snap.serial;
        }
        else if (c.kind == Cmd::Create)
        {
            setSending (true);
            say ("Sending your song section...", false);
            const auto wav = c.req.renderTrack ? c.req.renderTrack() : juce::File();
            juce::String err;
            const auto track = wav.existsAsFile() ? upload (wav, "track", err) : juce::String();
            setSending (false);
            if (track.isEmpty()) { say (err.isNotEmpty() ? err : juce::String ("Couldn't record the song section"), true); return; }
            juce::var body (new juce::DynamicObject());
            auto* o = body.getDynamicObject();
            o->setProperty ("lyrics", c.req.lyrics.substring (0, 4000));
            o->setProperty ("style", c.req.style.substring (0, 300));
            o->setProperty ("track", track);
            o->setProperty ("voice", c.req.voice);
            o->setProperty ("key", c.req.key);
            o->setProperty ("bpm", juce::jlimit (50, 220, juce::roundToInt (c.req.bpm)));
            o->setProperty ("takes", juce::jlimit (1, 8, c.req.takes));
            o->setProperty ("results", juce::jlimit (1, 3, c.req.results));
            o->setProperty ("tune", juce::jlimit (0.0f, 1.0f, c.req.tune));
            o->setProperty ("runs", juce::jlimit (0, 8, c.req.runs));
            o->setProperty ("clean", c.req.clean);
            o->setProperty ("consent", c.req.consent);
            const auto v = callJson ("/api/create", &body, code);
            say (code == 200 ? juce::String ("Started") : errorOf (v, code), code != 200);
            if (code == 200)
            {
                anyLive = true;
                const auto id = v.getProperty ("id", "").toString();
                if (validJobId (id)) { const std::lock_guard<std::mutex> l (lock); snap.tags[id] = c.req.tag; ++snap.serial; }
            }
        }
        poll();
    }
    void setSending (bool s) { const std::lock_guard<std::mutex> l (lock); snap.sending = s; ++snap.serial; }

    void run() override
    {
        while (! threadShouldExit())
        {
            Cmd c;
            bool has = false;
            {
                const std::lock_guard<std::mutex> l (lock);
                if (! cmds.empty()) { c = std::move (cmds.front()); cmds.pop_front(); has = true; }
            }
            if (has) handle (c);
            else if (wanted.load() || anyLive.load()) poll();
            if (! has) wait (1500);
        }
    }

    mutable std::mutex lock;
    Snapshot snap;
    std::deque<Cmd> cmds;
    std::atomic<bool> wanted { false }, anyLive { false };
    int port = 0;
    juce::String token;
};
} // namespace lp::voice
