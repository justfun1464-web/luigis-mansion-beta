// Luigi's Mansion BETA: replacement music (see beta_music.h).
//
// The game's music: sequences listed in the BARC block of /AudioRes/JaiInit.aaf (data in
// /AudioRes/Seqs/JaiArcS.arc), started with fn_80188760(0x80000000 | n) on the main music handle
// (JAISound* at lbl_804D91A4, +8 = the id) or on the second handle (lbl_804D91A8: room / light
// layers), and AFC streams (/AudioRes/Stream/*.afc) played through DSP ring voices (title,
// walk-up, opening). docs/AUDIO.md "Volume sliders", src/recomp/jukebox.cpp.
//
// A replaced sequence is muted voice by voice (its sequence data pointer, learned from
// Jaq_SetSeqData_Limit), a replaced stream by muting the ring voices; the file plays in their
// place, mixed into the output at the console's rate with the music volume.
//
// Ghost fights: the special entry "danger" plays in place of the mansion music (LuigiSings, 7, on
// the main handle) while ghosts are out (one drawn visibly in the ghost pass within the last
// 4 s, no event running), and in place of the ghost-catching music the game switches to while
// Luigi vacuums one (fn_8018BAC0: Fishing_Zako 19 / Fishing_Event 20). The game's sequence on
// the main handle is muted meanwhile (its data pointer, recorded at its start).
//
// The second handle (room / light layers, e.g. Light.com in a lit room) wins over the main one
// when both are replaced: it is the layer the game added on top.
#include "recomp/beta_music.h"

#include "port/install.h"
#include "port/settings.h"
#include "recomp/audio.h"
#include "recomp/beta.h"
#include "recomp/guest_util.h"
#include "recomp/hle.h"
#include "recomp/rarc.h"
#include "recomp/version.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#include "recomp/third_party/stb_vorbis.c"
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "recomp/third_party/dr_mp3.h"
#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include "recomp/third_party/dr_wav.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#include "recomp/third_party/dr_flac.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
// stb_vorbis leaves short macro names behind.
#undef L
#undef C
#undef R

