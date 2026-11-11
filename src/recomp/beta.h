#pragma once
// Luigi's Mansion BETA: per-frame and per-map hooks of the BETA edition's own features
// (beta_*.cpp). Called from dev_tools.cpp next to the unused-content hooks.
#include "recomp/ctx.h"

namespace recomp::beta {

void onFrame(Ctx& c);     // end of every game frame (LMChangeFrameBuffer)
void onMapSetup(Ctx& c);  // a map is being set up (fn_8000C188)

// beta_hud.cpp: HUD and screen effects (guest thread)
bool hudOn();                    // the BETA health meter replaces the heart
void meterBegin();               // health meter draw (fn_800417F8) starts ...
bool meterSkipHeart();           // ... its heart draw: skip?
bool meterSkipNumber(Ctx& c);    // ... its number draw: skip? (records position, value, alpha)
void meterEnd(Ctx& c);           // ... and ends: draws the BETA meter in its place
void hudBegin(Ctx& c);           // in-game HUD starts (cloth frame, scare overlay)
bool titleGlowOn();              // the E3 title glow is on (USA game)
void titleDraw(Ctx& c);          // title screen: pink glow behind the logo
bool scareOn();                  // the scare overlay option is on
void onStartle(Ctx& c, u32 luigi);  // a ghost startled Luigi from behind (message 2 accepted)
void hudFrame(Ctx& c);           // once per game frame
// beta_gbh.cpp: Game Boy Horror ghost profiles rewritten (once per game frame)
void gbhFrame(Ctx& c);
bool borderRect(int& x, int& y, int& w, int& h);  // view 0's rectangle with the cloth border
bool inEvent();                  // the event interpreter runs a script (cutscene, dialogue)
// beta_camera.cpp: the camera while a ghost speaks
void onSprite(Ctx& c, u32 sprite);  // fn_8003BEEC draws a sprite (dialogue speaker icons)
void onViewMatrix(u32 cam);         // fn_8000853C built a camera's view matrix
// Ghost pass (hle_gx.cpp GXSetDstAlpha): `actor` drawn with ghost-layer alpha `alpha`
void onGhostDrawn(u32 actor, u32 alpha);
u32 framesSinceGhostSeen();      // game frames since a ghost was last drawn visibly (~0u: never)

}  // namespace recomp::beta
