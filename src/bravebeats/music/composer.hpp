#pragma once

// Turns a seed into a complete list of notes
//
// The ensemble is modelled on a drum circle rather than a drum machine. Parts
// are built from euclidean timelines, several of them running on cycle lengths
// that do not divide the bar, so the pattern drifts against itself and only
// comes back around every few bars. Nothing here touches audio; the renderer
// takes this note list and plays it

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "bravebeats/core/random.hpp"
#include "bravebeats/music/arrangement.hpp"
#include "bravebeats/music/euclid.hpp"
#include "bravebeats/music/scale.hpp"

namespace bravebeats {
namespace music {

enum class Voice {
    Kick,     // the deep floor drum on the ground pulse
    LowDrum,  // tuned low hand drum
    MidDrum,  // tuned mid drum, usually the polyrhythmic one
    HighDrum, // tuned high drum, the fast talking part
    Slap,     // sharp rim slap answering the phrase
    Clap,     // hands around the circle
    Shaker,   // seed rattle on the subdivision
    Bell,     // the timeline everything else hangs off
    Balafon,  // struck wooden bars
    Kalimba,  // plucked metal tines
    Flute,    // breathy overblown pipe
    Chant,    // sustained voices
    Drone,    // the low continuous breath under everything
    Count
};

inline const char *voiceName(Voice voice) {
    switch (voice) {
        case Voice::Kick: return "kick";
        case Voice::LowDrum: return "low-drum";
        case Voice::MidDrum: return "mid-drum";
        case Voice::HighDrum: return "high-drum";
        case Voice::Slap: return "slap";
        case Voice::Clap: return "clap";
        case Voice::Shaker: return "shaker";
        case Voice::Bell: return "bell";
        case Voice::Balafon: return "balafon";
        case Voice::Kalimba: return "kalimba";
        case Voice::Flute: return "flute";
        case Voice::Chant: return "chant";
        case Voice::Drone: return "drone";
        default: return "?";
    }
}

struct Note {
    double time = 0.0;     // seconds from the start of the piece
    Voice voice = Voice::Kick;
    float velocity = 0.8f; // 0 to 1
    float pitch = 60.0f;   // MIDI note for melodic voices, pitch ratio for drums
    float duration = 0.5f; // seconds, only the sustained voices use it
    float pan = 0.0f;      // -1 left to +1 right
    float colour = 0.5f;   // decay for drums, brightness for the rest
    // The intensity its part needs before it is heard. In a fixed arrangement
    // the composer has already applied this; a game driving intensity live
    // uses it to decide what plays right now
    float minIntensity = 0.0f;
};

struct ComposerSettings {
    uint64_t seed = 20260828u;
    double tempo = 0.0;             // beats per minute, 0 picks one from the seed
    double durationSeconds = 150.0;
    int meterPulses = 0;            // 12 for compound, 16 for simple, 0 picks one
    std::string scaleName;          // empty picks one
    int root = 0;                   // MIDI note, 0 picks one
    // Bars in a seamless loop for live use. 0 writes the full arc instead
    int loopBars = 0;
};

struct Composition {
    std::vector<Note> notes;
    std::vector<Section> sections;
    double tempo = 96.0;
    double barSeconds = 2.5;
    double totalSeconds = 0.0;
    int totalBars = 0;
    int pulsesPerBar = 12;
    int beatsPerBar = 4;
    int subdivision = 3;
    int rootMidi = 57;
    std::string scaleName = "minor-pentatonic";
    uint64_t seed = 0;
    // Length of one turn of the loop, or 0 when this is a fixed piece
    double loopSeconds = 0.0;
};

// Where the pitched parts join in. The drum layers carry their own entry
// points; these are the matching numbers for everything else
constexpr float kBalafonIntensity = 0.44f;
constexpr float kKalimbaIntensity = 0.62f;
constexpr float kFluteIntensity = 0.28f;
constexpr float kChantIntensity = 0.66f;
constexpr float kFillIntensity = 0.55f;

// Below this a stroke cannot be heard under the rest of the ensemble. Letting
// one through is not harmless: it takes a voice from the pool and cuts off a
// drum that is still ringing
constexpr float kMinAudibleVelocity = 0.002f;

namespace detail {

// One rhythmic part of the ensemble
struct DrumLayer {
    Voice voice = Voice::Kick;
    std::vector<bool> pattern;      // onsets over `cycle` pulses
    std::vector<float> accents;     // per-step level, same length as pattern
    int cycle = 12;                 // may differ from the bar, which is the point
    float entry = 0.0f;             // section intensity needed before it plays
    float level = 0.9f;
    float pan = 0.0f;
    float timingBias = 0.0f;        // seconds, a constant push or lay-back
    float jitter = 0.004f;          // seconds of per-hit drift
    float ghostChance = 0.0f;       // probability of a quiet extra stroke
    float dropChance = 0.0f;        // probability of leaving a stroke out
    float pitch = 1.0f;             // pitch ratio handed to the drum voice
    float colour = 0.5f;
};

// Beats get the weight, the pulse before a beat gets a lift into it
inline std::vector<float> accentMap(int cycle, int subdivision, Rng &rng) {
    std::vector<float> accents(static_cast<std::size_t>(cycle), 0.62f);
    for (int i = 0; i < cycle; ++i) {
        const int positionInBeat = subdivision > 0 ? i % subdivision : 0;
        if (positionInBeat == 0) accents[static_cast<std::size_t>(i)] = 1.0f;
        else if (positionInBeat == subdivision - 1) accents[static_cast<std::size_t>(i)] = 0.78f;
        accents[static_cast<std::size_t>(i)] *= 0.94f + 0.12f * rng.uniform();
    }
    if (cycle > 0) accents[0] = std::min(1.0f, accents[0] * 1.08f);
    return accents;
}

}  // namespace detail

class Composer {
public:
    explicit Composer(const ComposerSettings &settings) : settings_(settings), rng_(settings.seed) {}

