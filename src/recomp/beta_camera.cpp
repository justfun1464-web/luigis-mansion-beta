// Luigi's Mansion BETA: the camera while a ghost speaks (beta.h).
//
// A dialogue window draws its speaker's icon with fn_8003BEEC from the icon sprites at
// lbl_803CE7D0 + 0xA90 (0x18 bytes each, icons 1-19 named by lbl_803054A8: 1 the professor,
// 2 ghost, 3 Luigi, 4 Boo, 5 Bowser, 6 Chauncey, 7 Mario, 8 Lydia, 9 Madame Clairvoya, 10 Melody,
// 11 Nana, 12 the twins, 13 Vincent Van Gore, 14 Sue Pea, 15 Shivers, 16/17 Toad, 18 Jarvis,
// 19 Sir Weston). While a window shows a ghost's icon, the main camera's view matrix
// (lbl_803A3820 + 0x18C, built by fn_8000853C with MTXLookAt from the camera's eye, up and
// target) is turned after the game built it: a dutch tilt with a slow sway, a move in towards
// what the camera looks at, and a lower (or higher) angle that keeps that point in the middle.
// Each ghost has its own. The camera's eye and target stay as the game set them; only the matrix
// the frame is drawn with changes, and what is built from it afterwards (the portal culling
// matrix, fn_80014228; the lights' view space). Eased in and out over half a second.
#include "recomp/beta.h"

#include "port/settings.h"
#include "recomp/hle.h"
#include "recomp/runtime.h"
#include "recomp/skip.h"
#include "recomp/version.h"

#include <algorithm>
#include <cmath>

