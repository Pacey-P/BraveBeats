#pragma once

// The shape of the piece over time
//
// Layers do not have hard on/off switches in the arrangement. Each one joins
// when the section intensity passes its entry point, so the ensemble fills in
// and thins out on its own as the arc moves

#include <algorithm>
#include <string>
#include <vector>

#include "bravebeats/core/random.hpp"

namespace bravebeats {
namespace music {

struct Section {
    std::string name;
    int bars = 4;
    float intensity = 0.5f;  // drives which layers play and how hard
    bool drumsMuted = false;  // breakdowns drop the ensemble but keep the air
    bool fadeOut = false;
};

// A fixed dramatic arc, stretched to fill the requested length
// Weights say how much of the total each stage should take
inline std::vector<Section> buildArrangement(int totalBars, Rng &rng) {
    struct Stage {
        const char *name;
        float weight;
        float intensity;
        bool drumsMuted;
        bool fadeOut;
    };
    static const Stage kStages[] = {
        {"invocation", 0.09f, 0.12f, true, false},
        {"first-call", 0.11f, 0.34f, false, false},
        {"gathering", 0.15f, 0.55f, false, false},
        {"circle", 0.14f, 0.72f, false, false},
        {"ascent", 0.13f, 0.86f, false, false},
        {"peak", 0.14f, 1.00f, false, false},
        {"hollow", 0.08f, 0.30f, true, false},
        {"return", 0.10f, 0.92f, false, false},
        {"embers", 0.06f, 0.40f, false, true},
    };
    const int stageCount = static_cast<int>(sizeof(kStages) / sizeof(kStages[0]));

    totalBars = std::max(totalBars, 1);

    // A piece shorter than the arc keeps a spread of stages rather than being
    // stretched out to fit all of them. Picking them evenly holds the shape:
    // it still opens quietly, rises to the peak and settles again
    std::vector<int> chosen;
    if (totalBars >= stageCount) {
        for (int i = 0; i < stageCount; ++i) chosen.push_back(i);
    } else {
        for (int i = 0; i < totalBars; ++i) {
            const int index = totalBars == 1
                ? stageCount / 2
                : (i * (stageCount - 1) + (totalBars - 1) / 2) / (totalBars - 1);
            chosen.push_back(index);
        }
    }
    const int usedStages = static_cast<int>(chosen.size());

    std::vector<Section> sections;
    sections.reserve(chosen.size());

    // Weights are renormalised over the stages actually used, so a trimmed
    // arc still fills exactly the bars asked for
    float weightTotal = 0.0f;
    for (int index : chosen) weightTotal += kStages[index].weight;
    if (weightTotal <= 0.0f) weightTotal = 1.0f;

    int assigned = 0;
    for (int i = 0; i < usedStages; ++i) {
        const Stage &stage = kStages[chosen[static_cast<std::size_t>(i)]];
        Section section;
        section.name = stage.name;
        section.intensity = stage.intensity;
        section.drumsMuted = stage.drumsMuted;
        section.fadeOut = stage.fadeOut;

        int bars = static_cast<int>(stage.weight / weightTotal * static_cast<float>(totalBars) + 0.5f);
        // Nudge the length so two renders of the same length still differ
        bars += rng.intRange(-1, 1);
        bars = std::max(bars, 1);
        // Leave at least one bar for every stage still to come
        const int remainingStages = usedStages - i - 1;
        bars = std::min(bars, std::max(1, totalBars - assigned - remainingStages));
        section.bars = bars;
        assigned += bars;
        sections.push_back(section);
    }

    // Any rounding shortfall goes to the longest stretch in the middle
    if (assigned < totalBars) {
        std::size_t widest = 0;
        for (std::size_t i = 1; i < sections.size(); ++i) {
            if (!sections[i].fadeOut && sections[i].bars > sections[widest].bars) widest = i;
        }
        sections[widest].bars += totalBars - assigned;
    }
    return sections;
}

}  // namespace music
}  // namespace bravebeats
