// SlideWyrm — Quantizer and Scale Definitions
//
// Scale tables ported from:
//   Mutable Instruments Braids (https://github.com/pichenettes/eurorack)
//     Copyright (c) 2012 Emilie Gillet.  MIT License.
//   Ornament & Crime (https://github.com/mxmxmx/O_C)
//     Copyright (c) 2016 Patrick Dowling, Max Stadler, Tim Churches.  MIT License.
//
// Full license text: see THIRD_PARTY_LICENSES.md in the repository root.
// For TB-303 style acid pattern generation (SlideWyrm module).

#pragma once
#include <cstdint>

namespace slidewyrm {

// Scale structure
struct Scale {
    uint16_t span;      // Number of notes in the scale
    uint8_t num_notes;  // Number of notes
    const int8_t* notes; // Note offsets in semitones
};

// Chromatic scale (all 12 semitones)
static const int8_t scale_chromatic[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

// Major scale
static const int8_t scale_major[] = {0, 2, 4, 5, 7, 9, 11};

// Minor scale
static const int8_t scale_minor[] = {0, 2, 3, 5, 7, 8, 10};

// Pentatonic major
static const int8_t scale_pentatonic_major[] = {0, 2, 4, 7, 9};

// Pentatonic minor
static const int8_t scale_pentatonic_minor[] = {0, 3, 5, 7, 10};

// Blues scale
static const int8_t scale_blues[] = {0, 3, 5, 6, 7, 10};

// Dorian
static const int8_t scale_dorian[] = {0, 2, 3, 5, 7, 9, 10};

// Phrygian
static const int8_t scale_phrygian[] = {0, 1, 3, 5, 7, 8, 10};

// Lydian
static const int8_t scale_lydian[] = {0, 2, 4, 6, 7, 9, 11};

// Mixolydian
static const int8_t scale_mixolydian[] = {0, 2, 4, 5, 7, 9, 10};

// Locrian
static const int8_t scale_locrian[] = {0, 1, 3, 5, 6, 8, 10};

// Harmonic minor
static const int8_t scale_harmonic_minor[] = {0, 2, 3, 5, 7, 8, 11};

// Melodic minor
static const int8_t scale_melodic_minor[] = {0, 2, 3, 5, 7, 9, 11};

// Whole tone
static const int8_t scale_whole_tone[] = {0, 2, 4, 6, 8, 10};

// Diminished
static const int8_t scale_diminished[] = {0, 2, 3, 5, 6, 8, 9, 11};

// GUNA (from O&C - sounds cool with TB-303)
static const int8_t scale_guna[] = {0, 1, 4, 5, 7, 8, 10};

// Raag Bhairav
static const int8_t scale_bhairav[] = {0, 1, 4, 5, 7, 8, 11};

// Japanese
static const int8_t scale_japanese[] = {0, 1, 5, 7, 8};

// Arabic
static const int8_t scale_arabic[] = {0, 1, 4, 5, 7, 8, 11};

// Spanish
static const int8_t scale_spanish[] = {0, 1, 4, 5, 7, 8, 10};

// Gypsy
static const int8_t scale_gypsy[] = {0, 2, 3, 6, 7, 8, 11};

// Egyptian
static const int8_t scale_egyptian[] = {0, 2, 5, 7, 10};

// Hawaiian
static const int8_t scale_hawaiian[] = {0, 2, 3, 5, 7, 9, 11};

// Bali Pelog
static const int8_t scale_bali_pelog[] = {0, 1, 3, 7, 8};

// Hirajoshi
static const int8_t scale_hirajoshi[] = {0, 2, 3, 7, 8};

// Iwato
static const int8_t scale_iwato[] = {0, 1, 5, 6, 10};

// Kumoi
static const int8_t scale_kumoi[] = {0, 2, 3, 7, 9};

// Pelog
static const int8_t scale_pelog[] = {0, 1, 3, 6, 7, 8, 10};

// Prometheus
static const int8_t scale_prometheus[] = {0, 2, 4, 6, 9, 10};

// Tritone
static const int8_t scale_tritone[] = {0, 1, 4, 6, 7, 10};

// 3b7+ (Major Triad + 7)
static const int8_t scale_triad_7[] = {0, 4, 7, 11};

// Scale definitions array
static const Scale scales[] = {
    {12, 12, scale_chromatic},           // 0: Chromatic
    {12, 7, scale_major},                // 1: Major
    {12, 7, scale_minor},                // 2: Minor
    {12, 5, scale_pentatonic_major},     // 3: Pentatonic Major
    {12, 5, scale_pentatonic_minor},     // 4: Pentatonic Minor
    {12, 6, scale_blues},                // 5: Blues
    {12, 7, scale_dorian},               // 6: Dorian
    {12, 7, scale_phrygian},             // 7: Phrygian
    {12, 7, scale_lydian},               // 8: Lydian
    {12, 7, scale_mixolydian},           // 9: Mixolydian
    {12, 7, scale_locrian},              // 10: Locrian
    {12, 7, scale_harmonic_minor},       // 11: Harmonic Minor
    {12, 7, scale_melodic_minor},        // 12: Melodic Minor
    {12, 6, scale_whole_tone},           // 13: Whole Tone
    {12, 8, scale_diminished},           // 14: Diminished
    {12, 7, scale_guna},                 // 15: GUNA
    {12, 7, scale_bhairav},              // 16: Bhairav
    {12, 5, scale_japanese},             // 17: Japanese
    {12, 7, scale_arabic},               // 18: Arabic
    {12, 7, scale_spanish},              // 19: Spanish
    {12, 7, scale_gypsy},                // 20: Gypsy
    {12, 5, scale_egyptian},             // 21: Egyptian
    {12, 7, scale_hawaiian},             // 22: Hawaiian
    {12, 5, scale_bali_pelog},           // 23: Bali Pelog
    {12, 5, scale_hirajoshi},            // 24: Hirajoshi
    {12, 5, scale_iwato},                // 25: Iwato
    {12, 5, scale_kumoi},                // 26: Kumoi
    {12, 7, scale_pelog},                // 27: Pelog
    {12, 6, scale_prometheus},           // 28: Prometheus
    {12, 6, scale_tritone},              // 29: Tritone
    {12, 4, scale_triad_7},              // 30: 3b7+
};

static const int NUM_SCALES = 31;

#if defined(__GNUC__) || defined(__clang__)
#define MORPHWORX_MAYBE_UNUSED __attribute__((unused))
#else
#define MORPHWORX_MAYBE_UNUSED
#endif

// Scale names
static const char* scale_names[] MORPHWORX_MAYBE_UNUSED = {
    "Chromatic",
    "Major",
    "Minor",
    "Pent Maj",
    "Pent Min",
    "Blues",
    "Dorian",
    "Phrygian",
    "Lydian",
    "Mixolydian",
    "Locrian",
    "Harm Min",
    "Melo Min",
    "Whole Tone",
    "Diminished",
    "GUNA",
    "Bhairav",
    "Japanese",
    "Arabic",
    "Spanish",
    "Gypsy",
    "Egyptian",
    "Hawaiian",
    "Bali Pelog",
    "Hirajoshi",
    "Iwato",
    "Kumoi",
    "Pelog",
    "Prometheus",
    "Tritone",
    "3b7+"
};

// Note names
static const char* note_names[] MORPHWORX_MAYBE_UNUSED = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

#undef MORPHWORX_MAYBE_UNUSED

// Simple quantizer class
class Quantizer {
private:
    int currentScale;
    int root;
    
public:
    Quantizer() : currentScale(0), root(0) {}
    
    void setScale(int scaleIndex) {
        if (scaleIndex >= 0 && scaleIndex < NUM_SCALES) {
            currentScale = scaleIndex;
        }
    }
    
    void setRoot(int rootNote) {
        root = rootNote % 12;
    }
    
    int getScaleSize() const {
        return scales[currentScale].num_notes;
    }
    
    // Lookup pitch CV for a note index (0-127)
    // Returns voltage in VCV Rack format (1V/octave, C4 = 0V)
    float lookup(int noteIndex) const {
        if (noteIndex < 0 || noteIndex > 127) {
            noteIndex = 64; // Default to C4
        }
        
        // Calculate octave and note within octave
        int octave = (noteIndex / 12) - 5; // C4 (64) = octave 0
        int noteInOctave = noteIndex % 12;
        
        // Find closest note in scale
        const Scale& scale = scales[currentScale];
        int closestNote = 0;
        int minDist = 12;
        
        for (int i = 0; i < scale.num_notes; i++) {
            int scaleNote = (scale.notes[i] + root) % 12;
            int dist = abs(noteInOctave - scaleNote);
            if (dist < minDist) {
                minDist = dist;
                closestNote = scaleNote;
            }
        }
        
        // Convert to voltage (1V/octave)
        float voltage = octave + (closestNote / 12.0f);
        return voltage;
    }
    
    // Get the note number (0-11) for a given note index
    int getNoteNumber(int noteIndex) const {
        if (noteIndex < 0 || noteIndex > 127) {
            noteIndex = 64;
        }
        
        int noteInOctave = noteIndex % 12;
        
        // Find closest note in scale
        const Scale& scale = scales[currentScale];
        int closestNote = 0;
        int minDist = 12;
        
        for (int i = 0; i < scale.num_notes; i++) {
            int scaleNote = (scale.notes[i] + root) % 12;
            int dist = abs(noteInOctave - scaleNote);
            if (dist < minDist) {
                minDist = dist;
                closestNote = scaleNote;
            }
        }
        
        return closestNote;
    }
};

} // namespace slidewyrm