namespace recomp::beta {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kDeg = kPi / 180.0f;

struct Look {
    float roll;   // degrees, dutch tilt
    float sway;   // degrees, slow back-and-forth on top
    float freq;   // sway cycles per game frame
    float dolly;  // share of the distance to the target the camera moves in
    float low;    // share of the distance the camera moves down (negative: up), looking at the target
    float shake;  // degrees of jitter
};

// Indexed by icon number (0 and the non-ghosts unused).
const Look kLooks[20] = {
    {},
    {},                                   // 1 professor
    {9.0f, 3.0f, 0.020f, 0.12f, 0.12f, 0.0f},    // 2 ghost
    {},                                   // 3 Luigi
    {-14.0f, 6.0f, 0.055f, 0.08f, 0.00f, 0.0f},  // 4 Boo: tilted and bobbing
    {5.0f, 2.0f, 0.015f, 0.22f, 0.30f, 0.6f},    // 5 Bowser: from below, trembling
    {-12.0f, 12.0f, 0.025f, 0.10f, 0.05f, 0.0f}, // 6 Chauncey: a cradle rocking
    {},                                   // 7 Mario
    {11.0f, 2.0f, 0.015f, 0.15f, -0.10f, 0.0f},  // 8 Lydia: from above
    {-8.0f, 8.0f, 0.010f, 0.20f, 0.15f, 0.0f},   // 9 Madame Clairvoya: slow, dreamy
    {7.0f, 4.0f, 0.030f, 0.12f, 0.08f, 0.0f},    // 10 Melody
    {-10.0f, 3.0f, 0.015f, 0.10f, -0.12f, 0.0f}, // 11 Nana: from above
    {15.0f, 5.0f, 0.045f, 0.10f, 0.00f, 0.0f},   // 12 the twins: playful, steep
    {-16.0f, 3.0f, 0.020f, 0.18f, 0.10f, 0.0f},  // 13 Vincent Van Gore
    {12.0f, 6.0f, 0.035f, 0.10f, -0.08f, 0.0f},  // 14 Sue Pea
    {-7.0f, 2.0f, 0.012f, 0.14f, 0.18f, 0.0f},   // 15 Shivers
    {},                                   // 16 green Toad
    {},                                   // 17 Toad
    {10.0f, 9.0f, 0.040f, 0.10f, 0.05f, 0.0f},   // 18 Jarvis
    {-9.0f, 2.0f, 0.015f, 0.20f, 0.20f, 0.3f},   // 19 Sir Weston: from below, shivering
};

bool ghostIcon(int icon) { return icon >= 1 && icon <= 19 && (kLooks[icon].roll != 0.0f); }

int sIcon = 0;          // last speaker icon drawn
u32 sIconFrame = 0;     // game frame it was drawn
int sLookIcon = 0;      // the look being shown (kept while easing out)
float sWeight = 0.0f;   // 0 off .. 1 full
u32 sLastFrame = ~0u;

float noise(u32 n) {
    n = (n << 13) ^ n;
    return 1.0f - float((n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7FFFFFFFu) / 1073741824.0f;
}

}  // namespace

void onSprite(Ctx& c, u32 sprite) {
    if (c.lr == VA(0x80043930)) return;  // the debug HUD test draws every icon
    const u32 base = VA(0x803CE7D0) + 0xA90;
    if (sprite < base || sprite >= base + 19 * 0x18 || (sprite - base) % 0x18) return;
    sIcon = int((sprite - base) / 0x18) + 1;
    sIconFrame = skip::guestFrames();
}

void onViewMatrix(u32 cam) {
    if (!ver::usa() || cam != VA(0x803A3820)) return;
    const u32 now = skip::guestFrames();
    if (now != sLastFrame) {  // once per game frame
        sLastFrame = now;
        const bool on = port::settings().unused2.ghostDialogCam && ghostIcon(sIcon) && now - sIconFrame <= 2;
        if (on && sLookIcon != sIcon) {
            if (sWeight <= 0.0f || !ghostIcon(sLookIcon)) {
                sLookIcon = sIcon;
                log("[beta] ghost dialogue camera: icon %d", sIcon);
            } else {
                sWeight = std::max(0.0f, sWeight - 1.0f / 15.0f);  // another ghost: out, then in
                if (sWeight <= 0.0f) sLookIcon = sIcon;
            }
        } else {
            sWeight = std::clamp(sWeight + (on ? 1.0f : -1.0f) / 15.0f, 0.0f, 1.0f);
        }
    }
    if (sWeight <= 0.0f || !ghostIcon(sLookIcon)) return;
    const Look& L = kLooks[sLookIcon];
    const float e = sWeight * sWeight * (3.0f - 2.0f * sWeight);
    const float t = float(now);
    const float roll = e * (L.roll + L.sway * std::sin(t * L.freq * 2.0f * kPi) + L.shake * noise(now)) * kDeg;

    const u32 m = cam + 0x18C;
    float V[12];
    for (int i = 0; i < 12; i++) V[i] = rdf32(m + 4 * i);
    // The target in view space (MTXLookAt puts it on the -z axis).
    float tv[3];
    for (int r = 0; r < 3; r++)
        tv[r] = V[4 * r] * rdf32(cam + 0x0C) + V[4 * r + 1] * rdf32(cam + 0x10) + V[4 * r + 2] * rdf32(cam + 0x14) + V[4 * r + 3];
    const float dist = -tv[2];
    if (!(dist > 10.0f && dist < 20000.0f)) return;
    const float dolly = e * L.dolly * dist, low = e * L.low * dist;
    // View' = Rz(roll) * Rx(pitch) * T(0, low, dolly) * View: the camera moves in and down, then
    // tilts up to keep the target in the middle, then rolls about its view axis.
    const float ty = tv[1] + low, tz = tv[2] + dolly;
    const float pitch = tz < -1.0f ? std::atan(ty / tz) : 0.0f;
    const float cp = std::cos(pitch), sp = std::sin(pitch), cr = std::cos(roll), sr = std::sin(roll);
    // A = Rz * Rx (3x3, rows)
    const float A[3][3] = {{cr, -sr * cp, sr * sp}, {sr, cr * cp, -cr * sp}, {0.0f, sp, cp}};
    float out[12];
    for (int r = 0; r < 3; r++) {
        for (int k = 0; k < 3; k++) out[4 * r + k] = A[r][0] * V[k] + A[r][1] * V[4 + k] + A[r][2] * V[8 + k];
        const float tx = V[3], tyy = V[7] + low, tzz = V[11] + dolly;
        out[4 * r + 3] = A[r][0] * tx + A[r][1] * tyy + A[r][2] * tzz;
    }
    for (int i = 0; i < 12; i++) wrf32(m + 4 * i, out[i]);
}

}  // namespace recomp::beta

namespace recomp {

// The camera's view matrix: MTXLookAt(cam + 0x18C, eye cam + 0, up cam + 0x48, target cam + 0xC).
HLE(fn_8000853C) {
    const u32 cam = c.r[3];
    callOriginal(c, "fn_8000853C");
    beta::onViewMatrix(cam);
}

}  // namespace recomp
