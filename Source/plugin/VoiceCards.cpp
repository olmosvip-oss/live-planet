#include "PluginEditor.h"
#include "AppFolder.h"
#include "License.h"
#include "BinaryData.h"
#include "../song/VoiceVariants.h"

using namespace juce;
using lp::ui::font;

namespace lp::ai
{
// ---- what a voice is, from its name ("Me - Deep": made from your voice; anything else: a recording you added)
// Loopwright's own voices: built in (their picture too), always there, never removed
static bool isOfficial (const String& name)
{
    const auto base = name.upToFirstOccurrenceOf (" - ", false, false).trim();
    return base.equalsIgnoreCase ("Jordan_Sing") || base.equalsIgnoreCase ("Jordan_Rap");
}
static Image officialPicture()
{
    static Image img = ImageFileFormat::loadFrom (BinaryData::jordan_jpg, (size_t) BinaryData::jordan_jpgSize);
    return img;
}
// your own voices: one per recording (with the voices made from it), 2 free
static int ownVoiceCount (const ComboBox& voices)
{
    StringArray bases;
    for (int i = 0; i < voices.getNumItems(); ++i)
    {
        const auto n = voices.getItemText (i);
        if (! isOfficial (n)) bases.addIfNotAlreadyThere (n.upToFirstOccurrenceOf (" - ", false, false).trim());
    }
    return bases.size();
}

VoicePicker::About VoicePicker::about (const String& name)
{
    const auto tier = lp::license::required() ? lp::license::jordanTier() : String ("none needed");
    const String lic = tier == "pro" ? " (Pro license)" : tier == "standard" ? " (Standard license)" : tier.isEmpty() ? " (license from $150)" : String();
    if (name.equalsIgnoreCase ("Jordan_Sing")) return { name, "Loopwright voice: singing" + lic, false };
    if (name.equalsIgnoreCase ("Jordan_Rap")) return { name, "Loopwright voice: rap" + lic, false };
    for (auto& v : lp::song::variants::all())
        if (name.endsWithIgnoreCase (String (" - ") + v.name))
            return { name.dropLastCharacters ((int) strlen (v.name) + 3) + String::fromUTF8 (" \xc2\xb7 ") + v.name, v.tags, true };
    return { name, "your recording", false };
}

// each voice its own art: a soft gradient in two colours picked from its name, and a figure in profile
// a voice's own picture (set by right-clicking its card): Loopwright/voice pictures/<voice>.jpg or .png.
// A voice made from another ("Jordan_Sing - Deep") shows its recording's picture.
static File pictureFolder() { return lp::appFolder().getChildFile ("voice pictures"); }
static File pictureFileFor (const String& name)
{
    for (auto n : { name, name.upToFirstOccurrenceOf (" - ", false, false).trim() })
        for (auto* ext : { ".jpg", ".png", ".jpeg" })
            if (auto f = pictureFolder().getChildFile (File::createLegalFileName (n) + ext); f.existsAsFile()) return f;
    return {};
}
static Image pictureFor (const String& name)
{
    if (isOfficial (name)) return officialPicture();
    const auto f = pictureFileFor (name);
    if (! f.existsAsFile()) return {};
    const auto key = (int64) f.getFullPathName().hashCode64() ^ f.getLastModificationTime().toMilliseconds();
    if (auto img = ImageCache::getFromHashCode (key); img.isValid()) return img;
    auto img = ImageFileFormat::loadFrom (f);
    if (img.isValid())
    {
        // (kept small: cards are small)
        const int side = jmin (img.getWidth(), img.getHeight(), 600);
        if (img.getWidth() > 600 || img.getHeight() > 600)
            img = img.rescaled (img.getWidth() * side / jmin (img.getWidth(), img.getHeight()), img.getHeight() * side / jmin (img.getWidth(), img.getHeight()), Graphics::highResamplingQuality);
        ImageCache::addImageToCache (img, key);
    }
    return img;
}

void VoicePicker::drawArt (Graphics& g, Rectangle<float> r, const String& name)
{
    if (auto pic = pictureFor (name); pic.isValid())
    {
        // the picture, filling the square (centred, a little toward the top, where faces are)
        Graphics::ScopedSaveState ss (g);
        Path clip;
        clip.addRoundedRectangle (r, r.getWidth() * 0.18f);
        g.reduceClipRegion (clip);
        const float sc = jmax (r.getWidth() / (float) pic.getWidth(), r.getHeight() / (float) pic.getHeight());
        const float w = pic.getWidth() * sc, h = pic.getHeight() * sc;
        g.setOpacity (1.0f); // (not the see-through colour set before it)
        g.drawImage (pic, Rectangle<float> (r.getCentreX() - w * 0.5f, r.getY() - (h - r.getHeight()) * 0.25f, w, h), RectanglePlacement::stretchToFit);
        return;
    }
    static const uint32 hues[] = { 0xff7c5cff, 0xffff6b8b, 0xff3fc1c9, 0xfff2a33a, 0xff8fd16a, 0xffe57ad1, 0xff5aa9ff, 0xffff8a5c };
    const auto h = (uint32) name.hashCode();
    const Colour a (hues[h % 8]), b (hues[(h / 8 + 3) % 8]);
    Graphics::ScopedSaveState ss (g);
    Path clip;
    clip.addRoundedRectangle (r, r.getWidth() * 0.18f);
    g.reduceClipRegion (clip);
    g.setGradientFill (ColourGradient (a, r.getX(), r.getY(), b.darker (0.3f), r.getRight(), r.getBottom(), false));
    g.fillRect (r);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.28f), r.getX() + r.getWidth() * 0.3f, r.getY(), Colours::transparentWhite, r.getCentreX(), r.getBottom(), true));
    g.fillRect (r);
    // a singer: head and shoulders, dark against the light
    g.setColour (Colour (0xff0d0d12).withAlpha (0.82f));
    const float w = r.getWidth();
    g.fillEllipse (r.getCentreX() - w * 0.17f, r.getY() + w * 0.2f, w * 0.34f, w * 0.38f);
    Path body;
    body.addRoundedRectangle (r.getCentreX() - w * 0.33f, r.getY() + w * 0.62f, w * 0.66f, w * 0.6f, w * 0.28f);
    g.fillPath (body);
}