    Composition compose() {
        Composition out;
        out.seed = settings_.seed;

        // Meter first, because everything else is measured in pulses
        const bool compound = settings_.meterPulses == 12 ? true
                            : settings_.meterPulses == 16 ? false
                            : rng_.chance(0.7f);  // the 12/8 feel is the house default
        out.pulsesPerBar = compound ? 12 : 16;
        out.subdivision = compound ? 3 : 4;
        out.beatsPerBar = 4;

        out.tempo = settings_.tempo > 0.0 ? settings_.tempo
                                          : static_cast<double>(rng_.range(84.0f, 112.0f));
        const double beatSeconds = 60.0 / out.tempo;
        out.barSeconds = beatSeconds * out.beatsPerBar;
        pulseSeconds_ = beatSeconds / out.subdivision;

        const Scale *scale = settings_.scaleName.empty() ? nullptr : findScale(settings_.scaleName);
        if (!scale) {
            const std::vector<Scale> &library = scaleLibrary();
            // Weighted towards the gapped scales, which leave more room
            static const std::vector<float> kWeights = {3.0f, 1.4f, 2.0f, 1.6f, 1.2f, 1.0f, 1.5f, 1.0f};
            scale = &library[static_cast<std::size_t>(rng_.weighted(kWeights))];
        }
        scale_ = *scale;
        out.scaleName = scale_.name;

        // Low roots keep the drone under the drums instead of fighting them
        out.rootMidi = settings_.root > 0 ? settings_.root : rng_.intRange(50, 58);
        root_ = out.rootMidi;

        if (settings_.loopBars > 0) {
            // One section at full intensity. Every part is written down, and
            // what is actually heard is decided at playback by the live
            // intensity rather than baked into the arrangement here
            out.totalBars = settings_.loopBars;
            Section loop;
            loop.name = "loop";
            loop.bars = out.totalBars;
            loop.intensity = 1.0f;
            out.sections.assign(1, loop);
            out.loopSeconds = static_cast<double>(out.totalBars) * out.barSeconds;
        } else {
            out.totalBars = std::max(4, static_cast<int>(std::lround(settings_.durationSeconds / out.barSeconds)));
            out.sections = buildArrangement(out.totalBars, rng_);
            int barsPlanned = 0;
            for (const Section &section : out.sections) barsPlanned += section.bars;
            out.totalBars = barsPlanned;
        }

        buildDrumLayers(out);
        buildMelodicMaterial(out);
        renderSections(out);

        out.notes.erase(std::remove_if(out.notes.begin(), out.notes.end(),
                                       [](const Note &note) {
                                           return note.velocity < kMinAudibleVelocity;
                                       }),
                        out.notes.end());

        std::stable_sort(out.notes.begin(), out.notes.end(),
                         [](const Note &a, const Note &b) { return a.time < b.time; });

        if (out.loopSeconds > 0.0) {
            out.totalSeconds = out.loopSeconds;
        } else {
            double last = 0.0;
            for (const Note &note : out.notes) {
                last = std::max(last, note.time + static_cast<double>(note.duration));
            }
            out.totalSeconds = std::max(last, static_cast<double>(out.totalBars) * out.barSeconds);
        }
        return out;
    }

private:
    // ---- rhythm -----------------------------------------------------------

