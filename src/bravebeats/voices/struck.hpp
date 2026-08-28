#pragma once

// Struck tuned bars: the wooden balafon and the metal kalimba tines
//
// Both are modal. A struck bar rings on a small set of inharmonic partials,
// and which ratios you use is most of what separates wood from metal. A free
// bar runs 1 : 2.76 : 5.40, but tuned instruments are carved underneath to
// pull the second mode up to two octaves, so the balafon here uses 1 : 4 : 9.2.
// A kalimba tine is clamped at one end, which gives the much wider
// 1 : 6.27 : 17.55 spread and its thinner, more bell-like tone

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "bravebeats/core/dsp.hpp"
#include "bravebeats/core/osc.hpp"

namespace bravebeats {
namespace voices {

struct StruckSpec {
    static constexpr int kMaxModes = 5;

    int modeCount;
    float ratio[kMaxModes];
    float level[kMaxModes];
    float decayScale[kMaxModes];  // relative to the fundamental's decay

    float baseDecaySeconds;       // fundamental decay at the reference note
    float decayTrackingOctaves;   // how much faster high notes die away

    float malletLevel;
    float malletDecaySeconds;
    float malletCentreHz;
    float malletQ;

    float resonatorQ;             // gourd or board resonance under the bar
    float resonatorMix;

    // Balafon gourds carry a spider-silk membrane that rattles on loud strokes
    float buzzAmount;
    float buzzThreshold;

    float outputTrim;
};

// Wooden bar over a tuned gourd
static constexpr StruckSpec kBalafonSpec = {
    4,
    {1.0f, 4.0f, 9.2f, 15.4f, 0.0f},
    {1.0f, 0.34f, 0.15f, 0.06f, 0.0f},
    {1.0f, 0.42f, 0.22f, 0.13f, 0.0f},
    0.95f,
    0.55f,
    0.48f, 0.0026f, 2400.0f, 1.0f,
    5.0f, 0.34f,
    0.30f, 0.42f,
    0.90f,
};

// Metal tine clamped at one end
static constexpr StruckSpec kKalimbaSpec = {
    4,
    {1.0f, 6.27f, 17.55f, 34.4f, 0.0f},
    {1.0f, 0.22f, 0.07f, 0.025f, 0.0f},
    {1.0f, 0.30f, 0.14f, 0.08f, 0.0f},
    1.70f,
    0.70f,
    0.22f, 0.0016f, 3400.0f, 1.4f,
    3.0f, 0.16f,
    0.0f, 1.0f,
    0.85f,
};

class StruckVoice {
public:
    void init(double sampleRate, const StruckSpec &spec, uint32_t seed = 0x8A12u) {
        sampleRate_ = sampleRate;
        spec_ = spec;
        for (int i = 0; i < StruckSpec::kMaxModes; ++i) modes_[i].init(sampleRate);
        noise_.seed(seed);
        malletFilter_.reset();
        resonator_.reset();
        buzzHighPass_.reset();
        blocker_.setCutoff(sampleRate, 12.0f);
        blocker_.reset();
        malletEnvelope_ = 0.0f;
        malletDecay_ = 0.0f;
        active_ = false;
    }

    // colour runs 0 to 1: dead and short at the bottom, ringing and bright at
    // the top. It is what makes repeated notes in an ostinato differ
    void trigger(float midiNote, float velocity, float colour) {
        const float fundamental = dsp::midiToHz(midiNote);
        if (!(fundamental > 0.0f) || fundamental > sampleRate_ * 0.45) { active_ = false; return; }

        colour = dsp::clampf(colour, 0.0f, 1.0f);
        velocity = dsp::clampf(velocity, 0.0f, 1.0f);

        // Short bars ring less, so decay follows pitch downward
        const float octavesAbove = std::log2(fundamental / 220.0f);
        const float tracking = std::pow(2.0f, -octavesAbove * spec_.decayTrackingOctaves);
        const float decay = spec_.baseDecaySeconds * tracking * (0.7f + 0.6f * colour);

        for (int i = 0; i < spec_.modeCount; ++i) {
            // A few cents of spread per mode keeps repeated strikes from
            // phasing into an obviously identical copy
            const float detune = 1.0f + noise_.process() * 0.0015f;
            const float partialHz = fundamental * spec_.ratio[i] * detune;
            // Harder strokes put more energy into the upper modes
            const float strikeWeight = i == 0 ? 1.0f : (0.45f + 0.75f * velocity) * (0.55f + 0.75f * colour);
            modes_[i].strike(partialHz,
                             spec_.level[i] * strikeWeight * velocity,
                             decay * spec_.decayScale[i],
                             i == 0 ? 0.0f : 0.25f * static_cast<float>(i));
        }
        for (int i = spec_.modeCount; i < StruckSpec::kMaxModes; ++i) modes_[i].stop();

        malletFilter_.setBandPass(sampleRate_, spec_.malletCentreHz * (0.8f + 0.5f * colour), spec_.malletQ);
        malletEnvelope_ = spec_.malletLevel * velocity;
        malletDecay_ = dsp::t60Coefficient(sampleRate_, spec_.malletDecaySeconds);

        resonator_.setBandPass(sampleRate_, fundamental, spec_.resonatorQ);
        buzzHighPass_.setHighPass(sampleRate_, 900.0f, 0.7071f);
        level_ = velocity;
        active_ = true;
    }

    float process() {
        if (!active_) return 0.0f;

        float sum = 0.0f;
        bool ringing = false;
        for (int i = 0; i < spec_.modeCount; ++i) {
            sum += modes_[i].process();
            ringing = ringing || modes_[i].active();
        }

        if (malletEnvelope_ > 1.0e-5f) {
            sum += malletFilter_.process(noise_.process()) * malletEnvelope_;
            malletEnvelope_ = dsp::flush(malletEnvelope_ * malletDecay_);
            ringing = true;
        }

        // The body under the bar, mixed in rather than filtering everything
        const float body = resonator_.process(sum);
        float out = sum + body * spec_.resonatorMix;

        if (spec_.buzzAmount > 0.0f) {
            // The membrane only rattles once the bar is driven hard enough
            const float driven = body * (2.2f + 3.0f * level_);
            const float excess = std::fabs(driven) - spec_.buzzThreshold;
            if (excess > 0.0f) {
                const float rattle = (driven > 0.0f ? excess : -excess);
                out += buzzHighPass_.process(dsp::softClip(rattle * 3.0f)) * spec_.buzzAmount;
            } else {
                buzzHighPass_.process(0.0f);
            }
        }

        if (!ringing) active_ = false;
        return blocker_.process(out) * spec_.outputTrim;
    }

    bool isActive() const { return active_; }
    void stop() {
        for (auto &mode : modes_) mode.stop();
        malletEnvelope_ = 0.0f;
        active_ = false;
    }

private:
    double sampleRate_ = 48000.0;
    StruckSpec spec_ = kBalafonSpec;
    dsp::ModalResonator modes_[StruckSpec::kMaxModes];
    dsp::Noise noise_;
    dsp::Biquad malletFilter_;
    dsp::Biquad resonator_;
    dsp::Biquad buzzHighPass_;
    dsp::DcBlocker blocker_;
    float malletEnvelope_ = 0.0f;
    float malletDecay_ = 0.0f;
    float level_ = 0.0f;
    bool active_ = false;
};

}  // namespace voices
}  // namespace bravebeats