// ---- the chip: the voice in use
void VoiceChip::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool over = isMouseOver (true);
    g.setColour (Colours::white.withAlpha (over ? 0.09f : 0.05f));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (Colours::white.withAlpha (over ? 0.22f : 0.12f));
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    const auto name = voices.getText();
    auto art = r.reduced (5.0f).removeFromLeft (r.getHeight() - 10.0f);
    if (name.isNotEmpty()) VoicePicker::drawArt (g, art, name);
    else { g.setColour (Colours::white.withAlpha (0.08f)); g.fillRoundedRectangle (art, 6.0f); }
    auto txt = r.withTrimmedLeft (art.getRight() - r.getX() + 10.0f).withTrimmedRight (28.0f);
    const auto ab = VoicePicker::about (name);
    g.setColour (th->ink);
    g.setFont (font (14.0f, lp::ui::Medium));
    g.drawText (name.isEmpty() ? String ("Pick a voice") : ab.title, txt.removeFromTop (txt.getHeight() * 0.55f), Justification::bottomLeft, true);
    g.setColour (th->inkDim);
    g.setFont (font (11.0f));
    g.drawText (name.isEmpty() ? String ("add a recording of you to start") : ab.tags, txt, Justification::topLeft, true);
    // a chevron: there are more
    Path chev;
    const float cx = r.getRight() - 16.0f, cy = r.getCentreY();
    chev.startNewSubPath (cx - 5.0f, cy - 2.5f);
    chev.lineTo (cx, cy + 2.5f);
    chev.lineTo (cx + 5.0f, cy - 2.5f);
    g.setColour (th->inkDim);
    g.strokePath (chev, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

void VoiceChip::mouseUp (const MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition())) return;
    auto* top = lp::ui::overlayHost (this);
    if (top == nullptr) return;
    SafePointer<VoiceChip> self (this);
    picker = std::make_unique<VoicePicker> (proc, th, voices, [self]
    {
        MessageManager::callAsync ([self] { if (self != nullptr) { self->picker.reset(); self->repaint(); } });
    });
    top->addAndMakeVisible (*picker);
    picker->setBounds (top->getLocalBounds());
    picker->toFront (true);
}