    void buildDrumLayers(const Composition &out) {
        layers_.clear();
        const int pulses = out.pulsesPerBar;
        const int sub = out.subdivision;
        Rng rng = rng_.branch(0x0BEA7u);

        // The timeline. Everything else is heard in relation to this, so it
        // stays locked to the bar and enters almost first
        {
            detail::DrumLayer bell;
            bell.voice = Voice::Bell;
            const int onsets = pulses == 12 ? 7 : (rng.chance(0.5f) ? 9 : 7);
            bell.pattern = euclidRotated(onsets, pulses, rng.pick(std::vector<int>{0, 0, 2, 4, 5}));
            bell.cycle = pulses;
            bell.accents = detail::accentMap(pulses, sub, rng);
            bell.entry = 0.10f;
            bell.level = 0.52f;
            bell.pan = rng.range(-0.55f, -0.28f);
            bell.timingBias = -0.004f;  // the bell leads the ensemble a touch
            bell.jitter = 0.0035f;
            // The hat voice sings around 10 kHz at its own tuning, which is
            // hiss rather than metal. Pitched well down it lands where a
            // struck iron bell actually rings
            bell.pitch = rng.range(0.30f, 0.46f);
            bell.colour = 0.30f;
            layers_.push_back(bell);
            bellPattern_ = bell.pattern;
        }

        // Floor drum. Always speaks on the first pulse, then follows a sparse
        // even spread, which in 12/8 lands as three against the four beats
        {
            detail::DrumLayer kick;
            kick.voice = Voice::Kick;
            const int onsets = rng.intRange(2, pulses == 12 ? 3 : 4);
            kick.pattern = euclidRotated(onsets, pulses, 0);
            kick.pattern[0] = true;
            if (rng.chance(0.45f)) {
                // A pickup just before the turnaround
                kick.pattern[static_cast<std::size_t>(pulses - 1)] = true;
            }
            kick.cycle = pulses;
            kick.accents = detail::accentMap(pulses, sub, rng);
            kick.entry = 0.30f;
            kick.level = 1.0f;
            kick.pan = 0.0f;
            kick.jitter = 0.003f;
            kick.dropChance = 0.05f;
            kick.colour = rng.range(0.55f, 0.80f);
            layers_.push_back(kick);
        }

        // Low drum answers the floor drum without doubling it
        {
            detail::DrumLayer low;
            low.voice = Voice::LowDrum;
            // Each draw is taken into its own variable on purpose. Passing two
            // rng calls as arguments to one function leaves their order up to
            // the compiler, which would make the seed mean different music on
            // different toolchains
            const int lowOnsets = rng.intRange(3, 4);
            const int lowRotation = rng.intRange(0, pulses - 1);
            low.pattern = without(euclidRotated(lowOnsets, pulses, lowRotation),
                                  layers_[1].pattern);
            if (onsetCount(low.pattern) == 0) low.pattern[static_cast<std::size_t>(sub)] = true;
            low.cycle = pulses;
            low.accents = detail::accentMap(pulses, sub, rng);
            low.entry = 0.34f;
            low.level = 0.86f;
            low.pan = rng.range(-0.42f, -0.15f);
            low.timingBias = 0.006f;  // sits behind the beat
            low.ghostChance = 0.10f;
            low.pitch = rng.range(0.80f, 0.95f);
            low.colour = rng.range(0.55f, 0.75f);
            layers_.push_back(low);
        }

        // The polyrhythmic part. Its cycle deliberately does not divide the
        // bar, so the phrase turns over across several bars
        {
            detail::DrumLayer mid;
            mid.voice = Voice::MidDrum;
            const std::vector<int> crossCycles = pulses == 12 ? std::vector<int>{8, 9, 10, 12}
                                                             : std::vector<int>{10, 12, 14, 16};
            mid.cycle = rng.pick(crossCycles);
            const int midOnsets = rng.intRange(3, 5);
            const int midRotation = rng.intRange(0, mid.cycle - 1);
            mid.pattern = euclidRotated(midOnsets, mid.cycle, midRotation);
            mid.accents = detail::accentMap(mid.cycle, sub, rng);
            mid.entry = 0.52f;
            mid.level = 0.72f;
            mid.pan = rng.range(0.16f, 0.46f);
            mid.timingBias = rng.range(-0.003f, 0.004f);
            mid.ghostChance = 0.14f;
            mid.pitch = rng.range(1.05f, 1.30f);
            mid.colour = rng.range(0.45f, 0.65f);
            layers_.push_back(mid);
        }

        // Fast talking drum, dense and full of ghost strokes
        {
            detail::DrumLayer high;
            high.voice = Voice::HighDrum;
            high.cycle = rng.chance(0.4f) ? (pulses == 12 ? 6 : 8) : pulses;
            const int highOnsets = rng.intRange(high.cycle / 2, high.cycle - 2);
            const int highRotation = rng.intRange(0, high.cycle - 1);
            high.pattern = euclidRotated(highOnsets, high.cycle, highRotation);
            high.accents = detail::accentMap(high.cycle, sub, rng);
            high.entry = 0.68f;
            high.level = 0.55f;
            high.pan = rng.range(-0.50f, -0.20f);
            high.timingBias = -0.002f;
            high.ghostChance = 0.22f;
            high.dropChance = 0.10f;
            high.pitch = rng.range(1.45f, 1.90f);
            high.colour = rng.range(0.30f, 0.50f);
            layers_.push_back(high);
        }

        // Rattle on the subdivision, with a coprime cycle so the accents move
        {
            detail::DrumLayer shaker;
            shaker.voice = Voice::Shaker;
            const std::vector<int> cycles = pulses == 12 ? std::vector<int>{5, 7, 12, 12}
                                                         : std::vector<int>{7, 9, 16, 16};
            shaker.cycle = rng.pick(cycles);
            const int onsets = std::max(2, static_cast<int>(shaker.cycle * rng.range(0.55f, 0.85f)));
            shaker.pattern = euclidRotated(onsets, shaker.cycle, rng.intRange(0, shaker.cycle - 1));
            shaker.accents = detail::accentMap(shaker.cycle, sub, rng);
            shaker.entry = 0.42f;
            shaker.level = 0.34f;
            shaker.pan = rng.range(0.30f, 0.62f);
            shaker.timingBias = 0.005f;
            shaker.jitter = 0.006f;
            shaker.ghostChance = 0.25f;
            // Same reasoning as the bell, kept higher so the rattle still
            // sits above the drums
            shaker.pitch = rng.range(0.52f, 0.78f);
            shaker.colour = rng.range(0.10f, 0.26f);
            layers_.push_back(shaker);
        }

        // The answering slap, placed on bell strokes in the back half of the bar
        {
            detail::DrumLayer slap;
            slap.voice = Voice::Slap;
            slap.cycle = pulses;
            slap.pattern.assign(static_cast<std::size_t>(pulses), false);
            for (int i = pulses / 2; i < pulses; ++i) {
                if (bellPattern_[static_cast<std::size_t>(i)] && rng.chance(0.45f)) {
                    slap.pattern[static_cast<std::size_t>(i)] = true;
                }
            }
            if (onsetCount(slap.pattern) == 0) {
                slap.pattern[static_cast<std::size_t>(pulses - sub)] = true;
            }
            slap.accents = detail::accentMap(pulses, sub, rng);
            slap.entry = 0.60f;
            slap.level = 0.80f;
            slap.pan = rng.range(0.10f, 0.38f);
            slap.timingBias = 0.003f;
            slap.ghostChance = 0.12f;
            slap.pitch = rng.range(1.0f, 1.35f);
            slap.colour = rng.range(0.30f, 0.55f);
            layers_.push_back(slap);
        }

        // Hands. Sparse, wide, and always doubled so it reads as several people
        {
            detail::DrumLayer clap;
            clap.voice = Voice::Clap;
            clap.cycle = pulses;
            clap.pattern.assign(static_cast<std::size_t>(pulses), false);
            const int firstBeat = sub * rng.intRange(1, 2);
            clap.pattern[static_cast<std::size_t>(firstBeat % pulses)] = true;
            clap.pattern[static_cast<std::size_t>((firstBeat + pulses / 2) % pulses)] = true;
            clap.accents = detail::accentMap(pulses, sub, rng);
            clap.entry = 0.76f;
            clap.level = 0.50f;
            clap.pan = 0.0f;
            clap.jitter = 0.008f;
            clap.colour = rng.range(0.45f, 0.70f);
            layers_.push_back(clap);
        }
    }

