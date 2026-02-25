/*
 * PhaseonAlgorithm.hpp — FM routing topologies for Phaseon
 *
 * Defines how 6 operators connect (who modulates whom, who goes to output).
 * No exposed operator math — the user selects an algorithm via macro controls.
 *
 * Copyright (c) 2026 Bemushroomed.  All rights reserved.
 * Proprietary — not licensed under GPL or any open-source license.
 */
#pragma once

namespace phaseon {

// ─── Algorithm definition ───────────────────────────────────────────
// Each algorithm is a list of connections:
//   connection[i] = { src operator, dst operator, is_output }
// If dst == -1, the source goes to audio output.
// Operator indices 0..5 correspond to ops 1..6.
//
// Operators are processed in *reverse* order (6→5→4→3→2→1) so that
// modulators are computed before their carriers in a single pass.

struct AlgoConnection {
    int src;         // source operator (0..5)
    int dst;         // destination operator (0..5), or -1 for audio output
};

struct Algorithm {
    const char* name;
    int connectionCount;
    AlgoConnection connections[24];  // max connections per algo
    bool isCarrier[6];               // true if operator goes to audio output
};

// ─── Curated algorithms ─────────────────────────────────────────────
// Designed for aggressive modern sounds. Numbering 0..N.

static constexpr int kAlgorithmCount = 8;

inline const Algorithm& getAlgorithm(int index) {
    // Clamp
    if (index < 0) index = 0;
    if (index >= kAlgorithmCount) index = kAlgorithmCount - 1;

    static const Algorithm algos[kAlgorithmCount] = {
        // ── 0: STACK ────────────────────────────────────────────────
        // 6→5→4→3→2→1→out   (full serial FM chain, maximum depth)
        {
            "Stack", 6,
            {
                {5, 4, }, {4, 3, }, {3, 2, }, {2, 1, }, {1, 0, }, {0, -1, },
            },
            { true, false, false, false, false, false }
        },

        // ── 1: PAIRS ────────────────────────────────────────────────
        // (6→5) + (4→3) + (2→1) → out   (three FM pairs mixed)
        {
            "Pairs", 6,
            {
                {5, 4, }, {4, -1, },
                {3, 2, }, {2, -1, },
                {1, 0, }, {0, -1, },
            },
            { true, false, true, false, true, false }
        },

        // ── 2: SWARM ────────────────────────────────────────────────
        // (6+5+4+3+2)→1→out   (five modulators into one carrier)
        {
            "Swarm", 6,
            {
                {5, 0, }, {4, 0, }, {3, 0, }, {2, 0, }, {1, 0, }, {0, -1, },
            },
            { true, false, false, false, false, false }
        },

        // ── 3: DUAL STACK ───────────────────────────────────────────
        // (6→5→4→out) + (3→2→1→out)   (two independent 3-op stacks)
        {
            "Dual Stack", 6,
            {
                {5, 4, }, {4, 3, }, {3, -1, },
                {2, 1, }, {1, 0, }, {0, -1, },
            },
            { true, false, false, true, false, false }
        },

        // ── 4: FEEDBACK RING ────────────────────────────────────────
        // 6→5→4→6 (feedback loop), 3→2→1→out
        // Ring creates chaotic self-modulating texture
        {
            "Feedback Ring", 7,
            {
                {5, 4, }, {4, 3, }, {3, 5, },  // ring: 6→5→4→6
                {3, -1, },                       // ring also outputs
                {2, 1, }, {1, 0, }, {0, -1, },  // clean stack
            },
            { true, false, false, true, false, false }
        },

        // ── 5: WIDE ─────────────────────────────────────────────────
        // All 6 operators go to output (additive synthesis / thick unison)
        // Each can still self-modulate via feedback
        {
            "Wide", 6,
            {
                {0, -1, }, {1, -1, }, {2, -1, },
                {3, -1, }, {4, -1, }, {5, -1, },
            },
            { true, true, true, true, true, true }
        },

        // ── 6: BRUTAL ───────────────────────────────────────────────
        // Cross-modulation: 6→1, 5→2, 4→3, with 1+2+3 to output
        // Asymmetric, harsh, great for industrial
        {
            "Brutal", 6,
            {
                {5, 0, }, {4, 1, }, {3, 2, },
                {0, -1, }, {1, -1, }, {2, -1, },
            },
            { true, true, true, false, false, false }
        },

        // ── 7: CASCADE ──────────────────────────────────────────────
        // 6→5, 5→4, 5→3, 4→2, 3→1, 2+1→out
        // Branching tree: one modulator feeds two paths
        {
            "Cascade", 7,
            {
                {5, 4, }, {4, 3, }, {4, 2, },
                {3, 1, }, {2, 0, },
                {1, -1, }, {0, -1, },
            },
            { true, true, false, false, false, false }
        },
    };

    return algos[index];
}

} // namespace phaseon