// ---- the picker
VoicePicker::VoicePicker (LivePlanetProcessor& p, const lp::ui::Theme*& theme, ComboBox& v, std::function<void()> closed)
    : proc (p), th (theme), voices (v), onClosed (std::move (closed))
{
    search.setTextToShowWhenEmpty ("Search voices", Colours::grey);
    search.setFont (font (14.0f));
    search.setIndents (14, 9);
    search.setColour (TextEditor::backgroundColourId, Colours::white.withAlpha (0.05f));
    search.setColour (TextEditor::outlineColourId, Colours::white.withAlpha (0.12f));
    search.setColour (TextEditor::focusedOutlineColourId, th->grassLight.withAlpha (0.7f));
    search.onTextChange = [this] { scroll = 0.0f; repaint(); };
    addAndMakeVisible (search);
    for (auto* b : { &tabAll, &tabMade, &tabRec, &makeBtn, &addBtn, &closeBtn })
    {
        b->getProperties().set ("glass", true);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    makeBtn.getProperties().set ("ai", true);
    makeBtn.setTooltip ("Pick a clean recording of you singing (10 to 30 seconds, no music): five more voices are made from it, "
                        "a deeper one, a warm one, a raspy one, a brighter one and a high, airy one. All yours to use.");
    addBtn.setTooltip ("Add a recording as a voice as it is (yours, or a singer who gave you permission)");
    tabAll.onClick = [this] { tab = 0; scroll = 0.0f; showTabs(); };
    tabMade.onClick = [this] { tab = 1; scroll = 0.0f; showTabs(); };
    tabRec.onClick = [this] { tab = 2; scroll = 0.0f; showTabs(); };
    closeBtn.onClick = [this] { if (onClosed) onClosed(); };
    auto pick = [this] (bool variants)
    {
        // your own voices: 2 free (each recording, with the voices made from it); more once unlocked
        if (ownVoiceCount (voices) >= lp::license::freeVoices() && ! lp::license::moreVoicesUnlocked())
        {
            askToUnlockVoices();
            return;
        }
        chooser = std::make_unique<FileChooser> (variants ? "A clean recording of you singing (10 to 30 seconds, no music)" : "A clean recording of the voice (no music)",
                                                 File(), "*.wav;*.m4a;*.mp3;*.aif;*.aiff;*.flac;*.caf;*.aac");
        SafePointer<VoicePicker> self (this);
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [self, variants] (const FileChooser& fc)
        {
            if (self == nullptr || ! fc.getResult().existsAsFile()) return;
            if (variants) self->makeVariants (fc.getResult());
            else
            {
                self->proc.voiceClient.addVoice (fc.getResult());
                self->message = "Adding \"" + fc.getResult().getFileNameWithoutExtension() + "\"...";
                self->repaint();
            }
        });
    };
    makeBtn.onClick = [pick] { pick (true); };
    addBtn.onClick = [pick] { pick (false); };
    showTabs();
}

void VoicePicker::showTabs()
{
    auto lit = [] (TextButton& b, bool on) { b.getProperties().set ("primary", on); b.repaint(); };
    lit (tabAll, tab == 0);
    lit (tabMade, tab == 1);
    lit (tabRec, tab == 2);
    repaint();
}

Rectangle<int> VoicePicker::card() const
{
    return getLocalBounds().withSizeKeepingCentre (jmin (720, getWidth() - 40), jmin (620, getHeight() - 40));
}

Rectangle<float> VoicePicker::gridArea() const
{
    auto c = card().reduced (24, 20).toFloat();
    c.removeFromTop (40 + 12 + 40 + 14 + 32 + 14); // title, search, tabs
    c.removeFromBottom (40 + 14);                  // the buttons
    return c;
}

std::vector<String> VoicePicker::shownVoices() const
{
    std::vector<String> out;
    const auto q = search.getText().trim();
    const auto hidden = lp::setting ("hiddenVoices", Array<var>());
    for (int i = 0; i < voices.getNumItems(); ++i)
    {
        const auto n = voices.getItemText (i);
        if (! isOfficial (n) && hidden.getArray() != nullptr && hidden.getArray()->contains (n)) continue; // (you removed it)
        const auto ab = about (n);
        if (tab == 1 && ! ab.madeFromYours) continue;
        if (tab == 2 && ab.madeFromYours) continue;
        if (q.isNotEmpty() && ! n.containsIgnoreCase (q) && ! ab.tags.containsIgnoreCase (q)) continue;
        out.push_back (n);
    }
    return out;
}

