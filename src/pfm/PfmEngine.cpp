/*
 * PfmEngine.cpp - Minimal PreenFM2 engine wrapper for VCV Rack
 *
 * Based on PreenFM2 by Xavier Hosxe (GPL-3.0-or-later)
 * VCV Rack port by Bemushroomed
 */

#include "PfmEngine.h"
#include "synth/Timbre.h"
#include "synth/Voice.h"
#include "SynthState.h"
#include "waveforms/UserWaveforms.h"
#include "stm32f4xx_rng.h"
#include "LiquidCrystal.h"
#include <cstring>
#include <cmath>

// Fast math helpers for per-block CV processing
// (matches expf_fast defined in Timbre.cpp for filter use)
static inline float expf_fast_local(float a) {
    union { float f; int x; } u;
    u.x = (int)(12102203 * a + 1064866805);
    return u.f;
}
static inline float logf_fast(float a) {
    union { float f; int x; } u = {a};
    return (u.x - 1064866805) * 8.2629582881927490e-8f;
}

// Global LCD stub (referenced by Timbre.cpp and LfoOsc.cpp)
LiquidCrystal lcd;

// Globals required by the PreenFM2 engine
float PREENFM_FREQUENCY = 40960.0f;         // ~41 kHz native rate
float PREENFM_FREQUENCY_INVERSED = 1.0f / 40960.0f;
float PREENFM_FREQUENCY_INVERSED_LFO = 32.0f / 40960.0f;

// Required by Synth.cpp / buildNewSampleBlock
extern float noise[32];

// Required by Osc.cpp
extern float* frequencyToUse;
extern float frequency[];

// panTable is defined in Timbre.cpp
extern float panTable[];

// Stubs for allParameterRows (needed by some engine code paths)
static struct ParameterRowDisplay dummyRows[NUMBER_OF_ROWS];
struct AllParameterRowsDisplay allParameterRows;

// Stub for midiConfig
struct MidiConfig midiConfig[MIDICONFIG_SIZE + 1];

// PreenFM2 DAC ratio - 18-bit DAC: (1 << 17) - 1 = 131071
// This matches RATIOINV (1.0/131072.0) defined in Timbre.cpp
// Used as the 'ratioTimbres' parameter to fxAfterBlock()
static const float DAC_RATIO = 131071.0f;

PfmEngine::PfmEngine()
    : synthState(nullptr)
    , timbre(nullptr)
    , voice(nullptr)
    , sampleRate(44100.0f)
    , engineRate(40960.0f)
    , blockPos(BLOCK_SIZE * 2) // force initial block generation
    , cvin1(0.0f)
    , cvin2(0.0f)
    , cvin2Amount(1.0f)
    , cvinEvo(0.0f)
    , cvinDrift(0.0f)
    , cv1Connected(false)
    , cv2Connected(false)
    , evoConnected(false)
    , driftConnected(false)
    , manualAlgo(-1)
    , baseAlgo(0.0f)
    , noteActive(false)
    , currentFrequency(440.0f)
    , targetFrequency(440.0f)
    , glideValue(0.0f)
    , resamplePhase(0.0)
    , resampleRatio(1.0)
    , lastOutL(0.0f)
    , lastOutR(0.0f)
    , prevOutL(0.0f)
    , prevOutR(0.0f)
    , initialized(false)
{
    memset(blockBuffer, 0, sizeof(blockBuffer));
    memset(noiseBuffer, 0, sizeof(noiseBuffer));
    memset(baseIm, 0, sizeof(baseIm));
    memset(baseImVelo, 0, sizeof(baseImVelo));
    memset(baseShape, 0, sizeof(baseShape));
    memset(baseAttack, 0, sizeof(baseAttack));
    memset(baseDecay, 0, sizeof(baseDecay));
    memset(baseRelease, 0, sizeof(baseRelease));
    memset(baseDetune, 0, sizeof(baseDetune));
}

PfmEngine::~PfmEngine() {
    delete voice;
    delete timbre;
    delete synthState;
}

