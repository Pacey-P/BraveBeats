#pragma once

// The percussion ensemble, played on the 606-inspired synth drum voices
//
// Those voices are a drum machine kit, and this is a drum circle, so each part
// is re-cast rather than used as named. The toms become the tuned hand drums
// and are the busiest thing here, the snare is played short and snappy as a
// rim slap, the closed hat stands in for a seed rattle and the open hat, tuned
// up and cut short, for the iron bell that carries the timeline
//
// Upstream: https://github.com/analogcode/606-Inspired-Synth-Drums (MIT)

#include <cstdint>

#include "BassDrum.hpp"
#include "Clap.hpp"
#include "HiHats.hpp"
#include "Snare.hpp"
#include "Toms.hpp"

#include "bravebeats/core/dsp.hpp"
#include "bravebeats/music/composer.hpp"

namespace bravebeats {
namespace voices {

// Round-robin pool. Hand drums overlap constantly once the fills start, so
// every part needs more than one voice to speak from
template <typename VoiceType, int Count>
class VoicePool {
public:
    struct Slot {
        VoiceType voice;
        float left = 0.7071f;
        float right = 0.7071f;
    };

    template <typename InitFn>
    void init(InitFn initialise) {
        for (int i = 0; i < Count; ++i) initialise(slots_[i].voice, i);
    }

    Slot &acquire(float pan) {
        int chosen = -1;
        for (int i = 0; i < Count; ++i) {
            const int index = (cursor_ + i) % Count;
            if (!slots_[index].voice.isActive()) { chosen = index; break; }
        }
        // Everything is still ringing, so the oldest one gives way
        if (chosen < 0) chosen = cursor_;
        cursor_ = (chosen + 1) % Count;
        dsp::panGains(pan, slots_[chosen].left, slots_[chosen].right);
        return slots_[chosen];
    }

    void process(float &left, float &right) {
        for (int i = 0; i < Count; ++i) {
            Slot &slot = slots_[i];
            if (!slot.voice.isActive()) continue;
            const float sample = slot.voice.process();
            left += sample * slot.left;
            right += sample * slot.right;
        }
    }

private:
    Slot slots_[Count];
    int cursor_ = 0;
};

// Level trims that put the parts in proportion to each other
// The hats in particular run hot out of the box and would sit on top of
// everything else if they were passed through untouched
struct DrumTrims {
    float kick = 0.70f;
    float lowDrum = 0.86f;
    float midDrum = 0.84f;
    float highDrum = 0.78f;
    float slap = 1.10f;
    float clap = 1.15f;
    float shaker = 1.30f;
    float bell = 1.00f;
};

class DrumKit {
public:
    void init(double sampleRate, uint32_t seed) {
        trims_ = DrumTrims();
        kick_.init([&](SynthDrums606::BassDrumVoice &voice, int i) {
            voice.init(sampleRate, seed + 0x101u + static_cast<uint32_t>(i));
        });
        lowDrum_.init([&](SynthDrums606::TomVoice &voice, int i) {
            voice.init(sampleRate, seed + 0x201u + static_cast<uint32_t>(i));
        });
        midDrum_.init([&](SynthDrums606::TomVoice &voice, int i) {
            voice.init(sampleRate, seed + 0x301u + static_cast<uint32_t>(i));
        });
        highDrum_.init([&](SynthDrums606::TomVoice &voice, int i) {
            voice.init(sampleRate, seed + 0x401u + static_cast<uint32_t>(i));
        });
        slap_.init([&](SynthDrums606::SnareVoice &voice, int i) {
            voice.init(sampleRate, seed + 0x501u + static_cast<uint32_t>(i));
        });
        clap_.init([&](SynthDrums606::ClapVoice &voice, int i) {
            voice.init(sampleRate, seed + 0x601u + static_cast<uint32_t>(i));
        });
        shaker_.init([&](SynthDrums606::MetalHiHatVoice &voice, int i) {
            voice.init(sampleRate, seed + 0x701u + static_cast<uint32_t>(i));
        });
        bell_.init([&](SynthDrums606::MetalHiHatVoice &voice, int i) {
            voice.init(sampleRate, seed + 0x801u + static_cast<uint32_t>(i));
        });
    }

    bool handles(music::Voice voice) const {
        switch (voice) {
            case music::Voice::Kick:
            case music::Voice::LowDrum:
            case music::Voice::MidDrum:
            case music::Voice::HighDrum:
            case music::Voice::Slap:
            case music::Voice::Clap:
            case music::Voice::Shaker:
            case music::Voice::Bell:
                return true;
            default:
                return false;
        }
    }

