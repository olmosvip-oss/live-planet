#pragma once
#include <juce_cryptography/juce_cryptography.h>
#include "AppFolder.h"

// Offline license keys. You (the seller) keep a private key; each buyer gets a code made from their
// name with tools/Keygen. The plugin only holds the public key, so it can check a code but never
// make one. No internet, no server.
//
// Licensing is OFF unless the build is given a public key (cmake -DLOOPWRIGHT_PUBLIC_KEY=...),
// so test builds for you and your friends just work.
namespace lp::license
{

#ifdef LOOPWRIGHT_PUBLIC_KEY
inline juce::String publicKey() { return LOOPWRIGHT_PUBLIC_KEY; }
#else
inline juce::String publicKey() { return {}; }
#endif

inline bool required() { return publicKey().isNotEmpty(); }
constexpr int trialDays = 7;
// what it costs: one payment, yours forever, free updates. $49, or $29 for Black Friday until the end of
// December 1 (California time); after that the window shows $49 by itself.
inline bool onSale() { return juce::Time::getCurrentTime() < juce::Time (2026, 11, 2, 8, 0, 0, 0, false); } // (Dec 2, 00:00 PST, in UTC)
inline juce::String regularPrice() { return "$49"; }
inline juce::String saleName() { return juce::String (juce::CharPointer_UTF8 ("BLACK FRIDAY \xc2\xb7 UNTIL DEC 1")); }
inline juce::String price() { return onSale() ? "$29" : regularPrice(); }
inline juce::String priceLine() { return price() + " one-time. Yours forever, with free updates."; }
#ifdef LOOPWRIGHT_BUY_URL
inline juce::String buyUrl() { return LOOPWRIGHT_BUY_URL; }
#else
inline juce::String buyUrl() { return {}; }
#endif

// the number a code signs: a hash of who it's for
inline juce::BigInteger digestOf (const juce::String& owner, const juce::String& prefix = "Loopwright license v1|")
{
    const juce::SHA256 sha ((prefix + owner.trim()).toUTF8());
    juce::BigInteger v;
    v.loadFromMemoryBlock (sha.getRawData());
    v.setBit (255, false); // always smaller than the key
    return v;
}

// a code is "LW1-" + base64 of "owner\nsignature-in-hex"
inline juce::String makeCode (const juce::String& owner, const juce::String& privateKeyText, const juce::String& prefix = "Loopwright license v1|")
{
    juce::BigInteger v = digestOf (owner, prefix);
    juce::RSAKey (privateKeyText).applyToValue (v);
    juce::MemoryOutputStream mo;
    mo << owner.trim() << "\n" << v.toString (16);
    return "LW1-" + juce::Base64::toBase64 (mo.getData(), mo.getDataSize());
}

// who a code was made for, or empty if it isn't a real code
inline juce::String ownerOf (const juce::String& code, const juce::String& publicKeyText = publicKey(), const juce::String& prefix = "Loopwright license v1|")
{
    const auto t = code.trim().removeCharacters (" \n\r\t");
    if (! t.startsWith ("LW1-") || publicKeyText.isEmpty()) return {};
    juce::MemoryOutputStream mo;
    if (! juce::Base64::convertFromBase64 (mo, t.substring (4))) return {};
    const auto text = mo.toString();
    const auto owner = text.upToFirstOccurrenceOf ("\n", false, false);
    juce::BigInteger sig;
    sig.parseString (text.fromFirstOccurrenceOf ("\n", false, false), 16);
    if (owner.isEmpty() || sig.isZero()) return {};
    juce::RSAKey (publicKeyText).applyToValue (sig);
    return sig == digestOf (owner, prefix) ? owner : juce::String();
}

// where the license files are kept: the app folder (LP_LICENSE_DIR instead, for recording demos with a demo key)
inline juce::File licenseFolder()
{
    if (auto* d = std::getenv ("LP_LICENSE_DIR")) return juce::File (juce::String (d));
    return lp::appFolder();
}
inline juce::File codeFile()  { return licenseFolder().getChildFile ("license.txt"); }
inline juce::File trialFile() { return licenseFolder().getChildFile (".first-run"); }

// ---- each license works on 2 computers
// Activating asks the website once (loop-wright.com/api/activate), which keeps the list of computers and
// answers with this computer's activation: the owner and this computer's id, signed with the private key.
// It's saved next to the code and checked with the public key, so it only works on this computer and
// Loopwright never needs the internet again. If the site can't be reached, it works anyway for a while
// (graceDays) and asks again at the next start.
constexpr int graceDays = 14;
inline juce::File activationFile() { return licenseFolder().getChildFile ("activation.txt"); }
inline juce::File pendingFile()    { return licenseFolder().getChildFile (".activation-pending"); }
inline juce::File refusedFile()    { return licenseFolder().getChildFile (".activation-refused"); }
// Jordan's voice license: its own code (jordan-voice-license.txt) and its own activation, also on 2 computers
inline juce::File jordanFile()                { return licenseFolder().getChildFile ("jordan-voice-license.txt"); }
inline juce::File jordanActivationFile()      { return licenseFolder().getChildFile ("jordan-activation.txt"); }
inline juce::File jordanPendingFile()         { return licenseFolder().getChildFile (".jordan-activation-pending"); }
inline juce::File jordanRefusedFile()         { return licenseFolder().getChildFile (".jordan-activation-refused"); }
inline juce::String jordanActivationPrefix()  { return "Loopwright Jordan activation v1|"; }

// this computer: a stable id (a hash, so nothing personal is sent) and its name (shown in the list of computers)
inline juce::String machineId()
{
    static const juce::String id = []
    {
        auto raw = juce::SystemStats::getUniqueDeviceID();
        if (raw.isEmpty()) raw = juce::SystemStats::getComputerName() + juce::SystemStats::getLogonName();
        return juce::SHA256 (("Loopwright machine|" + raw).toUTF8()).toHexString().substring (0, 32);
    }();
    return id;
}
inline juce::String machineName() { return juce::SystemStats::getComputerName().substring (0, 40); }

inline bool activationValid (const juce::String& owner, const juce::String& token, const juce::String& publicKeyText = publicKey(),
                             const juce::String& prefix = "Loopwright activation v1|")
{
    if (owner.isEmpty() || token.trim().isEmpty() || publicKeyText.isEmpty()) return false;
    juce::BigInteger sig;
    sig.parseString (token.trim(), 16);
    if (sig.isZero()) return false;
    juce::RSAKey (publicKeyText).applyToValue (sig);
    return sig == digestOf (owner.trim() + "|" + machineId(), prefix);
}

// the saved code's owner when this computer may use it (empty = not activated here)
inline juce::String activatedTo()
{
    // (asked on every repaint: only check again when the files change)
    struct Cache { juce::String code, token, owner; bool refused = false; juce::int64 pending = 0; };
    static Cache cache;
    static juce::SpinLock lock;
    const auto codeText = codeFile().loadFileAsString().trim();
    const auto token = activationFile().loadFileAsString().trim();
    const bool refused = refusedFile().existsAsFile();
    const auto pending = pendingFile().loadFileAsString().getLargeIntValue();
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (cache.code == codeText && cache.token == token && cache.refused == refused && cache.pending == pending) return cache.owner;
    }
    juce::String result;
    const auto owner = ownerOf (codeText);
    if (owner.isNotEmpty())
    {
        if (activationValid (owner, token)) result = owner;
        else if (! refused)
        {
            // not activated online yet (the site couldn't be reached, or a code from before activations):
            // fine for a while, counted from the first time this computer noticed
            auto started = pending;
            if (started <= 0) { started = juce::Time::currentTimeMillis(); pendingFile().replaceWithText (juce::String (started)); }
            if (juce::Time::currentTimeMillis() - started < (juce::int64) graceDays * 24 * 60 * 60 * 1000) result = owner;
        }
    }
    const juce::SpinLock::ScopedLockType sl (lock);
    cache = { codeText, token, result, refused, pendingFile().loadFileAsString().getLargeIntValue() };
    return result;
}