namespace recomp::beta_music {
namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
constexpr double kPi = 3.14159265358979323846;

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return s.substr(a, b - a);
}
// "LuigiSings.com" -> "luigisings"; "/AudioRes/Stream/TMansion.afc" -> "tmansion"
std::string keyOf(const std::string& name) {
    std::string s = name;
    if (const size_t slash = s.find_last_of("/\\"); slash != std::string::npos) s = s.substr(slash + 1);
    if (const size_t dot = s.find_last_of('.'); dot != std::string::npos) s = s.substr(0, dot);
    return lower(trim(s));
}
std::string pathU8(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

// ---- the game's music table ----------------------------------------------------------------
struct Seq {
    int index;
    std::string name;
    u32 offset = 0, size = 0;
    int aliasOf = -1;
};
std::vector<Seq> sSeqs;
rarc::Bytes sArc;  // JaiArcS.arc (to tell sequences of equal size apart)

void loadTable() {
    const rarc::Bytes aaf = rarc::readDisc("/AudioRes/JaiInit.aaf");
    for (size_t i = 0; i + 0x20 <= aaf.size(); i += 4) {
        if (std::memcmp(&aaf[i], "BARC----", 8)) continue;
        auto be = [&](size_t o) { return u32(aaf[o]) << 24 | u32(aaf[o + 1]) << 16 | u32(aaf[o + 2]) << 8 | aaf[o + 3]; };
        const u32 count = be(i + 0xC);
        for (u32 k = 0; k < count && i + 0x40 + 0x20 * k <= aaf.size(); k++) {
            const size_t e = i + 0x20 + 0x20 * k;
            Seq s;
            s.index = int(k);
            s.name.assign(reinterpret_cast<const char*>(&aaf[e]), strnlen(reinterpret_cast<const char*>(&aaf[e]), 14));
            s.offset = be(e + 0x18);
            s.size = be(e + 0x1C);
            for (const Seq& p : sSeqs)
                if (p.offset == s.offset && p.aliasOf < 0) s.aliasOf = p.index;
            sSeqs.push_back(s);
        }
        break;
    }
    sArc = rarc::readDisc("/AudioRes/Seqs/JaiArcS.arc");
}

// ---- configuration -------------------------------------------------------------------------
std::once_flag sInit;
std::map<std::string, fs::path> sByKey;  // sequence name / number / stream name -> file
fs::path sDir;
int sConfigured = 0;

bool isAudioFile(const fs::path& p) {
    const std::string e = lower(pathU8(p.extension()));
    return e == ".ogg" || e == ".mp3" || e == ".wav" || e == ".flac";
}

void init() {
    sDir = port::exeDir() / "beta" / "music";
    loadTable();
    std::error_code ec;
    if (!fs::is_directory(sDir, ec)) {
        log("[music] no beta/music folder: the game's own music plays");
        return;
    }
    // Files named after what they replace.
    for (auto it = fs::directory_iterator(sDir, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec) && isAudioFile(it->path())) sByKey[keyOf(pathU8(it->path().filename()))] = it->path();
    // music.ini lines: <what> = <file>
    std::ifstream ini(sDir / "music.ini");
    std::string line;
    while (std::getline(ini, line)) {
        if (const size_t sc = line.find_first_of(";#"); sc != std::string::npos) line = line.substr(0, sc);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string what = trim(line.substr(0, eq)), file = trim(line.substr(eq + 1));
        if (what.empty() || file.empty()) continue;
        const fs::path p = sDir / fs::path(std::u8string(file.begin(), file.end()));
        if (!fs::is_regular_file(p, ec)) {
            log("[music] music.ini: %s = %s: file not found", what.c_str(), file.c_str());
            continue;
        }
        std::string key = keyOf(what);
        // a number: the sequence's name
        char* end = nullptr;
        const long n = std::strtol(what.c_str(), &end, 0);
        if (end && *end == 0 && n > 0 && n < long(sSeqs.size())) key = keyOf(sSeqs[size_t(n)].name);
        sByKey[key] = p;
    }
    sConfigured = int(sByKey.size());
    for (const auto& [k, p] : sByKey) log("[music] %s -> %s", k.c_str(), pathU8(p.filename()).c_str());
}

const fs::path* fileFor(const std::string& key) {
    const auto it = sByKey.find(key);
    return it == sByKey.end() ? nullptr : &it->second;
}
const fs::path* fileForSeq(int index) {
    if (index <= 0 || index >= int(sSeqs.size())) return nullptr;
    if (const fs::path* p = fileFor(keyOf(sSeqs[size_t(index)].name))) return p;
    const int alias = sSeqs[size_t(index)].aliasOf;
    return alias > 0 ? fileFor(keyOf(sSeqs[size_t(alias)].name)) : nullptr;
}

// ---- decoding (worker threads), cache ---------------------------------------------------
struct Track {
    std::vector<s16> pcm;  // stereo, interleaved, at kConsoleRate32
    std::atomic<bool> ready{false};
    std::atomic<bool> failed{false};
};

// Windowed-sinc resampling (offline, worker thread) of interleaved float stereo.
std::vector<s16> resample(const std::vector<float>& in, u32 inRate) {
    const double outRate = audio::kConsoleRate32;
    const size_t inFrames = in.size() / 2;
    const double step = double(inRate) / outRate;  // input frames per output frame
    const size_t outFrames = size_t(double(inFrames) / step);
    const double cutoff = std::min(1.0, outRate / double(inRate)) * 0.94;
    constexpr int kHalf = 12;
    const int half = int(std::ceil(kHalf / cutoff));
    std::vector<s16> out(outFrames * 2);
    for (size_t n = 0; n < outFrames; n++) {
        const double t = double(n) * step;
        const long c = long(std::floor(t));
        double l = 0, r = 0, wsum = 0;
        for (long k = c - half + 1; k <= c + half; k++) {
            if (k < 0 || size_t(k) >= inFrames) continue;
            const double x = (double(k) - t) * cutoff;
            const double sinc = std::fabs(x) < 1e-9 ? 1.0 : std::sin(kPi * x) / (kPi * x);
            const double wx = (double(k) - t) / double(half);
            const double win = std::fabs(wx) >= 1.0 ? 0.0 : 0.42 + 0.5 * std::cos(kPi * wx) + 0.08 * std::cos(2 * kPi * wx);
            const double w = sinc * win;
            l += in[size_t(k) * 2] * w;
            r += in[size_t(k) * 2 + 1] * w;
            wsum += w;
        }
        if (wsum > 1e-6) l /= wsum, r /= wsum;
        out[n * 2] = s16(std::clamp(std::lround(l * 32767.0), -32768L, 32767L));
        out[n * 2 + 1] = s16(std::clamp(std::lround(r * 32767.0), -32768L, 32767L));
    }
    return out;
}

void decode(std::shared_ptr<Track> t, fs::path file) {
    std::ifstream f(file, std::ios::binary);
    std::vector<u8> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const std::string ext = lower(pathU8(file.extension()));
    std::vector<float> st;  // stereo float
    u32 rate = 0;
    auto take = [&](const float* p, u64 frames, u32 ch, u32 r) {
        if (!p || !frames || !ch) return;
        rate = r;
        st.resize(size_t(frames) * 2);
        for (u64 i = 0; i < frames; i++) {
            const float a = p[i * ch], b = ch > 1 ? p[i * ch + 1] : a;
            st[size_t(i) * 2] = a;
            st[size_t(i) * 2 + 1] = b;
        }
    };
    if (ext == ".ogg") {
        int err = 0;
        stb_vorbis* v = stb_vorbis_open_memory(bytes.data(), int(bytes.size()), &err, nullptr);
        if (v) {
            const stb_vorbis_info info = stb_vorbis_get_info(v);
            std::vector<float> all, chunk(4096 * std::max(1, info.channels));
            for (;;) {
                const int got = stb_vorbis_get_samples_float_interleaved(v, info.channels, chunk.data(), int(chunk.size()));
                if (got <= 0) break;
                all.insert(all.end(), chunk.begin(), chunk.begin() + got * info.channels);
            }
            stb_vorbis_close(v);
            take(all.data(), all.size() / std::max(1, info.channels), u32(info.channels), info.sample_rate);
        }
    } else if (ext == ".mp3") {
        drmp3_config cfg{};
        drmp3_uint64 frames = 0;
        float* p = drmp3_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &cfg, &frames, nullptr);
        take(p, frames, cfg.channels, cfg.sampleRate);
        drmp3_free(p, nullptr);
    } else if (ext == ".wav") {
        unsigned ch = 0, r = 0;
        drwav_uint64 frames = 0;
        float* p = drwav_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &ch, &r, &frames, nullptr);
        take(p, frames, ch, r);
        drwav_free(p, nullptr);
    } else if (ext == ".flac") {
        unsigned ch = 0, r = 0;
        drflac_uint64 frames = 0;
        float* p = drflac_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &ch, &r, &frames, nullptr);
        take(p, frames, ch, r);
        drflac_free(p, nullptr);
    }
    if (st.empty() || !rate) {
        t->failed = true;
        log("[music] ERROR: could not decode %s", pathU8(file).c_str());
        return;
    }
    t->pcm = resample(st, rate);
    t->ready = true;
    log("[music] %s ready (%.1f s, %u Hz source)", pathU8(file.filename()).c_str(),
        double(t->pcm.size() / 2) / audio::kConsoleRate32, rate);
}