void PfmEngine::init(float hostSampleRate) {
    this->sampleRate = hostSampleRate;

    // PreenFM2 native sample rate
    engineRate = PREENFM_FREQUENCY;
    resampleRatio = engineRate / (double)hostSampleRate;

    // Initialize allParameterRows stub
    for (int i = 0; i < NUMBER_OF_ROWS; i++) {
        allParameterRows.row[i] = &dummyRows[i];
    }

    // Initialize midiConfig stubs
    for (int i = 0; i < MIDICONFIG_SIZE + 1; i++) {
        midiConfig[i].valueName = nullptr;
    }

    // Create SynthState
    synthState = new SynthState();
    synthState->fullState.globalTuning = 440.0f;

    // Set frequencyToUse to the standard frequency table
    frequencyToUse = frequency;

    // Create voice and timbre
    voice = new Voice();
    voice->init();

    timbre = new Timbre();
    // Zero-initialize all synth params before init.
    // On bare-metal ARM (MetaModule), new does NOT zero-initialize memory.
    // Without this, garbage values for oscillator shapes, algorithm indices,
    // filter types etc. can cause out-of-bounds array accesses and crashes.
    memset(&timbre->params, 0, sizeof(struct OneSynthParams));
    timbre->init(0, synthState);
    timbre->initVoicePointer(0, voice);

    // Set 1 voice
    timbre->params.engine1.numberOfVoice = 1;
    timbre->setVoiceNumber(0, 0);
    for (int v = 1; v < MAX_NUMBER_OF_VOICES; v++) {
        timbre->setVoiceNumber(v, -1);
    }
    timbre->numberOfVoicesChanged();

    // Set the SynthState params pointer
    synthState->setParamsAndTimbre(&timbre->params, 0);

    blockPos = BLOCK_SIZE * 2; // Force first block generation
    initialized = true;
}

void PfmEngine::loadPatch(const struct OneSynthParams& params) {
    if (!initialized) return;

    // Copy all params
    memcpy(&timbre->params, &params, sizeof(struct OneSynthParams));

    // Force 1 voice
    timbre->params.engine1.numberOfVoice = 1;

    // Re-init voice assignment
    timbre->setVoiceNumber(0, 0);
    for (int v = 1; v < MAX_NUMBER_OF_VOICES; v++) {
        timbre->setVoiceNumber(v, -1);
    }
    timbre->numberOfVoicesChanged();

    // Trigger post-load initialization
    timbre->afterNewParamsLoad();

    // Force LFO recheck
    timbre->verifyLfoUsed(ENCODER_MATRIX_SOURCE, 0.0f, 1.0f);

    // Update SynthState params pointer
    synthState->setParamsAndTimbre(&timbre->params, 0);

    // Store base values for CV modulation
    baseAlgo = timbre->params.engine1.algo;
    glideValue = timbre->params.engine1.glide;
    if (glideValue < 0.0f) glideValue = 0.0f;
    if (glideValue > 12.0f) glideValue = 12.0f;
    baseIm[0] = timbre->params.engineIm1.modulationIndex1;
    baseIm[1] = timbre->params.engineIm1.modulationIndex2;
    baseIm[2] = timbre->params.engineIm2.modulationIndex3;
    baseIm[3] = timbre->params.engineIm2.modulationIndex4;
    baseIm[4] = timbre->params.engineIm3.modulationIndex5;
    baseImVelo[0] = timbre->params.engineIm1.modulationIndexVelo1;
    baseImVelo[1] = timbre->params.engineIm1.modulationIndexVelo2;
    baseImVelo[2] = timbre->params.engineIm2.modulationIndexVelo3;
    baseImVelo[3] = timbre->params.engineIm2.modulationIndexVelo4;
    baseImVelo[4] = timbre->params.engineIm3.modulationIndexVelo5;

    // Store base values for EVO modulation
    baseShape[0] = timbre->params.osc1.shape;
    baseShape[1] = timbre->params.osc2.shape;
    baseShape[2] = timbre->params.osc3.shape;
    baseShape[3] = timbre->params.osc4.shape;
    baseShape[4] = timbre->params.osc5.shape;
    baseShape[5] = timbre->params.osc6.shape;

    baseAttack[0] = timbre->params.env1a.attackTime;
    baseAttack[1] = timbre->params.env2a.attackTime;
    baseAttack[2] = timbre->params.env3a.attackTime;
    baseAttack[3] = timbre->params.env4a.attackTime;
    baseAttack[4] = timbre->params.env5a.attackTime;
    baseAttack[5] = timbre->params.env6a.attackTime;

    baseDecay[0] = timbre->params.env1a.decayTime;
    baseDecay[1] = timbre->params.env2a.decayTime;
    baseDecay[2] = timbre->params.env3a.decayTime;
    baseDecay[3] = timbre->params.env4a.decayTime;
    baseDecay[4] = timbre->params.env5a.decayTime;
    baseDecay[5] = timbre->params.env6a.decayTime;

    baseRelease[0] = timbre->params.env1b.releaseTime;
    baseRelease[1] = timbre->params.env2b.releaseTime;
    baseRelease[2] = timbre->params.env3b.releaseTime;
    baseRelease[3] = timbre->params.env4b.releaseTime;
    baseRelease[4] = timbre->params.env5b.releaseTime;
    baseRelease[5] = timbre->params.env6b.releaseTime;

    // Store base detune values for DRIFT modulation
    baseDetune[0] = timbre->params.osc1.detune;
    baseDetune[1] = timbre->params.osc2.detune;
    baseDetune[2] = timbre->params.osc3.detune;
    baseDetune[3] = timbre->params.osc4.detune;
    baseDetune[4] = timbre->params.osc5.detune;
    baseDetune[5] = timbre->params.osc6.detune;

    // If the patch uses any USER waveforms that are not loaded, hard-mute output.
    // This makes missing files immediately obvious (requested behavior).
    missingUserWave = false;
    missingUserWavesMask = 0;
    auto userWaveNumberFromShape = [](float shapeF) -> int {
        int shape = (int)shapeF;
        if (shape >= OSC_SHAPE_USER1 && shape <= OSC_SHAPE_USER6)
            return (shape - OSC_SHAPE_USER1) + 1; // 1..6
        return 0;
    };

    // Only treat missing user waves as fatal if that oscillator is actually mixed to output.
    // If a USER wave is used only as a modulator (mix=0), the patch can still produce sound.
    static constexpr float kMixEps = 1e-4f;
    auto isMixed = [&](int oscIndex0) -> bool {
        switch (oscIndex0) {
            case 0: return timbre->params.engineMix1.mixOsc1 > kMixEps;
            case 1: return timbre->params.engineMix1.mixOsc2 > kMixEps;
            case 2: return timbre->params.engineMix2.mixOsc3 > kMixEps;
            case 3: return timbre->params.engineMix2.mixOsc4 > kMixEps;
            case 4: return timbre->params.engineMix3.mixOsc5 > kMixEps;
            case 5: return timbre->params.engineMix3.mixOsc6 > kMixEps;
            default: return true;
        }
    };

    int u;
    auto markMissing = [&](int userWaveNumber) {
        if (userWaveNumber >= 1 && userWaveNumber <= 6) {
            missingUserWavesMask |= (uint8_t)(1u << (userWaveNumber - 1));
            missingUserWave = true;
        }
    };

    if ((u = userWaveNumberFromShape(timbre->params.osc1.shape)) && isMixed(0) && !pfm::isUserWaveformLoaded(u)) markMissing(u);
    if ((u = userWaveNumberFromShape(timbre->params.osc2.shape)) && isMixed(1) && !pfm::isUserWaveformLoaded(u)) markMissing(u);
    if ((u = userWaveNumberFromShape(timbre->params.osc3.shape)) && isMixed(2) && !pfm::isUserWaveformLoaded(u)) markMissing(u);
    if ((u = userWaveNumberFromShape(timbre->params.osc4.shape)) && isMixed(3) && !pfm::isUserWaveformLoaded(u)) markMissing(u);
    if ((u = userWaveNumberFromShape(timbre->params.osc5.shape)) && isMixed(4) && !pfm::isUserWaveformLoaded(u)) markMissing(u);
    if ((u = userWaveNumberFromShape(timbre->params.osc6.shape)) && isMixed(5) && !pfm::isUserWaveformLoaded(u)) markMissing(u);
}

