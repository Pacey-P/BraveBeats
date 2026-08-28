#pragma once

// Euclidean rhythms via Bjorklund's algorithm
//
// Spreading k onsets as evenly as possible over n steps reproduces a large
// family of traditional timelines. E(3,8) is the tresillo, E(5,8) the
// cinquillo, and E(7,12) the West African bell pattern this generator leans
// on. Rotating a pattern picks a different starting point in the same cycle,
// which is how the interlocking parts are built here

#include <algorithm>
#include <cstddef>
#include <vector>

namespace bravebeats {
namespace music {

// k onsets distributed over n steps
inline std::vector<bool> euclid(int onsets, int steps) {
    if (steps <= 0) return {};
    onsets = std::max(0, std::min(onsets, steps));
    if (onsets == 0) return std::vector<bool>(static_cast<std::size_t>(steps), false);
    if (onsets == steps) return std::vector<bool>(static_cast<std::size_t>(steps), true);

    // Start with `onsets` groups of [1] and `steps - onsets` groups of [0],
    // then repeatedly fold the remainder groups onto the front groups
    std::vector<std::vector<bool>> front(static_cast<std::size_t>(onsets), std::vector<bool>{true});
    std::vector<std::vector<bool>> back(static_cast<std::size_t>(steps - onsets), std::vector<bool>{false});

    while (back.size() > 1 && front.size() > 0) {
        const std::size_t pairs = std::min(front.size(), back.size());
        std::vector<std::vector<bool>> merged;
        merged.reserve(pairs);
        for (std::size_t i = 0; i < pairs; ++i) {
            std::vector<bool> group = front[i];
            group.insert(group.end(), back[i].begin(), back[i].end());
            merged.push_back(std::move(group));
        }
        std::vector<std::vector<bool>> remainder;
        if (front.size() > pairs) {
            remainder.assign(front.begin() + static_cast<long>(pairs), front.end());
        } else if (back.size() > pairs) {
            remainder.assign(back.begin() + static_cast<long>(pairs), back.end());
        }
        front = std::move(merged);
        back = std::move(remainder);
    }

    std::vector<bool> pattern;
    pattern.reserve(static_cast<std::size_t>(steps));
    for (const auto &group : front) pattern.insert(pattern.end(), group.begin(), group.end());
    for (const auto &group : back) pattern.insert(pattern.end(), group.begin(), group.end());
    pattern.resize(static_cast<std::size_t>(steps), false);
    return pattern;
}

// Move the starting point of a cycle. Negative rotations run backwards
inline std::vector<bool> rotate(const std::vector<bool> &pattern, int by) {
    const int size = static_cast<int>(pattern.size());
    if (size == 0) return pattern;
    std::vector<bool> out(static_cast<std::size_t>(size), false);
    for (int i = 0; i < size; ++i) {
        int source = (i + by) % size;
        if (source < 0) source += size;
        out[static_cast<std::size_t>(i)] = pattern[static_cast<std::size_t>(source)];
    }
    return out;
}

inline std::vector<bool> euclidRotated(int onsets, int steps, int by) {
    return rotate(euclid(onsets, steps), by);
}

inline int onsetCount(const std::vector<bool> &pattern) {
    int count = 0;
    for (bool step : pattern) count += step ? 1 : 0;
    return count;
}

// Everything in `a` that `b` does not already play, used to keep the drums
// from stacking every part on the same pulse
inline std::vector<bool> without(const std::vector<bool> &a, const std::vector<bool> &b) {
    std::vector<bool> out = a;
    for (std::size_t i = 0; i < out.size() && i < b.size(); ++i) {
        if (b[i]) out[i] = false;
    }
    return out;
}

}  // namespace music
}  // namespace bravebeats
