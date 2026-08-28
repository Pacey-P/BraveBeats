#pragma once

// Plays a composition into a stereo buffer
//
// Each part is summed on its own before it reaches the mix, so the send levels
// can differ: the floor drum stays dry and close, while the pipe and the
// voices sit further back in the room. Drums run through a shared compressor
// so the ensemble breathes as one, and the whole mix ends on a lookahead
// limiter rather than a hard clip

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "bravebeats/core/dsp.hpp"
#include "bravebeats/music/composer.hpp"
#include "bravebeats/voices/chant.hpp"
#include "bravebeats/voices/drumkit.hpp"
#include "bravebeats/voices/struck.hpp"
#include "bravebeats/voices/wind.hpp"

namespace bravebeats {
namespace engine {

struct RenderSettings {
    int sampleRate = 48000;
    double tailSeconds = 4.0;   // room to let the last reverb tail decay
    float targetPeak = 0.89f;   // 0 or less leaves the mix at its natural level
    float reverbSize = 0.80f;
    float reverbDamping = 0.34f;
    float delayFeedback = 0.34f;
};

struct RenderStats {
    double seconds = 0.0;
    float peak = 0.0f;
    float rms = 0.0f;
    long nonFinite = 0;
    float limiterMinGain = 1.0f;
};

// How much of each part goes to the room and to the echo
struct SendLevels {
    float reverb;
    float delay;
};

inline SendLevels sendsFor(music::Voice voice) {
    switch (voice) {
        case music::Voice::Kick:     return {0.03f, 0.00f};
        case music::Voice::LowDrum:  return {0.11f, 0.00f};
        case music::Voice::MidDrum:  return {0.15f, 0.04f};
        case music::Voice::HighDrum: return {0.18f, 0.07f};
        case music::Voice::Slap:     return {0.22f, 0.06f};
        case music::Voice::Clap:     return {0.30f, 0.09f};
        case music::Voice::Shaker:   return {0.11f, 0.03f};
        case music::Voice::Bell:     return {0.26f, 0.16f};
        case music::Voice::Balafon:  return {0.28f, 0.14f};
        case music::Voice::Kalimba:  return {0.33f, 0.24f};
        case music::Voice::Flute:    return {0.38f, 0.28f};
        case music::Voice::Chant:    return {0.44f, 0.10f};
        case music::Voice::Drone:    return {0.18f, 0.00f};
        default:                     return {0.15f, 0.05f};
    }
}

class Renderer {
public:
    RenderStats render(const music::Composition &composition,
                       const RenderSettings &settings,
                       std::vector<float> &interleaved) {
        const double sampleRate = static_cast<double>(settings.sampleRate);
        const uint32_t seed = static_cast<uint32_t>(composition.seed ^ (composition.seed >> 32));

        drums_.init(sampleRate, seed);
        for (int i = 0; i < kBalafonVoices; ++i) balafon_[i].init(sampleRate, voices::kBalafonSpec, seed + 0x900u + static_cast<uint32_t>(i));
        for (int i = 0; i < kKalimbaVoices; ++i) kalimba_[i].init(sampleRate, voices::kKalimbaSpec, seed + 0xA00u + static_cast<uint32_t>(i));
        for (int i = 0; i < kFluteVoices; ++i) flute_[i].init(sampleRate, seed + 0xB00u + static_cast<uint32_t>(i));
        for (int i = 0; i < kChantVoices; ++i) chant_[i].init(sampleRate, seed + 0xC00u + static_cast<uint32_t>(i));
        for (int i = 0; i < kDroneVoices; ++i) drone_[i].init(sampleRate, seed + 0xD00u + static_cast<uint32_t>(i));
        for (int i = 0; i < kBalafonVoices; ++i) balafonPan_[i] = {0.7071f, 0.7071f};
        for (int i = 0; i < kKalimbaVoices; ++i) kalimbaPan_[i] = {0.7071f, 0.7071f};
        for (int i = 0; i < kFluteVoices; ++i) flutePan_[i] = {0.7071f, 0.7071f};
        for (int i = 0; i < kChantVoices; ++i) chantPan_[i] = {0.7071f, 0.7071f};
        for (int i = 0; i < kDroneVoices; ++i) dronePan_[i] = {0.7071f, 0.7071f};
        balafonCursor_ = kalimbaCursor_ = fluteCursor_ = chantCursor_ = droneCursor_ = 0;

        reverb_.init(sampleRate, seed ^ 0x5217u);
        reverb_.setRoom(settings.reverbSize);
        reverb_.setDamping(settings.reverbDamping);
        delay_.init(sampleRate);
        // A dotted-eighth echo locks the repeats to the groove
        delay_.setTime(static_cast<float>(60.0 / composition.tempo * 0.75));
        delay_.setFeedback(settings.delayFeedback);
        drumGlue_.init(sampleRate);
        drumGlue_.configure(-14.0f, 2.6f);
        drumGlue_.setMakeupDb(1.6f);
        limiter_.init(sampleRate, 0.006f);
        limiter_.setCeiling(0.97f);
        masterHighPass_[0].setHighPass(sampleRate, 26.0f, 0.7071f);
        masterHighPass_[1].setHighPass(sampleRate, 26.0f, 0.7071f);
        masterTilt_[0].setHighShelf(sampleRate, 7200.0f, -0.6f);
        masterTilt_[1].setHighShelf(sampleRate, 7200.0f, -0.6f);

        const double totalSeconds = composition.totalSeconds + settings.tailSeconds;
        const long totalFrames = static_cast<long>(totalSeconds * sampleRate);
        interleaved.assign(static_cast<std::size_t>(totalFrames) * 2u, 0.0f);

        const float fadeInSeconds = 0.05f;
        const float fadeOutSeconds = 3.2f;
        const double fadeOutStart = totalSeconds - static_cast<double>(fadeOutSeconds);

        RenderStats stats;
        stats.seconds = totalSeconds;
        double sumSquares = 0.0;
        std::size_t noteIndex = 0;

        for (long frame = 0; frame < totalFrames; ++frame) {
            const double now = static_cast<double>(frame) / sampleRate;
            while (noteIndex < composition.notes.size() && composition.notes[noteIndex].time <= now) {
                trigger(composition.notes[noteIndex]);
                ++noteIndex;
            }

            float dryLeft = 0.0f, dryRight = 0.0f;
            float reverbLeft = 0.0f, reverbRight = 0.0f;
            float delayLeft = 0.0f, delayRight = 0.0f;
            float drumLeft = 0.0f, drumRight = 0.0f;

            // Percussion, one part at a time so each keeps its own sends
            for (int v = 0; v <= static_cast<int>(music::Voice::Bell); ++v) {
                const music::Voice voice = static_cast<music::Voice>(v);
                float left = 0.0f, right = 0.0f;
                drums_.processPart(voice, left, right);
                if (left == 0.0f && right == 0.0f) continue;
                const SendLevels sends = sendsFor(voice);
                drumLeft += left;
                drumRight += right;
                reverbLeft += left * sends.reverb;
                reverbRight += right * sends.reverb;
                delayLeft += left * sends.delay;
                delayRight += right * sends.delay;
            }

            addPitched(music::Voice::Balafon, balafon_, balafonPan_, kBalafonVoices,
                       dryLeft, dryRight, reverbLeft, reverbRight, delayLeft, delayRight);
            addPitched(music::Voice::Kalimba, kalimba_, kalimbaPan_, kKalimbaVoices,
                       dryLeft, dryRight, reverbLeft, reverbRight, delayLeft, delayRight);
            addPitched(music::Voice::Flute, flute_, flutePan_, kFluteVoices,
                       dryLeft, dryRight, reverbLeft, reverbRight, delayLeft, delayRight);
            addPitched(music::Voice::Chant, chant_, chantPan_, kChantVoices,
                       dryLeft, dryRight, reverbLeft, reverbRight, delayLeft, delayRight);
            addPitched(music::Voice::Drone, drone_, dronePan_, kDroneVoices,
                       dryLeft, dryRight, reverbLeft, reverbRight, delayLeft, delayRight);

            // Glue on the drums only, so the melodic parts keep their attack.
            // The drive after it is for tone: it rounds the hardest strokes the
            // way a loud room does. Peak control is the limiter's job, further on
            float gluedLeft = 0.0f, gluedRight = 0.0f;
            drumGlue_.process(drumLeft, drumRight, gluedLeft, gluedRight);
            gluedLeft = dsp::softClip(gluedLeft * kDrumDrive) / kDrumDrive;
            gluedRight = dsp::softClip(gluedRight * kDrumDrive) / kDrumDrive;

            float echoLeft = 0.0f, echoRight = 0.0f;
            delay_.process(delayLeft, delayRight, echoLeft, echoRight);
            float roomLeft = 0.0f, roomRight = 0.0f;
            reverb_.process(reverbLeft + echoLeft * 0.35f, reverbRight + echoRight * 0.35f,
                            roomLeft, roomRight);

            float mixLeft = gluedLeft + dryLeft + echoLeft * 0.55f + roomLeft * 0.85f;
            float mixRight = gluedRight + dryRight + echoRight * 0.55f + roomRight * 0.85f;

            mixLeft = masterTilt_[0].process(masterHighPass_[0].process(mixLeft * kMasterGain));
            mixRight = masterTilt_[1].process(masterHighPass_[1].process(mixRight * kMasterGain));

            float outLeft = 0.0f, outRight = 0.0f;
            limiter_.process(mixLeft, mixRight, outLeft, outRight);
            stats.limiterMinGain = std::min(stats.limiterMinGain, limiter_.gain());

            float envelope = 1.0f;
            if (now < fadeInSeconds) envelope = static_cast<float>(now / fadeInSeconds);
            if (now > fadeOutStart) {
                const float remaining = static_cast<float>((totalSeconds - now) / fadeOutSeconds);
                envelope *= dsp::clampf(remaining, 0.0f, 1.0f);
                envelope *= envelope;  // a smoother close than a straight line
            }
            outLeft *= envelope;
            outRight *= envelope;

            if (!std::isfinite(outLeft)) { outLeft = 0.0f; ++stats.nonFinite; }
            if (!std::isfinite(outRight)) { outRight = 0.0f; ++stats.nonFinite; }

            interleaved[static_cast<std::size_t>(frame) * 2u] = outLeft;
            interleaved[static_cast<std::size_t>(frame) * 2u + 1u] = outRight;

            stats.peak = std::max(stats.peak, std::max(std::fabs(outLeft), std::fabs(outRight)));
            sumSquares += static_cast<double>(outLeft) * outLeft + static_cast<double>(outRight) * outRight;
        }

        if (totalFrames > 0) {
            stats.rms = static_cast<float>(std::sqrt(sumSquares / (static_cast<double>(totalFrames) * 2.0)));
        }

        // One clean gain move at the end rather than pushing the limiter harder
        // Turning it off keeps parts comparable when rendering them separately
        if (settings.targetPeak > 0.0f && stats.peak > 1.0e-4f) {
            const float makeup = settings.targetPeak / stats.peak;
            for (float &sample : interleaved) sample *= makeup;
            stats.rms *= makeup;
            stats.peak = settings.targetPeak;
        }
        return stats;
    }

private:
    struct PanGains {
        float left;
        float right;
    };

