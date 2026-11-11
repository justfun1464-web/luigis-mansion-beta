// Luigi's Mansion BETA: the beta HUD and screen effects (beta.h).
//
// - Health: the retail heart meter (fn_800417F8) is replaced by Luigi's head and his health as a
//   fraction in the chrome digits left in the game archive (kawano/2dsub/l_number_0..9 and
//   l_number_sla, never drawn by the retail game: a leftover of an "HP xx/yy" meter). The
//   meter's own logic still runs (its fade, the counted-down value r13+0x530, the training room's
//   position over Luigi): only its two draws, the heart (fn_8003C250) and the number
//   (fn_8003FAC0), are skipped while it runs, and their position, value and alpha are used.
// - Coins: a spinning coin (the Game Boy Horror's treasure list icon list_treasure_01, turned by
//   narrowing it) with the coins Luigi carries (Luigi +0x12F4) above the health.
// - Scare: when a ghost startles Luigi from behind (Luigi message 2) a purple mist rolls over the screen and
//   "BAHHHH!" pops up; for a few seconds Luigi's health is held at 50 at most (the part above
//   is given back afterwards) and the meter shows it in purple out of 50.
// - Cloth border: a green cloth frame over the edges of the picture (or filling the bordered
//   view's black border, patch 50); during event scenes it slides off and comes back after.
// - Title: a slow pink glow over the title screen, as in the E3 2001 build.
//
// Textures drawn for the BETA edition (scare lettering, mist, cloth, glow) are in
// beta_assets.inc (beta/tools/gen_assets.py); the rest are the game's own, read from the
// player's disc. All 2D goes through the game's textured quad helper fn_80057AD8 (640x480
// orthographic, colour from fn_80057ACC), as the ending pictures (gallery.cpp).
#include "recomp/beta.h"

