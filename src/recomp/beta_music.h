#pragma once
// Luigi's Mansion BETA: replacement music. Audio files in <program folder>/beta/music/ play in
// place of the game's own music, following the game: a replacement starts when the game starts
// the music it replaces and stops when the game stops or changes it.
//
// Which file replaces what: beta/music/music.ini, one line per replacement
//     <what> = <file>          e.g.   LuigiSings = e3_dark_rooms.ogg
// <what> is a music sequence of /AudioRes/JaiInit.aaf by name (with or without ".com"/".bms") or
// number (decimal or 0x..), or a streamed track by its file name (TMansion, TMOpen, ...).
// A file named after a sequence or stream (LuigiSings.ogg, TMansion.mp3) works without a line.
// Formats: .ogg (Vorbis), .mp3, .wav, .flac. Files loop while the game's music plays.
#include "recomp/ctx.h"

namespace recomp::beta_music {

// Guest thread, once per game frame (dev_tools.cpp end-of-frame hook).
void frame(Ctx& c);
// Jaq_SetSeqData_Limit: a sequence starts from guest `data` (`size` bytes).
void noteSequence(u32 data, u32 size);
// DVD open of a streamed track (/AudioRes/Stream/*.afc).
void noteFileOpen(const char* path);
// DSP mixer (audio thread): true while the sequence playing from `seqData` is replaced; ring
// (streamed) voices are muted while a stream is replaced. noteRingVoice marks stream activity.
bool muteSequence(u32 seqData);
bool muteStreams();
void noteRingVoice();
// Audio output (audio thread): adds the replacement music to an interleaved float buffer at the
// console's sample rate (`ch` channels, front left/right first), scaled by `gain`.
void mix(float* buf, u32 frames, int ch, float gain);
// For the F1 menu / logs: how many replacements are configured.
int configuredCount();

}  // namespace recomp::beta_music
