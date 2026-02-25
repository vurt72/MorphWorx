#pragma once

// ============================================================================
// Pattern Data for Trigonomicon Drum Trigger Module
// ============================================================================
//
// Architecture:
//   Each pattern contains 5 VoicePatterns.
//   Each VoicePattern has its own step count (numSteps) and probability array.
//   Voices wrap independently at their own lengths, enabling:
//     - Standard patterns (all voices same length)
//     - Odd time signatures (all voices at 14, 24, 28 steps, etc.)
//     - Polymetric patterns (each voice at a different length)
//     - High-resolution patterns (64 steps for 32nd note detail)
//
// Voices: 0=Kick, 1=Snare1, 2=Snare2, 3=ClosedHiHat, 4=OpenHiHat
//
// Probability values:
//   1.0  = always fires
//   0.0  = never fires
//   0.7  = likely hit
//   0.5  = coin flip (variation / ghost accent)
//   0.3  = light ghost / occasional
//   0.2  = rare texture
//
// 60 patterns organized as:
//   0-9:   Original patterns (32 steps, 16th note resolution, 2 bars 4/4)
//   10-11: Jungle / Breakbeat inspired
//   12-13: 6/8 compound time grooves
//   14-15: 7-based odd time signatures
//   16-17: Polymetric patterns (per-voice variable lengths)
//   18-19: Experimental but musical
//   20-39: Jungle, Breakcore, Amen expansions
//   40-44: Drum & Bass / Jungle
//   45-49: UK Garage
//   50-54: Dub / Dubwise / Reggae influenced
//   55-59: IDM / Drillcore
// ============================================================================

static const int MAX_STEPS    = 64;
static const int NUM_VOICES   = 5;
static const int NUM_PATTERNS = 60;

// Voice indices
static const int VOICE_KICK   = 0;
static const int VOICE_SNARE1 = 1;
static const int VOICE_SNARE2 = 2;
static const int VOICE_CHAT   = 3;  // Closed hihat
static const int VOICE_OHAT   = 4;  // Open hihat

// ============================================================================
// Data structures
// ============================================================================

struct VoicePattern {
    int numSteps;             // Number of active steps (voice wraps here)
    float steps[MAX_STEPS];   // Probability data (only first numSteps used)
};

struct Pattern {
    VoicePattern voices[NUM_VOICES];
};

// ============================================================================
// Pattern probability tables
// ============================================================================