#include "port/settings.h"
#include "recomp/guest_util.h"
#include "recomp/hle.h"
#include "recomp/rarc.h"
#include "recomp/runtime.h"
#include "recomp/skip.h"
#include "recomp/unused2.h"
#include "recomp/version.h"
#include "recomp/widescreen.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace recomp::beta {
namespace {

#include "recomp/beta_assets.inc"

u16 be16(const u8* p) { return u16(p[0] << 8 | p[1]); }
u32 be32(const u8* p) { return u32(p[0]) << 24 | u32(p[1]) << 16 | u32(p[2]) << 8 | p[3]; }

const auto& opts() { return port::settings().unused2; }

// ---- textures --------------------------------------------------------------------------------
struct Tex {
    u32 obj = 0;  // GXTexObj
    int w = 0, h = 0;
    bool tried = false;
    bool ok() const { return obj != 0; }
};

bool initTex(Ctx& c, Tex& t, const u8* bti, size_t n, const char* what) {
    if (t.tried) return t.ok();
    t.tried = true;
    if (!bti || n < 0x20) {
        log("[beta] texture %s missing", what);
        return false;
    }
    const u32 img = unused2::extraAlloc(u32(n), 32), obj = unused2::extraAlloc(0x20, 32);
    if (!img || !obj) return false;
    std::memcpy(hostp(img), bti, n);
    t.w = be16(bti + 2);
    t.h = be16(bti + 4);
    gu::call(c, GU_SYM("GXInitTexObj"), {obj, img + be32(bti + 0x1C), u32(t.w), u32(t.h), bti[0], bti[6], bti[7], 0});
    t.obj = obj;
    return true;
}

// The resident game archive, read once from the disc (or the BETA files) on the host.
rarc::Archive* gameArchive() {
    static std::unique_ptr<rarc::Archive> sArc;
    static bool sTried = false;
    if (!sTried) {
        sTried = true;
        auto a = std::make_unique<rarc::Archive>();
        if (a->parse(rarc::decompress(rarc::readDisc(ver::pal() ? "/Game/game.szp" : "/Game/game_usa.szp"))))
            sArc = std::move(a);
        else
            log("[beta] game archive not readable: beta HUD textures missing");
    }
    return sArc.get();
}

bool gameTex(Ctx& c, Tex& t, const char* member) {
    if (t.tried) return t.ok();
    rarc::Archive* a = gameArchive();
    const rarc::Item* it = a ? a->find(member) : nullptr;
    if (!it || it->dir) {
        t.tried = true;
        log("[beta] %s not in the game archive", member);
        return false;
    }
    const rarc::Bytes d = rarc::decompress(it->data);
    return initTex(c, t, d.data(), d.size(), member);
}

Tex sDigit[10], sSlash, sHead, sCoin, sBahhh, sMist, sCloth, sGlow;
bool sTexLoaded = false;

bool loadAll(Ctx& c) {
    if (sTexLoaded) return sDigit[0].ok();
    sTexLoaded = true;
    char path[64];
    for (int i = 0; i < 10; i++) {
        std::snprintf(path, sizeof path, "kawano/2dsub/l_number_%d.bti", i);
        gameTex(c, sDigit[i], path);
    }
    gameTex(c, sSlash, "kawano/2dsub/l_number_sla.bti");
    gameTex(c, sHead, "kawano/dmman/pressed/luigi_icon.bti");
    gameTex(c, sCoin, "kawano/base/timg/list_treasure_01.bti");
    initTex(c, sBahhh, kBeta_bahhh, sizeof kBeta_bahhh, "bahhh");
    initTex(c, sMist, kBeta_mist, sizeof kBeta_mist, "mist");
    initTex(c, sCloth, kBeta_cloth, sizeof kBeta_cloth, "cloth");
    initTex(c, sGlow, kBeta_glow, sizeof kBeta_glow, "glow");
    log("[beta] HUD textures loaded (digits %s, head %s, coin %s)", sDigit[0].ok() ? "ok" : "MISSING",
        sHead.ok() ? "ok" : "MISSING", sCoin.ok() ? "ok" : "MISSING");
    return sDigit[0].ok();
}

// ---- drawing ---------------------------------------------------------------------------------
u32 sColorWord = 0;  // guest u32 (RGBA) for fn_80057ACC

void quad(Ctx& c, const Tex& t, int x, int y, int w, int h, u32 rgba, int alpha) {
    if (!t.ok() || w <= 0 || h <= 0 || alpha <= 0) return;
    if (!sColorWord) sColorWord = unused2::extraAlloc(8, 4);
    wr32(sColorWord, rgba);
    gu::call(c, GU_SYM("fn_80057ACC"), {sColorWord});
    gu::call(c, GU_SYM("fn_80057AD8"), {t.obj, u32(x), u32(y), u32(w), u32(h), u32(std::min(alpha, 255))});
}

// Width of the screen beyond the 4:3 frame on each side, in the game's 640x480 units.
int sideExtent() { return ws::active() ? int(std::ceil(320.0f / ws::factor() - 320.0f)) + 2 : 0; }

// A number in the chrome digits; returns the x after it. `glyph` is the digit cell height.
int drawNumber(Ctx& c, int x, int y, int value, int glyph, u32 rgba, int alpha, bool shadow) {
    const int gw = glyph * 3 / 4, adv = glyph * 5 / 8;
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d", std::max(0, value));
    for (const char* p = buf; *p; p++) {
        const Tex& t = sDigit[*p - '0'];
        if (shadow) quad(c, t, x + 2, y + 2, gw, glyph, 0x000000FFu, alpha * 3 / 5);
        quad(c, t, x, y, gw, glyph, rgba, alpha);
        x += adv;
    }
    return x;
}

// ---- state -----------------------------------------------------------------------------------
u32 luigiActor() {
    const u32 l = rd32(VA(0x803D48A0));
    return gu::inMem1(l) ? l : 0;
}

struct Scare {
    int frames = 0;     // left
    int total = 0;
    u32 actor = 0;
    int heldBack = 0;   // health above 50 taken away for the scare, given back at its end
} sScare;
constexpr int kScareFrames = 150;  // 5 s at the game's 30 Hz
constexpr int kScareMax = 50;

float sBorderK = 1.0f;  // cloth border: 1 framed, 0 whole screen (event scenes)

bool eventRunning() { return rd32(VA(0x803D2D40) + 0x0C) != 0; }  // event interpreter cursor

// The health meter's draws, skipped and recorded while it runs.
struct Meter {
    bool active = false;
    bool seen = false;
    int x = 0, y = 0, value = 0, alpha = 0;
} sMeter;

}  // namespace