const char* PfmEngine::getPatchName() const {
    if (!initialized) return "";
    return timbre->params.presetName;
}

void PfmEngine::noteOn(float frequencyHz, int velocity) {
    if (!initialized) return;

    velocity = velocity > 127 ? 127 : velocity;

    // Handle glide: if a note is already playing and glide > 0,
    // the one-pole filter in buildBlock() will smoothly interpolate.
    if (noteActive && glideValue > 0.0f) {
        // Set target; currentFrequency stays where it is
        // and the exponential approach filter will glide to it
        targetFrequency = frequencyHz;
    } else {
        // First note or no glide - jump immediately
        currentFrequency = frequencyHz;
        targetFrequency = frequencyHz;
    }

    // Use note 128 convention for CV frequency (like the hardware CVIN)
    timbre->setCvFrequency(currentFrequency);
    timbre->noteOn(128, velocity);

    // For note=128 (CV), preenNoteOnUpdateMatrix skips setting velocity/note
    // matrix sources. We set them manually so patches using velocity modulation work.
    timbre->setMatrixSource(MATRIX_SOURCE_VELOCITY, velocity * (1.0f / 127.0f));

    noteActive = true;
}

void PfmEngine::noteOff() {
    if (!initialized || !noteActive) return;
    timbre->noteOff(128);
    noteActive = false;
}