void VoicePicker::resized()
{
    auto c = card().reduced (24, 20);
    auto head = c.removeFromTop (40);
    closeBtn.setBounds (head.removeFromRight (84).withSizeKeepingCentre (84, 32));
    c.removeFromTop (12);
    search.setBounds (c.removeFromTop (40));
    c.removeFromTop (14);
    auto t = c.removeFromTop (32);
    tabAll.setBounds (t.removeFromLeft (120)); t.removeFromLeft (8);
    tabMade.setBounds (t.removeFromLeft (150)); t.removeFromLeft (8);
    tabRec.setBounds (t.removeFromLeft (120));
    auto b = c.removeFromBottom (40);
    makeBtn.setBounds (b.removeFromLeft (290));
    b.removeFromLeft (10);
    addBtn.setBounds (b.removeFromLeft (190));
}

void VoicePicker::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.6f));
    const auto r = card().toFloat();
    {
        Path p;
        p.addRoundedRectangle (r, 18.0f);
        DropShadow (Colours::black.withAlpha (0.6f), 40, { 0, 14 }).drawForPath (g, p);
    }
    g.setColour (th->body.withAlpha (0.97f));
    g.fillRoundedRectangle (r, 18.0f);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.07f), 0.0f, r.getY(), Colours::white.withAlpha (0.0f), 0.0f, r.getY() + 160.0f, false));
    g.fillRoundedRectangle (r, 18.0f);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.3f), 0.0f, r.getY(), Colours::white.withAlpha (0.06f), 0.0f, r.getBottom(), false));
    g.drawRoundedRectangle (r.reduced (0.5f), 18.0f, 1.0f);
    auto in = r.reduced (24.0f, 20.0f);
    g.setColour (Colours::white);
    g.setFont (font (21.0f, lp::ui::Medium, -0.01f));
    g.drawText ("Voices", in.removeFromTop (40.0f), Justification::centredLeft, false);

    // the cards, two to a row
    const auto area = gridArea();
    const auto list = shownVoices();
    const auto current = voices.getText();
    cards.clear();
    const float gap = 10.0f, h = 68.0f, w = (area.getWidth() - gap) * 0.5f;
    const int rows = ((int) list.size() + 1) / 2;
    const float maxScroll = std::max (0.0f, rows * (h + gap) - gap - area.getHeight());
    scroll = jlimit (0.0f, maxScroll, scroll);
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (area.toNearestInt());
        for (size_t i = 0; i < list.size(); ++i)
        {
            const auto cr = Rectangle<float> (area.getX() + (float) (i % 2) * (w + gap), area.getY() + (float) (i / 2) * (h + gap) - scroll, w, h);
            if (cr.getBottom() < area.getY() || cr.getY() > area.getBottom()) continue;
            cards.push_back ({ cr, list[i] });
            const bool on = list[i] == current, hov = (int) i == hover;
            g.setColour (on ? th->grass.withAlpha (0.28f) : Colours::white.withAlpha (hov ? 0.08f : 0.045f));
            g.fillRoundedRectangle (cr, 10.0f);
            g.setColour (on ? th->grassLight.withAlpha (0.8f) : Colours::white.withAlpha (hov ? 0.2f : 0.1f));
            g.drawRoundedRectangle (cr.reduced (0.5f), 10.0f, 1.0f);
            const auto art = cr.reduced (8.0f).removeFromLeft (h - 16.0f);
            drawArt (g, art, list[i]);
            const auto ab = about (list[i]);
            auto txt = cr.withTrimmedLeft (art.getRight() - cr.getX() + 12.0f).withTrimmedRight (40.0f);
            g.setColour (th->ink);
            g.setFont (font (15.0f, lp::ui::Medium));
            g.drawText (ab.title, txt.removeFromTop (txt.getHeight() * 0.55f), Justification::bottomLeft, true);
            g.setColour (th->inkDim);
            g.setFont (font (11.5f));
            g.drawText (ab.tags, txt, Justification::topLeft, true);
            // in use: a tick; else a plus
            const auto mark = Rectangle<float> (cr.getRight() - 32.0f, cr.getCentreY() - 10.0f, 20.0f, 20.0f);
            g.setColour (on ? th->grassLight : Colours::white.withAlpha (hov ? 0.9f : 0.55f));
            Path m;
            if (on) { m.startNewSubPath (mark.getX() + 4.0f, mark.getCentreY()); m.lineTo (mark.getX() + 8.5f, mark.getBottom() - 5.0f); m.lineTo (mark.getRight() - 3.0f, mark.getY() + 4.0f); }
            else { m.startNewSubPath (mark.getCentreX(), mark.getY() + 3.0f); m.lineTo (mark.getCentreX(), mark.getBottom() - 3.0f);
                   m.startNewSubPath (mark.getX() + 3.0f, mark.getCentreY()); m.lineTo (mark.getRight() - 3.0f, mark.getCentreY()); }
            g.strokePath (m, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
        }
    }
    if (list.empty())
    {
        g.setColour (th->inkDim);
        g.setFont (font (14.0f));
        g.drawFittedText (voices.getNumItems() == 0 ? "No voices yet. Start the voice helper, then add a recording of you, or make voices from one."
                          : tab == 1 ? "No voices made from yours yet: \"Make voices from a recording\" makes five from one clip of you."
                                     : "Nothing matches.", area.reduced (30.0f).toNearestInt(), Justification::centred, 3);
    }
    if (message.isNotEmpty())
    {
        g.setColour (th->grassLight);
        g.setFont (font (12.0f));
        g.drawText (message, Rectangle<float> (r.getX() + 24.0f, r.getBottom() - 24.0f - 40.0f - 22.0f, r.getWidth() - 48.0f, 18.0f), Justification::centredLeft, true);
    }
}

