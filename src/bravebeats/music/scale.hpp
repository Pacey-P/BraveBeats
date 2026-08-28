#pragma once

// Pitch material for the melodic voices
//
// The generator stays inside one mode for a whole piece. Pentatonic sets are
// the default because gapped five-note scales sit well under the interlocking
// ostinatos and leave the drums room

#include <cstddef>
#include <string>
#include <vector>

namespace bravebeats {
namespace music {

struct Scale {
    std::string name;
    std::vector<int> degrees;  // semitones above the root, ascending, one octave
};

inline const std::vector<Scale> &scaleLibrary() {
    static const std::vector<Scale> kScales = {
        {"minor-pentatonic", {0, 3, 5, 7, 10}},
        {"major-pentatonic", {0, 2, 4, 7, 9}},
        {"dorian", {0, 2, 3, 5, 7, 9, 10}},
        {"phrygian", {0, 1, 3, 5, 8, 10}},
        {"aeolian", {0, 2, 3, 5, 7, 8, 10}},
        {"mixolydian", {0, 2, 4, 5, 7, 9, 10}},
        {"hexatonic", {0, 2, 3, 5, 7, 10}},
        {"in-sen", {0, 1, 5, 7, 10}},
    };
    return kScales;
}

inline const Scale *findScale(const std::string &name) {
    for (const Scale &scale : scaleLibrary()) {
        if (scale.name == name) return &scale;
    }
    return nullptr;
}

// Degree may run past the ends of the scale, which wraps into other octaves
inline int degreeToSemitone(const Scale &scale, int degree) {
    const int size = static_cast<int>(scale.degrees.size());
    if (size == 0) return 0;
    int octave = degree / size;
    int index = degree % size;
    if (index < 0) {
        index += size;
        --octave;
    }
    return scale.degrees[static_cast<std::size_t>(index)] + 12 * octave;
}

inline float degreeToNote(const Scale &scale, int rootMidi, int degree) {
    return static_cast<float>(rootMidi + degreeToSemitone(scale, degree));
}

// Stack of scale steps used for the sustained chant harmony
// Skipping alternate degrees gives the thirds-and-fifths shape of the mode
inline std::vector<float> chordOn(const Scale &scale, int rootMidi, int degree, int voices) {
    std::vector<float> notes;
    notes.reserve(static_cast<std::size_t>(voices));
    for (int i = 0; i < voices; ++i) {
        notes.push_back(degreeToNote(scale, rootMidi, degree + 2 * i));
    }
    return notes;
}

}  // namespace music
}  // namespace bravebeats