void PfmEngine::panic() {
    if (!initialized) return;

    // Kill all voices immediately (bypasses the normal envelope/note lifecycle)
    timbre->stopPlayingNow();

    noteActive = false;
    currentFrequency = 440.0f;
    targetFrequency = 440.0f;
    resamplePhase = 0.0;
    blockPos = BLOCK_SIZE * 2;
    lastOutL = lastOutR = 0.0f;
    prevOutL = prevOutR = 0.0f;
    memset(blockBuffer, 0, sizeof(blockBuffer));
}

void PfmEngine::setCvin1(float value) {
    cvin1 = value;
}

void PfmEngine::setCvin2(float value) {
    cvin2 = value;
}

void PfmEngine::setCvin2Amount(float amount) {
    cvin2Amount = amount;
}

void PfmEngine::setCv1Connected(bool connected) {
    cv1Connected = connected;
}

void PfmEngine::setCv2Connected(bool connected) {
    cv2Connected = connected;
}

void PfmEngine::setCvinEvo(float value) {
    cvinEvo = value;
}

void PfmEngine::setEvoConnected(bool connected) {
    evoConnected = connected;
}

void PfmEngine::setCvinDrift(float value) {
    cvinDrift = value;
}

void PfmEngine::setDriftConnected(bool connected) {
    driftConnected = connected;
}

void PfmEngine::setManualAlgo(int algo) {
    manualAlgo = algo;
}

int PfmEngine::getManualAlgo() const {
    return manualAlgo;
}

int PfmEngine::getBaseAlgo() const {
    return (int)baseAlgo;
}

void PfmEngine::setGlide(float glide) {
    if (!initialized) return;
    glideValue = glide;
    if (glideValue < 0.0f) glideValue = 0.0f;
    if (glideValue > 12.0f) glideValue = 12.0f;
    // Also set in params for reference (though PFM's built-in glide doesn't
    // work in CV mode; we handle portamento ourselves at the engine level)
    timbre->params.engine1.glide = glideValue;
}

void PfmEngine::updatePitch(float frequencyHz) {
    if (!initialized || !noteActive) return;

    // Ignore tiny frequency changes (avoids jitter from floating-point noise)
    float ratio = frequencyHz / targetFrequency;
    if (ratio > 0.999f && ratio < 1.001f) return;

    targetFrequency = frequencyHz;

    if (glideValue <= 0.0f) {
        // No glide - jump immediately
        currentFrequency = frequencyHz;
    }
    // Otherwise the one-pole filter in buildBlock() handles the smooth transition
}

void PfmEngine::setSampleRate(float newRate) {
    sampleRate = newRate;
    resampleRatio = engineRate / (double)newRate;
}

