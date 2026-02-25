#pragma once
#include <cstdint>

// Fast deterministic PRNG — replaces std::mt19937.
// std::mt19937 has 2.5KB state and expensive seeding (~2500 cycles on ARM).
// xoshiro128** has 16 bytes of state and ~5 cycles per output.
// Same seed = same pattern (deterministic).
namespace pwmt {

class RandomGenerator {
public:
    explicit RandomGenerator(uint32_t seed = 0) { setSeed(seed); }

    void setSeed(uint32_t seed) {
        // SplitMix32 to initialize 4 state words from a single seed
        s_[0] = splitmix(seed);
        s_[1] = splitmix(s_[0]);
        s_[2] = splitmix(s_[1]);
        s_[3] = splitmix(s_[2]);
        if (s_[0] == 0 && s_[1] == 0 && s_[2] == 0 && s_[3] == 0) s_[3] = 1;
    }

    // Generate random float in range [min, max]
    float uniform(float min, float max) {
        return min + (max - min) * uniformFloat();
    }

    // Generate random float in [0.0, 1.0)
    float uniform() { return uniformFloat(); }

    // Generate random int in range [min, max] (inclusive)
    int uniformInt(int min, int max) {
        uint32_t range = static_cast<uint32_t>(max - min + 1);
        return min + static_cast<int>(next() % range);
    }

    // Generate random bool with given probability
    bool bernoulli(float probability) {
        return uniformFloat() < probability;
    }

    // Gaussian approximation using sum of 6 uniforms (Central Limit Theorem)
    // Avoids std::normal_distribution + Box-Muller (uses log + sqrt + sin)
    float gaussian(float mean, float stddev) {
        float sum = 0.0f;
        for (int i = 0; i < 6; i++) sum += uniformFloat();
        return mean + stddev * (sum - 3.0f);  // ~N(0,1) via CLT
    }

    static uint32_t hashCombine(uint32_t seed, uint32_t value) {
        seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }

private:
    uint32_t s_[4];

    // xoshiro128** core
    uint32_t next() {
        uint32_t result = rotl(s_[1] * 5, 7) * 9;
        uint32_t t = s_[1] << 9;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 11);
        return result;
    }

    static uint32_t rotl(uint32_t x, int k) {
        return (x << k) | (x >> (32 - k));
    }

    static uint32_t splitmix(uint32_t z) {
        z += 0x9e3779b9;
        z ^= z >> 16;
        z *= 0x85ebca6b;
        z ^= z >> 13;
        z *= 0xc2b2ae35;
        z ^= z >> 16;
        return z;
    }

    float uniformFloat() {
        return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f);
    }
};

} // namespace pwmt
