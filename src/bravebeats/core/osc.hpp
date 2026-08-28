#pragma once

// Oscillators for the melodic voices
//
// The sawtooth and pulse use PolyBLEP so the sharp corners do not fold
// harmonics back down the spectrum, which matters here because the drone and
// the chant both run bright sources through narrow filters

#include <cmath>

#include "bravebeats/core/dsp.hpp"

namespace bravebeats {
namespace dsp {

class Phasor {
public:
    void init(double sampleRate) { sampleRate_ = sampleRate; }
    void setFrequency(float hz) { increment_ = static_cast<float>(hz / sampleRate_); }
    void setPhase(float phase) { phase_ = phase - std::floor(phase); }

    float advance() {
        const float current = phase_;
        phase_ += increment_;
        if (phase_ >= 1.0f) phase_ -= 1.0f;
        else if (phase_ < 0.0f) phase_ += 1.0f;
        return current;
    }

    float phase() const { return phase_; }

protected:
    double sampleRate_ = 48000.0;
    float increment_ = 0.0f;
    float phase_ = 0.0f;
};

class SineOsc : public Phasor {
public:
    float process() { return std::sin(kTwoPi * advance()); }
};

// Correction term that rounds off the discontinuity in a naive waveform
inline float polyBlep(float t, float increment) {
    if (increment <= 0.0f) return 0.0f;
    if (t < increment) {
        t /= increment;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - increment) {
        t = (t - 1.0f) / increment;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

class SawOsc : public Phasor {
public:
    float process() {
        const float t = advance();
        return 2.0f * t - 1.0f - polyBlep(t, increment_);
    }
};

class PulseOsc : public Phasor {
public:
    void setWidth(float width) { width_ = clampf(width, 0.02f, 0.98f); }

    float process() {
        const float t = advance();
        float value = t < width_ ? 1.0f : -1.0f;
        value += polyBlep(t, increment_);
        float shifted = t - width_;
        if (shifted < 0.0f) shifted += 1.0f;
        value -= polyBlep(shifted, increment_);
        return value;
    }

private:
    float width_ = 0.5f;
};

// One decaying sinusoid. Struck instruments are built from a handful of these
class ModalResonator {
public:
    void init(double sampleRate) { osc_.init(sampleRate); sampleRate_ = sampleRate; }

    void strike(float frequencyHz, float amplitude, float decaySeconds, float startPhase) {
        osc_.setFrequency(frequencyHz);
        osc_.setPhase(startPhase);
        amplitude_ = amplitude;
        decay_ = t60Coefficient(sampleRate_, decaySeconds);
        envelope_ = 1.0f;
        // Above Nyquist the partial is silent rather than aliased down
        active_ = frequencyHz > 0.0f && frequencyHz < sampleRate_ * 0.48;
    }

    float process() {
        if (!active_) return 0.0f;
        envelope_ = flush(envelope_ * decay_);
        if (envelope_ < 1.0e-6f) { active_ = false; return 0.0f; }
        return osc_.process() * envelope_ * amplitude_;
    }

    bool active() const { return active_; }
    void stop() { active_ = false; envelope_ = 0.0f; }

private:
    SineOsc osc_;
    double sampleRate_ = 48000.0;
    float amplitude_ = 0.0f;
    float decay_ = 0.0f;
    float envelope_ = 0.0f;
    bool active_ = false;
};

}  // namespace dsp
}  // namespace bravebeats