void PfmEngine::buildBlock() {
    if (!initialized) {
        memset(blockBuffer, 0, sizeof(blockBuffer));
        return;
    }

    // Generate noise (replaces RNG_GetRandomNumber from hardware)
    uint32_t random32bit = RNG_GetRandomNumber();
    noise[0] = (random32bit & 0xffff) * .000030518f - 1.0f;
    noise[1] = (random32bit >> 16) * .000030518f - 1.0f;
    for (int i = 2; i < 32; ) {
        random32bit = 214013 * random32bit + 2531011;
        noise[i++] = (random32bit & 0xffff) * .000030518f - 1.0f;
        noise[i++] = (random32bit >> 16) * .000030518f - 1.0f;
    }

    // Clear matrix CV sources (CV1/CV2 now drive params directly)
    timbre->setMatrixSource(MATRIX_SOURCE_CVIN1, 0.0f);
    timbre->setMatrixSource(MATRIX_SOURCE_CVIN2, 0.0f);
    timbre->setMatrixSource(MATRIX_SOURCE_CVIN3, 0.0f);
    timbre->setMatrixSource(MATRIX_SOURCE_CVIN4, 0.0f);

    // ---- CV1: Algorithm selection ----
    // Priority: CV1 connected > manual button override > patch default
    if (cv1Connected) {
        // Maps -1.0..+1.0 (i.e. -5V..+5V) linearly to algorithms 0..27
        float algoF = (cvin1 + 1.0f) * 0.5f * 27.0f;
        int algo = (int)(algoF + 0.5f);
        if (algo < 0) algo = 0;
        if (algo > 27) algo = 27;
        timbre->params.engine1.algo = (float)algo;
    } else if (manualAlgo >= 0) {
        timbre->params.engine1.algo = (float)manualAlgo;
    } else {
        timbre->params.engine1.algo = baseAlgo;
    }

    // ---- CV2: IM Scanner ("Spectral Spotlight") ----
    // Maps -1.0..+1.0 to a phase that sweeps a spotlight across IM1-IM5
    // The spotlit IM is boosted to 2.5x, others fade to 0.2x of base value
    // Velocity IMs follow with a half-zone phase offset for evolving character
    // With a synced LFO, each operator pair gets its moment to shine
    if (cv2Connected) {
        float phase = (cvin2 + 1.0f) * 0.5f;
        if (phase < 0.0f) phase = 0.0f;
        if (phase > 1.0f) phase = 1.0f;

        float amt = cvin2Amount;
        if (amt < 0.0f) amt = 0.0f;
        if (amt > 1.0f) amt = 1.0f;

        for (int i = 0; i < 5; i++) {
            // IM spotlight: centers at 0.0, 0.25, 0.5, 0.75, 1.0
            float center = i * 0.25f;
            float dist = fabsf(phase - center);
            float bump = 1.0f - dist * 4.0f;
            if (bump < 0.0f) bump = 0.0f;
            // Smooth the triangle into a cosine bell for more musical transitions
            bump = bump * bump * (3.0f - 2.0f * bump); // smoothstep
            float imMult = 0.2f + 2.3f * bump;

            // Velocity spotlight: shifted by half a zone (0.125)
            // Creates rhythmic interplay when IM and velocity emphasis alternate
            float vCenter = center + 0.125f;
            float vDist = fabsf(phase - vCenter);
            float vBump = 1.0f - vDist * 4.0f;
            if (vBump < 0.0f) vBump = 0.0f;
            vBump = vBump * vBump * (3.0f - 2.0f * vBump);
            float veloMult = 0.2f + 2.3f * vBump;

            // Blend from base (amt=0) to full spotlight (amt=1)
            // multiplier = 1 + (spotlightMult - 1) * amt
            imMult = 1.0f + (imMult - 1.0f) * amt;
            veloMult = 1.0f + (veloMult - 1.0f) * amt;

            // Apply to the engine params
            switch (i) {
                case 0:
                    timbre->params.engineIm1.modulationIndex1 = baseIm[0] * imMult;
                    timbre->params.engineIm1.modulationIndexVelo1 = baseImVelo[0] * veloMult;
                    break;
                case 1:
                    timbre->params.engineIm1.modulationIndex2 = baseIm[1] * imMult;
                    timbre->params.engineIm1.modulationIndexVelo2 = baseImVelo[1] * veloMult;
                    break;
                case 2:
                    timbre->params.engineIm2.modulationIndex3 = baseIm[2] * imMult;
                    timbre->params.engineIm2.modulationIndexVelo3 = baseImVelo[2] * veloMult;
                    break;
                case 3:
                    timbre->params.engineIm2.modulationIndex4 = baseIm[3] * imMult;
                    timbre->params.engineIm2.modulationIndexVelo4 = baseImVelo[3] * veloMult;
                    break;
                case 4:
                    timbre->params.engineIm3.modulationIndex5 = baseIm[4] * imMult;
                    timbre->params.engineIm3.modulationIndexVelo5 = baseImVelo[4] * veloMult;
                    break;
            }
        }
    } else {
        // Restore base IM values when CV2 not connected
        timbre->params.engineIm1.modulationIndex1 = baseIm[0];
        timbre->params.engineIm1.modulationIndex2 = baseIm[1];
        timbre->params.engineIm2.modulationIndex3 = baseIm[2];
        timbre->params.engineIm2.modulationIndex4 = baseIm[3];
        timbre->params.engineIm3.modulationIndex5 = baseIm[4];
        timbre->params.engineIm1.modulationIndexVelo1 = baseImVelo[0];
        timbre->params.engineIm1.modulationIndexVelo2 = baseImVelo[1];
        timbre->params.engineIm2.modulationIndexVelo3 = baseImVelo[2];
        timbre->params.engineIm2.modulationIndexVelo4 = baseImVelo[3];
        timbre->params.engineIm3.modulationIndexVelo5 = baseImVelo[4];
    }

    // ---- EVO CV: Shape + Envelope evolution ----
    // Maps 0.0..1.0 (0V..10V unipolar) to simultaneous modulation of:
    //   - Oscillator shape (SIN → SAW → SQUARE → RAND)
    //   - Attack time (inverted: high V = punchy, low V = slow pad)
    //   - Decay time (inverted: high V = percussive, low V = sustained)
    //   - Release time (normal: high V = long ambient tails, low V = tight)
    // Shapes switch discretely (FM wavetable swap); envelopes use exponential curves.
    // Attack/Release take effect on next gate on/off; Decay updates in real-time.
    if (evoConnected) {
        float phase = cvinEvo;
        if (phase < 0.0f) phase = 0.0f;
        if (phase > 1.0f) phase = 1.0f;

        // phase=0 should leave the patch fully unchanged
        if (phase <= 0.0001f) {
            // Restore base oscillator shapes
            timbre->params.osc1.shape = baseShape[0];
            timbre->params.osc2.shape = baseShape[1];
            timbre->params.osc3.shape = baseShape[2];
            timbre->params.osc4.shape = baseShape[3];
            timbre->params.osc5.shape = baseShape[4];
            timbre->params.osc6.shape = baseShape[5];
            // Keep envelope multipliers at 1.0 (do nothing)
        } else {

        // --- Oscillator shape switching ---
        // 4 zones progressing from warm to chaotic:
        //   0.00-0.25: SIN (smooth, warm FM)
        //   0.25-0.50: SAW (bright, aggressive harmonics)
        //   0.50-0.75: SQUARE (hollow, harsh)
        //   0.75-1.00: RAND (noise, glitchy/digital)
        // Only changes operators that aren't OFF in the base patch.
        float shapeF;
        if (phase < 0.25f)      shapeF = (float)OSC_SHAPE_SIN;
        else if (phase < 0.50f) shapeF = (float)OSC_SHAPE_SAW;
        else if (phase < 0.75f) shapeF = (float)OSC_SHAPE_SQUARE;
        else                    shapeF = (float)OSC_SHAPE_RAND;

        struct OscillatorParams* oscs[6] = {
            &timbre->params.osc1, &timbre->params.osc2, &timbre->params.osc3,
            &timbre->params.osc4, &timbre->params.osc5, &timbre->params.osc6
        };
        for (int i = 0; i < 6; i++) {
            if (baseShape[i] != (float)OSC_SHAPE_OFF) {
                oscs[i]->shape = shapeF;
            }
        }

        // --- Envelope modulation (relative to patch base values) ---
        // All envelopes are MULTIPLIED from their base values, never replaced.
        // At 0V (phase=0): patch sounds exactly as designed (1.0x multiplier).
        // As voltage rises: attack/decay get tighter, release gets longer.
        //
        // Attack multiplier (inverted): only faster, never slower
        //   phase=0: 1.0x,  phase=0.5: 0.32x,  phase=1.0: 0.05x (20x faster)
        // powf(0.05, phase) = exp(ln(0.05) * phase) = exp(-2.99573 * phase)
        float attackMult = expf_fast_local(-2.99573f * phase);
        //
        // Decay multiplier (inverted): only shorter, never longer
        //   phase=0: 1.0x,  phase=0.5: 0.39x,  phase=1.0: 0.1x (10x shorter)
        // powf(0.1, phase) = exp(ln(0.1) * phase) = exp(-2.30259 * phase)
        float decayMult = expf_fast_local(-2.30259f * phase);
        //
        // Release multiplier (normal): only longer, never shorter
        //   phase=0: 1.0x,  phase=0.5: 2.24x,  phase=1.0: 5.0x (5x longer)
        // powf(5.0, phase) = exp(ln(5) * phase) = exp(1.60944 * phase)
        float releaseMult = expf_fast_local(1.60944f * phase);

        // Apply multipliers to all 6 operator envelopes
        struct EnvelopeParamsA* envA[6] = {
            &timbre->params.env1a, &timbre->params.env2a, &timbre->params.env3a,
            &timbre->params.env4a, &timbre->params.env5a, &timbre->params.env6a
        };
        struct EnvelopeParamsB* envB[6] = {
            &timbre->params.env1b, &timbre->params.env2b, &timbre->params.env3b,
            &timbre->params.env4b, &timbre->params.env5b, &timbre->params.env6b
        };
        for (int i = 0; i < 6; i++) {
            float att = baseAttack[i] * attackMult;
            if (att < 0.01f) att = 0.01f;    // incTab minimum
            envA[i]->attackTime = att;

            float dec = baseDecay[i] * decayMult;
            if (dec < 0.01f) dec = 0.01f;
            envA[i]->decayTime = dec;

            float rel = baseRelease[i] * releaseMult;
            if (rel > 16.0f) rel = 16.0f;    // incTab maximum
            envB[i]->releaseTime = rel;
        }

        // Reload decay rates so they take effect mid-note (real-time "breathing")
        // Attack and release are picked up at next gate on/off respectively
        timbre->env1.reloadADSR(2);
        timbre->env2.reloadADSR(2);
        timbre->env3.reloadADSR(2);
        timbre->env4.reloadADSR(2);
        timbre->env5.reloadADSR(2);
        timbre->env6.reloadADSR(2);

        }

    } else {
        // Restore base values when EVO not connected
        timbre->params.osc1.shape = baseShape[0];
        timbre->params.osc2.shape = baseShape[1];
        timbre->params.osc3.shape = baseShape[2];
        timbre->params.osc4.shape = baseShape[3];
        timbre->params.osc5.shape = baseShape[4];
        timbre->params.osc6.shape = baseShape[5];

        struct EnvelopeParamsA* envA[6] = {
            &timbre->params.env1a, &timbre->params.env2a, &timbre->params.env3a,
            &timbre->params.env4a, &timbre->params.env5a, &timbre->params.env6a
        };
        struct EnvelopeParamsB* envB[6] = {
            &timbre->params.env1b, &timbre->params.env2b, &timbre->params.env3b,
            &timbre->params.env4b, &timbre->params.env5b, &timbre->params.env6b
        };
        for (int i = 0; i < 6; i++) {
            envA[i]->attackTime = baseAttack[i];
            envA[i]->decayTime = baseDecay[i];
            envB[i]->releaseTime = baseRelease[i];
        }
        // Restore decay rates
        timbre->env1.reloadADSR(2);
        timbre->env2.reloadADSR(2);
        timbre->env3.reloadADSR(2);
        timbre->env4.reloadADSR(2);
        timbre->env5.reloadADSR(2);
        timbre->env6.reloadADSR(2);
    }

    // ---- DRIFT CV: Operator Detune / Harmonic Spread ----
    // Maps -1.0..+1.0 (-5V..+5V bipolar) to asymmetric detuning of all 6 operators.
    // Each operator gets a different spread multiplier so they fan out unevenly,
    // creating rich, complex textures rather than uniform chorus:
    //   - Low voltage: subtle analog-style warmth/chorusing
    //   - Medium voltage: metallic, bell-like inharmonic tones
    //   - High voltage: extreme inharmonic/noise territory
    // The asymmetric pattern (+1.00, -0.73, +1.27, -1.13, +0.53, -0.87) ensures
    // that operators detune in opposing directions with varying intensity, creating
    // a cloud of frequencies that sounds alive and dimensional.
    // Max detune offset at ±5V = ±3.0 (= ±15% frequency shift in KEYBOARD mode).
    if (driftConnected) {
        float drift = cvinDrift;
        if (drift < -1.0f) drift = -1.0f;
        if (drift > 1.0f) drift = 1.0f;

        // Make DRIFT less subtle: apply a curve that increases perceived depth,
        // and increase max detune at extremes.
        float absDrift = fabsf(drift);
        // powf(x, 0.75) = exp(0.75 * ln(x)) — fast approximation avoids libm powf
        float curved = (absDrift > 0.0001f) ? expf_fast_local(0.75f * logf_fast(absDrift)) : 0.0f;
        drift = (drift < 0.0f) ? -curved : curved;

        // Asymmetric per-operator spread multipliers
        // Alternating positive/negative with varying magnitudes
        static const float spreadPattern[6] = {
            +1.00f, -0.73f, +1.27f, -1.13f, +0.53f, -0.87f
        };

        // Scale: at full ±5V, maximum detune offset is ±3.0
        float maxDetune = 6.0f;

        struct OscillatorParams* oscs[6] = {
            &timbre->params.osc1, &timbre->params.osc2, &timbre->params.osc3,
            &timbre->params.osc4, &timbre->params.osc5, &timbre->params.osc6
        };
        for (int i = 0; i < 6; i++) {
            // Only detune operators that aren't OFF
            if (baseShape[i] != (float)OSC_SHAPE_OFF) {
                oscs[i]->detune = baseDetune[i] + drift * maxDetune * spreadPattern[i];
            }
        }
    } else {
        // Restore base detune when DRIFT not connected
        timbre->params.osc1.detune = baseDetune[0];
        timbre->params.osc2.detune = baseDetune[1];
        timbre->params.osc3.detune = baseDetune[2];
        timbre->params.osc4.detune = baseDetune[3];
        timbre->params.osc5.detune = baseDetune[4];
        timbre->params.osc6.detune = baseDetune[5];
    }

    // ---- Glide / Portamento ----
    // PreenFM2's built-in glide doesn't work in CV frequency mode (note 128)
    // because propagateCvFreq() directly overwrites mainFrequency each block.
    //
    // We implement classic analog-style portamento using a one-pole lowpass filter
    // on log-frequency. This creates an exponential approach curve (like an RC
    // circuit on a control voltage) - it moves fast at first, then gently settles
    // into the target note. This sounds far more musical than linear glide because
    // it spends less time lingering in dissonant "between" frequencies.
    //
    // Glide 0 = completely off (instant pitch changes).
    if (noteActive && glideValue > 0.0f && currentFrequency != targetFrequency
        && currentFrequency > 0.0f && targetFrequency > 0.0f) {

        // Filter coefficient derived from PreenFM2's glide speed table.
        // Higher glide values = slower = smaller coefficient.
        // coeff = 3.0 / timeInBlocks gives ~95% convergence in that many blocks.
        //   glide 1:  3/5    = 0.600  →  ~4ms  (very fast snap)
        //   glide 4:  3/22   = 0.136  → ~17ms
        //   glide 6:  3/50   = 0.060  → ~39ms
        //   glide 9:  3/200  = 0.015  → 156ms
        //   glide 12: 3/2700 = 0.0011 → 2.1s   (slow, lush)
        static const float glideTimeBlocks[12] = {
            5.0f, 9.0f, 15.0f, 22.0f, 35.0f, 50.0f,
            90.0f, 140.0f, 200.0f, 500.0f, 1200.0f, 2700.0f
        };

        int idx = (int)(glideValue - 0.95f);
        if (idx < 0) idx = 0;
        if (idx > 11) idx = 11;

        float coeff = 3.0f / glideTimeBlocks[idx];

        // One-pole lowpass in log-frequency space (= linear in pitch/semitones)
        float logCurrent = logf(currentFrequency);
        float logTarget = logf(targetFrequency);
        logCurrent += (logTarget - logCurrent) * coeff;

        // Snap to target when within ~0.1 cent (inaudible difference)
        if (fabsf(logTarget - logCurrent) < 0.00006f) {
            currentFrequency = targetFrequency;
        } else {
            currentFrequency = expf(logCurrent);
        }
    }

    // Update CV frequency if note is active (for pitch tracking)
    if (noteActive) {
        timbre->setCvFrequency(currentFrequency);
        timbre->propagateCvFreq(128);
    }

    // Prepare and render the block
    timbre->cleanNextBlock();
    if (timbre->params.engine1.numberOfVoice > 0) {
        timbre->prepareForNextBlock();
        timbre->glide();
    }
    timbre->prepareMatrixForNewBlock();
    timbre->voicesNextBlock();

    // Apply gain (fxAfterBlock handles filter + gate + mixing)
    // ratioTimbres must match the 18-bit DAC scale (131071) to work correctly
    // with RATIOINV (1/131072) used internally by Timbre for gain/filter math
    if (timbre->params.engine1.numberOfVoice > 0) {
        timbre->fxAfterBlock(DAC_RATIO);
    }

    // Copy to our buffer
    const float* src = timbre->getSampleBlock();
    memcpy(blockBuffer, src, BLOCK_SIZE * 2 * sizeof(float));

    blockPos = 0;
}