    static constexpr int kBalafonVoices = 8;
    static constexpr int kKalimbaVoices = 6;
    static constexpr int kFluteVoices = 3;
    static constexpr int kChantVoices = 6;
    static constexpr int kDroneVoices = 2;
    static constexpr float kMasterGain = 1.80f;
    static constexpr float kDrumDrive = 1.35f;

    template <typename VoiceType>
    static int acquireIndex(VoiceType *pool, int count, int &cursor) {
        for (int i = 0; i < count; ++i) {
            const int index = (cursor + i) % count;
            if (!pool[index].isActive()) { cursor = (index + 1) % count; return index; }
        }
        const int index = cursor;
        cursor = (cursor + 1) % count;
        return index;
    }

    template <typename VoiceType>
    void addPitched(music::Voice voice, VoiceType *pool, PanGains *pans, int count,
                    float &dryLeft, float &dryRight,
                    float &reverbLeft, float &reverbRight,
                    float &delayLeft, float &delayRight) {
        float left = 0.0f, right = 0.0f;
        for (int i = 0; i < count; ++i) {
            if (!pool[i].isActive()) continue;
            const float sample = pool[i].process();
            left += sample * pans[i].left;
            right += sample * pans[i].right;
        }
        if (left == 0.0f && right == 0.0f) return;
        const SendLevels sends = sendsFor(voice);
        dryLeft += left;
        dryRight += right;
        reverbLeft += left * sends.reverb;
        reverbRight += right * sends.reverb;
        delayLeft += left * sends.delay;
        delayRight += right * sends.delay;
    }