    void trigger(const music::Note &note) {
        const float velocity = dsp::clampf(note.velocity, 0.0f, 1.0f);
        const float colour = dsp::clampf(note.colour, 0.0f, 1.0f);
        const float pitch = note.pitch;

        switch (note.voice) {
            case music::Voice::Kick: {
                auto &slot = kick_.acquire(note.pan);
                // Little click, long body: a floor drum rather than a click track
                // More click than a drum machine kick wants: it has to be
                // heard through the ensemble without taking the whole low end
                const float transient = 0.30f + 0.38f * velocity;
                // A hand-struck floor drum is damped by the player, so the
                // tail is much shorter than the machine's default
                const float decay = 0.34f + 0.28f * colour;
                // Tuned up from the machine's own range. A floor drum sits
                // around 70 Hz, not 50, and down there it is felt rather than
                // heard on most systems
                const float tune = 1.5f + 6.0f * (pitch - 1.0f) + 2.0f * colour;
                slot.voice.trigger(transient, decay, tune, (velocity - 0.5f) * 0.9f);
                applyGain(slot, trims_.kick * velocity);
                break;
            }
            case music::Voice::LowDrum: {
                auto &slot = lowDrum_.acquire(note.pan);
                slot.voice.trigger(SynthDrums606::kLowTomSpec, 0.45f + 0.45f * colour,
                                   dsp::clampf(pitch, 0.25f, 4.0f));
                applyGain(slot, trims_.lowDrum * velocity);
                break;
            }
            case music::Voice::MidDrum: {
                auto &slot = midDrum_.acquire(note.pan);
                slot.voice.trigger(SynthDrums606::kLowTomSpec, 0.35f + 0.40f * colour,
                                   dsp::clampf(pitch, 0.25f, 4.0f));
                applyGain(slot, trims_.midDrum * velocity);
                break;
            }
            case music::Voice::HighDrum: {
                auto &slot = highDrum_.acquire(note.pan);
                slot.voice.trigger(SynthDrums606::kHighTomSpec, 0.24f + 0.36f * colour,
                                   dsp::clampf(pitch, 0.25f, 4.0f));
                applyGain(slot, trims_.highDrum * velocity);
                break;
            }
            case music::Voice::Slap: {
                auto &slot = slap_.acquire(note.pan);
                // Short and wiry, which reads as a hand slap on a drum head
                slot.voice.trigger(0.18f + 0.30f * colour,
                                   dsp::clampf(pitch, 0.5f, 2.0f),
                                   0.55f + 0.40f * velocity,
                                   1.10f + 0.25f * colour);
                applyGain(slot, trims_.slap * velocity);
                break;
            }
            case music::Voice::Clap: {
                auto &slot = clap_.acquire(note.pan);
                slot.voice.trigger(0.45f + 0.45f * colour, dsp::clampf(pitch, 0.5f, 2.0f),
                                   0.40f + 0.35f * colour);
                applyGain(slot, trims_.clap * velocity);
                break;
            }
            case music::Voice::Shaker: {
                auto &slot = shaker_.acquire(note.pan);
                slot.voice.trigger(SynthDrums606::kClosedHatSpec, 0.10f + 0.35f * colour,
                                   dsp::clampf(pitch, 0.5f, 4.0f));
                applyGain(slot, trims_.shaker * velocity);
                break;
            }
            case music::Voice::Bell: {
                auto &slot = bell_.acquire(note.pan);
                // The open hat cut short and tuned up rings like struck iron
                slot.voice.trigger(SynthDrums606::kOpenHatSpec, 0.16f + 0.26f * colour,
                                   dsp::clampf(pitch, 0.5f, 4.0f));
                applyGain(slot, trims_.bell * velocity);
                break;
            }
            default:
                break;
        }
    }

    // Each part is summed separately so the mix can send them to the room in
    // different amounts
    void processPart(music::Voice voice, float &left, float &right) {
        switch (voice) {
            case music::Voice::Kick: kick_.process(left, right); break;
            case music::Voice::LowDrum: lowDrum_.process(left, right); break;
            case music::Voice::MidDrum: midDrum_.process(left, right); break;
            case music::Voice::HighDrum: highDrum_.process(left, right); break;
            case music::Voice::Slap: slap_.process(left, right); break;
            case music::Voice::Clap: clap_.process(left, right); break;
            case music::Voice::Shaker: shaker_.process(left, right); break;
            case music::Voice::Bell: bell_.process(left, right); break;
            default: break;
        }
    }

private:
    template <typename Slot>
    static void applyGain(Slot &slot, float gain) {
        slot.left *= gain;
        slot.right *= gain;
    }

    VoicePool<SynthDrums606::BassDrumVoice, 2> kick_;
    VoicePool<SynthDrums606::TomVoice, 3> lowDrum_;
    VoicePool<SynthDrums606::TomVoice, 3> midDrum_;
    VoicePool<SynthDrums606::TomVoice, 4> highDrum_;
    VoicePool<SynthDrums606::SnareVoice, 3> slap_;
    VoicePool<SynthDrums606::ClapVoice, 3> clap_;
    VoicePool<SynthDrums606::MetalHiHatVoice, 4> shaker_;
    VoicePool<SynthDrums606::MetalHiHatVoice, 3> bell_;
    DrumTrims trims_;
};

}  // namespace voices
}  // namespace bravebeats