void VoicePicker::mouseDown (const MouseEvent& e)
{
    if (! card().contains (e.getPosition())) { if (onClosed) onClosed(); return; }
    for (auto& [cr, name] : cards)
        if (cr.contains (e.position) && e.mods.isPopupMenu())
        {
            // right-click: take it out of Loopwright (its file stays where it is), or bring removed ones back
            const auto hidden = lp::setting ("hiddenVoices", Array<var>());
            const int nHidden = hidden.getArray() != nullptr ? hidden.getArray()->size() : 0;
            PopupMenu m;
            m.addSectionHeader (name);
            if (isOfficial (name))
            {
                m.addItem (-1, "A Loopwright voice: always here", false);
                m.addSeparator();
                m.addItem (2, "Bring back removed voices (" + String (nHidden) + ")", nHidden > 0);
                SafePointer<VoicePicker> self (this);
                m.showMenuAsync (PopupMenu::Options(), [self] (int r)
                {
                    if (self == nullptr || r != 2) return;
                    lp::setSetting ("hiddenVoices", Array<var>());
                    self->repaint();
                });
                return;
            }
            m.addItem (3, "Set a picture...");
            m.addItem (4, "Remove its picture", pictureFileFor (name).existsAsFile() && ! name.contains (" - "));
            m.addSeparator();
            m.addItem (1, "Remove this voice");
            m.addItem (2, "Bring back removed voices (" + String (nHidden) + ")", nHidden > 0);
            SafePointer<VoicePicker> self (this);
            const String nm = name;
            m.showMenuAsync (PopupMenu::Options(), [self, nm] (int r)
            {
                if (self == nullptr || r <= 0) return;
                if (r == 3)
                {
                    self->chooser = std::make_unique<FileChooser> ("A picture for " + nm, File::getSpecialLocation (File::userPicturesDirectory), "*.jpg;*.jpeg;*.png");
                    self->chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [self, nm] (const FileChooser& fc)
                    {
                        const auto src = fc.getResult();
                        if (self == nullptr || ! src.existsAsFile() || ! ImageFileFormat::loadFrom (src).isValid()) return;
                        pictureFolder().createDirectory();
                        for (auto* ext : { ".jpg", ".png", ".jpeg" }) pictureFolder().getChildFile (File::createLegalFileName (nm) + ext).deleteFile();
                        src.copyFileTo (pictureFolder().getChildFile (File::createLegalFileName (nm) + src.getFileExtension().toLowerCase()));
                        self->message = nm + " has its picture (and so do the voices made from it)";
                        self->repaint();
                    });
                    return;
                }
                if (r == 4)
                {
                    for (auto* ext : { ".jpg", ".png", ".jpeg" }) pictureFolder().getChildFile (File::createLegalFileName (nm) + ext).deleteFile();
                    self->repaint();
                    return;
                }
                Array<var> list;
                if (r == 1)
                {
                    if (auto* a = lp::setting ("hiddenVoices", Array<var>()).getArray()) list = *a;
                    if (! list.contains (nm)) list.add (nm);
                    self->message = "\"" + nm + "\" is removed (right-click any voice to bring it back)";
                }
                lp::setSetting ("hiddenVoices", list);
                self->repaint();
            });
            return;
        }
    for (auto& [cr, name] : cards)
        if (cr.contains (e.position))
        {
            // Jordan's voices: bought on loop-wright.com (Standard or Pro); until then, tried with the spoken tag
            if (isOfficial (name) && ! lp::license::jordanLicensed()) { askToUnlockJordan (name); return; }
            choose (name);
            return;
        }
}

