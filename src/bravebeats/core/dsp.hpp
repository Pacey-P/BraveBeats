#pragma once

// The small signal-processing pieces the voices and the mix bus share
// Nothing here is specific to tribal music, it is just the toolbox

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bravebeats {
namespace dsp {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// Denormals cost real time in the long reverb tails
inline float flush(float x) { return std::fabs(x) < 1.0e-25f ? 0.0f : x; }

inline float dbToGain(float db) { return std::pow(10.0f, db * 0.05f); }
inline float gainToDb(float gain) { return 20.0f * std::log10(std::max(gain, 1.0e-9f)); }

// MIDI note 69 is A440, matching the tuning used by the note helpers
inline float midiToHz(float note, float a4Hz = 440.0f) {
    return a4Hz * std::pow(2.0f, (note - 69.0f) / 12.0f);
}

// Time constant for an exponential decay reaching -60 dB after `seconds`
inline float t60Coefficient(double sampleRate, float seconds) {
    if (seconds <= 0.0f) return 0.0f;
    return static_cast<float>(std::exp(-6.907755278982137 / (sampleRate * seconds)));
}

inline float onePoleCoefficient(double sampleRate, float seconds) {
    if (seconds <= 0.0f) return 0.0f;
    return static_cast<float>(std::exp(-1.0 / (sampleRate * seconds)));
}

// Odd-symmetric soft clip. Linear near zero, so quiet passages stay clean
inline float softClip(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

// Constant-power pan. -1 hard left, +1 hard right
inline void panGains(float pan, float &left, float &right) {
    const float angle = (clampf(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    left = std::cos(angle);
    right = std::sin(angle);
}

class OnePole {
public:
    void setCoefficient(float coefficient) { a_ = clampf(coefficient, 0.0f, 0.9999999f); }
    void setTime(double sampleRate, float seconds) { setCoefficient(onePoleCoefficient(sampleRate, seconds)); }
    void setCutoff(double sampleRate, float hz) {
        const float x = std::exp(-kTwoPi * std::max(hz, 0.1f) / static_cast<float>(sampleRate));
        setCoefficient(clampf(x, 0.0f, 0.9999999f));
    }
    void reset(float value = 0.0f) { z_ = value; }
    float process(float x) { z_ = flush(x + a_ * (z_ - x)); return z_; }

private:
    float a_ = 0.0f;
    float z_ = 0.0f;
};

class DcBlocker {
public:
    void reset() { x1_ = y1_ = 0.0f; }
    void setCutoff(double sampleRate, float hz) {
        r_ = 1.0f - (kTwoPi * hz / static_cast<float>(sampleRate));
        r_ = clampf(r_, 0.0f, 0.99999f);
    }
    float process(float x) {
        const float y = x - x1_ + r_ * y1_;
        x1_ = x;
        y1_ = flush(y);
        return y1_;
    }

private:
    float r_ = 0.995f;
    float x1_ = 0.0f;
    float y1_ = 0.0f;
};

// Transposed direct form II biquad, RBJ cookbook coefficients
class Biquad {
public:
    void reset() { z1_ = z2_ = 0.0f; }

    void setLowPass(double sampleRate, float hz, float q) {
        float w0, cs, sn, alpha;
        prepare(sampleRate, hz, q, w0, cs, sn, alpha);
        const float b1 = 1.0f - cs;
        set(b1 * 0.5f, b1, b1 * 0.5f, 1.0f + alpha, -2.0f * cs, 1.0f - alpha);
    }

    void setHighPass(double sampleRate, float hz, float q) {
        float w0, cs, sn, alpha;
        prepare(sampleRate, hz, q, w0, cs, sn, alpha);
        const float b0 = (1.0f + cs) * 0.5f;
        set(b0, -(1.0f + cs), b0, 1.0f + alpha, -2.0f * cs, 1.0f - alpha);
    }

    void setBandPass(double sampleRate, float hz, float q) {
        float w0, cs, sn, alpha;
        prepare(sampleRate, hz, q, w0, cs, sn, alpha);
        set(alpha, 0.0f, -alpha, 1.0f + alpha, -2.0f * cs, 1.0f - alpha);
    }

    void setHighShelf(double sampleRate, float hz, float gainDb) {
        float w0, cs, sn, alpha;
        prepare(sampleRate, hz, 0.7071f, w0, cs, sn, alpha);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float beta = 2.0f * std::sqrt(a) * alpha;
        set(a * ((a + 1.0f) + (a - 1.0f) * cs + beta),
            -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cs),
            a * ((a + 1.0f) + (a - 1.0f) * cs - beta),
            (a + 1.0f) - (a - 1.0f) * cs + beta,
            2.0f * ((a - 1.0f) - (a + 1.0f) * cs),
            (a + 1.0f) - (a - 1.0f) * cs - beta);
    }

    float process(float x) {
        const float y = b0_ * x + z1_;
        z1_ = flush(b1_ * x - a1_ * y + z2_);
        z2_ = flush(b2_ * x - a2_ * y);
        return y;
    }

private:
    static void prepare(double sampleRate, float hz, float q,
                        float &w0, float &cs, float &sn, float &alpha) {
        const float nyquist = static_cast<float>(sampleRate) * 0.5f;
        const float f = clampf(hz, 1.0f, nyquist * 0.99f);
        w0 = kTwoPi * f / static_cast<float>(sampleRate);
        cs = std::cos(w0);
        sn = std::sin(w0);
        alpha = sn / (2.0f * std::max(q, 0.01f));
    }

    void set(float b0, float b1, float b2, float a0, float a1, float a2) {
        const float inv = 1.0f / a0;
        b0_ = b0 * inv; b1_ = b1 * inv; b2_ = b2 * inv;
        a1_ = a1 * inv; a2_ = a2 * inv;
    }

    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
    float z1_ = 0.0f, z2_ = 0.0f;
};

// White noise on the same generator family as the rest of the project
class Noise {
public:
    void seed(uint32_t s) { state_ = s ? s : 0x9E3779B9u; }
    float process() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return static_cast<float>(static_cast<int32_t>(state_)) * (1.0f / 2147483648.0f);
    }

private:
    uint32_t state_ = 0x9E3779B9u;
};

// Fractional delay with linear interpolation
class DelayLine {
public:
    void init(int maxSamples) {
        buffer_.assign(static_cast<std::size_t>(std::max(maxSamples, 4)), 0.0f);
        writeIndex_ = 0;
    }

    void write(float x) {
        buffer_[static_cast<std::size_t>(writeIndex_)] = x;
        if (++writeIndex_ >= static_cast<int>(buffer_.size())) writeIndex_ = 0;
    }

    float readInt(int delaySamples) const {
        const int size = static_cast<int>(buffer_.size());
        int index = writeIndex_ - 1 - std::min(std::max(delaySamples, 0), size - 1);
        if (index < 0) index += size;
        return buffer_[static_cast<std::size_t>(index)];
    }

    float read(float delaySamples) const {
        const int size = static_cast<int>(buffer_.size());
        const float d = clampf(delaySamples, 0.0f, static_cast<float>(size - 2));
        const int whole = static_cast<int>(d);
        const float frac = d - static_cast<float>(whole);
        return lerpf(readInt(whole), readInt(whole + 1), frac);
    }

    int size() const { return static_cast<int>(buffer_.size()); }

private:
    std::vector<float> buffer_;
    int writeIndex_ = 0;
};

// Freeverb-style room: eight damped combs into four allpasses per channel
// The comb and allpass lengths are the classic tunings, scaled to sample rate
class Reverb {
public:
    void init(double sampleRate, uint32_t seed = 0x517Bu) {
        static const int kCombTuning[kCombCount] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        static const int kAllpassTuning[kAllpassCount] = {556, 441, 341, 225};
        const double scale = sampleRate / 44100.0;
        // The right channel runs slightly longer lines, which is what opens
        // the tail out into stereo
        const int spread = static_cast<int>(23.0 * scale);
        (void)seed;
        for (int channel = 0; channel < 2; ++channel) {
            const int offset = channel ? spread : 0;
            for (int i = 0; i < kCombCount; ++i) {
                combLength_[channel][i] = static_cast<int>(kCombTuning[i] * scale) + offset;
                comb_[channel][i].init(combLength_[channel][i] + 2);
                combStore_[channel][i] = 0.0f;
            }
            for (int i = 0; i < kAllpassCount; ++i) {
                allpassLength_[channel][i] = static_cast<int>(kAllpassTuning[i] * scale) + offset;
                allpass_[channel][i].init(allpassLength_[channel][i] + 2);
            }
        }
        setRoom(0.82f);
        setDamping(0.35f);
    }

    // 0 is a small room, 1 rings for a long time
    void setRoom(float amount) { feedback_ = 0.7f + 0.28f * clampf(amount, 0.0f, 1.0f); }

    // How quickly the highs disappear from the tail
    void setDamping(float amount) { damping_ = clampf(amount, 0.0f, 0.95f); }

    void process(float inLeft, float inRight, float &outLeft, float &outRight) {
        const float input = (inLeft + inRight) * 0.015f;
        for (int channel = 0; channel < 2; ++channel) {
            float sum = 0.0f;
            for (int i = 0; i < kCombCount; ++i) {
                const float delayed = comb_[channel][i].readInt(combLength_[channel][i]);
                // One-pole in the feedback path, so each pass loses more top end
                combStore_[channel][i] = flush(lerpf(delayed, combStore_[channel][i], damping_));
                comb_[channel][i].write(input + combStore_[channel][i] * feedback_);
                sum += delayed;
            }
            for (int i = 0; i < kAllpassCount; ++i) {
                const float delayed = allpass_[channel][i].readInt(allpassLength_[channel][i]);
                allpass_[channel][i].write(sum + delayed * kAllpassFeedback);
                sum = delayed - sum;
            }
            (channel == 0 ? outLeft : outRight) = flush(sum);
        }
    }

private:
    static constexpr int kCombCount = 8;
    static constexpr int kAllpassCount = 4;
    static constexpr float kAllpassFeedback = 0.5f;

    DelayLine comb_[2][kCombCount];
    DelayLine allpass_[2][kAllpassCount];
    int combLength_[2][kCombCount] = {{0}};
    int allpassLength_[2][kAllpassCount] = {{0}};
    float combStore_[2][kCombCount] = {{0.0f}};
    float feedback_ = 0.9f;
    float damping_ = 0.3f;
};

// Ping-pong echo. Wide, and the repeats get darker each pass
class PingPongDelay {
public:
    void init(double sampleRate) {
        sampleRate_ = sampleRate;
        const int maxSamples = static_cast<int>(sampleRate * 4.0) + 8;
        left_.init(maxSamples);
        right_.init(maxSamples);
        damp_[0].setCutoff(sampleRate, 3600.0f);
        damp_[1].setCutoff(sampleRate, 3200.0f);
        setTime(0.35f);
    }

    void setTime(float seconds) {
        delaySamples_ = clampf(static_cast<float>(seconds * sampleRate_), 8.0f,
                               static_cast<float>(left_.size() - 4));
    }
    void setFeedback(float amount) { feedback_ = clampf(amount, 0.0f, 0.92f); }

    void process(float inLeft, float inRight, float &outLeft, float &outRight) {
        const float tapLeft = left_.read(delaySamples_);
        const float tapRight = right_.read(delaySamples_);
        // Each side feeds the other, which is what makes the repeats bounce
        left_.write(flush(inLeft + damp_[0].process(tapRight) * feedback_));
        right_.write(flush(inRight + damp_[1].process(tapLeft) * feedback_));
        outLeft = tapLeft;
        outRight = tapRight;
    }

private:
    double sampleRate_ = 48000.0;
    DelayLine left_, right_;
    OnePole damp_[2];
    float delaySamples_ = 1000.0f;
    float feedback_ = 0.4f;
};

// Peak limiter with lookahead
//
// The window holds every sample between the output tap and the newest input,
// so its maximum is the loudest sample still to come. The gain needed to keep
// that under the ceiling is therefore known ahead of time, and the smoothed
// gain is clamped to it every sample. That clamp is what makes the ceiling a
// guarantee rather than an aim: smoothing alone only approaches the target and
// lets the sharpest transients through
class Limiter {
public:
    void init(double sampleRate, float lookaheadSeconds = 0.005f) {
        lookahead_ = std::max(2, static_cast<int>(sampleRate * lookaheadSeconds));
        delay_[0].init(lookahead_ + 4);
        delay_[1].init(lookahead_ + 4);
        maximum_.init(lookahead_);
        release_ = onePoleCoefficient(sampleRate, 0.12f);
        gain_ = 1.0f;
        smoothed_ = 1.0f;
    }

    void setCeiling(float ceiling) { ceiling_ = clampf(ceiling, 0.05f, 1.0f); }

    void process(float inLeft, float inRight, float &outLeft, float &outRight) {
        delay_[0].write(inLeft);
        delay_[1].write(inRight);
        const float peak = maximum_.push(std::max(std::fabs(inLeft), std::fabs(inRight)));

        const float required = peak > ceiling_ ? ceiling_ / peak : 1.0f;
        // Recovery is gradual, but the clamp below always wins on the way down
        smoothed_ = flush(required + release_ * (smoothed_ - required));
        gain_ = std::min(smoothed_, required);

        outLeft = delay_[0].readInt(lookahead_ - 1) * gain_;
        outRight = delay_[1].readInt(lookahead_ - 1) * gain_;
    }

    float gain() const { return gain_; }

private:
    // Sliding-window maximum over a monotonic queue. Rescanning the window on
    // every sample is the obvious version and is far too slow at audio rates
    class SlidingMaximum {
    public:
        void init(int window) {
            window_ = std::max(window, 1);
            values_.assign(static_cast<std::size_t>(window_ + 1), 0.0f);
            indices_.assign(static_cast<std::size_t>(window_ + 1), 0);
            front_ = 0;
            count_ = 0;
            position_ = 0;
        }

        float push(float value) {
            // Anything smaller than the new sample can never be the maximum again
            while (count_ > 0 && values_[slot(front_ + count_ - 1)] <= value) --count_;
            values_[slot(front_ + count_)] = value;
            indices_[slot(front_ + count_)] = position_;
            ++count_;
            // Drop whatever has fallen out of the back of the window
            while (count_ > 0 && indices_[slot(front_)] + window_ <= position_) {
                front_ = slot(front_ + 1);
                --count_;
            }
            ++position_;
            return count_ > 0 ? values_[slot(front_)] : value;
        }

    private:
        std::size_t slot(std::size_t index) const { return index % values_.size(); }

        std::vector<float> values_;
        std::vector<long> indices_;
        std::size_t front_ = 0;
        std::size_t count_ = 0;
        long position_ = 0;
        int window_ = 1;
    };

    DelayLine delay_[2];
    SlidingMaximum maximum_;
    int lookahead_ = 1;
    float release_ = 0.0f;
    float gain_ = 1.0f;
    float smoothed_ = 1.0f;
    float ceiling_ = 0.95f;
};

// Feed-forward compressor used to glue the drum bus together
class Compressor {
public:
    void init(double sampleRate) {
        attack_ = onePoleCoefficient(sampleRate, 0.006f);
        release_ = onePoleCoefficient(sampleRate, 0.140f);
    }
    void configure(float thresholdDb, float ratio) {
        thresholdDb_ = thresholdDb;
        ratio_ = std::max(ratio, 1.0f);
    }

    void process(float inLeft, float inRight, float &outLeft, float &outRight) {
        const float level = std::max(std::fabs(inLeft), std::fabs(inRight));
        const float overDb = gainToDb(level) - thresholdDb_;
        const float targetDb = overDb > 0.0f ? -overDb * (1.0f - 1.0f / ratio_) : 0.0f;
        const float coefficient = targetDb < envelopeDb_ ? attack_ : release_;
        envelopeDb_ = flush(targetDb + coefficient * (envelopeDb_ - targetDb));
        const float gain = dbToGain(envelopeDb_ + makeupDb_);
        outLeft = inLeft * gain;
        outRight = inRight * gain;
    }

    void setMakeupDb(float db) { makeupDb_ = db; }

private:
    float attack_ = 0.0f;
    float release_ = 0.0f;
    float thresholdDb_ = -12.0f;
    float ratio_ = 3.0f;
    float makeupDb_ = 0.0f;
    float envelopeDb_ = 0.0f;
};

// Attack, hold, release envelope for the sustained voices
class SustainEnvelope {
public:
    void init(double sampleRate) { sampleRate_ = sampleRate; }

    void trigger(float level, float attackSeconds, float holdSeconds, float releaseSeconds) {
        level_ = level;
        attackCoefficient_ = onePoleCoefficient(sampleRate_, std::max(attackSeconds, 1.0e-4f));
        releaseCoefficient_ = onePoleCoefficient(sampleRate_, std::max(releaseSeconds, 1.0e-4f));
        holdSamples_ = static_cast<long>(holdSeconds * sampleRate_);
        elapsed_ = 0;
        value_ = 0.0f;
        running_ = true;
    }

    float process() {
        if (!running_) return 0.0f;
        const bool holding = elapsed_ < holdSamples_;
        const float target = holding ? 1.0f : 0.0f;
        const float coefficient = holding ? attackCoefficient_ : releaseCoefficient_;
        value_ = flush(target + coefficient * (value_ - target));
        ++elapsed_;
        if (!holding && value_ < 1.0e-5f) running_ = false;
        return value_ * level_;
    }

    bool active() const { return running_; }
    void stop() { running_ = false; value_ = 0.0f; }

private:
    double sampleRate_ = 48000.0;
    float level_ = 0.0f;
    float attackCoefficient_ = 0.0f;
    float releaseCoefficient_ = 0.0f;
    long holdSamples_ = 0;
    long elapsed_ = 0;
    float value_ = 0.0f;
    bool running_ = false;
};

}  // namespace dsp
}  // namespace bravebeats
