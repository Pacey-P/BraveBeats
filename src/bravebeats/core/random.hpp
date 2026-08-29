#pragma once

// Deterministic random source for the generator
// Everything the composer decides comes from one of these, so a seed
// always rebuilds the same piece note for note

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bravebeats {

// splitmix64 for seeding, xoshiro128+ for the stream
// Small, fast, and identical on every platform, which matters because
// the seed is part of the output contract
class Rng {
public:
    Rng() { seed(0x5EEDu); }
    explicit Rng(uint64_t s) { seed(s); }

    void seed(uint64_t s) {
        uint64_t z = s + 0x9E3779B97F4A7C15ull;
        for (int i = 0; i < 4; ++i) {
            uint64_t x = (z += 0x9E3779B97F4A7C15ull);
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
            x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
            state_[i] = static_cast<uint32_t>((x ^ (x >> 31)) >> 16);
        }
        if ((state_[0] | state_[1] | state_[2] | state_[3]) == 0u) state_[0] = 1u;
        for (int i = 0; i < 16; ++i) next();
    }

    uint32_t next() {
        const uint32_t result = state_[0] + state_[3];
        const uint32_t t = state_[1] << 9;
        state_[2] ^= state_[0];
        state_[3] ^= state_[1];
        state_[1] ^= state_[2];
        state_[0] ^= state_[3];
        state_[2] ^= t;
        state_[3] = (state_[3] << 11) | (state_[3] >> 21);
        return result;
    }

    // Uniform in [0,1)
    float uniform() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }

    float range(float lo, float hi) { return lo + (hi - lo) * uniform(); }

    // Uniform in [0,n)
    int below(int n) { return n > 0 ? static_cast<int>(next() % static_cast<uint32_t>(n)) : 0; }

    int intRange(int lo, int hi) { return hi > lo ? lo + below(hi - lo + 1) : lo; }

    bool chance(float probability) { return uniform() < probability; }

    // Sum of three uniforms, so most values land near the middle. Symmetric
    // drift for humanising timing and level, without the hard edges of a
    // flat distribution.
    //
    // The three draws are taken one statement at a time. Written as one sum
    // the compiler may evaluate them in any order, and since floating-point
    // addition is not associative that would put the last bits of the result,
    // and so the odd note time, at the mercy of the toolchain
    float centred() {
        const float first = uniform();
        const float second = uniform();
        const float third = uniform();
        return (first + second + third) * (2.0f / 3.0f) - 1.0f;
    }

    // Pick an index from unnormalised weights
    int weighted(const std::vector<float> &weights) {
        float total = 0.0f;
        for (float w : weights) total += w > 0.0f ? w : 0.0f;
        if (total <= 0.0f) return 0;
        float pick = uniform() * total;
        for (std::size_t i = 0; i < weights.size(); ++i) {
            const float w = weights[i] > 0.0f ? weights[i] : 0.0f;
            if (pick < w) return static_cast<int>(i);
            pick -= w;
        }
        return static_cast<int>(weights.size()) - 1;
    }

    template <typename T>
    const T &pick(const std::vector<T> &items) {
        return items[static_cast<std::size_t>(below(static_cast<int>(items.size())))];
    }

    // A named child stream. Layers draw from their own branch so adding a
    // shaker does not reshuffle the melody
    Rng branch(uint32_t tag) const {
        const uint64_t mixed = (static_cast<uint64_t>(state_[0]) << 32) ^ state_[3];
        return Rng(mixed ^ (static_cast<uint64_t>(tag) * 0xD1B54A32D192ED03ull));
    }

private:
    uint32_t state_[4] = {1u, 2u, 3u, 4u};
};

}  // namespace bravebeats
