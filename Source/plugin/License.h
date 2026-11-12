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
constexpr int trialDays = 14;

// the number a code signs: a hash of who it's for
inline juce::BigInteger digestOf (const juce::String& owner)
{
    const juce::SHA256 sha (("Loopwright license v1|" + owner.trim()).toUTF8());
    juce::BigInteger v;
    v.loadFromMemoryBlock (sha.getRawData());
    v.setBit (255, false); // always smaller than the key
    return v;
}

// a code is "LW1-" + base64 of "owner\nsignature-in-hex"
inline juce::String makeCode (const juce::String& owner, const juce::String& privateKeyText)
{
    juce::BigInteger v = digestOf (owner);
    juce::RSAKey (privateKeyText).applyToValue (v);
    juce::MemoryOutputStream mo;
    mo << owner.trim() << "\n" << v.toString (16);
    return "LW1-" + juce::Base64::toBase64 (mo.getData(), mo.getDataSize());
}

// who a code was made for, or empty if it isn't a real code
inline juce::String ownerOf (const juce::String& code, const juce::String& publicKeyText = publicKey())
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
    return sig == digestOf (owner) ? owner : juce::String();
}

inline juce::File codeFile()  { return lp::appFolder().getChildFile ("license.txt"); }
inline juce::File trialFile() { return lp::appFolder().getChildFile (".first-run"); }

// the saved code's owner (empty = not activated)
inline juce::String activatedTo() { return ownerOf (codeFile().loadFileAsString()); }

inline bool activate (const juce::String& code)
{
    if (ownerOf (code).isEmpty()) return false;
    codeFile().getParentDirectory().createDirectory();
    return codeFile().replaceWithText (code.trim());
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

} // namespace lp::license
