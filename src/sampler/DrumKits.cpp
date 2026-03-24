#include "DrumKits.h"

#include <cmath>

#ifdef METAMODULE
#include "plugin.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#endif

namespace bdkit {

Sample g_kits[NUM_KITS][NUM_INST][NUM_LAYERS] = {};

static constexpr uint32_t kPlaceholderSampleRate = 48000;
static constexpr uint32_t kFrames = 4096;

static int16_t g_bd[NUM_LAYERS][kFrames];
static int16_t g_sn[NUM_LAYERS][kFrames];
static int16_t g_gsn[NUM_LAYERS][kFrames];
static int16_t g_ch[NUM_LAYERS][kFrames];
static int16_t g_oh[NUM_LAYERS][kFrames];
static int16_t g_rc[NUM_LAYERS][kFrames];

static bool g_inited = false;

static inline float clamp1(float x) {
    if (x < -1.f) return -1.f;
    if (x > 1.f) return 1.f;
    return x;
}

static inline uint32_t xorshift32(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

#ifdef METAMODULE
static inline uint16_t rd16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t rd32(const uint8_t* p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
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

// Minimal WAV reader: RIFF/WAVE, PCM 16-bit, mono.
static bool loadWavMonoPcm16(const std::string& path, std::vector<int16_t>& outPcm, uint32_t& outSampleRate) {
    std::vector<uint8_t> bytes;
    if (!readAllBytes(path, bytes)) return false;
    if (bytes.size() < 44) return false;
    if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) return false;

    bool fmtFound = false;
    bool dataFound = false;
    uint16_t audioFormat = 0;
    uint16_t numChannels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    uint32_t dataPos = 0;
    uint32_t dataSize = 0;

    uint32_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        const uint8_t* c = bytes.data() + pos;
        uint32_t chunkSize = rd32(c + 4);
        pos += 8;
        if (pos + chunkSize > bytes.size()) break;

        if (std::memcmp(c, "fmt ", 4) == 0) {
            if (chunkSize < 16) return false;
            const uint8_t* f = bytes.data() + pos;
            audioFormat = rd16(f + 0);
            numChannels = rd16(f + 2);
            sampleRate = rd32(f + 4);
            bitsPerSample = rd16(f + 14);
            fmtFound = true;
        }
        else if (std::memcmp(c, "data", 4) == 0) {
            dataPos = pos;
            dataSize = chunkSize;
            dataFound = true;
        }

        pos += chunkSize;
        if (pos & 1) pos++; // word-align
    }

    if (!fmtFound || !dataFound) return false;
    if (audioFormat != 1) return false;
    if (numChannels != 1) return false;
    if (bitsPerSample != 16) return false;
    if (sampleRate == 0) return false;

    uint32_t nSamp = dataSize / 2;
    outPcm.resize(nSamp);
    const uint8_t* d = bytes.data() + dataPos;
    for (uint32_t i = 0; i < nSamp; ++i) {
        outPcm[i] = (int16_t)rd16(d + i * 2);
    }
    outSampleRate = sampleRate;
    return true;
}

static const char* filePrefixForInst(int inst) {
    switch (inst) {
    case BD: return "BD";
    case SN: return "SN1";
    case GSN: return "SN2";
    case CH: return "CH";
    case OH: return "OH";
    case RC: return "ridcra";
    default: return "??";
    }
}

static bool initBundledKit01() {
    static std::vector<int16_t> pcm[NUM_INST][NUM_LAYERS];
    static uint32_t sr[NUM_INST][NUM_LAYERS] = {};

    for (int inst = 0; inst < NUM_INST; ++inst) {
        for (int layer = 0; layer < NUM_LAYERS; ++layer) {
            std::string fn = std::string(filePrefixForInst(inst)) + "_" + std::to_string(layer + 1) + ".wav";
            std::string rel = std::string("samples/amenolith/Kit01/") + fn;
            std::string path = asset::plugin(pluginInstance, rel);
            if (!loadWavMonoPcm16(path, pcm[inst][layer], sr[inst][layer])) {
                return false;
            }
        }
    }

    for (int k = 0; k < NUM_KITS; ++k) {
        for (int inst = 0; inst < NUM_INST; ++inst) {
            for (int layer = 0; layer < NUM_LAYERS; ++layer) {
                g_kits[k][inst][layer].data = pcm[inst][layer].data();
                g_kits[k][inst][layer].frames = (uint32_t)pcm[inst][layer].size();
                g_kits[k][inst][layer].sampleRate = sr[inst][layer];
            }
        }
    }
    return true;
}
#endif

static void initProceduralKits() {
    const float sr = (float)kPlaceholderSampleRate;

    for (int layer = 0; layer < NUM_LAYERS; ++layer) {
        const float vel = (layer + 1) / 3.f;

        // BD: decaying sine + click
        float f0 = 52.f + 8.f * layer;
        float phase = 0.f;
        for (uint32_t i = 0; i < kFrames; ++i) {
            float t = (float)i / sr;
            float env = std::exp(-t * (10.f - 2.f * layer));
            float s = (i < 8 ? 0.6f : 0.f) + std::sin(phase) * env;
            phase += 2.f * 3.14159265f * f0 / sr;
            g_bd[layer][i] = (int16_t)std::lrintf(32767.f * clamp1(s * 0.9f * vel));
        }

        // SN: noise burst + short body
        uint32_t seed = 0x1234567u + (uint32_t)layer * 101u;
        float bodyPhase = 0.f;
        for (uint32_t i = 0; i < kFrames; ++i) {
            float t = (float)i / sr;
            float env = std::exp(-t * (35.f - 5.f * layer));

            float n = ((xorshift32(seed) & 0xFFFFu) / 32768.f) - 1.f;
            float body = std::sin(bodyPhase) * std::exp(-t * 70.f);
            bodyPhase += 2.f * 3.14159265f * (180.f + 30.f * layer) / sr;

            float s = (0.75f * n + 0.25f * body) * env;
            g_sn[layer][i] = (int16_t)std::lrintf(32767.f * clamp1(s * 0.9f * vel));
        }

        // GSN: softer, shorter noise
        seed = 0x89ABCDEFu + (uint32_t)layer * 97u;
        for (uint32_t i = 0; i < kFrames; ++i) {
            float t = (float)i / sr;
            float env = std::exp(-t * 55.f);
            float n = ((xorshift32(seed) & 0xFFFFu) / 32768.f) - 1.f;
            float s = n * env;
            g_gsn[layer][i] = (int16_t)std::lrintf(32767.f * clamp1(s * 0.35f * vel));
        }

        // CH/OH: bright-ish noise, different decay times
        seed = 0x2468ACEu + (uint32_t)layer * 53u;
        float lp = 0.f;
        for (uint32_t i = 0; i < kFrames; ++i) {
            float t = (float)i / sr;
            float envCH = std::exp(-t * (120.f - 20.f * layer));
            float envOH = std::exp(-t * (25.f - 3.f * layer));

            float n = ((xorshift32(seed) & 0xFFFFu) / 32768.f) - 1.f;
            lp += 0.02f * (n - lp);
            float hp = n - lp;

            g_ch[layer][i] = (int16_t)std::lrintf(32767.f * clamp1(hp * envCH * 0.6f * vel));
            g_oh[layer][i] = (int16_t)std::lrintf(32767.f * clamp1(hp * envOH * 0.6f * vel));
        }

        // RC: longer metallic cymbal/noise hybrid for ride/crash duty
        seed = 0x13579BDFu + (uint32_t)layer * 67u;
        float rcLp = 0.f;
        float rcPhaseA = 0.f;
        float rcPhaseB = 0.f;
        float rcFreqA = 3120.f + 240.f * layer;
        float rcFreqB = 5170.f + 330.f * layer;
        for (uint32_t i = 0; i < kFrames; ++i) {
            float t = (float)i / sr;
            float envRC = std::exp(-t * (8.5f - 0.8f * layer));

            float n = ((xorshift32(seed) & 0xFFFFu) / 32768.f) - 1.f;
            rcLp += 0.012f * (n - rcLp);
            float hp = n - rcLp;
            float metallic = 0.6f * std::sin(rcPhaseA) + 0.4f * std::sin(rcPhaseB);
            rcPhaseA += 2.f * 3.14159265f * rcFreqA / sr;
            rcPhaseB += 2.f * 3.14159265f * rcFreqB / sr;
            float s = (0.55f * hp + 0.45f * metallic) * envRC;
            g_rc[layer][i] = (int16_t)std::lrintf(32767.f * clamp1(s * 0.55f * vel));
        }
    }

    for (int k = 0; k < NUM_KITS; ++k) {
        for (int l = 0; l < NUM_LAYERS; ++l) {
            g_kits[k][BD][l].data = g_bd[l];
            g_kits[k][BD][l].frames = kFrames;
            g_kits[k][BD][l].sampleRate = kPlaceholderSampleRate;

            g_kits[k][SN][l].data = g_sn[l];
            g_kits[k][SN][l].frames = kFrames;
            g_kits[k][SN][l].sampleRate = kPlaceholderSampleRate;

            g_kits[k][GSN][l].data = g_gsn[l];
            g_kits[k][GSN][l].frames = kFrames;
            g_kits[k][GSN][l].sampleRate = kPlaceholderSampleRate;

            g_kits[k][CH][l].data = g_ch[l];
            g_kits[k][CH][l].frames = kFrames;
            g_kits[k][CH][l].sampleRate = kPlaceholderSampleRate;

            g_kits[k][OH][l].data = g_oh[l];
            g_kits[k][OH][l].frames = kFrames;
            g_kits[k][OH][l].sampleRate = kPlaceholderSampleRate;

            g_kits[k][RC][l].data = g_rc[l];
            g_kits[k][RC][l].frames = kFrames;
            g_kits[k][RC][l].sampleRate = kPlaceholderSampleRate;
        }
    }
}

void initKits() {
    if (g_inited) return;
    g_inited = true;

#ifdef METAMODULE
    if (!initBundledKit01()) {
        initProceduralKits();
    }
#else
    initProceduralKits();
#endif
}

const char* instrumentName(int inst) {
    switch (inst) {
    case BD: return "BD";
    case SN: return "SN";
    case GSN: return "GSN";
    case CH: return "CH";
    case OH: return "OH";
    case RC: return "RC";
    default: return "?";
    }
}

} // namespace bdkit