// why the site said no, for the license window (empty = it didn't)
inline juce::String refusal() { return refusedFile().loadFileAsString().trim(); }

// the website's activation address (from the Buy link: https://site/api/checkout -> https://site/api/activate)
inline juce::String activateUrl()
{
    const auto buy = buyUrl();
    return buy.isEmpty() ? juce::String() : juce::URL (buy).getOrigin() + "/api/activate";
}

struct Answer { bool ok = false, reached = false; juce::String message; };

// asks the website (call it off the message thread: it waits for the answer, 15 seconds at most)
inline Answer askSite (const juce::String& code, const juce::String& action, const juce::String& product = {})
{
    Answer a;
    const auto url = activateUrl();
    if (url.isEmpty()) return a;
    auto* o = new juce::DynamicObject();
    o->setProperty ("code", code.trim());
    o->setProperty ("machine", machineId());
    o->setProperty ("name", machineName());
    o->setProperty ("action", action);
    if (product.isNotEmpty()) o->setProperty ("product", product); // ("jordan": the voice license's own computers)
    int status = 0;
    std::unique_ptr<juce::InputStream> in (juce::URL (url).withPOSTData (juce::JSON::toString (juce::var (o), true))
        .createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                .withExtraHeaders ("Content-Type: application/json")
                                .withConnectionTimeoutMs (15000)
                                .withStatusCode (&status)));
    if (in == nullptr || status >= 500 || status == 0) return a; // not reached: try again later
    const auto reply = juce::JSON::parse (in->readEntireStreamAsString());
    a.reached = reply.isObject();
    a.ok = (bool) reply.getProperty ("ok", false);
    a.message = reply.getProperty ("message", "").toString();
    if (a.ok && action == "activate" && product == "jordan")
    {
        jordanActivationFile().replaceWithText (reply.getProperty ("token", "").toString());
        jordanPendingFile().deleteFile();
        jordanRefusedFile().deleteFile();
    }
    else if (a.ok && action == "activate")
    {
        activationFile().replaceWithText (reply.getProperty ("token", "").toString());
        pendingFile().deleteFile();
        refusedFile().deleteFile();
    }
    return a;
}