    // ---- pitched material -------------------------------------------------

    void buildMelodicMaterial(const Composition &out) {
        Rng rng = rng_.branch(0x50A6u);
        const int pulses = out.pulsesPerBar;

        // Balafon ostinato: an even rhythm with a walking line over it
        const int balafonOnsets = rng.intRange(4, 6);
        const int balafonRotation = rng.intRange(0, pulses - 1);
        balafonRhythm_ = euclidRotated(balafonOnsets, pulses, balafonRotation);
        balafonLine_.clear();
        int degree = rng.pick(std::vector<int>{0, 0, 2, 4});
        for (int i = 0; i < onsetCount(balafonRhythm_); ++i) {
            balafonLine_.push_back(degree);
            // Small steps most of the time, with the occasional leap
            const int step = rng.chance(0.72f) ? rng.intRange(-1, 1) : rng.intRange(-3, 3);
            degree = std::max(-3, std::min(degree + step, 9));
        }
        if (!balafonLine_.empty()) balafonLine_[0] = 0;

        // Kalimba fills the gaps the balafon leaves
        const int kalimbaOnsets = rng.intRange(3, 5);
        const int kalimbaRotation = rng.intRange(0, pulses - 1);
        const std::vector<bool> kalimbaBase = euclid(kalimbaOnsets, pulses);
        // Taking the balafon's pulses out is what makes the two interlock, but
        // one rotation in ten lands entirely underneath it and leaves nothing
        // at all. Turn the cycle until something survives
        kalimbaRhythm_.clear();
        for (int offset = 0; offset < pulses; ++offset) {
            const int rotation = (kalimbaRotation + offset) % pulses;
            const std::vector<bool> candidate = without(rotate(kalimbaBase, rotation), balafonRhythm_);
            if (onsetCount(candidate) > 0) {
                kalimbaRhythm_ = candidate;
                break;
            }
        }
        // The balafon covers every pulse, so interlocking is not on offer
        if (kalimbaRhythm_.empty()) kalimbaRhythm_ = rotate(kalimbaBase, kalimbaRotation);
        kalimbaLine_.clear();
        degree = rng.intRange(2, 6);
        for (int i = 0; i < onsetCount(kalimbaRhythm_); ++i) {
            kalimbaLine_.push_back(degree);
            degree = std::max(0, std::min(degree + rng.intRange(-2, 2), 11));
        }

        // A slow modal movement for the sustained voices, one chord per two bars
        static const std::vector<std::vector<int>> kProgressions = {
            {0, 0, 4, 3}, {0, 3, 0, 4}, {0, 6, 4, 0}, {0, 0, 2, 4}, {0, 4, 3, 0},
        };
        progression_ = rng.pick(kProgressions);
        balafonOctave_ = rng.chance(0.5f) ? 12 : 24;
    }