void PfmEngine::process(float& outLeft, float& outRight) {
    if (!initialized) {
        outLeft = outRight = 0.0f;
        return;
    }

    // If the patch references missing user waveforms we still run the engine
    // (so noteOn/noteOff/glide state can't get stuck), but we hard-mute output.
    const bool muteOutput = missingUserWave;

    // Simple resampling: PreenFM2 runs at ~41kHz, host may run at 44.1/48/96kHz
    resamplePhase += resampleRatio;

    while (resamplePhase >= 1.0) {
        resamplePhase -= 1.0;

        // Advance one engine sample
        if (blockPos >= BLOCK_SIZE * 2) {
            buildBlock();
        }

        prevOutL = lastOutL;
        prevOutR = lastOutR;

        // Read stereo interleaved: [L0, R0, L1, R1, ...]
        lastOutL = blockBuffer[blockPos];
        lastOutR = blockBuffer[blockPos + 1];
        blockPos += 2;
    }

    // Linear interpolation between engine samples
    float frac = (float)resamplePhase;
    outLeft = prevOutL + (lastOutL - prevOutL) * frac;
    outRight = prevOutR + (lastOutR - prevOutR) * frac;

    // Scale from PreenFM2's 18-bit DAC range to VCV's +/-5V
    // After fxAfterBlock(131071), samples are in approximately [-131071, +131071]
    // Scale to [-5V, +5V] for VCV Rack
    const float scale = 5.0f / DAC_RATIO;
    outLeft *= scale;
    outRight *= scale;

    if (muteOutput) {
        outLeft = 0.0f;
        outRight = 0.0f;
    }
}
