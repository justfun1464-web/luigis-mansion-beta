// Luigi's Mansion BETA: loose-file overlay (see beta_files.h).
#include "recomp/beta_files.h"

#include "port/install.h"
#include "recomp/hle.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace recomp::beta_files {
namespace {

namespace fs = std::filesystem;

constexpr s32 kEntryBase = 0x58000000;  // virtual FST entries (HM 0x40000000, rooms 0x48000000,
                                        // maps 0x4C000000, unused2 0x50000000)

struct File {
    fs::path host;
    std::vector<u8> bytes;
    bool loaded = false;
    bool failed = false;
};

std::mutex sM;
bool sScanned = false;
std::vector<File> sFiles;
std::unordered_map<std::string, size_t> sIndex;  // normalized disc path -> sFiles index

std::string normalize(const char* p) {
    std::string s;
    for (; p && *p; ++p) {
        char ch = *p == '\\' ? '/' : *p;
        s += char(std::tolower(static_cast<unsigned char>(ch)));
    }
    size_t i = 0;
    while (i < s.size() && s[i] == '/') ++i;
    return s.substr(i);
}

std::string pathU8(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

fs::path filesDir() { return port::exeDir() / "beta" / "files"; }

void scanLocked() {
    if (sScanned) return;
    sScanned = true;
    std::error_code ec;
    const fs::path dir = filesDir();
    if (!fs::is_directory(dir, ec)) {
        log("[beta] no beta/files folder next to the program: the disc's own files are used");
        return;
    }
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string rel = pathU8(fs::relative(it->path(), dir, ec));
        if (ec || rel.empty()) continue;
        const std::string key = normalize(rel.c_str());
        if (sIndex.count(key)) continue;
        sIndex[key] = sFiles.size();
        sFiles.push_back({it->path(), {}, false, false});
    }
    log("[beta] %zu replacement file(s) in %s", sFiles.size(), pathU8(dir).c_str());
}

bool loadLocked(File& f) {
    if (f.loaded) return true;
    if (f.failed) return false;
    std::ifstream in(f.host, std::ios::binary);
    if (!in) {
        f.failed = true;
        log("[beta] ERROR: cannot read %s", pathU8(f.host).c_str());
        return false;
    }
    f.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    f.loaded = true;
    log("[beta] serving %s (%zu bytes)", pathU8(f.host).c_str(), f.bytes.size());
    return true;
}

s32 findLocked(const char* guestPath) {
    scanLocked();
    if (sFiles.empty() || !guestPath) return -1;
    const auto it = sIndex.find(normalize(guestPath));
    if (it == sIndex.end()) return -1;
    return loadLocked(sFiles[it->second]) ? s32(it->second) : -1;
}

}  // namespace

s32 overlayEntry(const char* guestPath) {
    std::lock_guard lk(sM);
    const s32 i = findLocked(guestPath);
    return i < 0 ? -1 : kEntryBase + i;
}

bool isOverlayEntry(s32 entry) {
    std::lock_guard lk(sM);
    return entry >= kEntryBase && entry < kEntryBase + s32(sFiles.size());
}

u32 overlayLength(s32 entry) {
    std::lock_guard lk(sM);
    if (entry < kEntryBase || entry >= kEntryBase + s32(sFiles.size())) return 0;
    return u32(sFiles[size_t(entry - kEntryBase)].bytes.size());
}

s32 overlayRead(s32 entry, void* dst, s32 length, s32 offset) {
    std::lock_guard lk(sM);
    if (entry < kEntryBase || entry >= kEntryBase + s32(sFiles.size())) return -1;
    const auto& b = sFiles[size_t(entry - kEntryBase)].bytes;
    if (offset < 0 || length < 0 || size_t(offset) > b.size()) return -1;
    const size_t n = std::min(size_t(length), b.size() - size_t(offset));
    std::memcpy(dst, b.data() + offset, n);
    if (n < size_t(length)) std::memset(static_cast<u8*>(dst) + n, 0, size_t(length) - n);
    return s32(n);
}

bool readFile(const char* guestPath, std::vector<u8>& out) {
    std::lock_guard lk(sM);
    const s32 i = findLocked(guestPath);
    if (i < 0) return false;
    out = sFiles[size_t(i)].bytes;
    return true;
}

std::string rootDir() { return pathU8(port::exeDir() / "beta"); }

size_t replacedCount() {
    std::lock_guard lk(sM);
    scanLocked();
    return sFiles.size();
}

}  // namespace recomp::beta_files