    // ---- laying the parts out over the arrangement -------------------------

    void renderSections(Composition &out) {
        Rng rng = rng_.branch(0xA22A9u);
        int barIndex = 0;

        for (std::size_t sectionIndex = 0; sectionIndex < out.sections.size(); ++sectionIndex) {
            const Section &section = out.sections[sectionIndex];
            for (int bar = 0; bar < section.bars; ++bar, ++barIndex) {
                const double barStart = static_cast<double>(barIndex) * out.barSeconds;
                const bool lastBarOfSection = (bar == section.bars - 1);

                if (!section.drumsMuted) {
                    emitDrums(out, section, barIndex, barStart, rng);
                    const bool leadsSomewhere = sectionIndex + 1 < out.sections.size() ||
                                                out.loopSeconds > 0.0;
                    if (lastBarOfSection && section.intensity > kFillIntensity - 0.15f && leadsSomewhere) {
                        emitFill(out, section, barStart, rng);
                    }
                }
                emitMelodic(out, section, bar, barIndex, barStart, rng);
            }
        }

        emitSustained(out, rng);
    }

    void emitDrums(Composition &out, const Section &section, int barIndex,
                   double barStart, Rng &rng) {
        for (const detail::DrumLayer &layer : layers_) {
            if (section.intensity < layer.entry) continue;
            // Just past its entry point a layer plays quietly and misses strokes
            const float settled = std::min(1.0f, (section.intensity - layer.entry) / 0.22f);

            for (int pulse = 0; pulse < out.pulsesPerBar; ++pulse) {
                const int globalPulse = barIndex * out.pulsesPerBar + pulse;
                const int step = ((globalPulse % layer.cycle) + layer.cycle) % layer.cycle;

                const bool onset = layer.pattern[static_cast<std::size_t>(step)];
                const bool ghost = !onset && layer.ghostChance > 0.0f &&
                                   rng.chance(layer.ghostChance * settled * section.intensity);
                if (!onset && !ghost) continue;
                if (onset && rng.chance(layer.dropChance * (1.0f - settled * 0.5f))) continue;

                Note note;
                note.voice = layer.voice;
                note.time = barStart + pulseTime(pulse, out) +
                            static_cast<double>(layer.timingBias) +
                            static_cast<double>(rng.centred() * layer.jitter);
                if (note.time < 0.0) note.time = 0.0;

                const float accent = layer.accents[static_cast<std::size_t>(step)];
                float velocity = layer.level * accent * (0.55f + 0.45f * section.intensity) * settled;
                if (ghost) velocity *= 0.34f;
                velocity *= 0.90f + 0.14f * rng.uniform();
                note.velocity = clamp01(velocity);
                note.pitch = layer.pitch;
                note.pan = layer.pan;
                note.colour = layer.colour;
                note.minIntensity = layer.entry;
                out.notes.push_back(note);

                // Hands never land together, so the clap is two strokes a few
                // milliseconds apart
                if (layer.voice == Voice::Clap && !ghost) {
                    Note second = note;
                    second.time += 0.011 + 0.009 * static_cast<double>(rng.uniform());
                    second.velocity *= 0.72f;
                    second.pan = -note.pan - 0.35f;
                    out.notes.push_back(second);
                    Note third = note;
                    third.time += 0.024 + 0.012 * static_cast<double>(rng.uniform());
                    third.velocity *= 0.5f;
                    third.pan = 0.4f;
                    out.notes.push_back(third);
                }
            }
        }
    }