void VoicePicker::choose (const String& name)
{
    // this voice, everywhere (remembered); the page's list follows
    for (int i = 0; i < voices.getNumItems(); ++i)
        if (voices.getItemText (i) == name) { voices.setSelectedItemIndex (i, sendNotification); break; }
    lp::setSetting ("voice", name);
    repaint();
}

void VoicePicker::mouseMove (const MouseEvent& e)
{
    int h = -1;
    for (size_t i = 0; i < cards.size(); ++i) if (cards[i].first.contains (e.position)) h = (int) i;
    // (the index into the shown list, so the card under the mouse lights up)
    if (h >= 0)
    {
        const auto list = shownVoices();
        for (size_t i = 0; i < list.size(); ++i) if (list[i] == cards[(size_t) h].second) h = (int) i;
    }
    if (h != hover) { hover = h; repaint(); }
}

void VoicePicker::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    scroll -= w.deltaY * (w.isSmooth ? 300.0f : 160.0f);
    repaint();
}

// five more voices from one clip of you: made here, then each added to the voice helper
void VoicePicker::makeVariants (const File& recording)
{
    message = "Making five voices from \"" + recording.getFileNameWithoutExtension() + "\"...";
    repaint();
    auto& client = proc.voiceClient;
    SafePointer<VoicePicker> self (this);
    Thread::launch ([recording, &client, self]
    {
        AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<AudioFormatReader> rd (fm.createReaderFor (recording));
        if (rd == nullptr)
        {
            MessageManager::callAsync ([self] { if (self != nullptr) { self->message = "Couldn't read that recording: try a WAV, M4A or MP3"; self->repaint(); } });
            return;
        }
        const int len = (int) jmin<int64> (rd->lengthInSamples, (int64) (rd->sampleRate * 30.0));
        AudioBuffer<float> b ((int) rd->numChannels, len);
        rd->read (&b, 0, len, 0, true, true);
        AudioBuffer<float> mono (1, len);
        mono.clear();
        for (int ch = 0; ch < b.getNumChannels(); ++ch) mono.addFrom (0, 0, b, ch, 0, len, 1.0f / (float) b.getNumChannels());
        const auto dir = lp::appFolder().getChildFile ("voices");
        dir.createDirectory();
        const auto stem = File::createLegalFileName (recording.getFileNameWithoutExtension()).substring (0, 40);
        Array<File> made;
        auto write = [&] (const AudioBuffer<float>& x, const String& name)
        {
            const auto f = dir.getChildFile (name + ".wav");
            f.deleteFile();
            WavAudioFormat wav;
            std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (new FileOutputStream (f), rd->sampleRate, 1, 16, {}, 0));
            if (w != nullptr && w->writeFromAudioSampleBuffer (x, 0, x.getNumSamples())) made.add (f);
        };
        write (mono, stem); // (the recording itself too: a voice of its own)
        for (auto& v : lp::song::variants::all())
            write (lp::song::variants::make (mono, rd->sampleRate, v), stem + " - " + v.name);
        MessageManager::callAsync ([self, made, &client, stem]
        {
            for (auto& f : made) client.addVoice (f);
            if (self != nullptr)
            {
                self->message = "Adding " + String (made.size()) + " voices made from \"" + stem + "\" (they appear here in a moment)";
                self->tab = 1;
                self->showTabs();
            }
        });
    });
}
} // namespace lp::ai