// ---- hooks used by other files -----------------------------------------------------------------
bool hudOn() { return opts().betaHud && !ver::jpn(); }

void meterBegin() { sMeter = {hudOn(), false, 0, 0, 0, 0}; }

bool meterSkipHeart() { return sMeter.active; }

bool meterSkipNumber(Ctx& c) {
    if (!sMeter.active) return false;
    if ((c.r[6] & 0xFF) == 0xFF) {  // the front copy (the shadow copy has colour 0)
        sMeter.seen = true;
        sMeter.x = s32(c.r[3]);
        sMeter.y = s32(c.r[4]);
        sMeter.value = s32(c.r[5]);
        sMeter.alpha = int(c.r[9] & 0xFF);
    }
    return true;
}

void meterEnd(Ctx& c) {
    const Meter m = sMeter;
    sMeter.active = false;
    if (!m.active || !m.seen || m.alpha <= 0 || !loadAll(c)) return;
    static bool sLogged = false;
    if (!sLogged) {
        sLogged = true;
        log("[beta] health meter at %d,%d (value %d, alpha %d)", m.x, m.y, m.value, m.alpha);
    }
    const u32 l = luigiActor();
    const bool scared = sScare.frames > 0;
    int maxHp = l ? s16(rd16(l + 0xFFC)) : 100;
    if (maxHp <= 0) maxHp = 100;
    if (scared) maxHp = std::min(maxHp, kScareMax);
    const u32 col = scared ? 0xC890FFFFu : 0xFFFFFFFFu;
    // Luigi's head where the heart was, the fraction to its right.
    const int head = 44;
    const int hx = m.x - head / 2, hy = m.y - head / 2;
    quad(c, sHead, hx + 2, hy + 2, head, head, 0x000000FFu, m.alpha / 2);
    quad(c, sHead, hx, hy, head, head, scared ? 0xE0B0FFFFu : 0xFFFFFFFFu, m.alpha);
    const int glyph = 26, gy = m.y - glyph / 2 + 2;
    int x = hx + head + 2;
    x = drawNumber(c, x, gy, std::min(m.value, maxHp), glyph, col, m.alpha, true);
    const int sw = glyph * 3 / 4;
    quad(c, sSlash, x + 2, gy + 2, sw, glyph, 0x000000FFu, m.alpha * 3 / 5);
    quad(c, sSlash, x, gy, sw, glyph, col, m.alpha);
    x += glyph * 5 / 8;
    drawNumber(c, x, gy, maxHp, glyph, col, m.alpha, true);
    // Coins above: a turning coin and the count.
    if (l && sCoin.ok()) {
        const int coins = s32(rd32(l + 0x12F4));
        const float t = float(skip::guestFrames()) * 0.11f;
        const float turn = std::fabs(std::cos(t));
        const int cs = 24, cw = std::max(2, int(cs * (0.12f + 0.88f * turn) + 0.5f));
        const int cx = m.x - cw / 2, cy = hy - cs - 6;
        quad(c, sCoin, cx, cy, cw, cs, turn > 0.15f ? 0xFFFFFFFFu : 0xC0A040FFu, m.alpha);
        drawNumber(c, m.x + cs / 2 + 6, cy + 3, coins, 20, 0xFFE890FFu, m.alpha, true);
    }
}

