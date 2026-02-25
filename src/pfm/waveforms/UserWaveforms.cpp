#include "UserWaveforms.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Defined in src/pfm/synth/Osc.cpp
extern float userWaveform[6][1025];

namespace {

static std::array<bool, pfm::USER_WAVE_COUNT> g_loaded = {false, false, false, false, false, false};
static std::string g_lastError;

static void setLastError(const std::string& err) {
    g_lastError = err;
}

static std::string joinPath(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    char last = dir.back();
    if (last == '/' || last == '\\') return dir + name;
    return dir + "/" + name;
}

static bool readAllBytes(const std::string& path, std::vector<uint8_t>& outBytes) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;

    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return false;
    }
    long size = std::ftell(f);
    if (size <= 0) {
        std::fclose(f);
        return false;
    }
    std::rewind(f);

    outBytes.resize((size_t)size);
    size_t nRead = std::fread(outBytes.data(), 1, outBytes.size(), f);
    std::fclose(f);
    return nRead == outBytes.size();
}

static void clampAndGuard(float* dst1025) {
    for (int i = 0; i < pfm::USER_WAVE_SAMPLES; ++i) {
        float v = dst1025[i];
        if (!std::isfinite(v)) v = 0.f;
        if (v > 1.f) v = 1.f;
        if (v < -1.f) v = -1.f;
        dst1025[i] = v;
    }
    // Guard sample for HQ interpolation (index+1)
    dst1025[pfm::USER_WAVE_SAMPLES] = dst1025[0];
}

static bool loadBin1024Float32(const std::string& path, float* dst1025) {
    std::vector<uint8_t> bytes;
    if (!readAllBytes(path, bytes)) return false;

    // PreenFM2 user wave BIN files appear in multiple formats in the wild:
    //  - 1024 float32 samples:            4096 bytes
    //  - 8-byte header + float32 samples: 4104 bytes
    //  - 1024 int16 samples:              2048 bytes
    //  - 8-byte header + int16 samples:   2056 bytes
    // Header (when present): 4-char name + uint32 sampleCount (little-endian)

    const size_t f32Size = (size_t)(pfm::USER_WAVE_SAMPLES * sizeof(float));
    const size_t i16Size = (size_t)(pfm::USER_WAVE_SAMPLES * sizeof(int16_t));

    size_t offset = 0;
    bool hasHeader = false;
    bool isFloat32 = false;

    if (bytes.size() == f32Size) {
        isFloat32 = true;
        offset = 0;
    } else if (bytes.size() == (size_t)(8 + f32Size)) {
        isFloat32 = true;
        hasHeader = true;
        offset = 8;
    } else if (bytes.size() == i16Size) {
        isFloat32 = false;
        offset = 0;
    } else if (bytes.size() == (size_t)(8 + i16Size)) {
        isFloat32 = false;
        hasHeader = true;
        offset = 8;
    } else {
        setLastError(
            "Bad BIN size for " + path + " (expected 2048/2056 int16 or 4096/4104 float32 bytes)"
        );
        return false;
    }

    if (hasHeader) {
        uint32_t sampleCount = 0;
        std::memcpy(&sampleCount, bytes.data() + 4, sizeof(sampleCount));
        if (sampleCount != (uint32_t)pfm::USER_WAVE_SAMPLES) {
            setLastError("Bad BIN header sample count for " + path);
            return false;
        }
    }

    if (isFloat32) {
        std::memcpy(dst1025, bytes.data() + offset, f32Size);
        clampAndGuard(dst1025);
        return true;
    }

    // int16 -> float [-1, +1]
    for (int i = 0; i < pfm::USER_WAVE_SAMPLES; ++i) {
        int16_t s;
        std::memcpy(&s, bytes.data() + offset + (size_t)i * sizeof(int16_t), sizeof(int16_t));
        // Map signed 16-bit range to [-1, 1]
        dst1025[i] = (s < 0) ? (float)s / 32768.0f : (float)s / 32767.0f;
    }
    clampAndGuard(dst1025);
    return true;
}

static bool loadTxtFloatList1024(const std::string& path, float* dst1025) {
    std::vector<uint8_t> bytes;
    if (!readAllBytes(path, bytes)) return false;

    std::string s(bytes.begin(), bytes.end());
    // Normalize commas to spaces (forum says separators can be anything)
    for (char& c : s) {
        if (c == ',') c = ' ';
    }

    const char* p = s.c_str();
    char* end = nullptr;
    std::vector<float> values;
    values.reserve(pfm::USER_WAVE_SAMPLES + 8);

    while (*p) {
        float v = std::strtof(p, &end);
        if (end != p) {
            values.push_back(v);
            p = end;
        } else {
            ++p;
        }
    }

    if (values.size() < (size_t)pfm::USER_WAVE_SAMPLES) {
        setLastError("TXT did not contain 1024 samples: " + path + " (found " + std::to_string(values.size()) + ")");
        return false;
    }

    // If a header was included (e.g. name + sampleCount), it would add extra numeric tokens.
    // Use the LAST 1024 values, which is robust for common header styles.
    size_t start = values.size() - (size_t)pfm::USER_WAVE_SAMPLES;
    for (int i = 0; i < pfm::USER_WAVE_SAMPLES; ++i) {
        dst1025[i] = values[start + (size_t)i];
    }

    clampAndGuard(dst1025);
    return true;
}

static bool tryLoadOneUserWave(int userIndex0, const std::string& dirPath) {
    const int userNumber = userIndex0 + 1;
    const std::string n = std::to_string(userNumber);

    // Prefer BIN (fast path). Windows is case-insensitive, but MetaModule may not be.
    const std::array<std::string, 8> binNames = {
        std::string("USR") + n + ".BIN",
        std::string("usr") + n + ".bin",
        std::string("USR") + n + ".bin",
        std::string("usr") + n + ".BIN",
        // Some tools export as USER1.BIN, etc.
        std::string("USER") + n + ".BIN",
        std::string("user") + n + ".bin",
        std::string("USER") + n + ".bin",
        std::string("user") + n + ".BIN",
    };

    for (const auto& bn : binNames) {
        const std::string path = joinPath(dirPath, bn);
        if (loadBin1024Float32(path, userWaveform[userIndex0])) {
            return true;
        }
    }

    // Fallback: TXT float list (Audacity sample export)
    const std::array<std::string, 4> txtNames = {
        std::string("usr") + n + ".txt",
        std::string("USR") + n + ".TXT",
        std::string("USR") + n + ".txt",
        std::string("usr") + n + ".TXT",
    };

    for (const auto& tn : txtNames) {
        const std::string path = joinPath(dirPath, tn);
        if (loadTxtFloatList1024(path, userWaveform[userIndex0])) {
            return true;
        }
    }

    setLastError("Missing USR" + n + " in " + dirPath);
    return false;
}

} // namespace

namespace pfm {

bool loadUserWaveformsFromDir(const std::string& dirPath) {
    bool any = false;
    for (int i = 0; i < USER_WAVE_COUNT; ++i) {
        bool ok = tryLoadOneUserWave(i, dirPath);
        // Important: this function may be called with multiple fallback dirs.
        // Do not clobber a previously-loaded waveform just because it's missing
        // in a later directory.
        if (ok) {
            g_loaded[i] = true;
            any = true;
        }
    }
    return any;
}

bool isUserWaveformLoaded(int userWaveNumber) {
    if (userWaveNumber < 1 || userWaveNumber > USER_WAVE_COUNT) return false;
    return g_loaded[userWaveNumber - 1];
}

const std::string& getUserWaveformsLastError() {
    return g_lastError;
}

} // namespace pfm
