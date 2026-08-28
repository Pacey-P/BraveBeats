#pragma once

// The blown voices: an end-blown pipe and a low drone
//
// The pipe is a tone plus a lot of moving air. Most of what makes a flute
// recognisable is the noise sitting on top of the note, so the breath here is
// filtered around the note itself and fades as the note settles.
//
// The drone is a buzzed-lip source, closer to a didgeridoo than a synth pad:
// a bright pulse train pushed through three fixed resonances, with a slow
// sweep that opens and closes them. Circular breathing shows up as periodic
// pushes of extra pressure rather than a break in the sound

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "bravebeats/core/dsp.hpp"
#include "bravebeats/core/osc.hpp"

namespace bravebeats {
namespace voices {

class FluteVoice {
public:
    void init(double sampleRate, uint32_t seed = 0xF1B7u) {
        sampleRate_ = sampleRate;
        tone_.init(sampleRate);
        second_.init(sampleRate);
        third_.init(sampleRate);
        vibrato_.init(sampleRate);
        vibrato_.setFrequency(5.2f);
        envelope_.init(sampleRate);
        breathEnvelope_.init(sampleRate);
        noise_.seed(seed);
        breathFilter_.reset();
        bodyFilter_.reset();
        blocker_.setCutoff(sampleRate, 20.0f);
        blocker_.reset();
        pitchGlide_.setTime(sampleRate, 0.045f);
        active_ = false;
    }

    void trigger(float midiNote, float velocity, float durationSeconds, float colour) {
        const float hz = dsp::midiToHz(midiNote);
        if (!(hz > 0.0f) || hz > sampleRate_ * 0.4) { active_ = false; return; }

        colour = dsp::clampf(colour, 0.0f, 1.0f);
        velocity = dsp::clampf(velocity, 0.0f, 1.0f);
        targetHz_ = hz;
        // Players arrive slightly under the note and settle up onto it
        pitchGlide_.reset(hz * 0.978f);

        const float attack = 0.035f + 0.045f * (1.0f - velocity);
        const float release = 0.10f + 0.16f * colour;
        const float hold = std::max(durationSeconds - attack - release * 0.4f, 0.05f);
        envelope_.trigger(velocity, attack, hold, release);
        // The breath is loudest at the start of the note, then thins out
        breathEnvelope_.trigger(velocity, attack * 0.5f, hold * 0.35f, release * 1.4f);

        breathFilter_.setBandPass(sampleRate_, hz * (1.8f + 0.9f * colour), 1.1f);
        bodyFilter_.setLowPass(sampleRate_, hz * (5.0f + 6.0f * colour), 0.72f);
        second_.setPhase(0.13f);
        third_.setPhase(0.41f);
        vibrato_.setPhase(0.0f);
        harmonicMix_ = 0.16f + 0.26f * colour;
        breathMix_ = 0.30f + 0.24f * (1.0f - velocity);
        elapsed_ = 0.0f;
        active_ = true;
    }

    float process() {
        if (!active_) return 0.0f;

        const float amplitude = envelope_.process();
        if (!envelope_.active()) { active_ = false; return 0.0f; }

        // Vibrato only opens up after the note has been held a moment
        const float vibratoDepth = std::min(elapsed_ / 0.45f, 1.0f) * 0.006f;
        const float wobble = vibrato_.process() * vibratoDepth;
        const float hz = pitchGlide_.process(targetHz_) * (1.0f + wobble);

        tone_.setFrequency(hz);
        second_.setFrequency(hz * 2.0f);
        third_.setFrequency(hz * 3.0f);

        float voiced = tone_.process();
        voiced += second_.process() * harmonicMix_;
        voiced += third_.process() * harmonicMix_ * 0.35f;

        const float breath = breathFilter_.process(noise_.process()) * breathEnvelope_.process() * breathMix_;
        const float out = bodyFilter_.process(voiced * amplitude + breath);

        elapsed_ += static_cast<float>(1.0 / sampleRate_);
        return blocker_.process(out) * 0.55f;
    }

