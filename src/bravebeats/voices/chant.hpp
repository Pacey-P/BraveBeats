#pragma once

// Sung voices, built by formant synthesis
//
// A vowel is mostly three resonances sitting on top of a buzzing glottal
// source, so that is how this is put together: a band-limited saw rolled off
// to roughly -12 dB per octave, then three parallel bandpasses at the formant
// frequencies. The note slides from one vowel towards another while it is
// held, which is what stops a long sustain sounding like a filter sweep.
//
// Three slightly detuned copies run at once. Real singers never land on the
// same pitch, and that spread is most of what makes a group sound like a group

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "bravebeats/core/dsp.hpp"
#include "bravebeats/core/osc.hpp"

namespace bravebeats {
namespace voices {

struct Vowel {
    float formantHz[3];
    float formantGain[3];
    float q[3];
};

// Measured formant centres for open and closed vowels
static constexpr Vowel kVowelAh = {{730.0f, 1090.0f, 2440.0f}, {1.0f, 0.45f, 0.25f}, {7.0f, 9.0f, 11.0f}};
static constexpr Vowel kVowelOh = {{570.0f, 840.0f, 2410.0f}, {1.0f, 0.50f, 0.16f}, {7.5f, 9.5f, 12.0f}};
static constexpr Vowel kVowelOo = {{300.0f, 870.0f, 2240.0f}, {1.0f, 0.28f, 0.10f}, {8.0f, 10.0f, 12.0f}};

class ChantVoice {
public:
    void init(double sampleRate, uint32_t seed = 0xC4A7u) {
        sampleRate_ = sampleRate;
        for (int i = 0; i < kUnison; ++i) {
            source_[i].init(sampleRate);
            vibrato_[i].init(sampleRate);
        }
        envelope_.init(sampleRate);
        noise_.seed(seed);
        for (auto &formant : formants_) formant.reset();
        glottalShaper_.reset();
        breathFilter_.reset();
        blocker_.setCutoff(sampleRate, 25.0f);
        blocker_.reset();
        active_ = false;
    }

    void trigger(float midiNote, float velocity, float durationSeconds, float colour) {
        const float hz = dsp::midiToHz(midiNote);
        if (!(hz > 0.0f) || hz > 1200.0f) { active_ = false; return; }

        colour = dsp::clampf(colour, 0.0f, 1.0f);
        velocity = dsp::clampf(velocity, 0.0f, 1.0f);
        baseHz_ = hz;

        // Open vowels for the brighter settings, closed for the darker ones
        startVowel_ = colour > 0.5f ? kVowelAh : kVowelOo;
        endVowel_ = colour > 0.5f ? kVowelOh : kVowelAh;

        static const float kDetuneCents[kUnison] = {-7.0f, 0.0f, 9.0f};
        for (int i = 0; i < kUnison; ++i) {
            source_[i].setFrequency(hz * std::pow(2.0f, kDetuneCents[i] / 1200.0f));
            source_[i].setPhase(0.17f * static_cast<float>(i) + 0.05f);
            vibrato_[i].setFrequency(4.6f + 0.55f * static_cast<float>(i));
            vibrato_[i].setPhase(0.31f * static_cast<float>(i));
        }

        const float attack = 0.28f + 0.45f * (1.0f - velocity);
        const float release = 0.55f + 0.5f * colour;
        envelope_.trigger(velocity, attack, std::max(durationSeconds - attack, 0.15f), release);

        // A tilt on the source, rather than on the output, so the formants
        // still sit where they should
        glottalShaper_.setLowPass(sampleRate_, hz * 3.0f + 240.0f, 0.6f);
        breathFilter_.setBandPass(sampleRate_, 2600.0f, 0.8f);

        morph_ = 0.0f;
        morphRate_ = 1.0f / std::max(durationSeconds * static_cast<float>(sampleRate_), 1.0f);
        formantCounter_ = 0;
        updateFormants();
        active_ = true;
    }

    float process() {
        if (!active_) return 0.0f;

        const float amplitude = envelope_.process();
        if (!envelope_.active()) { active_ = false; return 0.0f; }

        morph_ = std::min(morph_ + morphRate_, 1.0f);
        if (++formantCounter_ >= kFormantUpdateInterval) {
            formantCounter_ = 0;
            updateFormants();
        }

        float glottal = 0.0f;
        for (int i = 0; i < kUnison; ++i) {
            const float wobble = vibrato_[i].process() * 0.0045f;
            static const float kDetuneCents[kUnison] = {-7.0f, 0.0f, 9.0f};
            source_[i].setFrequency(baseHz_ * std::pow(2.0f, kDetuneCents[i] / 1200.0f) * (1.0f + wobble));
            glottal += source_[i].process();
        }
        glottal *= 1.0f / static_cast<float>(kUnison);
        glottal = glottalShaper_.process(glottal);

        float shaped = 0.0f;
        for (int i = 0; i < 3; ++i) shaped += formants_[i].process(glottal) * formantGain_[i];

        // Air escaping around the voice
        shaped += breathFilter_.process(noise_.process()) * 0.045f;

        return blocker_.process(shaped * amplitude) * 0.95f;
    }

    bool isActive() const { return active_; }
    void stop() { envelope_.stop(); active_ = false; }

private:
    static constexpr int kUnison = 3;
    static constexpr int kFormantUpdateInterval = 128;

    void updateFormants() {
        for (int i = 0; i < 3; ++i) {
            const float hz = dsp::lerpf(startVowel_.formantHz[i], endVowel_.formantHz[i], morph_);
            const float q = dsp::lerpf(startVowel_.q[i], endVowel_.q[i], morph_);
            formants_[i].setBandPass(sampleRate_, hz, q);
            formantGain_[i] = dsp::lerpf(startVowel_.formantGain[i], endVowel_.formantGain[i], morph_);
        }
    }

    double sampleRate_ = 48000.0;
    dsp::SawOsc source_[kUnison];
    dsp::SineOsc vibrato_[kUnison];
    dsp::SustainEnvelope envelope_;
    dsp::Noise noise_;
    dsp::Biquad formants_[3];
    dsp::Biquad glottalShaper_, breathFilter_;
    dsp::DcBlocker blocker_;
    Vowel startVowel_ = kVowelAh;
    Vowel endVowel_ = kVowelOh;
    float formantGain_[3] = {1.0f, 0.5f, 0.25f};
    float baseHz_ = 220.0f;
    float morph_ = 0.0f;
    float morphRate_ = 0.0001f;
    int formantCounter_ = 0;
    bool active_ = false;
};

}  // namespace voices
}  // namespace bravebeats
