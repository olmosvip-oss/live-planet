#include "PluginEditor.h"

using namespace juce;
using lp::ui::font;

// EXPORT: the song's files, in the type you want, wherever you want them. The same audio as the drag
// pills (the mix as you hear it, mastered if a master is on; each track with its effects recorded in when
// it's set to), written again as WAV, AIFF, FLAC or (on a Mac) M4A, at 48 or 44.1 kHz.
namespace
{
const lp::ui::Theme& exportTheme() { return lp::ui::theme (0); }

#if JUCE_MAC
constexpr bool canM4a = true;
#else
constexpr bool canM4a = false;
#endif

StringArray typeNames()
{
    StringArray s { "WAV 24-bit", "WAV 16-bit", "AIFF", "FLAC" };
    if (canM4a) s.add ("M4A");
    return s;
}
String typeExt (int type) { return type <= 1 ? ".wav" : type == 2 ? ".aif" : type == 3 ? ".flac" : ".m4a"; }

// one audio file, re-written: resampled if asked, in the chosen type. Answers what went wrong (empty = fine).
String convert (const File& in, const File& out, int type, double rate)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> rd (fm.createReaderFor (in));
    if (rd == nullptr) return "couldn't read " + in.getFileName();
    const int chs = (int) jlimit (1u, 2u, rd->numChannels);
    AudioBuffer<float> buf (chs, (int) rd->lengthInSamples);
    rd->read (&buf, 0, buf.getNumSamples(), 0, true, chs > 1);
    if (std::abs (rd->sampleRate - rate) > 0.5)
    {
        const double ratio = rd->sampleRate / rate;
        AudioBuffer<float> rs (chs, (int) std::floor (buf.getNumSamples() / ratio));
        for (int c = 0; c < chs; ++c) { WindowedSincInterpolator ip; ip.process (ratio, buf.getReadPointer (c), rs.getWritePointer (c), rs.getNumSamples()); }
        buf = std::move (rs);
    }
    // (M4A: written as WAV first, then encoded by macOS's own afconvert)
    const bool m4a = type == 4;
    const File target = m4a ? out.getSiblingFile (out.getFileNameWithoutExtension() + ".tmp.wav") : out;
    target.deleteFile();
    std::unique_ptr<AudioFormat> fmt;
    if (type == 2) fmt = std::make_unique<AiffAudioFormat>();
    else if (type == 3) fmt = std::make_unique<FlacAudioFormat>();
    else fmt = std::make_unique<WavAudioFormat>();
    const int bits = type == 1 ? 16 : 24;
    {
        std::unique_ptr<AudioFormatWriter> w (fmt->createWriterFor (new FileOutputStream (target), rate, (unsigned) chs, bits, {}, 0));
        if (w == nullptr) return "couldn't write " + target.getFileName();
        if (! w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples())) return "couldn't write " + target.getFileName();
    }
    if (m4a)
    {
        out.deleteFile();
        ChildProcess p;
        const StringArray args { "/usr/bin/afconvert", "-f", "m4af", "-d", "aac", "-b", "256000", target.getFullPathName(), out.getFullPathName() };
        const bool ok = p.start (args) && p.waitForProcessToFinish (120000) && p.getExitCode() == 0 && out.existsAsFile();
        target.deleteFile();
        if (! ok) return "couldn't make the M4A of " + in.getFileName();
    }
    return {};
}
}

ExportPanel::ExportPanel (LivePlanetProcessor& p, std::function<void()> closed)
    : proc (p), onClosed (std::move (closed)),
      what ({ "Mix", "Stems", "MIDI", "Everything" }, [this] { return whatN; }, [this] (int i) { whatN = i; repaint(); }, th),
      type (typeNames(), [this] { return typeN; }, [this] (int i) { typeN = i; repaint(); }, th),
      rate ({ "48 kHz", "44.1 kHz" }, [this] { return rateN; }, [this] (int i) { rateN = i; repaint(); }, th)
{
    setWantsKeyboardFocus (true);
    whatN = jlimit (0, 3, (int) lp::setting ("exportWhat", 0));
    typeN = jlimit (0, typeNames().size() - 1, (int) lp::setting ("exportType", 0));
    rateN = jlimit (0, 1, (int) lp::setting ("exportRate", 0));
    for (auto* c : { &what, &type, &rate }) addAndMakeVisible (*c);
    goBtn.getProperties().set ("primary", true);
    goBtn.onClick = [this] { choose(); };
    cancelBtn.onClick = [this] { if (onClosed) onClosed(); };
    for (auto* b : { &goBtn, &cancelBtn }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (*b); }
}