    // A run of strokes down the drums to push into the next section
    void emitFill(Composition &out, const Section &section, double barStart, Rng &rng) {
        const int pulses = out.pulsesPerBar;
        const int start = pulses - out.subdivision * rng.intRange(1, 2);
        const std::vector<Voice> order = {Voice::HighDrum, Voice::HighDrum, Voice::MidDrum,
                                          Voice::MidDrum, Voice::LowDrum, Voice::LowDrum};
        const int strokes = (pulses - start) * 2;
        for (int i = 0; i < strokes; ++i) {
            Note note;
            note.voice = order[static_cast<std::size_t>(i) % order.size()];
            const double pulsePosition = static_cast<double>(start) + 0.5 * static_cast<double>(i);
            note.time = barStart + pulsePosition * pulseSeconds_ +
                        static_cast<double>(rng.centred() * 0.004f);
            // Rising into the downbeat
            const float ramp = static_cast<float>(i) / static_cast<float>(std::max(1, strokes - 1));
            note.velocity = clamp01((0.52f + 0.42f * ramp) * (0.6f + 0.4f * section.intensity));
            note.pitch = 1.7f - 0.55f * ramp;
            note.pan = rng.range(-0.45f, 0.45f);
            note.colour = 0.35f;
            note.minIntensity = kFillIntensity;
            out.notes.push_back(note);
        }
    }