std::mutex sCacheM;
std::map<std::string, std::shared_ptr<Track>> sCache;  // file -> track (kept for the session)

std::shared_ptr<Track> trackFor(const fs::path& file) {
    std::lock_guard lk(sCacheM);
    const std::string k = pathU8(file);
    auto it = sCache.find(k);
    if (it != sCache.end()) return it->second;
    auto t = std::make_shared<Track>();
    sCache[k] = t;
    std::thread(decode, t, file).detach();
    return t;
}

// ---- playback (two slots: sequenced music, streams), audio thread mixes ----------------
struct Voice {
    std::shared_ptr<Track> track;
    size_t pos = 0;      // frame
    float level = 0;     // current fade level
    float target = 1;    // 1 = playing, 0 = fading out (removed at 0)
};
struct Slot {
    std::string key;     // what plays now ("" = nothing)
    std::vector<Voice> voices;
};
std::mutex sPlayM;
Slot sSlots[2];  // 0 sequences, 1 streams

void setSlot(int slot, const std::string& key, const fs::path* file) {
    std::lock_guard lk(sPlayM);
    Slot& s = sSlots[slot];
    if (s.key == key) return;
    for (Voice& v : s.voices) v.target = 0;  // fade the old one out
    s.key = key;
    if (file) {
        Voice v;
        v.track = trackFor(*file);
        v.level = 0;
        v.target = 1;
        s.voices.push_back(std::move(v));
        log("[music] %s: %s", slot ? "stream" : "music", key.c_str());
    } else if (!key.empty()) {
        log("[music] %s: %s (game's own)", slot ? "stream" : "music", key.c_str());
    }
}