// Start of the HUD (fn_8003E3D0): the cloth frame, then the scare's mist and lettering, under the HUD.
void hudBegin(Ctx& c) {
    if (!loadAll(c)) return;
    const int ext = sideExtent();
    // Cloth frame. Drawn over the edges of the picture, at the edges of the display (Hor+ wide
    // screens included), so it always meets the 3D view. With the bordered view (patch 50) the
    // view itself is framed at 40,30 - 600,430 in the 4:3 frame and the cloth fills up to it.
    if (opts().clothBorder && sBorderK > 0.001f) {
        const bool framed = g_patches && g_patches[50];
        const int bx = int(40.0f * sBorderK + 0.5f), top = int(30.0f * sBorderK + 0.5f), bottom = int(50.0f * sBorderK + 0.5f);
        const int left = -ext, right = 640 + ext, tile = 64;
        const int inL = framed ? bx : left + 2 + bx, inR = framed ? 640 - bx : right - 2 - bx;
        auto band = [&](int x0, int y0, int x1, int y1) {
            for (int y = y0; y < y1; y += tile)
                for (int x = x0; x < x1; x += tile)
                    quad(c, sCloth, x, y, std::min(tile, x1 - x), std::min(tile, y1 - y), 0xFFFFFFFFu, 255);
        };
        band(left, 0, right, top);
        band(left, 480 - bottom, right, 480);
        band(left, top, inL, 480 - bottom);
        band(inR, top, right, 480 - bottom);
        // a dark seam on the inner edge, like the shadow of the cloth on the picture
        const u32 sh = 0x000000FFu;
        quad(c, sCloth, inL, top, 3, 480 - bottom - top, sh, 120);
        quad(c, sCloth, inR - 3, top, 3, 480 - bottom - top, sh, 120);
        quad(c, sCloth, inL, top, inR - inL, 3, sh, 120);
        quad(c, sCloth, inL, 480 - bottom - 3, inR - inL, 3, sh, 120);
    }
    // Scare: purple mist, then the lettering.
    if (sScare.frames > 0 && opts().scareFx) {
        const int age = sScare.total - sScare.frames;
        const float in = std::min(1.0f, float(age) / 8.0f), out = std::min(1.0f, float(sScare.frames) / 40.0f);
        const int a = int(150.0f * in * out);
        const int drift = int(float(age) * 1.5f) % 256;
        for (int k = 0; k < 2; k++) {
            const int off = k ? 128 - drift / 2 : -drift;
            for (int y = -128; y < 480; y += 256)
                for (int x = -ext - 256 + off; x < 640 + ext; x += 256)
                    quad(c, sMist, x, y + (k ? 64 : 0), 256, 256, k ? 0x7040A0FFu : 0xA070D0FFu, a);
        }
        if (age < 70) {
            // pops in big, settles with a shake, then fades
            const float pop = age < 6 ? 0.4f + 0.12f * float(age) : 1.0f + 0.06f * std::sin(float(age) * 1.3f) * std::max(0.0f, 1.0f - float(age) / 30.0f);
            const int w = int(400.0f * pop), h = int(100.0f * pop);
            const int fade = age < 50 ? 255 : 255 * (70 - age) / 20;
            const int sx = int(4.0f * std::sin(float(age) * 2.7f)) * (age < 25);
            quad(c, sBahhh, 320 - w / 2 + sx, 150 - h / 2, w, h, 0xFFFFFFFFu, fade);
        }
    }
}

bool titleGlowOn() { return opts().titleGlow && ver::usa(); }

void titleDraw(Ctx& c) {
    if (!loadAll(c)) return;
    const float t = float(skip::guestFrames()) * 0.035f;
    const int a = int(150.0f + 50.0f * std::sin(t));
    const int ext = sideExtent();
    // behind the logo: a wide pink haze and a brighter core
    quad(c, sGlow, -ext - 80, -60, 800 + 2 * ext, 560, 0xFF40B0FFu, a / 2);
    quad(c, sGlow, 30, 60, 580, 300, 0xFF60C8FFu, a);
    quad(c, sGlow, 140, 120, 360, 180, 0xFFA0E0FFu, a * 2 / 3);
}