    void emitMelodic(Composition &out, const Section &section, int barInSection,
                     int barIndex, double barStart, Rng &rng) {
        const int pulses = out.pulsesPerBar;

        // Balafon carries the tune from the middle of the piece onward
        if (section.intensity >= kBalafonIntensity) {
            int onsetIndex = 0;
            for (int pulse = 0; pulse < pulses; ++pulse) {
                if (!balafonRhythm_[static_cast<std::size_t>(pulse)]) continue;
                const int degreeIndex = onsetIndex++ % std::max<int>(1, static_cast<int>(balafonLine_.size()));
                if (balafonLine_.empty()) break;
                int degree = balafonLine_[static_cast<std::size_t>(degreeIndex)];
                // Every fourth bar the line is nudged, so it never quite repeats
                if ((barIndex % 4) == 3 && rng.chance(0.35f)) degree += rng.intRange(-1, 2);
                if (rng.chance(0.07f)) continue;  // an occasional rest

                Note note;
                note.voice = Voice::Balafon;
                note.time = barStart + pulseTime(pulse, out) + static_cast<double>(rng.centred() * 0.005f);
                note.pitch = degreeToNote(scale_, root_ + balafonOctave_, degree);
                note.velocity = clamp01(rng.range(0.44f, 0.72f) * (0.55f + 0.45f * section.intensity));
                note.duration = static_cast<float>(pulseSeconds_ * 2.0);
                note.pan = rng.range(-0.30f, 0.10f);
                note.colour = rng.range(0.35f, 0.70f);
                note.minIntensity = kBalafonIntensity;
                out.notes.push_back(note);
            }
        }

        // Kalimba interlocks with it
        if (section.intensity >= kKalimbaIntensity && !kalimbaLine_.empty()) {
            int onsetIndex = 0;
            for (int pulse = 0; pulse < pulses; ++pulse) {
                if (!kalimbaRhythm_[static_cast<std::size_t>(pulse)]) continue;
                const int degree = kalimbaLine_[static_cast<std::size_t>(onsetIndex++ % kalimbaLine_.size())];
                if (rng.chance(0.18f)) continue;

                Note note;
                note.voice = Voice::Kalimba;
                note.time = barStart + pulseTime(pulse, out) + static_cast<double>(rng.centred() * 0.006f);
                note.pitch = degreeToNote(scale_, root_ + 24, degree);
                note.velocity = clamp01(rng.range(0.32f, 0.55f) * (0.5f + 0.5f * section.intensity));
                note.duration = static_cast<float>(pulseSeconds_ * 3.0);
                note.pan = rng.range(0.10f, 0.48f);
                note.colour = rng.range(0.40f, 0.75f);
                note.minIntensity = kKalimbaIntensity;
                out.notes.push_back(note);
            }
        }

        // The pipe calls across the top, mostly in the quiet stretches and at
        // the ends of phrases
        // The end of a four-bar phrase, or of a short section that never
        // reaches one. Without the second case a piece built from one-bar
        // sections has nowhere for the pipe to speak
        const bool phraseEnd = (barInSection % 4) == 3 || barInSection == section.bars - 1;
        const bool callBar = section.drumsMuted ? (barInSection % 2) == 1 || phraseEnd : phraseEnd;
        if (callBar && (section.intensity >= kFluteIntensity) && rng.chance(0.8f)) {
            const int noteCount = rng.intRange(2, 4);
            double position = static_cast<double>(rng.intRange(0, out.subdivision));
            int degree = rng.pick(std::vector<int>{4, 5, 6, 7});
            for (int i = 0; i < noteCount; ++i) {
                Note note;
                note.voice = Voice::Flute;
                note.time = barStart + position * pulseSeconds_;
                note.pitch = degreeToNote(scale_, root_ + 24, degree);
                note.duration = static_cast<float>(pulseSeconds_ * rng.range(1.6f, 3.4f));
                note.velocity = clamp01(rng.range(0.34f, 0.56f));
                note.pan = rng.range(-0.22f, 0.22f);
                note.colour = rng.range(0.40f, 0.80f);
                note.minIntensity = kFluteIntensity;
                out.notes.push_back(note);
                position += static_cast<double>(rng.intRange(1, out.subdivision + 1));
                if (position >= pulses) break;
                degree += rng.intRange(-2, 1);
                degree = std::max(0, std::min(degree, 10));
            }
        }
    }

