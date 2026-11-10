#pragma once
// Luigi's Mansion BETA: per-frame and per-map hooks of the BETA edition's own features
// (beta_*.cpp). Called from dev_tools.cpp next to the unused-content hooks.
#include "recomp/ctx.h"

namespace recomp::beta {

void onFrame(Ctx& c);     // end of every game frame (LMChangeFrameBuffer)
void onMapSetup(Ctx& c);  // a map is being set up (fn_8000C188)

}  // namespace recomp::beta