ExportPanel::~ExportPanel() { *alive = false; }

Rectangle<int> ExportPanel::card() const { return Rectangle<int> (jmin (560, getWidth() - 40), 470).withCentre (getLocalBounds().getCentre()); }

void ExportPanel::resized()
{
    auto c = card().reduced (34, 30);
    closeX = Rectangle<int> (card().getRight() - 50, card().getY() + 18, 30, 30);
    c.removeFromTop (14 + 14 + 38 + 22);
    auto row = [&c] (Segmented& s) { c.removeFromTop (26); s.setBounds (c.removeFromTop (38)); c.removeFromTop (12); };
    row (what);
    row (type);
    row (rate);
    auto b = card().reduced (34, 30).removeFromBottom (44);
    goBtn.setBounds (b.removeFromLeft (b.getWidth() / 2 - 6));
    b.removeFromLeft (12);
    cancelBtn.setBounds (b);
    goBtn.setEnabled (! busy);
}

void ExportPanel::paint (Graphics& g)
{
    const auto* lnf = dynamic_cast<LivePlanetLookAndFeel*> (&getLookAndFeel());
    const auto& t = lnf != nullptr ? *lnf->th : exportTheme();
    th = &t;
    g.fillAll (Colours::black.withAlpha (0.66f));
    const auto cf = card().toFloat();
    {
        Graphics::ScopedSaveState s (g);
        Path clip; clip.addRoundedRectangle (cf, 20.0f);
        g.reduceClipRegion (clip);
        g.setColour (t.bodyDark.interpolatedWith (Colour (0xff0d0d11), 0.6f));
        g.fillRect (cf);
        ColourGradient glow (t.grass.withAlpha (0.28f), cf.getRight() - 40.0f, cf.getY(), t.grass.withAlpha (0.0f), cf.getRight() - 300.0f, cf.getY() + 260.0f, true);
        g.setGradientFill (glow);
        g.fillRect (cf);
    }
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawRoundedRectangle (cf.reduced (0.5f), 20.0f, 1.0f);
    {
        auto x = closeX.toFloat().reduced (9.0f);
        g.setColour (Colours::white.withAlpha (0.5f));
        g.drawLine (x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.6f);
        g.drawLine (x.getRight(), x.getY(), x.getX(), x.getBottom(), 1.6f);
    }
    auto r = cf.reduced (34.0f, 30.0f);
    g.setFont (lp::ui::mono (10.0f, true, 0.18f));
    g.setColour (t.grassLight);
    g.drawText ("EXPORT", r.removeFromTop (14.0f), Justification::centredLeft, false);
    r.removeFromTop (14.0f);
    const auto st = proc.songJob.getStatus();
    g.setFont (font (30.0f, lp::ui::Medium, -0.02f));
    g.setColour (t.ink);
    g.drawText (st.haveSong ? st.files.folder.getFileName() : String ("No song yet"), r.removeFromTop (38.0f), Justification::centredLeft, true);
    g.setFont (font (14.0f));
    g.setColour (t.ink.withAlpha (0.65f));
    const String about = whatN == 0 ? "The whole song in one file, as you hear it (mastered, if a master is on)."
                       : whatN == 1 ? "One file per track, all the same length: line them up at bar 1 in any DAW."
                       : whatN == 2 ? "Every MIDI part in one file (drums, 808, hook, bass lines, chords)."
                                    : "The mix, every track and the MIDI, in one folder.";
    g.drawText (about, r.removeFromTop (22.0f), Justification::centredLeft, true);
    g.setFont (lp::ui::mono (9.5f, true, 0.16f));
    g.setColour (t.ink.withAlpha (0.5f));
    for (auto* s : { &what, &type, &rate })
    {
        const String label = s == &what ? "WHAT" : s == &type ? (whatN == 2 ? "FILE TYPE (FOR THE AUDIO)" : "FILE TYPE") : "SAMPLE RATE";
        g.drawText (label, s->getBounds().toFloat().translated (0.0f, -22.0f).withHeight (16.0f), Justification::centredLeft, false);
    }
    const String note = message.isNotEmpty() ? message
                      : typeN == 4 ? "M4A (AAC, 256 kbps): small, plays everywhere. For sharing, not for mixing."
                      : typeN == 3 ? "FLAC: the same sound as WAV, about half the size."
                      : typeN == 1 ? "16-bit at 44.1 kHz is CD quality."
                                   : "Full quality.";
    g.setFont (font (13.0f));
    g.setColour (message.isNotEmpty() ? t.grassLight : t.ink.withAlpha (0.55f));
    g.drawFittedText (note, rate.getBounds().translated (0, rate.getHeight() + 6).withHeight (36), Justification::centredLeft, 2, 1.0f);
}

