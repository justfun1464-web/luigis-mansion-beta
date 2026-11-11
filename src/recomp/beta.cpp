// Luigi's Mansion BETA: frame / map hooks of the BETA features (beta.h).
#include "recomp/beta.h"

#include "recomp/beta_music.h"

namespace recomp::beta {

void onFrame(Ctx& c) {
    beta_music::frame(c);
    hudFrame(c);
}

void onMapSetup(Ctx&) {}

}  // namespace recomp::beta
