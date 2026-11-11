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
void titleDraw(Ctx& c);          // title screen drawn (pink glow)
void onStartle(Ctx& c, u32 luigi);  // a ghost startled Luigi (message 2 accepted)
void hudFrame(Ctx& c);           // once per game frame
bool borderRect(int& x, int& y, int& w, int& h);  // view 0's rectangle with the cloth border

}  // namespace recomp::beta