    void trigger(const music::Note &note) {
        if (drums_.handles(note.voice)) {
            drums_.trigger(note);
            return;
        }
        switch (note.voice) {
            case music::Voice::Balafon: {
                const int index = acquireIndex(balafon_, kBalafonVoices, balafonCursor_);
                balafon_[index].trigger(note.pitch, note.velocity, note.colour);
                setPan(balafonPan_[index], note.pan, note.velocity);
                break;
            }
            case music::Voice::Kalimba: {
                const int index = acquireIndex(kalimba_, kKalimbaVoices, kalimbaCursor_);
                kalimba_[index].trigger(note.pitch, note.velocity, note.colour);
                setPan(kalimbaPan_[index], note.pan, note.velocity);
                break;
            }
            case music::Voice::Flute: {
                const int index = acquireIndex(flute_, kFluteVoices, fluteCursor_);
                flute_[index].trigger(note.pitch, note.velocity, note.duration, note.colour);
                setPan(flutePan_[index], note.pan, 1.0f);
                break;
            }
            case music::Voice::Chant: {
                const int index = acquireIndex(chant_, kChantVoices, chantCursor_);
                chant_[index].trigger(note.pitch, note.velocity, note.duration, note.colour);
                setPan(chantPan_[index], note.pan, 1.0f);
                break;
            }
            case music::Voice::Drone: {
                const int index = acquireIndex(drone_, kDroneVoices, droneCursor_);
                drone_[index].trigger(note.pitch, note.velocity, note.duration, note.colour);
                setPan(dronePan_[index], note.pan, 1.0f);
                break;
            }
            default:
                break;
        }
    }