bool scareOn() { return opts().scareFx && !ver::jpn(); }

void onStartle(Ctx& c, u32 luigi) {
    (void)c;
    if (!scareOn() || !gu::inMem1(luigi)) return;
    if (sScare.frames > 0 && sScare.actor == luigi) {
        sScare.frames = std::max(sScare.frames, kScareFrames / 2);  // a second scare keeps it going
        return;
    }
    // at most one scare every 20 s: some rooms startle Luigi again and again
    static u32 sLast = 0;
    const u32 now = skip::guestFrames();
    if (sLast && now - sLast < 600) return;
    sLast = now;
    sScare = {kScareFrames, kScareFrames, luigi, 0};
    const int hp = s16(rd16(luigi + 0xFC));
    if (hp > kScareMax) {
        sScare.heldBack = hp - kScareMax;
        wr16(luigi + 0xFC, u16(kScareMax));
    }
    log("[beta] scare at frame %u (health %d, %d held back)", skip::guestFrames(), hp, sScare.heldBack);
}

void hudFrame(Ctx& c) {
    (void)c;
    // Border: slide off during event scenes, back when they end.
    const float target = eventRunning() ? 0.0f : 1.0f;
    sBorderK += std::clamp(target - sBorderK, -1.0f / 18.0f, 1.0f / 18.0f);
    // Scare countdown; at its end the health it held back comes back (not after a game over).
    if (sScare.frames > 0) {
        const u32 l = luigiActor();
        if (l != sScare.actor) {
            sScare = {};
        } else if (--sScare.frames == 0) {
            const int hp = s16(rd16(l + 0xFC)), maxHp = s16(rd16(l + 0xFFC));
            if (hp > 0 && sScare.heldBack > 0) {
                const int back = std::min(maxHp > 0 ? maxHp : 100, hp + sScare.heldBack);
                wr16(l + 0xFC, u16(back));
                log("[beta] scare over: health %d -> %d", hp, back);
            }
            sScare = {};
        } else if (s16(rd16(l + 0xFC)) > kScareMax) {
            wr16(l + 0xFC, u16(kScareMax));  // healed during the scare: still held at 50
        }
    }
}

// View 0's rectangle for the cloth border's slide (fn_80010F20, after the game set it).
bool borderRect(int& x, int& y, int& w, int& h) {
    if (!opts().clothBorder) return false;
    x = int(40.0f * sBorderK + 0.5f);
    y = int(30.0f * sBorderK + 0.5f);
    w = 640 - 2 * x;
    h = 480 - y - int(50.0f * sBorderK + 0.5f);
    return true;
}

}  // namespace recomp::beta

namespace recomp {

// Heart and number of the health meter: skipped while the BETA meter replaces them.
HLE(fn_8003C250) {
    if (beta::meterSkipHeart()) return;
    callOriginal(c, "fn_8003C250");
}
HLE(fn_8003FAC0) {
    if (beta::meterSkipNumber(c)) return;
    callOriginal(c, "fn_8003FAC0");
}
// In-game HUD.
HLE(fn_8003E3D0) {
    beta::hudBegin(c);
    callOriginal(c, "fn_8003E3D0");
}
// Title screen. TitleScene::draw is the title picture (fn_8003AE7C) and then the 2D layer with the
// logo (fn_80007F90): the glow goes between them, behind the logo.
HLE(draw__10TitleSceneFv) {
    if (!beta::titleGlowOn()) return callOriginal(c, "draw__10TitleSceneFv");
    gu::call(c, VA(0x8003AE7C));
    beta::titleDraw(c);
    gu::call(c, VA(0x80007F90));
}

}  // namespace recomp