// the 2 free voices are used: unlock more (buy, or a code you have)
void lp::ai::VoicePicker::askToUnlockVoices()
{
    auto* w = new AlertWindow ("More voices of your own",
                               "You've used your " + String (lp::license::freeVoices()) + " free voices. Unlock as many as you like: "
                               "buy it on loop-wright.com and paste the code you get here. "
                               "(To make room instead, right-click one of yours and Remove this voice.)",
                               MessageBoxIconType::NoIcon);
    w->addTextEditor ("code", {}, "Your code");
    w->addButton ("Unlock", 1, KeyPress (KeyPress::returnKey));
    if (lp::license::buyUrl().isNotEmpty()) w->addButton ("Buy more voices", 2);
    w->addButton ("Not now", 0, KeyPress (KeyPress::escapeKey));
    Component::SafePointer<lp::ai::VoicePicker> self (this);
    w->enterModalState (true, ModalCallbackFunction::create ([self, w] (int r)
    {
        if (self == nullptr) return;
        if (r == 2) { URL (lp::license::buyUrl() + (lp::license::buyUrl().contains ("?") ? "&" : "?") + "item=voices").launchInDefaultBrowser(); return; }
        if (r != 1) return;
        const bool ok = lp::license::unlockMoreVoices (w->getTextEditorContents ("code"));
        self->message = ok ? String ("Unlocked: add as many voices of your own as you like") : String ("That code isn't a voices code: check it and try again");
        self->repaint();
    }), true);
}

// Jordan's voices: a license (Standard or Pro) bought on loop-wright.com, or a code you have
void lp::ai::VoicePicker::askToUnlockJordan (const String& voiceName)
{
    auto* w = new AlertWindow (voiceName + " needs a voice license",
                               "Jordan's voices are licensed for your songs:\n\n"
                               "Standard: $150, plus 50% of a song's earnings when it uses more than 30 seconds of the voice.\n"
                               "Pro: $300, plus 25% of a song's earnings when it uses more than 60 seconds of the voice.\n\n"
                               "You read and accept the voice license before you pay. Paste the code you get here.\n\n"
                               "Or try it first: without a license, its vocals have a spoken tag over them.",
                               MessageBoxIconType::NoIcon);
    w->addTextEditor ("code", {}, "Your voice license code");
    w->addButton ("Unlock", 1, KeyPress (KeyPress::returnKey));
    if (lp::license::buyUrl().isNotEmpty()) { w->addButton ("Buy Standard", 2); w->addButton ("Buy Pro", 3); }
    w->addButton ("Try it (with tag)", 4);
    w->addButton ("Not now", 0, KeyPress (KeyPress::escapeKey));
    Component::SafePointer<lp::ai::VoicePicker> self (this);
    const String nm = voiceName;
    w->enterModalState (true, ModalCallbackFunction::create ([self, w, nm] (int r)
    {
        if (self == nullptr) return;
        if (r == 4)
        {
            self->choose (nm);
            self->message = "\"" + nm + "\": its vocals have a spoken tag until you buy a license";
            self->repaint();
            return;
        }
        if (r == 2 || r == 3)
        {
            const auto origin = URL (lp::license::buyUrl()).getOrigin();
            URL (origin + "/voices?tier=" + (r == 2 ? "standard" : "pro")).launchInDefaultBrowser();
            return;
        }
        if (r != 1) return;
        const bool ok = lp::license::unlockJordan (w->getTextEditorContents ("code"));
        self->message = ok ? "Unlocked: Jordan's voices (" + lp::license::jordanTier() + " license)" : String ("That code isn't a voice license code: check it and try again");
        if (ok) self->choose (nm);
        self->repaint();
    }), true);
}