void ExportPanel::mouseUp (const MouseEvent& e)
{
    const auto p = e.getPosition();
    if (closeX.contains (p) || ! card().contains (p)) { if (! busy && onClosed) onClosed(); }
}

bool ExportPanel::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey && ! busy) { if (onClosed) onClosed(); return true; }
    return false;
}

void ExportPanel::choose()
{
    if (busy || ! proc.songJob.getStatus().haveSong) return;
    lp::setSetting ("exportWhat", whatN);
    lp::setSetting ("exportType", typeN);
    lp::setSetting ("exportRate", rateN);
    const File start (lp::setting ("exportFolder", File::getSpecialLocation (File::userMusicDirectory).getFullPathName()).toString());
    chooser = std::make_unique<FileChooser> ("Where should the files go?", start.isDirectory() ? start : File::getSpecialLocation (File::userMusicDirectory));
    SafePointer<ExportPanel> self (this);
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [self] (const FileChooser& fc)
    {
        if (self == nullptr || ! fc.getResult().isDirectory()) return;
        lp::setSetting ("exportFolder", fc.getResult().getFullPathName());
        self->run (fc.getResult());
    });
}

void ExportPanel::run (const File& where)
{
    busy = true;
    message = "Exporting...";
    resized();
    repaint();
    auto* p = &proc;
    const int w = whatN, ty = typeN;
    const double sr = rateN == 0 ? 48000.0 : 44100.0;
    const String ext = typeExt (ty);
    SafePointer<ExportPanel> self (this);
    auto alv = alive;
    Thread::launch ([p, w, ty, sr, ext, where, self, alv]
    {
        auto say = [self, alv] (const String& m, bool done)
        {
            MessageManager::callAsync ([self, alv, m, done]
            {
                if (! *alv || self == nullptr) return;
                self->message = m;
                if (done) { self->busy = false; self->resized(); }
                self->repaint();
            });
        };
        const auto st = p->songJob.getStatus();
        if (! st.haveSong) { say ("No song to export", true); return; }
        const String song = File::createLegalFileName (st.files.folder.getFileName());
        // everything goes in one folder named after the song (a number added if it's there already)
        File dir = where.getChildFile (song);
        if (w != 0 && w != 2) dir = dir.getNonexistentSibling (false);
        else dir = where;
        dir.createDirectory();
        String problem;
        int made = 0;
        if (w == 0 || w == 3)
        {
            say ("Exporting the mix...", false);
            File mix;
            if (p->masterPreset.load() >= 0 && p->masterAfter.load()) mix = p->masteredFile();
            if (! mix.existsAsFile()) mix = p->mixNowFile(); // (the song as it is now, not as first written)
            if (! mix.existsAsFile()) mix = st.files.mixFile;
            const auto out = (w == 0 ? dir.getChildFile (song + ext).getNonexistentSibling (false) : dir.getChildFile (song + " - mix" + ext));
            if (auto e = convert (mix, out, ty, sr); e.isNotEmpty()) problem = e; else ++made;
        }
        if (w == 1 || w == 3)
        {
            const File stems = w == 3 ? dir.getChildFile ("stems") : dir;
            stems.createDirectory();
            for (int t = 0; t < (int) st.tracks.size(); ++t)
            {
                say ("Exporting track " + String (t + 1) + " of " + String ((int) st.tracks.size()) + "...", false);
                auto f = p->fileToDrag (t);
                if (! f.existsAsFile()) f = st.tracks[(size_t) t].stem;
                const auto name = String (t + 1).paddedLeft ('0', 2) + " " + File::createLegalFileName (st.tracks[(size_t) t].name);
                if (auto e = convert (f, stems.getChildFile (name + ext), ty, sr); e.isNotEmpty()) problem = e; else ++made;
            }
        }
        if (w == 2 || w == 3)
        {
            const auto midi = p->songJob.allMidi();
            const auto out = w == 2 ? dir.getChildFile (song + ".mid").getNonexistentSibling (false) : dir.getChildFile (song + ".mid");
            if (midi.existsAsFile() && midi.copyFileTo (out)) ++made; else problem = "no MIDI in this song yet";
        }
        say (problem.isNotEmpty() ? "Done, but " + problem : "Done: " + String (made) + (made == 1 ? " file" : " files") + " exported.", true);
        if (made > 0) dir.revealToUser();
    });
}