// pasting a code: checked here first, then on the website. Answers an empty string when it worked,
// else what to tell the person. (Off the message thread.)
inline juce::String activate (const juce::String& code)
{
    const auto owner = ownerOf (code);
    if (owner.isEmpty()) return "That code didn't work. Copy the whole thing, starting with LW1-";
    codeFile().getParentDirectory().createDirectory();
    const auto a = askSite (code, "activate");
    if (a.reached && ! a.ok) return a.message.isNotEmpty() ? a.message : "That code didn't work.";
    codeFile().replaceWithText (code.trim());
    if (! a.reached) { refusedFile().deleteFile(); } // (couldn't reach the site: works for now, asks again at the next start)
    return {};
}

// at every start: a code without this computer's activation yet asks the site again (quietly)
inline void checkInBackground()
{
    if (! required()) return;
    const auto codeText = codeFile().loadFileAsString().trim();
    if (ownerOf (codeText).isEmpty() || activationValid (ownerOf (codeText), activationFile().loadFileAsString())) return;
    static std::atomic<bool> asked { false };
    if (asked.exchange (true)) return; // (once per launch, however many Loopwrights are open)
    juce::Thread::launch ([codeText]
    {
        const auto a = askSite (codeText, "activate");
        if (a.reached && ! a.ok) refusedFile().replaceWithText (a.message.isNotEmpty() ? a.message : "This license can't be used on this computer.");
    });
}

// frees this computer's place, so the license can go on another one (off the message thread)
inline bool deactivate()
{
    const auto codeText = codeFile().loadFileAsString().trim();
    const auto a = askSite (codeText, "deactivate");
    if (! a.reached) return false;
    codeFile().deleteFile();
    activationFile().deleteFile();
    pendingFile().deleteFile();
    refusedFile().deleteFile();
    return true;
}

inline int trialDaysLeft()
{
    auto f = trialFile();
    if (! f.existsAsFile()) { f.getParentDirectory().createDirectory(); f.replaceWithText (juce::String (juce::Time::currentTimeMillis())); }
    const auto started = f.loadFileAsString().getLargeIntValue();
    const auto days = (int) ((juce::Time::currentTimeMillis() - started) / (1000LL * 60 * 60 * 24));
    return juce::jmax (0, trialDays - days);
}

// may it write songs? (always, when licensing is off)
inline bool canWrite() { return ! required() || activatedTo().isNotEmpty() || trialDaysLeft() > 0; }


