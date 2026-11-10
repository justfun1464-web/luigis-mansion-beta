#pragma once
// Luigi's Mansion BETA: loose-file overlay. Every file under <program folder>/beta/files/ replaces
// the disc file with the same path (case-insensitive), e.g. beta/files/Event/event01.szp serves
// /Event/event01.szp. The disc image itself is never modified. Files are read from the folder at
// the first open and kept in memory; the folder is scanned once at startup.
#include "recomp/ctx.h"

#include <string>
#include <vector>

namespace recomp::beta_files {

// Disc overlay used by hle_dvd.cpp (virtual entries 0x58000000+, distinct from the other overlays').
s32 overlayEntry(const char* guestPath);  // -1 when the path is not replaced
bool isOverlayEntry(s32 entry);
u32 overlayLength(s32 entry);
s32 overlayRead(s32 entry, void* dst, s32 length, s32 offset);

// Whole replacement file for a disc path (used by rarc::readDisc so the port's own rebuilt
// archives start from the BETA files too). False when the path is not replaced.
bool readFile(const char* guestPath, std::vector<u8>& out);

// Folders of the BETA edition (next to the program).
std::string rootDir();   // <exe dir>/beta
size_t replacedCount();  // number of files in beta/files

}  // namespace recomp::beta_files