    bool isActive() const { return active_; }
    void stop() { envelope_.stop(); breathEnvelope_.stop(); active_ = false; }

private:
    double sampleRate_ = 48000.0;
    dsp::SineOsc tone_, second_, third_, vibrato_;
    dsp::SustainEnvelope envelope_, breathEnvelope_;
    dsp::Noise noise_;
    dsp::Biquad breathFilter_, bodyFilter_;
    dsp::DcBlocker blocker_;
    dsp::OnePole pitchGlide_;
    float targetHz_ = 440.0f;
    float harmonicMix_ = 0.2f;
    float breathMix_ = 0.3f;
    float elapsed_ = 0.0f;
    bool active_ = false;
};

class DroneVoice {
public:
    void init(double sampleRate, uint32_t seed = 0xD40Eu) {
        sampleRate_ = sampleRate;
        source_.init(sampleRate);
        source_.setWidth(0.28f);
        sub_.init(sampleRate);
        sweep_.init(sampleRate);
        breathLfo_.init(sampleRate);
        envelope_.init(sampleRate);
        noise_.seed(seed);
        for (auto &formant : formants_) formant.reset();
        toneShaper_.reset();
        // Keeps the drone out of the region where it would mask the drums
        rumbleFilter_.setHighPass(sampleRate, 52.0f, 0.7071f);
        rumbleFilter_.reset();
        blocker_.setCutoff(sampleRate, 18.0f);
        blocker_.reset();
        active_ = false;
    }

    void trigger(float midiNote, float velocity, float durationSeconds, float colour) {
        const float hz = dsp::midiToHz(midiNote);
        if (!(hz > 0.0f) || hz > 400.0f) { active_ = false; return; }

        colour_ = dsp::clampf(colour, 0.0f, 1.0f);
        velocity = dsp::clampf(velocity, 0.0f, 1.0f);
        baseHz_ = hz;
        source_.setFrequency(hz);
        // Deliberately not an octave down: at these roots that lands near
        // 23 Hz, which is inaudible and eats the whole headroom budget
        sub_.setFrequency(hz);

        const float attack = 0.35f + 0.5f * (1.0f - velocity);
        const float release = 0.9f;
        envelope_.trigger(velocity, attack, std::max(durationSeconds - attack, 0.2f), release);

        // Two slow movements, deliberately not in step with each other
        sweep_.setFrequency(0.07f + 0.09f * colour_);
        sweep_.setPhase(0.0f);
        breathLfo_.setFrequency(0.31f + 0.22f * colour_);
        breathLfo_.setPhase(0.25f);

        toneShaper_.setLowPass(sampleRate_, 240.0f + 900.0f * colour_, 0.8f);
        updateFormants(0.0f);
        formantCounter_ = 0;
        active_ = true;
    }

    float process() {
        if (!active_) return 0.0f;

        const float amplitude = envelope_.process();
        if (!envelope_.active()) { active_ = false; return 0.0f; }

        const float sweepValue = sweep_.process();
        // Retuning biquads every sample is wasted work on a 0.1 Hz sweep
        if (++formantCounter_ >= kFormantUpdateInterval) {
            formantCounter_ = 0;
            updateFormants(sweepValue);
        }

        // Pressure pushes from circular breathing, never down to silence
        const float breath = 0.82f + 0.18f * breathLfo_.process();

        float source = source_.process() * 0.6f + sub_.process() * 0.22f;
        source += noise_.process() * 0.08f;
        source = toneShaper_.process(source);

        float shaped = 0.0f;
        for (int i = 0; i < kFormantCount; ++i) {
            shaped += formants_[i].process(source) * formantGain_[i];
        }
        // A little drive is what gives the buzz its edge
        shaped = dsp::softClip(shaped * (1.4f + 0.9f * colour_));

        return rumbleFilter_.process(blocker_.process(shaped * amplitude * breath)) * 0.55f;
    }

    bool isActive() const { return active_; }
    void stop() { envelope_.stop(); active_ = false; }

private:
    static constexpr int kFormantCount = 3;
    static constexpr int kFormantUpdateInterval = 64;

    void updateFormants(float sweep) {
        // Roughly where a long wooden tube resonates, moved by the mouth
        const float centres[kFormantCount] = {
            baseHz_ * 3.4f + 90.0f * sweep,
            760.0f + 260.0f * sweep + 240.0f * colour_,
            1850.0f + 420.0f * sweep,
        };
        const float qs[kFormantCount] = {4.5f, 7.0f, 9.0f};
        static const float gains[kFormantCount] = {1.0f, 0.55f, 0.28f};
        for (int i = 0; i < kFormantCount; ++i) {
            formants_[i].setBandPass(sampleRate_, centres[i], qs[i]);
            formantGain_[i] = gains[i];
        }
    }

    double sampleRate_ = 48000.0;
    dsp::PulseOsc source_;
    dsp::SineOsc sub_, sweep_, breathLfo_;
    dsp::SustainEnvelope envelope_;
    dsp::Noise noise_;
    dsp::Biquad formants_[kFormantCount];
    dsp::Biquad toneShaper_;
    dsp::Biquad rumbleFilter_;
    dsp::DcBlocker blocker_;
    float formantGain_[kFormantCount] = {1.0f, 0.5f, 0.25f};
    float baseHz_ = 55.0f;
    float colour_ = 0.5f;
    int formantCounter_ = 0;
    bool active_ = false;
};

}  // namespace voices
}  // namespace bravebeats