// ---- more voices of your own: 2 are free, more with a "more voices" code (a code signed for voices, not the app)
inline juce::String voicesPrefix() { return "Loopwright voices v1|"; }
inline int freeVoices() { return 2; }
inline bool moreVoicesUnlocked()
{
    if (! required()) return true; // (a test build: everything open)
    const auto code = licenseFolder().getChildFile ("voices-license.txt").loadFileAsString();
    return ownerOf (code, publicKey(), voicesPrefix()).isNotEmpty();
}
inline bool unlockMoreVoices (const juce::String& code)
{
    if (ownerOf (code, publicKey(), voicesPrefix()).isEmpty()) return false;
    licenseFolder().getChildFile ("voices-license.txt").replaceWithText (code.trim());
    return true;
}

// ---- Jordan's voices (Jordan_Sing, Jordan_Rap): a license bought on loop-wright.com, Standard or Pro.
// The code is signed for "<email>|standard" or "<email>|pro". What each allows is in the voice license
// (loop-wright.com/voice-license): Standard $150 + 50% of a song using over 30 s of the voice,
// Pro $300 + 25% of a song using over 60 s.
inline juce::String jordanPrefix() { return "Loopwright Jordan voice v1|"; }
inline bool isJordanVoice (const juce::String& name)
{
    const auto base = name.upToFirstOccurrenceOf (" - ", false, false).trim();
    return base.equalsIgnoreCase ("Jordan_Sing") || base.equalsIgnoreCase ("Jordan_Rap");
}
// "standard", "pro", or empty (not licensed)
inline juce::String jordanTierOf (const juce::String& code)
{
    const auto owner = ownerOf (code, publicKey(), jordanPrefix());
    const auto tier = owner.fromLastOccurrenceOf ("|", false, false).toLowerCase();
    return owner.contains ("|") && (tier == "standard" || tier == "pro") ? tier : juce::String();
}
// the saved code's tier when this computer may use it: activated here (checked offline), or not activated yet
// but within the grace days (the site couldn't be reached, or a code from before activations; it asks again)
inline juce::String jordanTier()
{
    if (! required()) return "pro"; // (a test build: everything open)
    const auto code = jordanFile().loadFileAsString().trim();
    const auto tier = jordanTierOf (code);
    if (tier.isEmpty()) return {};
    const auto owner = ownerOf (code, publicKey(), jordanPrefix());
    if (activationValid (owner, jordanActivationFile().loadFileAsString(), publicKey(), jordanActivationPrefix())) return tier;
    if (jordanRefusedFile().existsAsFile()) return {};
    auto started = jordanPendingFile().loadFileAsString().getLargeIntValue();
    if (started <= 0) { started = juce::Time::currentTimeMillis(); jordanPendingFile().replaceWithText (juce::String (started)); }
    return juce::Time::currentTimeMillis() - started < (juce::int64) graceDays * 24 * 60 * 60 * 1000 ? tier : juce::String();
}
inline bool jordanLicensed() { return jordanTier().isNotEmpty(); }
// pasting a voice license code: checked here, then the site adds this computer (2 at most). Answers an empty
// string when it worked, else what to tell the person. (Off the message thread: it waits for the site.)
inline juce::String unlockJordan (const juce::String& code)
{
    if (jordanTierOf (code).isEmpty()) return "That code isn't a voice license code: check it and try again";
    const auto a = askSite (code, "activate", "jordan");
    if (a.reached && ! a.ok) return a.message.isNotEmpty() ? a.message : "That voice license can't be used on this computer.";
    jordanFile().getParentDirectory().createDirectory();
    jordanFile().replaceWithText (code.trim());
    jordanRefusedFile().deleteFile();
    return {}; // (not reached: works for the grace days, and asks again at the next start)
}
// at every start: a voice license without this computer's activation yet asks the site again (quietly)
inline void checkJordanInBackground()
{
    if (! required()) return;
    const auto code = jordanFile().loadFileAsString().trim();
    const auto owner = ownerOf (code, publicKey(), jordanPrefix());
    if (owner.isEmpty() || activationValid (owner, jordanActivationFile().loadFileAsString(), publicKey(), jordanActivationPrefix())) return;
    static std::atomic<bool> asked { false };
    if (asked.exchange (true)) return;
    juce::Thread::launch ([code]
    {
        const auto a = askSite (code, "activate", "jordan");
        if (a.reached && ! a.ok) jordanRefusedFile().replaceWithText (a.message.isNotEmpty() ? a.message : "This voice license can't be used on this computer.");
    });
}
} // namespace lp::license
