// Loopwright license keys (keep this tool and your private key to yourself).
//
//   Keygen --new <folder>                 make your key pair: private.key (secret!) and public.key
//   Keygen --make "Buyer Name" <private.key>   print a license code for a buyer
//   Keygen --check <code> <public.key>    check a code
//
// Build the plugin with the public key to switch licensing on:
//   cmake -B build -DLOOPWRIGHT_PUBLIC_KEY="$(cat public.key)"
#include "../Source/plugin/License.h"
#include <cstdio>

int main (int argc, char** argv)
{
    const juce::String cmd = argc > 1 ? argv[1] : "";
    if (cmd == "--new" && argc > 2)
    {
        juce::File dir (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
        dir.createDirectory();
        juce::RSAKey pub, priv;
        juce::RSAKey::createKeyPair (pub, priv, 1024);
        dir.getChildFile ("private.key").replaceWithText (priv.toString());
        dir.getChildFile ("public.key").replaceWithText (pub.toString());
        std::printf ("wrote %s and public.key\nKEEP private.key SECRET and backed up: lose it and you can't make codes; leak it and anyone can.\n",
                     dir.getChildFile ("private.key").getFullPathName().toRawUTF8());
        return 0;
    }
    if (cmd == "--make" && argc > 3)
    {
        const auto priv = juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]).loadFileAsString().trim();
        std::printf ("%s\n", lp::license::makeCode (argv[2], priv).toRawUTF8());
        return 0;
    }
    if (cmd == "--check" && argc > 3)
    {
        const auto pub = juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]).loadFileAsString().trim();
        const auto owner = lp::license::ownerOf (argv[2], pub);
        std::printf ("%s\n", owner.isNotEmpty() ? ("valid, for: " + owner).toRawUTF8() : "NOT valid");
        return owner.isNotEmpty() ? 0 : 1;
    }
    std::printf ("usage: Keygen --new <folder> | --make \"Name\" <private.key> | --check <code> <public.key>\n");
    return 1;
}