static const Pattern PATTERNS[NUM_PATTERNS] = {

// ========================================================================
// PATTERN 0: Classic Amen Break (bars 1-2)
// 32 steps | 16th note clock | 2 bars 4/4
// The iconic break: K--K--S-K-K---S- | K--K--S-K-K---S-
// ========================================================================
{{
  { 32, { 1, 0, 0, 1, 0, 0, 0, 0,  1, 1, 0, 0, 0, 0, 0, 0,
          1, 0, 0, 1, 0, 0, 0, 0,  1, 1, 0, 0, 0, 0, 0, 0 }},
  { 32, { 0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  { 32, { 0, 0,.3, 0, 0, 0,.3, 0,  0, 0,.3, 0, 0, 0,.3, 0,
          0, 0,.3, 0, 0, 0,.3, 0,  0, 0,.3, 0, 0, 0,.3, 0 }},
  { 32, { 1,.3, 1,.3, 1,.3, 1,.3,  1,.3, 1,.3, 1,.3, 1,.3,
          1,.3, 1,.3, 1,.3, 1,.3,  1,.3, 1,.3, 1,.3, 1,.3 }},
  { 32, { 0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERN 1: Amen Break Bars 3-4 (syncopated variation)
// 32 steps | 16th note clock | 2 bars 4/4
// Empty beat 1 in bar 2, delayed snare, pickup fills
// ========================================================================
{{
  { 32, { 1, 0, 0, 1, 0, 0, 0, 0,  1, 1, 0, 0, 0, 0, 0, 0,
          0, 0, 1, 0, 0, 0, 0, 0,  0, 1, 0, 1, 0, 0, 0, 0 }},
  { 32, { 0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 0, 1, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  { 32, { 0, 0, 0, 0, 0, 0,.3, 0,  0, 0, 0, 0, 0, 0,.3, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0,.5, 0, 0,.5, 0,.5 }},
  { 32, { 1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0,
          1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0 }},
  { 32, { 0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 1, 0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 2: Think Break (Lyn Collins)
// 32 steps | 16th note clock | 2 bars 4/4
// Raw funky breakbeat, foundation of countless sampled tracks
// ========================================================================
{{
  { 32, { 1, 0, 0, 0, 0, 0, 1, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 1, 0,  1, 0, 0, 0, 1, 0, 0, 0 }},
  { 32, { 0, 0, 1, 0, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 0, 0,
          0, 0, 1, 0, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 1, 0 }},
  { 32, { 0,.3, 0,.3, 0,.3, 0,.3,  0,.3, 0,.3, 0,.3, 0,.3,
          0,.3, 0,.3, 0,.3, 0,.3,  0,.3, 0,.3, 0,.3, 0,.3 }},
  { 32, { 1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
          1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1 }},
  { 32, { 0, 0, 0, 0, 0, 1, 0, 0,  0, 0, 0, 0, 0, 1, 0, 0,
          0, 0, 0, 0, 0, 1, 0, 0,  0, 0, 0, 0, 0, 1, 0, 0 }},
}},

// ========================================================================
// PATTERN 3: Funky Drummer (James Brown / Clyde Stubblefield inspired)
// 32 steps | 16th note clock | 2 bars 4/4
// The most sampled break in history. Ghost-note paradise.
// ========================================================================
{{
  { 32, { 1, 0, 0, 1, 0, 0, 0, 1,  0, 0, 1, 0, 1, 0, 0, 0,
          0, 0, 1, 0, 0, 0, 1, 0,  0, 0, 1, 0, 1, 0, 0, 0 }},
  { 32, { 0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  { 32, { 0,.3, 0,.3, 0,.5,.3, 0, .3, 0, 0,.5, 0,.3,.5,.3,
          0,.3, 0,.3, 0,.5,.3, 0, .3, 0, 0,.5, 0,.3,.5,.3 }},
  { 32, { 1,.7, 1,.7, 1,.7, 1,.7,  1,.7, 1,.7, 1,.7, 1,.7,
          1,.7, 1,.7, 1,.7, 1,.7,  1,.7, 1,.7, 1,.7, 1,.7 }},
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 1, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 1, 0 }},
}},

// ========================================================================
// PATTERN 4: Breakcore Chopped Amen
// 32 steps | 16th note clock | 2 bars 4/4
// Fragmented, stutter-edited, rapid kick/snare swaps
// ========================================================================
{{
  { 32, { 1, 1, 0, 0, 0, 0, 1, 1,  0, 0, 1, 0, 0, 1, 0, 0,
          1, 0, 0, 1, 1, 0, 0, 0,  1, 1, 0, 0, 1, 0, 1, 0 }},
  { 32, { 0, 0, 1, 1, 0, 0, 0, 0,  1, 1, 0, 0, 1, 0, 0, 1,
          0, 0, 1, 0, 0, 1, 1, 0,  0, 0, 1, 1, 0, 0, 0, 1 }},
  { 32, { 0, 0, 0, 0,.5,.5, 0, 0,  0, 0, 0,.5, 0, 0,.5, 0,
          0,.5, 0, 0, 0, 0, 0,.5,  0, 0, 0, 0, 0,.5, 0, 0 }},
  { 32, { 1, 0, 0, 1, 1, 0, 1, 0,  0, 1, 0, 0, 1, 0, 0, 1,
          0, 1, 0, 0, 1, 0, 1, 1,  0, 0, 1, 0, 0, 1, 0, 0 }},
  { 32, { 0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERN 5: IDM Glitch (Autechre / Aphex Twin influenced)
// 32 steps | 16th note clock | 2 bars 4/4
// Irregular groupings, non-obvious accents, sparse textures
// ========================================================================
{{
  { 32, { 1, 0, 0, 0, 0, 0, 1, 0,  0, 0, 0, 1, 0, 0, 1, 0,
          0, 1, 0, 0, 0, 1, 0, 0,  1, 0, 0, 0, 0, 1, 0, 0 }},
  { 32, { 0, 0, 0, 1, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 1, 0, 0, 1,  0, 0, 0, 0, 0, 0, 1, 0 }},
  { 32, {.2, 0,.3, 0, 0,.2, 0,.3,  0,.2, 0, 0,.3, 0, 0,.2,
         .3, 0, 0,.2, 0, 0,.3, 0,  0,.2, 0,.3, 0, 0, 0,.2 }},
  { 32, { 0, 1, 0, 0, 1, 0, 0, 0,  1, 0, 0, 1, 0, 1, 0, 0,
          0, 0, 1, 0, 0, 0, 1, 0,  0, 1, 0, 0, 1, 0, 0, 1 }},
  { 32, { 0, 0, 0, 0, 0, 1, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 6: Drill Core (half-time 808)
// 32 steps | 16th note clock | 2 bars 4/4
// Sparse kick, hard snare on 3, rolling triplet-feel hihats
// ========================================================================
{{
  { 32, { 1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0,.5, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0 }},
  { 32, { 0, 0, 0, 0, 0, 0,.3,.5,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0,.3,.5,  0, 0, 0, 0, 0, 0,.3,.5 }},
  { 32, { 1, 0, 1, 1, 0, 1, 1, 0,  1, 1, 0, 1, 1, 0, 1, 1,
          0, 1, 1, 0, 1, 1, 0, 1,  1, 0, 1, 1, 0, 1, 1, 0 }},
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERN 7: Jungle Roller
// 32 steps | 16th note clock | 2 bars 4/4
// Fast rolling breakbeat, 160-170 BPM feel
// ========================================================================
{{
  { 32, { 1, 0, 0, 0, 0, 0, 1, 0,  0, 0, 1, 0, 0, 1, 0, 0,
          1, 0, 0, 0, 0, 0, 0, 1,  0, 0, 1, 0, 1, 0, 0, 0 }},
  { 32, { 0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 0, 0, 1, 0 }},
  { 32, { 0, 0, 0,.5, 0,.3, 0, 0,  0,.3, 0, 0, 0, 0,.3,.5,
          0, 0,.3, 0, 0,.3, 0, 0, .5, 0, 0,.3, 0, 0, 0, 0 }},
  { 32, { 1, 1, 1, 0, 1, 1, 0, 1,  1, 0, 1, 1, 1, 0, 1, 0,
          1, 1, 0, 1, 1, 0, 1, 1,  1, 1, 0, 1, 0, 1, 1, 1 }},
  { 32, { 0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 8: IDM Polyrhythmic (Venetian Snares / Squarepusher)
// 32 steps | 16th note clock | 2 bars 4/4
// Multiple metric cycles layered: 7-cycle kick, 5-cycle snare, 3-cycle hat
// ========================================================================
{{
  { 32, { 1, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 1, 0,
          0, 0, 0, 0, 0, 1, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  { 32, { 0, 0, 0, 0, 0, 1, 0, 0,  0, 0, 1, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 1, 0, 0, 0,  0, 1, 0, 0, 0, 0, 1, 0 }},
  { 32, { 0, 0,.4, 0, 0, 0,.4, 0,  0,.4, 0, 0, 0,.4, 0, 0,
         .4, 0, 0, 0, 0, 0,.4, 0,  0, 0,.4, 0, 0, 0, 0,.4 }},
  { 32, { 1, 0, 0, 1, 0, 0, 1, 0,  0, 1, 0, 0, 1, 0, 0, 1,
          0, 0, 1, 0, 0, 1, 0, 0,  1, 0, 0, 1, 0, 0, 1, 0 }},
  { 32, { 0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 1, 0, 0, 0, 0,
          0, 0, 0, 1, 0, 0, 0, 0,  0, 0, 0, 0, 0, 1, 0, 0 }},
}},

// ========================================================================
// PATTERN 9: Breakcore Distorted (Machine Girl / Igorrr)
// 32 steps | 16th note clock | 2 bars 4/4
// Extreme density, blast beats, chaotic energy
// ========================================================================
{{
  { 32, { 1, 0, 1, 0, 1, 0, 1, 0,  0, 0, 0, 0, 1, 1, 1, 1,
          0, 0, 1, 0, 0, 1, 0, 1,  1, 0, 0, 1, 0, 1, 0, 0 }},
  { 32, { 0, 1, 0, 1, 0, 1, 0, 1,  0, 0, 0, 0, 0, 0, 0, 0,
          1, 1, 0, 0, 1, 0, 1, 0,  0, 1, 1, 0, 1, 0, 0, 1 }},
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0, .7,.7,.7,.7, 0, 0, 0, 0,
          0, 0, 0,.5, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0,.5, 0 }},
  { 32, { 0, 0, 1, 0, 0, 0, 0, 0,  1, 1, 1, 1, 0, 0, 1, 0,
          0, 0, 0, 1, 0, 1, 0, 0,  1, 0, 0, 0, 0, 0, 1, 1 }},
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 10: Jungle Ghost Amen
// 64 steps | 32nd note clock | 2 bars 4/4
// High-resolution amen with 32nd-note ghost snares between 16ths.
// Each beat = 8 steps. 16ths on even steps. Ghost detail on odd steps.
// This is the pattern that justifies 64-step resolution.
// ========================================================================
{{
  // Kick: amen-derived with a ratchet double on beat 3 and anticipation hits
  { 64, { 1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          1, 0, 0,.4, 0, 0, 0, 0,  1, 0, 1, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0,.3, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare1: backbeat on 2 and 4, bar 2 has displaced snare on beat 6
  { 64, { 0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare2: 32nd-note ghost notes — the key high-res feature
  // Placed at odd positions (between 16th notes) for detailed jungle feel
  { 64, { 0, 0, 0,.3, 0, 0, 0, 0,  0, 0, 0,.2, 0,.3, 0, 0,
          0, 0, 0, 0, 0,.3, 0,.2,  0, 0, 0,.3, 0, 0, 0, 0,
          0,.2, 0, 0, 0,.3, 0, 0,  0, 0, 0, 0, 0,.2, 0,.3,
          0, 0, 0,.3, 0, 0,.2, 0,  0, 0, 0,.3, 0,.5,.3,.5 }},
  // Closed HiHat: 8th notes on every 4th step, ghost 32nds between
  { 64, { 1, 0,.3, 0, 1, 0,.3, 0,  1, 0,.3, 0, 1, 0,.3, 0,
          1, 0,.3, 0, 1, 0,.3, 0,  1, 0,.3, 0, 1, 0,.3, 0,
          1, 0,.3, 0, 1, 0,.3, 0,  1, 0,.3, 0, 1, 0,.3, 0,
          1, 0,.3, 0, 1, 0,.3, 0,  1, 0,.3, 0, 1, 0,.3, 0 }},
  // Open HiHat: accents before snare hits, crash at bar end
  { 64, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 1, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 1, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 1, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERN 11: Jungle Steppers
// 32 steps | 16th note clock | 2 bars 4/4
// Dub-influenced jungle. Half-time snare (beat 3), deep reggae bass,
// rimshot ghosts, and open hat on the "&" of 2.
// ========================================================================
{{
  // Kick: dub bass — root on 1, syncopated pushes
  { 32, { 1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 1, 0,  0, 0, 0, 0, 0, 0, 1, 0 }},
  // Snare1: half-time — snare only on beat 3 of each bar (steps 8, 24)
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare2: rimshot ghost pattern — offbeat skank
  { 32, { 0, 0, 0,.3, 0, 0,.3, 0,  0, 0, 0,.3, 0, 0,.3, 0,
          0,.3, 0, 0, 0,.3, 0, 0,  0, 0,.3, 0, 0,.3, 0,.3 }},
  // Closed HiHat: steady 8ths
  { 32, { 1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0,
          1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0 }},
  // Open HiHat: reggae-style on the "&" of beat 2 and 4
  { 32, { 0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 12: 6/8 Afrobeat (Tony Allen inspired)
// 24 steps | 16th-note-equivalent clock in 12/8 | 1 bar 12/8
// Compound time. 4 dotted-quarter beats, each = 6 steps.
// 8th notes on even steps: 0,2,4,6,8,10,12,14,16,18,20,22
// Classic West African bell pattern on closed hat.
// ========================================================================
{{
  // Kick: beat 1, the "a" of beat 2, and beat 3
  // Tony Allen's displaced kick creates the groove
  { 24, { 1, 0, 0, 0, 0, 0,  0, 0, 0, 0, 1, 0,
          1, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0 }},
  // Snare1: beats 2 and 4 (steps 6, 18) — the backbeat in 12/8
  { 24, { 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0 }},
  // Snare2: ghost notes on off-8ths (odd steps) — fills the compound feel
  { 24, { 0,.3, 0,.3, 0,.3,  0,.3, 0,.3, 0,.3,
          0,.3, 0,.3, 0,.3,  0,.3, 0,.3, 0,.3 }},
  // Closed HiHat: standard 12/8 bell pattern (X.X.XX.X.X.X)
  // Mapped to 24 steps: hits on 8th notes 1,3,5,6,8,10,12
  { 24, { 1, 0, 0, 0, 1, 0,  0, 0, 1, 0, 1, 0,
          0, 0, 1, 0, 0, 0,  1, 0, 0, 0, 1, 0 }},
  // Open HiHat: accent on the 3rd 8th of beat 1 and beat 3
  { 24, { 0, 0, 0, 0, 1, 0,  0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 1, 0,  0, 0, 0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 13: 6/8 Compound Jungle
// 24 steps | 16th-note-equivalent in 12/8 | 1 bar 12/8
// Compound time meets jungle energy. Syncopated kicks against
// compound subdivision. Broken hats create urgency.
// ========================================================================
{{
  // Kick: syncopated within compound time — pushes against the 3-feel
  { 24, { 1, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 1,
          0, 0, 1, 0, 0, 0,  0, 0, 1, 0, 0, 0 }},
  // Snare1: on beat 2 and displaced beat 4 (step 19 instead of 18)
  { 24, { 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0,  0, 1, 0, 0, 0, 0 }},
  // Snare2: ghost rolls building toward snare hits
  { 24, { 0, 0, 0, 0,.5,.3,  0, 0, 0, 0,.3, 0,
          0, 0, 0, 0,.3,.5,  0, 0, 0, 0,.3,.5 }},
  // Closed HiHat: broken compound 8ths — gaps create the jungle skip
  { 24, { 1, 0, 1, 0, 0, 0,  1, 0, 1, 0, 0, 0,
          1, 0, 0, 0, 1, 0,  1, 0, 0, 0, 1, 0 }},
  // Open HiHat: stabs on off-beats for energy
  { 24, { 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERN 14: 7/8 Aksak (2+2+3 grouping)
// 28 steps | 32nd note clock | 1 bar 7/8
// Bulgarian/Aksak rhythm. 7 eighth notes grouped as 2+2+3.
// 4 steps per 8th note = 28 steps total.
// Group 1 (2 eighths): steps 0-7
// Group 2 (2 eighths): steps 8-15
// Group 3 (3 eighths): steps 16-27
// Musical emphasis on group boundaries creates the limping feel.
// ========================================================================
{{
  // Kick: mark each group start — the 2+2+3 skeleton
  { 28, { 1, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0 }},
  // Snare1: on the 3-group's second 8th (step 20) — the "long" beat accent
  { 28, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0 }},
  // Snare2: ghost notes that outline the 2+2+3 subdivision
  { 28, { 0, 0, 0,.3, 0, 0, 0, 0,  0, 0, 0,.3, 0, 0, 0, 0,
          0, 0, 0,.3, 0, 0, 0,.3,  0, 0,.3, 0 }},
  // Closed HiHat: 8th notes (every 4 steps) with ghost 16ths
  { 28, { 1, 0,.3, 0, 1, 0,.3, 0,  1, 0,.3, 0, 1, 0,.3, 0,
          1, 0,.3, 0, 1, 0,.3, 0,  1, 0,.3, 0 }},
  // Open HiHat: breathe at group transitions
  { 28, { 0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERN 15: 7/4 Amen in Seven (Venetian Snares inspired)
// 28 steps | 16th note clock | 1 bar 7/4
// The amen break remapped into 7/4 time. 2+2+3 grouping.
// 7 beats × 4 sixteenths = 28 steps.
// Beats: 1(0-3) 2(4-7) 3(8-11) 4(12-15) 5(16-19) 6(20-23) 7(24-27)
// Groups: [1,2] + [3,4] + [5,6,7]
// ========================================================================
{{
  // Kick: amen kick pattern mapped across 7 beats
  { 28, { 1, 0, 0, 0, 0, 0, 0, 0,  1, 0, 1, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 1, 0,  0, 0, 0, 0 }},
  // Snare1: amen snare mapped — hits on beats 2, 4, displaced on 6, and 7
  { 28, { 0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 1,
          0, 0, 0, 0, 1, 0, 0, 0,  0, 1, 0, 1 }},
  // Snare2: ghost notes filling the gaps — jungle texture
  { 28, { 0, 0,.3, 0, 0, 0,.3, 0,  0, 0, 0, 0, 0, 0,.3, 0,
          0, 0,.3, 0, 0, 0, 0, 0, .3, 0, 0, 0 }},
  // Closed HiHat: 8th notes on beats
  { 28, { 1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0,
          1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0 }},
  // Open HiHat: accent marking the 3-group transition
  { 28, { 0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 16: Polymeter 4 vs 3
// Per-voice variable step counts — voices phase against each other.
// Kick: 16 steps (4-feel)
// Snare1: 12 steps (3-feel)
// Snare2: 24 steps (fills the 4:3 space)
// CHat: 8 steps (fast ostinato)
// OHat: 6 steps (2-beat compound accent)
// LCM = 24 — all voices realign every 24 clock ticks.
// Creates a rich polymetric texture from simple individual parts.
// ========================================================================
{{
  // Kick: 16-step pattern — solid 4-based groove
  { 16, { 1, 0, 0, 0, 1, 0, 0,.5,  0, 0, 1, 0, 0, 0, 0, 0 }},
  // Snare1: 12-step pattern — 3-based cycle against the kick
  { 12, { 0, 0, 0, 1, 0, 0,  0, 0, 0, 1, 0, 0 }},
  // Snare2: 24-step ghost fills bridging the 4:3 space
  { 24, { 0,.3, 0, 0, 0,.3,  0, 0,.3, 0, 0, 0,
          0,.3, 0, 0, 0,.3,  0, 0,.3, 0, 0, 0 }},
  // Closed HiHat: 8-step fast ostinato — rapid and tight
  {  8, { 1, 0, 1, 0, 1, 0, 1, 0 }},
  // Open HiHat: 6-step compound accent — slow phase drift
  {  6, { 0, 0, 1, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 17: Polymeter 5 vs 4 vs 3
// Per-voice variable step counts — three-way phase shifting.
// Kick: 20 steps (5-feel, groups of 4)
// Snare1: 16 steps (4-feel)
// Snare2: 12 steps (3-feel ghost texture)
// CHat: 15 steps (5×3 compound)
// OHat: 10 steps (5×2 accent)
// LCM = 240 — essentially never repeats the same way.
// Each voice is simple alone, but together they create a slowly
// evolving generative texture that stays musical.
// ========================================================================
{{
  // Kick: 20-step pattern — 5 groups of 4 with syncopation
  { 20, { 1, 0, 0, 0, 1, 0, 0,.5,  0, 0, 1, 0, 0, 0, 0, 1,
          0, 0, 0, 0 }},
  // Snare1: 16-step — clean backbeat in 4
  { 16, { 0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  // Snare2: 12-step — 3-feel ghost texture
  { 12, { 0, 0,.3, 0, 0,.3,  0, 0,.3, 0, 0,.3 }},
  // Closed HiHat: 15-step — 5×3 compound grouping
  { 15, { 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0 }},
  // Open HiHat: 10-step — sparse 5×2 accent cycle
  { 10, { 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERN 18: Tresillo Jungle
// 32 steps | 16th note clock | 2 bars 4/4
// 3+3+2 Afro-Cuban tresillo applied to jungle/breakbeat context.
// The tresillo (3+3+2 = 8 8th notes) maps to: steps 0, 6, 12 in 16.
// This creates a beautiful tension between the asymmetric tresillo
// grouping and the straight-time hats and snare backbeat.
// ========================================================================
{{
  // Kick: tresillo rhythm — 3+3+2 grouping, varied in bar 2
  { 32, { 1, 0, 0, 0, 0, 0, 1, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 1, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  // Snare1: straight backbeat on 2 and 4 — fights the tresillo beautifully
  { 32, { 0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  // Snare2: ghost notes weaving between tresillo and backbeat
  { 32, { 0, 0,.3, 0, 0, 0, 0,.3,  0,.3, 0, 0, 0,.3, 0,.3,
          0, 0,.3, 0, 0, 0, 0,.3,  0,.3, 0, 0, 0,.3, 0,.3 }},
  // Closed HiHat: tresillo 3+3+2 hat pattern (8 steps, repeats 4 times)
  {  8, { 1, 0, 0, 1, 0, 0, 1, 0 }},
  // Open HiHat: accent on the last 8th before the loop
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 1, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 1, 0 }},
}},

// ========================================================================
// PATTERN 19: Phase Drift
// Per-voice variable step counts — gradual phase shifting.
// Kick: 32 steps (standard 4/4 anchor)
// Snare1: 30 steps (slightly short, drifts slowly)
// Snare2: 28 steps (7-based ghost texture)
// CHat: 24 steps (3/4 against 4/4)
// OHat: 20 steps (5-based accent)
// LCM = 3360 — at 120 BPM with 16th note clock, this pattern
// takes ~7 minutes before exactly repeating. Each cycle is
// subtly different as voices drift in and out of phase.
// Musical because each individual voice is simple and grounded.
// ========================================================================
{{
  // Kick: 32-step anchor — solid 4/4 with syncopation
  { 32, { 1, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 1, 0,
          0, 0, 1, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare1: 30 steps — drifts against kick by 2 steps each cycle
  { 30, { 0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 0, 0 }},
  // Snare2: 28-step ghost (7-based) — different phase drift rate
  { 28, { 0,.3, 0, 0, 0, 0,.3, 0,  0, 0, 0,.3, 0, 0, 0,.3,
          0, 0, 0, 0,.3, 0, 0, 0, .3, 0, 0, 0 }},
  // Closed HiHat: 24 steps — 3/4 feel against 4/4 kick
  { 24, { 1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0,
          1, 0, 1, 0, 1, 0, 1, 0 }},
  // Open HiHat: 20 steps — 5-based accent, slowest drift
  { 20, { 0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 0, 0, 1, 0,
          0, 0, 0, 0 }},
}},

// ========================================================================
// ========================================================================
//   PATTERNS 20-39: JUNGLE / BREAKCORE / AMEN EXPANSIONS
// ========================================================================
// ========================================================================

// ========================================================================
// PATTERN 20: Amen Double-Time Chop
// 64 steps | 32nd note resolution | 2 bars 4/4
// The amen chopped into rapid 32nd note slices. Kick and snare
// alternate in tight bursts. Ghost snares fill the gaps.
// ========================================================================
{{
  // Kick: rapid chops — double hits followed by gaps
  { 64, { 1, 1, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          1, 0, 1, 0, 0, 0, 0, 0,  1, 1, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 1, 1, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 1, 1,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare1: chopped snare bursts interleaved with kick
  { 64, { 0, 0, 0, 0, 1, 1, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 1, 1,  0, 0, 0, 0, 0, 0, 0, 0,
          1, 0, 1, 0, 0, 0, 0, 0,  0, 0, 1, 1, 0, 0, 0, 0,
          0, 0, 0, 0, 1, 1, 0, 0,  1, 0, 0, 0, 1, 1, 0, 0 }},
  // Snare2: 32nd note ghost fills between the chops
  { 64, { 0, 0, 0,.3, 0, 0, 0,.2,  0, 0,.3, 0, 0,.2, 0,.3,
          0, 0, 0, 0, 0,.3, 0, 0,  0, 0, 0,.2, 0,.3, 0,.2,
          0, 0, 0,.3, 0, 0, 0,.2,  0,.3, 0, 0, 0, 0,.3, 0,
          0,.2, 0,.3, 0, 0, 0, 0,  0, 0,.3, 0, 0, 0, 0,.3 }},
  // Closed HiHat: rapid 32nd hats with gaps matching chop structure
  { 64, { 0, 0, 0, 0, 0, 0, 1, 1,  0, 0, 1, 1, 1, 1, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 1, 1, 1, 0, 0,
          0, 0, 0, 0, 0, 0, 1, 1,  0, 0, 0, 0, 1, 1, 1, 1,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 1, 0, 0, 1, 1 }},
  // Open HiHat: crash on bar 2 downbeat and end
  { 64, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERN 21: Liquid Jungle
// 32 steps | 16th note clock | 2 bars 4/4
// Smooth rolling DnB. Less aggressive, more flowing.
// ========================================================================
{{
  // Kick: classic liquid — beat 1 and pushed beat 4
  { 32, { 1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 1, 0,
          1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 1, 0, 0 }},
  // Snare1: half-time backbeat on beat 3
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare2: gentle ghost pattern — the "liquid" polish
  { 32, { 0, 0,.2, 0, 0, 0,.2, 0,  0, 0,.2, 0, 0,.2, 0, 0,
          0, 0,.2, 0, 0,.2, 0, 0,  0, 0,.2, 0, 0, 0,.2, 0 }},
  // Closed HiHat: steady 16ths, slightly accented 8ths
  { 32, { 1,.7, 1,.7, 1,.7, 1,.7,  1,.7, 1,.7, 1,.7, 1,.7,
          1,.7, 1,.7, 1,.7, 1,.7,  1,.7, 1,.7, 1,.7, 1,.7 }},
  // Open HiHat: smooth ride-like accent
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERN 22: Darkside Jungle
// 32 steps | 16th note clock | 2 bars 4/4
// Photek / Source Direct style. Minimal, menacing, space is the weapon.
// ========================================================================
{{
  // Kick: minimal — only 3 hits, maximum weight
  { 32, { 1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 1, 0 }},
  // Snare1: the classic dark half-time crack
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare2: tense ghost notes — crescendo into the snare
  { 32, { 0, 0, 0, 0, 0,.2, 0,.3,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0,.2,.3,.4,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Closed HiHat: sparse, broken — gaps create tension
  { 32, { 0, 0, 1, 0, 0, 0, 1, 0,  0, 0, 1, 0, 0, 1, 0, 0,
          0, 0, 1, 0, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 0, 1 }},
  // Open HiHat: dark wash
  { 32, { 0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 23: Amen Backwards
// 32 steps | 16th note clock | 2 bars 4/4
// Bar order reversed — bar 2 plays first, bar 1 second.
// ========================================================================
{{
  // Kick: bar2 then bar1 of classic amen
  { 32, { 1, 0, 0, 1, 0, 0, 0, 0,  0, 1, 0, 1, 0, 0, 0, 0,
          1, 0, 0, 1, 0, 0, 0, 0,  1, 1, 0, 0, 0, 0, 0, 0 }},
  // Snare1: reversed bar order
  { 32, { 0, 0, 0, 0, 0, 1, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  // Snare2: ghost fills building backwards
  { 32, { 0, 0,.5, 0, 0,.5, 0,.5,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0,.3, 0,  0, 0,.3, 0, 0, 0,.3, 0 }},
  // Closed HiHat: 8ths
  { 32, { 1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0,
          1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0 }},
  // Open HiHat: displaced accent
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 1, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 24: Breakcore Snare Rush
// 64 steps | 32nd note resolution | 2 bars 4/4
// Accelerating snare roll: sparse → 8th → 16th → 32nd fill.
// ========================================================================
{{
  // Kick: anchoring — sparse, letting the snares take over
  { 64, { 1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare1: accelerating — 8th > 16th > 32nd
  { 64, { 0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 1, 0, 0, 0,  1, 0, 0, 0, 1, 0, 0, 0,
          1, 0, 1, 0, 1, 0, 1, 0,  1, 1, 1, 1, 1, 1, 1, 1 }},
  // Snare2: ghost texture under the build
  { 64, { 0, 0, 0,.3, 0, 0, 0, 0,  0, 0, 0,.3, 0, 0, 0, 0,
          0, 0,.3, 0, 0, 0,.3, 0,  0, 0,.3, 0, 0,.3, 0, 0,
         .3, 0,.3, 0, 0, 0,.3, 0, .3, 0,.3, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Closed HiHat: drops out as snare builds
  { 64, { 1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0,
          1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0,
          1, 0, 1, 0, 0, 0, 1, 0,  0, 0, 1, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Open HiHat: crash at the climax
  { 64, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERN 25: Ragga Jungle
// 32 steps | 16th note clock | 2 bars 4/4
// Dancehall-influenced jungle. Offbeat skank, Congo Natty style.
// ========================================================================
{{
  // Kick: reggae-derived — offbeat emphasis
  { 32, { 1, 0, 0, 0, 0, 0, 1, 0,  0, 0, 0, 1, 0, 0, 0, 0,
          0, 0, 1, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 1, 0, 0 }},
  // Snare1: half-time on beat 3
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare2: rimshot offbeat skank — the dancehall DNA
  { 32, { 0, 0, 0, 1, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 1, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 1 }},
  // Closed HiHat: rapid offbeat 16ths
  { 32, { 0, 1, 0, 1, 0, 1, 0, 0,  0, 1, 0, 1, 0, 1, 0, 0,
          0, 1, 0, 1, 0, 1, 0, 0,  0, 1, 0, 1, 0, 1, 0, 0 }},
  // Open HiHat: offbeat accent
  { 32, { 0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 26: Amen Halftime Tearout
// 32 steps | 16th note clock | 2 bars 4/4
// Neurofunk/tearout. Ultra-sparse, maximum weight.
// ========================================================================
{{
  // Kick: massive hits — beat 1 and a pushed anticipation
  { 32, { 1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  // Snare1: one devastating hit on beat 3 of bar 2
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare2: tense build ghosts in bar 2
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0,.2, 0,.3, 0, .4, 0, 0, 0, 0,.3,.4,.5 }},
  // Closed HiHat: sparse rhythmic texture
  { 32, { 0, 0, 1, 0, 0, 0, 0, 0,  1, 0, 0, 1, 0, 0, 0, 0,
          0, 0, 1, 0, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 0, 0 }},
  // Open HiHat: tension wash in bar 1
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 27: Breakcore Call & Response
// 32 steps | 16th note clock | 2 bars 4/4
// Bar 1 = kick-heavy question, bar 2 = snare-heavy answer.
// ========================================================================
{{
  // Kick: aggressive in bar 1, silent in bar 2
  { 32, { 1, 0, 1, 0, 0, 1, 1, 0,  0, 0, 1, 0, 1, 1, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare1: silent in bar 1, aggressive in bar 2
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 1, 0, 1, 1, 0, 0, 1,  1, 0, 1, 0, 0, 1, 1, 0 }},
  // Snare2: ghost fill bridging the bars
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0,.5,.7,
         .7,.5, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Closed HiHat: follows whoever is active
  { 32, { 0, 1, 0, 1, 1, 0, 0, 1,  1, 1, 0, 1, 0, 0, 1, 0,
          1, 0, 1, 0, 0, 1, 1, 0,  0, 1, 0, 1, 1, 0, 0, 1 }},
  // Open HiHat: marking the transition
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1,
          1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERN 28: Jungle Skip (Skippy Drums)
// 32 steps | 16th note clock | 2 bars 4/4
// Classic old-school jungle. Offbeats louder than downbeats.
// ========================================================================
{{
  // Kick: amen-derived with skippy displacement
  { 32, { 1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 1, 0,  0, 0, 0, 0, 0, 0, 1, 0 }},
  // Snare1: standard backbeat
  { 32, { 0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  // Snare2: ghost notes leading into snares
  { 32, { 0, 0, 0,.3, 0, 0, 0, 0,  0, 0, 0,.3, 0, 0, 0, 0,
          0, 0, 0,.3, 0, 0, 0, 0,  0,.3, 0,.4, 0, 0, 0, 0 }},
  // Closed HiHat: the SKIPPY pattern — offbeats louder
  { 32, {.5, 0, 1, 0,.5, 0, 1, 0, .5, 0, 1, 0,.5, 0, 1, 0,
         .5, 0, 1, 0,.5, 0, 1, 0, .5, 0, 1, 0,.5, 0, 1, 0 }},
  // Open HiHat: occasional lift
  { 32, { 0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERN 29: Amen Triplet Swing
// 48 steps | Triplet 8th clock | 2 bars 4/4 in 12/8 feel
// The amen reinterpreted in compound time. Each beat = 6 subs.
// ========================================================================
{{
  // Kick: amen kick mapped to triplets
        { 48, { 1, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0,
                                        1, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0,
                                        1, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 1,
                                        0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 0, 0 }},
  // Snare1: beats 2, 4, 6, 8 in compound time
  { 48, { 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0 }},
  // Snare2: triplet ghost shuffle
  { 48, { 0, 0,.3, 0, 0,.2,  0, 0,.3, 0, 0,.2,
          0, 0,.3, 0, 0,.2,  0, 0,.3, 0, 0,.2,
          0, 0,.3, 0, 0,.2,  0, 0,.3, 0, 0,.2,
          0, 0,.3, 0, 0,.2,  0, 0,.3, 0, 0,.5 }},
  // Closed HiHat: compound triplet 8ths
  { 48, { 1, 0, 0, 1, 0, 0,  1, 0, 0, 1, 0, 0,
          1, 0, 0, 1, 0, 0,  1, 0, 0, 1, 0, 0,
          1, 0, 0, 1, 0, 0,  1, 0, 0, 1, 0, 0,
          1, 0, 0, 1, 0, 0,  1, 0, 0, 1, 0, 0 }},
  // Open HiHat: lift on last triplet of bar
  { 48, { 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERN 30: Breakcore Stuttercore
// 64 steps | 32nd note resolution | 2 bars 4/4
// 4-step micro-cells that shift. Think Bong-Ra, Dev/Null.
// ========================================================================
{{
  // Kick: stutter cells — repeating micro-patterns
  { 64, { 1, 0, 1, 0, 1, 0, 1, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          1, 1, 0, 0, 1, 1, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 1, 0, 0, 1,  0, 0, 1, 0, 0, 1, 0, 0,
          1, 1, 1, 0, 0, 0, 0, 0,  1, 0, 1, 0, 1, 0, 1, 0 }},
  // Snare1: complementary stutter
  { 64, { 0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 1, 0, 1, 0, 1, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 1, 0, 0, 1, 1, 0, 0,
          1, 1, 1, 1, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 1, 1, 1, 1,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare2: noise bursts
  { 64, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0,.5,.5, 0, 0,.5,.5,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0, .5,.5,.5,.5,.5,.5,.5,.5,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Closed HiHat: stutter complement
  { 64, { 0, 1, 0, 1, 0, 1, 0, 1,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 1, 1, 0, 0, 1, 1,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 1, 1, 0,  1, 1, 0, 1, 1, 0, 1, 1,
          0, 0, 0, 1, 0, 0, 0, 1,  0, 1, 0, 1, 0, 1, 0, 1 }},
  // Open HiHat: stabs between cells
  { 64, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERN 31: Jungle Two-Step
// 32 steps | 16th note clock | 2 bars 4/4
// 2-step garage meets jungle tempo. Kick on 1 and "&" of 2.
// ========================================================================
{{
  // Kick: 2-step pattern
  { 32, { 1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 0, 0 }},
  // Snare1: beats 2 and 4
  { 32, { 0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  // Snare2: shuffle ghosts
  { 32, { 0, 0, 0,.3, 0, 0,.3, 0,  0, 0, 0, 0, 0, 0,.3, 0,
          0, 0, 0,.3, 0, 0,.3, 0,  0, 0, 0, 0, 0, 0,.3, 0 }},
  // Closed HiHat: swung 8ths
  { 32, { 1, 0,.8, 0, 1, 0,.8, 0,  1, 0,.8, 0, 1, 0,.8, 0,
          1, 0,.8, 0, 1, 0,.8, 0,  1, 0,.8, 0, 1, 0,.8, 0 }},
  // Open HiHat: "&" of 4 for lift
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 1, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 1, 0 }},
}},

// ========================================================================
// PATTERN 32: Amen Reverse Snare
// 64 steps | 32nd note resolution | 2 bars 4/4
// Ghost crescendo INTO snare hit simulating reversed snare sample.
// ========================================================================
{{
  // Kick: clean amen kicks
  { 64, { 1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 0, 0,  1, 0, 1, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare1: clean hits on the backbeat
  { 64, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1 }},
  // Snare2: REVERSE SNARE — crescendo into each snare hit
  { 64, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0,.15,.2,.3,.5,.7,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0,.15,.2,.3,.5,.7,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0,.15,.2,.3,.5,.7,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0,.15,.2,.3,.5,.7 }},
  // Closed HiHat: steady 8ths (every 4 steps)
  { 64, { 1, 0, 0, 0, 1, 0, 0, 0,  1, 0, 0, 0, 1, 0, 0, 0,
          1, 0, 0, 0, 1, 0, 0, 0,  1, 0, 0, 0, 1, 0, 0, 0,
          1, 0, 0, 0, 1, 0, 0, 0,  1, 0, 0, 0, 1, 0, 0, 0,
          1, 0, 0, 0, 1, 0, 0, 0,  1, 0, 0, 0, 1, 0, 0, 0 }},
  // Open HiHat: accent before snare creates tension
  { 64, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 33: Breakcore Blast Beat
// 32 steps | 16th note clock | 2 bars 4/4
// Metal blast with sudden gap/break in bar 1.
// ========================================================================
{{
  // Kick: blast — every other 16th, sudden 2-beat gap
  { 32, { 1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 0, 0, 0, 0,
          1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0 }},
  // Snare1: interleaved blast
  { 32, { 0, 1, 0, 1, 0, 1, 0, 1,  0, 1, 0, 1, 0, 0, 0, 0,
          0, 1, 0, 1, 0, 1, 0, 1,  0, 1, 0, 1, 0, 1, 0, 1 }},
  // Snare2: fill in the gap with a roll
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0,.5,.5,.7, 1,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Closed HiHat: sparse — only in the gap
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 1, 1, 1, 1,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Open HiHat: crash marking the blast restart
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 34: Amen Octave Displacement
// 32 steps | 16th note clock | 2 bars 4/4
// Bar 1 standard amen, bar 2 same pattern shifted by 2 steps.
// ========================================================================
{{
  // Kick: standard bar 1, shifted bar 2
  { 32, { 1, 0, 0, 1, 0, 0, 0, 0,  1, 1, 0, 0, 0, 0, 0, 0,
          0, 0, 1, 0, 0, 1, 0, 0,  0, 0, 1, 1, 0, 0, 0, 0 }},
  // Snare1: bar 1 backbeat, bar 2 shifted
  { 32, { 0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 1, 0,  0, 0, 0, 0, 0, 0, 1, 0 }},
  // Snare2: ghosts filling the displaced gap
  { 32, { 0, 0,.3, 0, 0, 0,.3, 0,  0, 0, 0, 0, 0, 0,.3, 0,
          0,.3, 0, 0, 0, 0, 0,.3,  0, 0, 0, 0, 0, 0, 0,.3 }},
  // Closed HiHat: 8ths both bars
  { 32, { 1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0,
          1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0 }},
  // Open HiHat: bar transition accent
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 35: Jungle Footwork
// 32 steps | 16th note clock | 2 bars 4/4
// Chicago footwork meets jungle. Syncopated interlocking kicks.
// ========================================================================
{{
  // Kick: footwork-style rapid syncopated hits
  { 32, { 1, 0, 0, 1, 0, 0, 1, 0,  0, 0, 0, 0, 1, 0, 0, 1,
          0, 0, 1, 0, 0, 1, 0, 0,  1, 0, 0, 0, 0, 0, 1, 0 }},
  // Snare1: clap on 2 and 4
  { 32, { 0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare2: machine-gun ghost rolls
  { 32, { 0, 0, 0, 0, 0,.3,.3,.3,  0, 0, 0, 0, 0, 0,.3,.3,
          0, 0, 0, 0, 0,.3,.3, 0,  0, 0, 0,.3,.3,.3, 0, 0 }},
  // Closed HiHat: open-close pattern
  { 32, { 1, 1, 0, 0, 1, 1, 0, 0,  1, 1, 0, 0, 1, 1, 0, 0,
          1, 1, 0, 0, 1, 1, 0, 0,  1, 1, 0, 0, 1, 1, 0, 0 }},
  // Open HiHat: where closed drops out
  { 32, { 0, 0, 1, 0, 0, 0, 1, 0,  0, 0, 1, 0, 0, 0, 1, 0,
          0, 0, 1, 0, 0, 0, 1, 0,  0, 0, 1, 0, 0, 0, 1, 0 }},
}},

// ========================================================================
// PATTERN 36: Amen Polymetric Jungle
// Per-voice variable step counts | polymetric
// Kick: 32 (amen anchor), Snare1: 28 (7-based drift),
// Snare2: 24 (triplet ghost), CHat: 32, OHat: 20 (5-based)
// ========================================================================
{{
  // Kick: classic amen — 32 steps (anchor)
  { 32, { 1, 0, 0, 1, 0, 0, 0, 0,  1, 1, 0, 0, 0, 0, 0, 0,
          1, 0, 0, 1, 0, 0, 0, 0,  1, 1, 0, 0, 0, 0, 0, 0 }},
  // Snare1: 28-step — drifts against 32-step kick
  { 28, { 0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0 }},
  // Snare2: 24-step triplet ghost texture
  { 24, { 0, 0,.3, 0, 0,.3, 0, 0, .3, 0, 0,.3,
          0, 0,.3, 0, 0,.3, 0, 0, .3, 0, 0,.3 }},
  // Closed HiHat: standard 8ths
  { 32, { 1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0,
          1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0 }},
  // Open HiHat: 20-step 5-based accent drift
  { 20, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 37: Breakcore Ratchet
// 64 steps | 32nd note resolution | 2 bars 4/4
// Modular-style ratcheting: 1→2→3→4 hit bursts.
// ========================================================================
{{
  // Kick: ratchets — 1 hit, 2 hits, 3 hits, 4 hits
  { 64, { 1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          1, 0, 1, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          1, 0, 1, 0, 1, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          1, 0, 1, 0, 1, 0, 1, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare1: counter-ratchet — 4,3,2,1
  { 64, { 0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 1, 0, 1, 0, 1, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 1, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 1, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare2: fill the empty space with ghosts
  { 64, { 0, 0, 0,.3, 0,.3, 0,.3,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0,.3, 0,.3,  0, 0, 0, 0, 0, 0, 0,.3,
          0, 0, 0, 0, 0, 0, 0,.3,  0, 0, 0, 0, 0,.3, 0,.3,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0,.3, 0,.3, 0,.3 }},
  // Closed HiHat: constant 16ths for grounding
  { 64, { 1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0,
          1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0,
          1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0,
          1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0 }},
  // Open HiHat: marking each ratchet group
  { 64, { 0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERN 38: Amen Stripped
// 32 steps | 16th note clock | 2 bars 4/4
// The amen reduced to its absolute essence. Maximum space.
// ========================================================================
{{
  // Kick: only the 3 essential kicks
  { 32, { 1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 1, 0, 0, 0 }},
  // Snare1: only the essential snare
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare2: the suggestion of what's missing
  { 32, { 0, 0, 0,.2, 0, 0, 0, 0,  0, 0, 0, 0,.2, 0, 0, 0,
          0, 0, 0,.2, 0, 0, 0, 0,  0, 0, 0, 0,.2, 0, 0, 0 }},
  // Closed HiHat: only downbeat 8ths
  { 32, { 1, 0, 0, 0, 1, 0, 0, 0,  1, 0, 0, 0, 1, 0, 0, 0,
          1, 0, 0, 0, 1, 0, 0, 0,  1, 0, 0, 0, 1, 0, 0, 0 }},
  // Open HiHat: single tension point
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERN 39: Amen Kitchen Sink
// 64 steps | 32nd note resolution | 2 bars 4/4
// Everything at once. Full amen with ghosts, fills, ratchets.
// ========================================================================
{{
  // Kick: full amen + 32nd anticipation hits and ratchets
  { 64, { 1, 0, 0, 0, 0, 0, 1, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 0, 0,  1, 0, 1, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 1, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 1, 0, 0, 0, 0, 0,  1, 0, 1, 0, 0, 0, 0, 0 }},
  // Snare1: amen snare with displaced bar2-beat4
  { 64, { 0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 1, 0 }},
  // Snare2: full ghost palette — crescendos, drags, flams
  { 64, { 0, 0, 0,.2, 0,.3, 0, 0,  0, 0,.2, 0,.3, 0,.2, 0,
          0,.2, 0,.3, 0,.2,.3, 0,  0, 0, 0,.3, 0,.2, 0,.3,
          0, 0,.3, 0,.2, 0, 0,.3,  0,.2, 0, 0,.3, 0,.2, 0,
          0,.3, 0,.2, 0,.3,.4,.5,  0, 0, 0,.3, 0,.5,.3,.5 }},
  // Closed HiHat: 8ths with ghost 16ths and 32nd detail
  { 64, { 1, 0,.2, 0, 1, 0,.2, 0,  1, 0,.2, 0, 1, 0,.3, 0,
          1, 0,.2, 0, 1, 0,.2, 0,  1, 0,.3, 0, 1, 0,.3, 0,
          1, 0,.2, 0, 1, 0,.2, 0,  1, 0,.2, 0, 1, 0,.3, 0,
          1, 0,.3, 0, 1, 0,.3, 0,  1, 0,.3, 0, 1, 0,.2, 0 }},
  // Open HiHat: crash bar2 downbeat, tension accents
  { 64, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 1, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 1, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERNS 40-44: DRUM & BASS / JUNGLE
// ========================================================================

// ========================================================================
// PATTERN 40: Classic DnB Two-Step
// 32 steps | 16th note clock | 2 bars 4/4 @ 170+ BPM
// Foundation DnB groove: kick on 1, displaced kick, snare on 2&4
// Ghost snares drive the shuffle. Hats provide ride-like continuity.
// ========================================================================
{{
  // Kick: beat 1 anchor + syncopated displacement
  { 32, { 1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 1, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare1: strong backbeat on beats 2 and 4
  { 32, { 0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  // Snare2: ghost notes around backbeat
  { 32, { 0, 0, 0,.3, 0, 0,.2, 0,  0,.3, 0, 0, 0, 0,.2, 0,
          0, 0,.3, 0, 0, 0, 0,.2,  0,.3, 0, 0, 0, 0,.3, 0 }},
  // Closed HiHat: driving 8ths with ghost 16ths
  { 32, { 1,.3, 1,.3, 1,.3, 1,.3,  1,.3, 1,.3, 1,.3, 1,.3,
          1,.3, 1,.3, 1,.3, 1,.3,  1,.3, 1,.3, 1,.3, 1,.3 }},
  // Open HiHat: occasional accent
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 41: Jungle Chopper
// 64 steps | 32nd note resolution | 2 bars 4/4 @ 160-170 BPM
// Chopped amen-style jungle with 32nd hat bursts and kick syncopation
// ========================================================================
{{
  // Kick: syncopated around the snare anchors
  { 64, { 1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 1, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare1: strong backbeat anchors
  { 64, { 0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare2: ghost drags and flams
  { 64, { 0, 0, 0,.2, 0, 0,.3, 0,  0, 0, 0, 0,.3, 0,.2, 0,
          0,.2, 0, 0, 0,.3, 0, 0,  0, 0,.2, 0, 0, 0, 0,.3,
          0, 0,.3, 0, 0, 0, 0,.2,  0, 0, 0,.3, 0, 0,.2, 0,
          0,.3, 0, 0, 0, 0,.3, 0,  0, 0, 0,.2, 0,.3, 0,.5 }},
  // Closed HiHat: riding 8ths with 32nd bursts at bar ends
  { 64, { 1, 0,.2, 0, 1, 0, 0, 0,  1, 0,.2, 0, 1, 0, 0, 0,
          1, 0,.2, 0, 1, 0, 0, 0,  1, 0,.2, 0, 1, 1, 1, 1,
          1, 0,.2, 0, 1, 0, 0, 0,  1, 0,.2, 0, 1, 0, 0, 0,
          1, 0, 0, 0, 1, 0, 0, 0,  1, 0, 0, 0, 1, 1, 1, 1 }},
  // Open HiHat: syncopated tension
  { 64, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 1, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 1, 0 }},
}},

// ========================================================================
// PATTERN 42: Liquid DnB Roller
// 32 steps | 16th note clock | 2 bars 4/4 @ 174 BPM
// Smooth rolling groove. Minimal kick, steady hat ride, ghost texture.
// ========================================================================
{{
  // Kick: minimal, bar 1 beat 1 + one syncopation
  { 32, { 1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 1, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare1: backbeats
  { 32, { 0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  // Snare2: subtle ghosts for texture
  { 32, { 0, 0, 0, 0, 0, 0, 0,.2,  0, 0,.2, 0, 0, 0, 0, 0,
          0, 0, 0,.2, 0, 0, 0, 0,  0, 0,.2, 0, 0, 0, 0,.2 }},
  // Closed HiHat: steady ride pattern
  { 32, { 1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0,
          1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0 }},
  // Open HiHat: bar-end accent
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 43: Darkside Jungle
// 64 steps | 32nd note resolution | 2 bars 4/4 @ 165 BPM
// Heavy sub-kick patterns, aggressive snare, menacing energy
// ========================================================================
{{
  // Kick: heavy, multiple hits per bar
  { 64, { 1, 0, 0, 0, 0, 0, 1, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 0, 0,  1, 0, 1, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 1, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  // Snare1: hard backbeats with displaced bar 2
  { 64, { 0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 1, 0 }},
  // Snare2: aggressive ghosts
  { 64, { 0, 0,.3, 0, 0,.4, 0, 0,  0, 0,.3, 0,.4, 0,.3, 0,
          0,.3, 0, 0,.3, 0, 0,.3,  0, 0, 0, 0,.3, 0, 0, 0,
          0, 0, 0,.3, 0, 0,.4, 0,  0, 0, 0,.3, 0,.4, 0, 0,
          0,.3, 0,.3, 0, 0, 0,.4,  0, 0, 0,.3, 0, 0, 0,.5 }},
  // Closed HiHat: choppy ride with bursts
  { 64, { 1, 0, 1, 0, 1, 0, 0, 0,  1, 0, 1, 0, 0, 1, 0, 0,
          1, 0, 0, 0, 1, 0, 1, 0,  1, 0, 0, 0, 1, 1, 1, 1,
          1, 0, 1, 0, 1, 0, 0, 0,  1, 0, 1, 0, 0, 1, 0, 0,
          1, 0, 1, 0, 0, 0, 1, 0,  0, 0, 1, 0, 1, 1, 1, 1 }},
  // Open HiHat: punctuation
  { 64, { 0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 44: Ragga Jungle
// 32 steps | 16th note clock | 2 bars 4/4 @ 165 BPM
// Dancehall-influenced jungle. Bouncy kick, shuffled hats, offbeat skank.
// ========================================================================
{{
  // Kick: bouncy ragga pattern
  { 32, { 1, 0, 0, 1, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 1, 0, 0, 0 }},
  // Snare1: solid backbeat
  { 32, { 0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  // Snare2: reggae-style rim shots
  { 32, { 0, 0, 0, 0, 0, 0,.5, 0,  0, 0, 0, 0, 0,.3, 0, 0,
          0, 0,.3, 0, 0, 0, 0, 0,  0,.5, 0, 0, 0, 0,.3, 0 }},
  // Closed HiHat: shuffle feel
  { 32, { 1, 0,.5, 1, 0,.5, 1, 0,  1, 0,.5, 1, 0,.5, 1, 0,
          1, 0,.5, 1, 0,.5, 1, 0,  1, 0,.5, 1, 0,.5, 1, 0 }},
  // Open HiHat: offbeat skank
  { 32, { 0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERNS 45-49: UK GARAGE
// ========================================================================

// ========================================================================
// PATTERN 45: Classic 2-Step Garage
// 32 steps | 16th note clock | 2 bars 4/4 @ 130 BPM
// Signature 2-step: kick skips beat 2, snare on 2&4, shuffled hats
// ========================================================================
{{
  // Kick: beat 1 strong, skip beat 2, displaced beat 3
  { 32, { 1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 1, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare1: crisp backbeat
  { 32, { 0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  // Snare2: ghost groove driver
  { 32, { 0, 0, 0,.3, 0, 0, 0, 0,  0, 0,.3, 0, 0, 0, 0,.3,
          0, 0, 0,.3, 0, 0, 0, 0,  0,.3, 0, 0, 0, 0, 0,.3 }},
  // Closed HiHat: shuffled, not straight
  { 32, { 1, 0,.3, 1, 0,.3, 0, 1,  0,.3, 1, 0,.3, 0, 1, 0,
          1, 0,.3, 1, 0,.3, 0, 1,  0,.3, 1, 0,.3, 0, 1, 0 }},
  // Open HiHat: sparse accent
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 1, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 46: UKG Bumpy Ride
// 32 steps | 16th note clock | 2 bars 4/4 @ 132 BPM
// Bump-heavy garage. Late kicks, ghost snare pocket.
// ========================================================================
{{
  // Kick: intentionally skipped expected positions, late placement
  { 32, { 1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 1, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 1, 0 }},
  // Snare1: backbeat
  { 32, { 0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  // Snare2: groove-driving ghosts
  { 32, { 0,.3, 0, 0, 0, 0,.2, 0,  0, 0, 0,.3, 0, 0, 0, 0,
          0, 0,.3, 0, 0, 0, 0,.2,  0, 0,.3, 0, 0, 0, 0, 0 }},
  // Closed HiHat: shuffle swing
  { 32, { 1, 0,.3, 0, 1,.3, 0, 1,  0,.3, 0, 1, 0,.3, 1, 0,
          1, 0,.3, 0, 1,.3, 0, 1,  0,.3, 0, 1, 0,.3, 1, 0 }},
  // Open HiHat: bar-end swell
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERN 47: Speed Garage
// 32 steps | 16th note clock | 2 bars 4/4 @ 135 BPM
// More four-to-floor but with garage swing and one kick skip.
// ========================================================================
{{
  // Kick: four-to-floor with one skip for garage feel
  { 32, { 1, 0, 0, 0, 1, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 1, 0, 0, 0,  1, 0, 0, 0, 1, 0, 0, 0 }},
  // Snare1: displaced snare, not always on 2&4
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  // Snare2: off-beat groove
  { 32, { 0, 0,.3, 0, 0, 0, 0,.3,  0, 0, 0, 0, 0, 0,.3, 0,
          0, 0,.3, 0, 0, 0, 0,.3,  0, 0, 0, 0, 0, 0,.3, 0 }},
  // Closed HiHat: swung 16ths
  { 32, { 1,.3, 0, 1,.3, 0, 1,.3,  1,.3, 0, 1, 0,.3, 1,.3,
          1,.3, 0, 1,.3, 0, 1,.3,  1,.3, 0, 1, 0,.3, 1,.3 }},
  // Open HiHat: upbeat accents
  { 32, { 0, 0, 0, 0, 0, 0, 1, 0,  0, 0, 0, 0, 0, 0, 1, 0,
          0, 0, 0, 0, 0, 0, 1, 0,  0, 0, 0, 0, 0, 0, 1, 0 }},
}},

// ========================================================================
// PATTERN 48: Garage Dub Plate
// 32 steps | 16th note clock | 2 bars 4/4 @ 130 BPM
// Stripped back dubplate style. Sparse kick, heavy reverb snare space.
// ========================================================================
{{
  // Kick: very sparse, only 2 hits
  { 32, { 1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare1: classic 2&4
  { 32, { 0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  // Snare2: single ghost
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0,.3, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0,.3 }},
  // Closed HiHat: sparse shuffle
  { 32, { 1, 0, 0, 1, 0, 0, 0, 1,  0, 0, 1, 0, 0, 0, 1, 0,
          1, 0, 0, 1, 0, 0, 0, 1,  0, 0, 1, 0, 0, 0, 1, 0 }},
  // Open HiHat: one accent
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERN 49: Broken Beat Garage
// 32 steps | 16th note clock | 2 bars 4/4 @ 128 BPM
// West London broken beat influence. Displaced everything.
// ========================================================================
{{
  // Kick: beat 1 bar 1 only, then syncopated
  { 32, { 1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 0, 0,
          0, 0, 0, 1, 0, 0, 0, 0,  0, 0, 0, 0, 0, 1, 0, 0 }},
  // Snare1: displaced backbeat
  { 32, { 0, 0, 0, 0, 0, 1, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 1, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  // Snare2: busy ghost pattern driving groove
  { 32, { 0,.3, 0, 0,.3, 0, 0, 0,  0,.3, 0,.3, 0, 0, 0,.3,
          0, 0,.3, 0, 0,.3, 0, 0,  0,.3, 0, 0, 0,.3, 0, 0 }},
  // Closed HiHat: broken shuffle
  { 32, { 1, 0, 1,.3, 0, 1, 0,.3,  1, 0,.3, 1, 0, 0, 1,.3,
          0, 1, 0,.3, 1, 0, 1, 0,  1,.3, 0, 1, 0,.3, 0, 1 }},
  // Open HiHat: tension accent
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 1, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERNS 50-54: DUB / DUBWISE / REGGAE
// ========================================================================

// ========================================================================
// PATTERN 50: One Drop
// 32 steps | 16th note clock | 2 bars 4/4 @ 75-80 BPM
// Classic reggae one-drop: kick+snare together on beat 3.
// Beat 1 is empty (the "drop"). Rim click on offbeats.
// ========================================================================
{{
  // Kick: only on beat 3 (the one-drop)
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare1: together with kick on beat 3 (cross-stick)
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare2: rim click on offbeats
  { 32, { 0, 0, 0,.5, 0, 0, 0,.5,  0, 0, 0,.5, 0, 0, 0,.5,
          0, 0, 0,.5, 0, 0, 0,.5,  0, 0, 0,.5, 0, 0, 0,.5 }},
  // Closed HiHat: steady 8ths
  { 32, { 1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0,
          1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0 }},
  // Open HiHat: none
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 51: Steppers
// 32 steps | 16th note clock | 2 bars 4/4 @ 75 BPM
// Four-on-the-floor reggae / dub. Kick every beat, snare on 2&4.
// Hypnotic, meditative groove for dub mixing.
// ========================================================================
{{
  // Kick: every beat (four-on-the-floor)
  { 32, { 1, 0, 0, 0, 1, 0, 0, 0,  1, 0, 0, 0, 1, 0, 0, 0,
          1, 0, 0, 0, 1, 0, 0, 0,  1, 0, 0, 0, 1, 0, 0, 0 }},
  // Snare1: 2 and 4
  { 32, { 0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  // Snare2: sparse rim
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0,.3, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0,.3, 0 }},
  // Closed HiHat: 8ths
  { 32, { 1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0,
          1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0 }},
  // Open HiHat: offbeat skank
  { 32, { 0, 0, 0, 1, 0, 0, 0, 1,  0, 0, 0, 1, 0, 0, 0, 1,
          0, 0, 0, 1, 0, 0, 0, 1,  0, 0, 0, 1, 0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERN 52: Dub Siren Drop
// 32 steps | 16th note clock | 2 bars 4/4 @ 72 BPM
// Ultra sparse. Kick on 1, snare on 3. Maximum space for dub FX.
// ========================================================================
{{
  // Kick: beat 1 only
  { 32, { 1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare1: beat 3 only
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare2: nothing
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Closed HiHat: sparse quarter notes
  { 32, { 1, 0, 0, 0, 1, 0, 0, 0,  1, 0, 0, 0, 1, 0, 0, 0,
          1, 0, 0, 0, 1, 0, 0, 0,  1, 0, 0, 0, 1, 0, 0, 0 }},
  // Open HiHat: none
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 53: Rockers Dub
// 32 steps | 16th note clock | 2 bars 4/4 @ 78 BPM
// Sly & Robbie inspired. Kick on 1 and 3, snare on 2 and 4.
// ========================================================================
{{
  // Kick: beats 1 and 3
  { 32, { 1, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare1: beats 2 and 4
  { 32, { 0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  // Snare2: occasional ghost
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0,.3, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0,.3, 0, 0, 0,.3, 0 }},
  // Closed HiHat: driving 8ths
  { 32, { 1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0,
          1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0 }},
  // Open HiHat: offbeat skank (the reggae signature)
  { 32, { 0, 0, 0, 1, 0, 0, 0, 1,  0, 0, 0, 1, 0, 0, 0, 1,
          0, 0, 0, 1, 0, 0, 0, 1,  0, 0, 0, 1, 0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERN 54: Digital Dub
// 32 steps | 16th note clock | 2 bars 4/4 @ 80 BPM
// Modern digital reggae / dancehall dub. Synth-drum aesthetic.
// ========================================================================
{{
  // Kick: beat 1 + displaced hit
  { 32, { 1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 1, 0,
          1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 0, 0 }},
  // Snare1: beat 3 (one-drop style)
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare2: digital rim pattern
  { 32, { 0, 0, 0,.4, 0, 0, 0, 0,  0, 0, 0,.4, 0, 0, 0, 0,
          0, 0, 0,.4, 0, 0, 0, 0,  0, 0, 0,.4, 0, 0, 0,.4 }},
  // Closed HiHat: 16ths with accents
  { 32, { 1,.3, 1,.3, 1,.3, 1,.3,  1,.3, 1,.3, 1,.3, 1,.3,
          1,.3, 1,.3, 1,.3, 1,.3,  1,.3, 1,.3, 1,.3, 1,.3 }},
  // Open HiHat: offbeat
  { 32, { 0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERNS 55-59: IDM / DRILLCORE
// ========================================================================

// ========================================================================
// PATTERN 55: IDM Stutter Grid
// 32 steps | 16th note clock | 2 bars 4/4
// Dense burst on bar 1, space in bar 2. Controlled chaos.
// ========================================================================
{{
  // Kick: clustered bar 1, sparse bar 2
  { 32, { 1, 0, 1, 0, 0, 0, 1, 0,  0, 1, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare1: displaced, avoids obvious positions
  { 32, { 0, 0, 0, 0, 0, 1, 0, 0,  0, 0, 0, 1, 0, 0, 0, 0,
          0, 0, 0, 1, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 1, 0 }},
  // Snare2: glitchy fills
  { 32, { 0,.3, 0, 0, 0, 0,.3,.5,  0, 0, 0, 0, 0,.3, 0,.5,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Closed HiHat: irregular density
  { 32, { 1, 0, 1, 1, 0, 0, 1, 0,  1, 0, 0, 1, 0, 1, 1, 0,
          0, 0, 1, 0, 0, 0, 0, 1,  0, 0, 1, 0, 0, 0, 1, 0 }},
  // Open HiHat: structural accent
  { 32, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 56: Drill Pattern Alpha
// 32 steps | 16th note clock | 2 bars 4/4
// UK Drill-influenced: sliding hi-hats, sparse kick, snare rolls
// ========================================================================
{{
  // Kick: sparse, heavy sub hits
  { 32, { 1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 1, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare1: triplet-feel placement with end-of-bar roll
  { 32, { 0, 0, 0, 0, 0, 0, 1, 0,  0, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 1, 0,  0, 0, 0, 0, 1, 0, 1, 1 }},
  // Snare2: rim shots
  { 32, { 0, 0, 0,.4, 0, 0, 0, 0,  0, 0,.4, 0, 0, 0, 0, 0,
          0,.4, 0, 0, 0, 0, 0,.4,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Closed HiHat: sliding rolls with gaps
  { 32, { 1, 1, 0, 1, 1, 1, 0, 0,  1, 1, 0, 1, 0, 1, 1, 0,
          1, 1, 0, 1, 1, 1, 0, 0,  1, 0, 1, 1, 0, 1, 1, 1 }},
  // Open HiHat: sustain accent
  { 32, { 0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0, 0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 57: IDM 7/8 Glitch
// 28 steps | 16th note clock | 2 bars of 7/8
// Unusual 7-beat grouping. Autechre / BOC territory.
// ========================================================================
{{
  // Kick: asymmetric placements within 7/8
  { 28, { 1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 1, 0,  0, 0, 0, 0, 0, 0 }},
  // Snare1: off-grid backbeat
  { 28, { 0, 0, 0, 0, 0, 0, 1, 0,  0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0 }},
  // Snare2: ghost texture
  { 28, { 0, 0,.3, 0, 0, 0, 0, 0,  0,.3, 0, 0, 0,.3,
          0, 0, 0, 0,.3, 0, 0, 0,  0, 0, 0,.3, 0, 0 }},
  // Closed HiHat: additive pattern
  { 28, { 1, 0, 1, 0, 0, 1, 0, 1,  0, 0, 1, 0, 1, 0,
          1, 0, 0, 1, 0, 1, 0, 0,  1, 0, 1, 0, 0, 1 }},
  // Open HiHat: sparse
  { 28, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 1, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0 }},
}},

// ========================================================================
// PATTERN 58: 9-Feel Subdivision
// 36 steps | Custom resolution | 4 bars of 9/16
// 9-beat grouping. Lopsided waltz feel. Loops cleanly.
// ========================================================================
{{
  // Kick: strong 1, displaced accents
  { 36, { 1, 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 1, 0, 0, 0,
          1, 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 0, 0, 0 }},
  // Snare1: off-center hit
  { 36, { 0, 0, 0, 0, 1, 0, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0, 0,
          0, 0, 0, 0, 1, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1, 0 }},
  // Snare2: textural ghosts
  { 36, { 0, 0,.3, 0, 0, 0, 0,.3, 0,  0, 0, 0,.3, 0, 0, 0, 0,.3,
          0,.3, 0, 0, 0, 0,.3, 0, 0,  0, 0, 0, 0,.3, 0, 0, 0, 0 }},
  // Closed HiHat: 3+3+3 grouping feel
  { 36, { 1, 0, 0, 1, 0, 0, 1, 0, 0,  1, 0, 0, 1, 0, 0, 1, 0, 0,
          1, 0, 0, 1, 0, 0, 1, 0, 0,  1, 0, 0, 1, 0, 0, 1, 0, 0 }},
  // Open HiHat: phrase marker
  { 36, { 0, 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, 1 }},
}},

// ========================================================================
// PATTERN 59: IDM Dense Scatter
// 64 steps | 32nd note resolution | 2 bars 4/4
// Sudden dense bursts followed by space. Venetian Snares energy.
// ========================================================================
{{
  // Kick: burst bar 1 beat 1, then gap, then isolated hits
  { 64, { 1, 0, 1, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 0, 0,
          1, 0, 1, 0, 1, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
  // Snare1: scattered snares avoiding obvious positions
  { 64, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 1, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 1 }},
  // Snare2: dense ghost burst then silence
  { 64, { 0,.3,.5,.3, 0,.3, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0,.3,.5,.3,.5,.3, 0, 0,  0, 0, 0, 0, 0, 0,.5,.3 }},
  // Closed HiHat: irregular clusters
  { 64, { 1, 0, 1, 1, 0, 0, 0, 0,  1, 0, 0, 0, 1, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  1, 0, 1, 0, 0, 0, 1, 1,
          0, 0, 0, 0, 1, 0, 0, 0,  0, 0, 1, 1, 1, 0, 0, 0,
          1, 1, 1, 0, 0, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0 }},
  // Open HiHat: crash at phrase boundary
  { 64, { 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 1,
          1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 }},
}},

};  // end PATTERNS