    // Drone and chant are laid out in long blocks rather than per bar
    void emitSustained(Composition &out, Rng &rng) {
        int barIndex = 0;
        for (const Section &section : out.sections) {
            const double sectionStart = static_cast<double>(barIndex) * out.barSeconds;

            // The drone runs the whole way through, retriggered every few bars
            // so each breath has a fresh attack
            const int chunkBars = 4;
            for (int bar = 0; bar < section.bars; bar += chunkBars) {
                const int bars = std::min(chunkBars, section.bars - bar);
                Note note;
                note.voice = Voice::Drone;
                note.time = sectionStart + static_cast<double>(bar) * out.barSeconds;
                note.pitch = static_cast<float>(root_ - 12);
                if (rng.chance(0.18f)) note.pitch += 7.0f;  // lifts to the fifth
                note.duration = static_cast<float>(static_cast<double>(bars) * out.barSeconds);
                note.velocity = clamp01(0.30f + 0.30f * section.intensity);
                note.pan = rng.range(-0.12f, 0.12f);
                note.colour = rng.range(0.35f, 0.75f);
                note.minIntensity = 0.0f;  // the drone is always there
                out.notes.push_back(note);
            }

            // Voices come in once the piece has some weight, or in the hollow
            if (section.intensity >= kChantIntensity || section.drumsMuted) {
                const int chordBars = 2;
                int chordIndex = 0;
                for (int bar = 0; bar < section.bars; bar += chordBars, ++chordIndex) {
                    const int bars = std::min(chordBars, section.bars - bar);
                    const int degree = progression_[static_cast<std::size_t>(chordIndex) % progression_.size()];
                    // Sung an octave below the balafon. At root + 12 the two
                    // parts landed on identical frequencies and masked each other
                    const std::vector<float> chord = chordOn(scale_, root_, degree, 3);
                    for (std::size_t v = 0; v < chord.size(); ++v) {
                        Note note;
                        note.voice = Voice::Chant;
                        note.time = sectionStart + static_cast<double>(bar) * out.barSeconds +
                                    static_cast<double>(rng.uniform()) * 0.06;
                        note.pitch = chord[v];
                        note.duration = static_cast<float>(static_cast<double>(bars) * out.barSeconds * 0.92);
                        note.velocity = clamp01((0.22f + 0.20f * section.intensity) *
                                                  (v == 0 ? 1.0f : 0.78f));
                        note.pan = -0.62f + 0.62f * static_cast<float>(v);
                        note.minIntensity = kChantIntensity;
                        note.colour = rng.range(0.30f, 0.70f);
                        out.notes.push_back(note);
                    }
                }
            }

            barIndex += section.bars;
        }
    }

    // Pulse position in seconds, with a light swing on the offbeats
    double pulseTime(int pulse, const Composition &out) const {
        double time = static_cast<double>(pulse) * pulseSeconds_;
        const int positionInBeat = pulse % out.subdivision;
        if (positionInBeat != 0) {
            // Delaying the inner pulses slightly is what stops the grid
            // sounding like a sequencer
            time += pulseSeconds_ * 0.055 * (positionInBeat == 1 ? 1.0 : 0.5);
        }
        return time;
    }

    static float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

    ComposerSettings settings_;
    Rng rng_;
    Scale scale_;
    int root_ = 57;
    double pulseSeconds_ = 0.2;
    std::vector<detail::DrumLayer> layers_;
    std::vector<bool> bellPattern_;
    std::vector<bool> balafonRhythm_;
    std::vector<bool> kalimbaRhythm_;
    std::vector<int> balafonLine_;
    std::vector<int> kalimbaLine_;
    std::vector<int> progression_;
    int balafonOctave_ = 12;
};

inline Composition compose(const ComposerSettings &settings) {
    Composer composer(settings);
    return composer.compose();
}

}  // namespace music
}  // namespace bravebeats