// ---- muting ------------------------------------------------------------------------------
constexpr int kMuteSlots = 8;
std::array<std::atomic<u32>, kMuteSlots> sMuteData{};  // sequence data of replaced sequences
std::array<std::atomic<u32>, 128> sSeqData{};          // sequence index -> data of its last start
std::atomic<u32> sDangerMute{0};                        // data muted while "danger" plays
std::atomic<bool> sMuteStreams{false};
std::atomic<int64_t> sLastRingNs{0};
std::string sStreamKey;  // guest thread

int indexForData(u32 data, u32 size) {
    int found = -1;
    for (const Seq& s : sSeqs) {
        if (s.index == 0 || s.size != size) continue;
        if (found < 0) found = s.aliasOf >= 0 ? s.aliasOf : s.index;
        // several sequences of this size: compare the start of the data with the archive
        if (!sArc.empty() && s.offset + 64 <= sArc.size() && gu::inMem1(data) && gu::inMem1(data + 63)) {
            bool same = true;
            for (u32 i = 0; i < 64 && same; i++) same = rd8(data + i) == sArc[s.offset + i];
            if (same) return s.aliasOf >= 0 ? s.aliasOf : s.index;
        }
    }
    return found;
}

int64_t nowNs() { return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count(); }

}  // namespace

int configuredCount() {
    std::call_once(sInit, init);
    return sConfigured;
}

void noteSequence(u32 data, u32 size) {
    std::call_once(sInit, init);
    if (!sConfigured || !data) return;
    const int index = indexForData(data, size);
    const bool replaced = index > 0 && fileForSeq(index) != nullptr;
    for (auto& s : sSeqData) {
        u32 d = data;
        s.compare_exchange_strong(d, 0);
    }
    if (index > 0 && index < int(sSeqData.size())) sSeqData[size_t(index)] = data;
    // forget this buffer's previous sequence, then remember it if replaced
    for (auto& m : sMuteData) {
        u32 d = data;
        m.compare_exchange_strong(d, 0);
    }
    if (replaced) {
        for (auto& m : sMuteData) {
            u32 z = 0;
            if (m.compare_exchange_strong(z, data)) break;
        }
        log("[music] sequence %d (%s) at %08X muted for its replacement", index, sSeqs[size_t(index)].name.c_str(), data);
    }
}

void noteFileOpen(const char* path) {
    if (!path) return;
    const std::string p = lower(path);
    if (p.size() < 4 || p.compare(p.size() - 4, 4, ".afc") != 0) return;
    std::call_once(sInit, init);
    sStreamKey = keyOf(p);
    sMuteStreams = sConfigured && fileFor(sStreamKey) != nullptr;
}

