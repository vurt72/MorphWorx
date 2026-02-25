/*
 * PfmEngine.h - Minimal PreenFM2 engine wrapper for VCV Rack
 *
 * Based on PreenFM2 by Xavier Hosxe (GPL-3.0-or-later)
 * VCV Rack port by Bemushroomed
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include "synth/Common.h"

// Forward declarations
class Timbre;
class Voice;
class SynthState;

class PfmEngine {
public:
    PfmEngine();
    ~PfmEngine();

    // Initialize the engine for a given sample rate
    void init(float sampleRate);

    // Load a patch into the engine (copies params)
    void loadPatch(const struct OneSynthParams& params);

    // Get the current patch name (up to 12 chars + null)
    const char* getPatchName() const;

    // Trigger a note on with frequency (Hz) and velocity (0-127)
    void noteOn(float frequencyHz, int velocity);

    // Release the current note
    void noteOff();

    // Set CV modulation inputs (-1.0 to 1.0)
    void setCvin1(float value);
    void setCvin2(float value);

    void setCvin2Amount(float amount);
    // Set EVO CV input (0.0 to 1.0, unipolar)
    void setCvinEvo(float value);

    // Set DRIFT CV input (-1.0 to 1.0, bipolar)
    void setCvinDrift(float value);

    // Set CV connection state (used to decide algo/IM/EVO/DRIFT modulation vs base values)
    void setCv1Connected(bool connected);
    void setCv2Connected(bool connected);
    void setEvoConnected(bool connected);
    void setDriftConnected(bool connected);

    // Manual algorithm override (-1 = use patch default or CV, 0-27 = specific algo)
    void setManualAlgo(int algo);
    int getManualAlgo() const;
    int getBaseAlgo() const;

    // Set glide time (0-12)
    void setGlide(float glide);

    // Update pitch while note is active (for glide/portamento)
    // Call this every frame with the current V/Oct frequency;
    // if glide > 0 and note is active, it smoothly interpolates.
    void updatePitch(float frequencyHz);

    // Process one sample, returns stereo pair
    void process(float& outLeft, float& outRight);

    // Hard reset voice state (kills any stuck voices)
    void panic();

    // Set the engine sample rate (for resampling)
    void setSampleRate(float sampleRate);

    // True if the currently loaded patch references User1..User6 but the files were not loaded.
    bool isMissingUserWave() const { return missingUserWave; }

    // Bitmask of missing user waves (bit0=USR1 ... bit5=USR6)
    uint8_t getMissingUserWavesMask() const { return missingUserWavesMask; }

private:
    void buildBlock();

    SynthState* synthState;
    Timbre* timbre;
    Voice* voice;

    float sampleRate;
    float engineRate; // PreenFM2 native rate

    // Block buffer (PreenFM2 generates BLOCK_SIZE stereo samples at a time)
    float blockBuffer[BLOCK_SIZE * 2];
    int blockPos; // current read position in block

    // CV values
    float cvin1, cvin2;
    float cvin2Amount;     // CV2 amount (0.0..1.0)
    float cvinEvo;         // EVO CV (0.0 to 1.0 unipolar)
    float cvinDrift;       // DRIFT CV (-1.0 to 1.0 bipolar)

    // CV connection state (set by module)
    bool cv1Connected, cv2Connected;
    bool evoConnected;
    bool driftConnected;

    // Base patch values for CV modulation (stored on patch load)
    int manualAlgo;        // -1 = patch default, 0-27 = manual override
    float baseAlgo;
    float baseIm[5];       // IM1-IM5
    float baseImVelo[5];   // Velocity IM1-IM5

    // Base patch values for EVO modulation
    float baseShape[6];    // Osc1-Osc6 waveform shape
    float baseAttack[6];   // Env1a-Env6a attack time
    float baseDecay[6];    // Env1a-Env6a decay time
    float baseRelease[6];  // Env1b-Env6b release time

    // Base patch values for DRIFT modulation
    float baseDetune[6];   // Osc1-Osc6 detune

    // Note state
    bool noteActive;
    float currentFrequency;  // The frequency currently being sent to the engine (glide-smoothed)
    float targetFrequency;   // The frequency we're gliding towards
    float glideValue;        // Current glide setting (0=off, 1-12 = speed)

    // Noise buffer for RNG
    float noiseBuffer[32];

    // Resampling state
    double resamplePhase;
    double resampleRatio;
    float lastOutL, lastOutR;
    float prevOutL, prevOutR;

    bool initialized;

    bool missingUserWave = false;
    uint8_t missingUserWavesMask = 0;
};
