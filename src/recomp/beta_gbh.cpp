// Luigi's Mansion BETA: the Game Boy Horror's ghost profiles, rewritten (the "creepy GBH
// scans" legend).
//
// The captured-ghost list of the Game Boy Horror (blo/list_2.blo in the archive compressed into
// main.dol at 0x802944C0 + header) has one text box per portrait ghost (panes c01a..c23a and their
// shadow copies c01b..c23b) with the profile text. The game decompresses the archive into its
// heap and J2DTextBox keeps its own copy of each string. Every few seconds the BETA looks for the
// first retail profile in guest memory; when it is there (the archive or a screen built from it),
// every retail profile found is overwritten in place by the BETA's text, which is never longer
// (the rest of the old text's bytes become NULs, so the string ends there).
#include "recomp/beta.h"

#include "port/settings.h"
#include "recomp/guest_util.h"
#include "recomp/hle.h"
#include "recomp/skip.h"
#include "recomp/version.h"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace recomp::beta {
namespace {

struct Profile {
    const char* retail;
    const char* beta;
};

const Profile kProfiles[] = {
    {"Neville spends his afterlife\nreading all the books he\nmissed while living.",
     "He reads to the children every\nnight. They have been gone\nfor years."},
    {"She's stashed her secret\nsavings away to prepare\nfor her long afterlife.",
     "She brushes her hair for\nhours. The mirror stopped\nshowing her face."},
    {"He cries loudly and never sleeps\nthrough the night, but since he was\nborn a ghost, this seems natural.",
     "He cries all night for someone\nto play with. Those who answered\nwere never seen again."},
    {"He prefers all-you-can-eat buffets\nto three meals a day. He ate himself\nto death but still wasn't satisfied.",
     "He ate himself to death and he\nis still hungry. Do not stand\ntoo close to his table."},
    {"Once the local waltz champions,\nthey can no longer compete since\ntheir feet don't touch the floor.",
     "They have danced since the\nnight of the fire. The music\nstopped long ago. They did not."},
    {"Despite playing moving melodies,\nshe has a surly disposition. Oddly\nenough, she loves video games.",
     "She plays the same sad song\nevery night for an audience\nthat never left."},
    {"He wanders the house, endlessly\nsearching for his master's will.\nHe hopes he's included in it!",
     "He searches the halls for his\nmaster's will. He found his\nmaster. He kept him."},
    {"Her close connection to the \nspirit world lets her see nearly\n49 days into the future.",
     "She has seen your future.\nShe will not tell you what\nshe saw. She only smiles."},
    {"This creepy canine will sink\nits chops into any seemingly\nedible thing in sight.",
     "He still guards the yard.\nNobody remembers what\nhe buried out there."},
    {"A product of the mansion's fear and\ndespair, he's not sure who to fear\nor what to despair of these days.",
     "Born of the mansion's fear and\ndespair. Every scream in this\nhouse makes him stronger."},
    {"This kind body builder loves\nmuscles and lilies. Why lilies?\nThey symbolize purity.",
     "He lifts weights day and\nnight, yet he can never put\ndown what he carries."},
    {"She's still knitting the scarf\nshe never finished in life.\nIt now stretches to 871 feet.",
     "She is still knitting the\nscarf she never finished.\nWho is it for? She won't say."},
    {"The Miss Ghost runner-up\nfrom six years ago, she hasn't\nhad much modeling work lately.",
     "The bathwater has been cold\nfor sixty years. She is still\nwaiting for her towel."},
    {"The twins like to play hide and\nseek, but since they both hide,\nthe game never seems to end.",
     "The twins play hide and seek.\nWhen they find you, it will be\nyour turn to hide. Forever."},
    {"Adrift in the darkness, he tends\nto keep to himself, which suits\nthe rest of the family just fine.",
     "He hates the light. If you\never hear him breathing,\nit is already too late."},
    {"When the shy, timid Boos get\ntogether, their body and their\nattitude get BIG!",
     "Alone, a Boo is shy.\nTogether, they become\nsomething else. Something BIG."},
    {"He loves his antique jars so\nmuch that he now lives in one.\nHe can hardly contain himself!",
     "He collects jars. He would\nlove to add a green one\nto his collection."},
    {"Despite failing to sell a single\npainting in 30 years, this stubborn\nman refuses to let his art die.",
     "He painted every ghost in this\nhouse. The last canvas is still\nblank. It is waiting for you."},
    {"What was meant to be a short\nnap seems to have turned into\neternal rest for sweet Sue Pea.",
     "She only meant to take a nap.\nDo not wake her. Those who\ndid were never the same."},
    {"They once belonged to a tightly-wound\ntoy collector, but now they march\non their own without any winding.",
     "They march on their own now.\nNobody winds them anymore.\nNobody knows who did before."},
    {"This strange fellow's icy abode\nmay be the reason he hasn't\nwarmed up to being a ghost.",
     "He climbed the mountain and\nnever came down. Something\nup there is still climbing."},
};

constexpr u32 kMem1 = 0x80000000u, kMem1Size = 0x01800000u;

// Replaces every occurrence of `from` in MEM1; returns how many.
int replaceAll(u8* mem, std::string_view from, std::string_view to) {
    int n = 0;
    u8* end = mem + kMem1Size;
    for (u8* p = mem; p < end;) {
        u8* hit = std::search(p, end, from.begin(), from.end());
        if (hit == end) break;
        std::memcpy(hit, to.data(), to.size());
        std::memset(hit + to.size(), 0, from.size() - to.size());
        n++;
        p = hit + from.size();
    }
    return n;
}

}  // namespace

void gbhFrame(Ctx& c) {
    (void)c;
    if (!port::settings().unused2.creepyProfiles || !ver::usa()) return;
    // Once every 2 s: one search for the first profile, the full pass only when it is there.
    if (skip::guestFrames() % 60 != 17) return;
    u8* mem = static_cast<u8*>(hostp(kMem1));
    const std::string_view probe = kProfiles[0].retail;
    if (std::search(mem, mem + kMem1Size, probe.begin(), probe.end()) == mem + kMem1Size) return;
    int n = 0;
    for (const Profile& p : kProfiles) {
        const std::string_view from = p.retail, to = p.beta;
        if (to.size() > from.size()) continue;
        n += replaceAll(mem, from, to);
    }
    log("[beta] Game Boy Horror profiles rewritten (%d texts)", n);
}

}  // namespace recomp::beta