bool muteSequence(u32 seqData) {
    if (!seqData) return false;
    if (seqData == sDangerMute.load(std::memory_order_relaxed)) return true;
    for (const auto& m : sMuteData)
        if (m.load(std::memory_order_relaxed) == seqData) return true;
    return false;
}

bool muteStreams() { return sMuteStreams.load(std::memory_order_relaxed); }

void noteRingVoice() { sLastRingNs.store(nowNs(), std::memory_order_relaxed); }

void frame(Ctx&) {
    std::call_once(sInit, init);
    if (!sConfigured) return;
    // Sequenced music: the main handle first, else the second handle (room / light layers).
    auto handleIndex = [](u32 addr) {
        const u32 h = rd32(addr);
        const u32 id = gu::inMem1(h) ? rd32(h + 8) : 0;
        return (id & 0x80000000u) ? int(id & 0xFFFF) : -1;
    };
    const int main = handleIndex(VA(0x804D91A4)), sub = handleIndex(VA(0x804D91A8));
    // Ghost fights (see the top of the file); 120 game frames = 4 s after the last ghost seen.
    const fs::path* danger = port::settings().unused2.dangerMusic ? fileFor("danger") : nullptr;
    const bool fishing = main == 19 || main == 20;
    const bool ghostsOut = beta::framesSinceGhostSeen() <= 120 && !beta::inEvent();
    const bool dangerOn = danger && (fishing || (main == 7 && ghostsOut && sub <= 0));
    sDangerMute = dangerOn && main > 0 && main < int(sSeqData.size()) ? sSeqData[size_t(main)].load() : 0u;
    int playing = -1;
    if (sub > 0 && fileForSeq(sub)) playing = sub;
    else if (main > 0 && fileForSeq(main)) playing = main;
    if (dangerOn) setSlot(0, "danger", danger);
    else if (playing > 0) setSlot(0, keyOf(sSeqs[size_t(playing)].name), fileForSeq(playing));
    else setSlot(0, "", nullptr);
    // Streams: while ring voices play (within the last 0.6 s).
    const bool streaming = nowNs() - sLastRingNs.load(std::memory_order_relaxed) < 600'000'000;
    const fs::path* sf = streaming && !sStreamKey.empty() ? fileFor(sStreamKey) : nullptr;
    if (sf) setSlot(1, sStreamKey, sf);
    else setSlot(1, "", nullptr);
}

void mix(float* buf, u32 frames, int ch, float gain) {
    if (!sConfigured) return;
    const float music = std::clamp(port::settings().audio.musicVolume, 0.0f, 1.0f);
    const float g = gain * music;  // gain already scales s16 samples to float
    const double rate = audio::kConsoleRate32;
    const float fadeIn = float(1.0 / (0.25 * rate)), fadeOut = float(1.0 / (0.6 * rate));
    std::lock_guard lk(sPlayM);
    for (Slot& s : sSlots) {
        for (Voice& v : s.voices) {
            if (!v.track || !v.track->ready.load()) continue;
            const std::vector<s16>& pcm = v.track->pcm;
            const size_t n = pcm.size() / 2;
            if (!n) continue;
            for (u32 i = 0; i < frames; i++) {
                if (v.level < v.target) v.level = std::min(v.target, v.level + fadeIn);
                else if (v.level > v.target) v.level = std::max(v.target, v.level - fadeOut);
                if (v.level <= 0 && v.target <= 0) break;
                const float k = g * v.level;
                buf[size_t(i) * ch] += pcm[v.pos * 2] * k;
                buf[size_t(i) * ch + 1] += pcm[v.pos * 2 + 1] * k;
                if (++v.pos >= n) v.pos = 0;  // loop
            }
        }
        s.voices.erase(std::remove_if(s.voices.begin(), s.voices.end(),
                                      [](const Voice& v) { return v.target <= 0 && v.level <= 0; }),
                       s.voices.end());
    }
}

}  // namespace recomp::beta_music