    // The struck voices carry their level in the strike itself, so only the
    // sustained ones need velocity folded into the pan gains
    static void setPan(PanGains &gains, float pan, float gain) {
        dsp::panGains(pan, gains.left, gains.right);
        gains.left *= gain;
        gains.right *= gain;
    }

    voices::DrumKit drums_;
    voices::StruckVoice balafon_[kBalafonVoices];
    voices::StruckVoice kalimba_[kKalimbaVoices];
    voices::FluteVoice flute_[kFluteVoices];
    voices::ChantVoice chant_[kChantVoices];
    voices::DroneVoice drone_[kDroneVoices];
    PanGains balafonPan_[kBalafonVoices];
    PanGains kalimbaPan_[kKalimbaVoices];
    PanGains flutePan_[kFluteVoices];
    PanGains chantPan_[kChantVoices];
    PanGains dronePan_[kDroneVoices];
    int balafonCursor_ = 0, kalimbaCursor_ = 0, fluteCursor_ = 0, chantCursor_ = 0, droneCursor_ = 0;

    dsp::Reverb reverb_;
    dsp::PingPongDelay delay_;
    dsp::Compressor drumGlue_;
    dsp::Limiter limiter_;
    dsp::Biquad masterHighPass_[2];
    dsp::Biquad masterTilt_[2];
};

}  // namespace engine
}  // namespace bravebeats
