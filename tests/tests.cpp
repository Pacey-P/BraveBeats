// Self-checks for the generator
//
// The rhythm tests pin the euclidean patterns to the traditional rhythms they
// are supposed to reproduce, and the render tests check that a seed always
// gives back the same audio, since that is the promise the tool makes

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "bravebeats/core/dsp.hpp"
#include "bravebeats/core/random.hpp"
#include "bravebeats/core/wav.hpp"
#include "bravebeats/engine/renderer.hpp"
#include "bravebeats/music/composer.hpp"
#include "bravebeats/music/euclid.hpp"
#include "bravebeats/music/scale.hpp"

namespace {

int gFailures = 0;
int gChecks = 0;

void check(bool condition, const std::string &what) {
    ++gChecks;
    if (!condition) {
        std::printf("  FAIL  %s\n", what.c_str());
        ++gFailures;
    }
}

std::string patternToString(const std::vector<bool> &pattern) {
    std::string out;
    for (bool step : pattern) out += step ? 'x' : '.';
    return out;
}

using namespace bravebeats;

void testEuclid() {
    std::printf("euclidean rhythms\n");
    // Bjorklund's algorithm reproduces these traditional timelines exactly
    check(patternToString(music::euclid(3, 8)) == "x..x..x.", "E(3,8) is the tresillo");
    check(patternToString(music::euclid(5, 8)) == "x.xx.xx.", "E(5,8) is the cinquillo");
    check(patternToString(music::euclid(7, 12)) == "x.xx.x.xx.x.", "E(7,12) is the West African bell");
    check(patternToString(music::euclid(2, 3)) == "xx.", "E(2,3)");
    check(patternToString(music::euclid(3, 4)) == "xxx.", "E(3,4)");
    check(patternToString(music::euclid(4, 9)) == "x.x.x.x..", "E(4,9)");

    // Onset counts hold across the whole useful range
    for (int steps = 1; steps <= 32; ++steps) {
        for (int onsets = 0; onsets <= steps; ++onsets) {
            const std::vector<bool> pattern = music::euclid(onsets, steps);
            if (static_cast<int>(pattern.size()) != steps || music::onsetCount(pattern) != onsets) {
                check(false, "E(" + std::to_string(onsets) + "," + std::to_string(steps) + ") shape");
            }
        }
    }
    check(true, "onset counts and lengths hold for every E(k,n) up to n=32");

    check(music::euclid(0, 8).size() == 8 && music::onsetCount(music::euclid(0, 8)) == 0,
          "no onsets gives an empty cycle");
    check(music::onsetCount(music::euclid(8, 8)) == 8, "every step filled");
    check(music::euclid(3, 0).empty(), "zero steps gives nothing");
    check(music::onsetCount(music::euclid(20, 8)) == 8, "more onsets than steps is clamped");

    // Rotation keeps the onsets and moves the starting point
    const std::vector<bool> bell = music::euclid(7, 12);
    check(music::onsetCount(music::rotate(bell, 5)) == 7, "rotation keeps the onset count");
    check(patternToString(music::rotate(bell, 12)) == patternToString(bell),
          "a full rotation is the identity");
    check(patternToString(music::rotate(bell, -1)) == patternToString(music::rotate(bell, 11)),
          "negative rotation wraps");

    const std::vector<bool> a = music::euclid(3, 8);
    const std::vector<bool> b = music::rotate(a, 1);
    check(music::onsetCount(music::without(a, b)) <= music::onsetCount(a),
          "difference never adds onsets");
    check(music::onsetCount(music::without(a, a)) == 0, "a pattern minus itself is empty");
}

void testScales() {
    std::printf("scales\n");
    const music::Scale *pentatonic = music::findScale("minor-pentatonic");
    check(pentatonic != nullptr, "the named scale is found");
    check(music::findScale("not-a-scale") == nullptr, "an unknown name gives nothing");
    if (!pentatonic) return;

    check(music::degreeToSemitone(*pentatonic, 0) == 0, "degree 0 is the root");
    check(music::degreeToSemitone(*pentatonic, 5) == 12, "one degree past the top is the octave");
    check(music::degreeToSemitone(*pentatonic, 10) == 24, "two octaves up");
    check(music::degreeToSemitone(*pentatonic, -1) == -2, "degree -1 wraps below the root");
    check(music::degreeToSemitone(*pentatonic, -5) == -12, "a full octave down");

    // Scales must ascend, or the melodic walks lose their sense of direction
    for (const music::Scale &scale : music::scaleLibrary()) {
        bool ascending = !scale.degrees.empty() && scale.degrees[0] == 0;
        for (std::size_t i = 1; i < scale.degrees.size(); ++i) {
            ascending = ascending && scale.degrees[i] > scale.degrees[i - 1];
        }
        ascending = ascending && scale.degrees.back() < 12;
        check(ascending, scale.name + " is ascending and inside one octave");
    }

    const std::vector<float> chord = music::chordOn(*pentatonic, 60, 0, 3);
    check(chord.size() == 3, "a chord has the voices asked for");
    check(chord[0] < chord[1] && chord[1] < chord[2], "chord tones stack upward");

    check(std::fabs(dsp::midiToHz(69.0f) - 440.0f) < 0.001f, "MIDI 69 is A440");
    check(std::fabs(dsp::midiToHz(57.0f) - 220.0f) < 0.001f, "an octave down halves the frequency");
}

void testRandom() {
    std::printf("random source\n");
    Rng a(12345u);
    Rng b(12345u);
    bool identical = true;
    for (int i = 0; i < 4096; ++i) identical = identical && a.next() == b.next();
    check(identical, "the same seed gives the same stream");

    Rng c(12345u), d(12346u);
    int differences = 0;
    for (int i = 0; i < 256; ++i) differences += c.next() != d.next() ? 1 : 0;
    check(differences > 240, "neighbouring seeds give unrelated streams");

    Rng uniform(99u);
    double sum = 0.0;
    float lowest = 1.0f, highest = 0.0f;
    for (int i = 0; i < 200000; ++i) {
        const float value = uniform.uniform();
        sum += value;
        lowest = std::min(lowest, value);
        highest = std::max(highest, value);
    }
    check(lowest >= 0.0f && highest < 1.0f, "uniform stays inside [0,1)");
    check(std::fabs(sum / 200000.0 - 0.5) < 0.01, "uniform averages near a half");

    Rng ranged(7u);
    bool inRange = true;
    for (int i = 0; i < 20000; ++i) {
        const int value = ranged.intRange(-3, 5);
        inRange = inRange && value >= -3 && value <= 5;
    }
    check(inRange, "intRange respects both ends");

    Rng weighted(11u);
    int picks[3] = {0, 0, 0};
    for (int i = 0; i < 30000; ++i) picks[weighted.weighted({0.0f, 1.0f, 3.0f})]++;
    check(picks[0] == 0, "a zero weight is never chosen");
    check(picks[2] > picks[1] * 2, "weights are respected in proportion");

    // Branches must not move when a sibling draws
    Rng parent(2024u);
    Rng first = parent.branch(0x10u);
    Rng second = parent.branch(0x10u);
    check(first.next() == second.next(), "the same branch tag gives the same stream");
    check(parent.branch(0x10u).next() != parent.branch(0x11u).next(), "different tags diverge");
}

void testDsp() {
    std::printf("signal processing\n");
    const double sampleRate = 48000.0;

    // The limiter must hold the ceiling even when hit with full-scale material
    dsp::Limiter limiter;
    limiter.init(sampleRate, 0.005f);
    limiter.setCeiling(0.9f);
    float worst = 0.0f;
    dsp::Noise noise;
    noise.seed(4u);
    for (int i = 0; i < 96000; ++i) {
        const float drive = noise.process() * 4.0f;
        float left = 0.0f, right = 0.0f;
        limiter.process(drive, drive * 0.8f, left, right);
        worst = std::max(worst, std::max(std::fabs(left), std::fabs(right)));
    }
    check(worst <= 0.95f, "the limiter holds close to its ceiling under heavy drive");

    // Filters stay stable across the whole range they get configured over
    bool stable = true;
    for (float hz = 20.0f; hz < 20000.0f; hz *= 1.5f) {
        dsp::Biquad filter;
        filter.setBandPass(sampleRate, hz, 9.0f);
        dsp::Noise source;
        source.seed(9u);
        for (int i = 0; i < 8000; ++i) {
            stable = stable && std::isfinite(filter.process(source.process()));
        }
    }
    check(stable, "bandpass filters stay finite from 20 Hz to 20 kHz");

    // A reverb that never decays would smear the whole piece
    dsp::Reverb reverb;
    reverb.init(sampleRate);
    reverb.setRoom(0.85f);
    float left = 0.0f, right = 0.0f;
    reverb.process(1.0f, 1.0f, left, right);
    float early = 0.0f;
    for (int i = 0; i < static_cast<int>(sampleRate); ++i) {
        reverb.process(0.0f, 0.0f, left, right);
        early = std::max(early, std::fabs(left));
    }
    float late = 0.0f;
    bool finite = true;
    for (int i = 0; i < static_cast<int>(sampleRate) * 12; ++i) {
        reverb.process(0.0f, 0.0f, left, right);
        late = std::max(late, std::fabs(left));
        finite = finite && std::isfinite(left) && std::isfinite(right);
    }
    check(finite, "the reverb tail stays finite");
    check(late < early, "the reverb tail decays");

    check(std::fabs(dsp::softClip(0.0f)) < 1.0e-6f, "soft clip leaves silence alone");
    check(dsp::softClip(100.0f) <= 1.0f && dsp::softClip(-100.0f) >= -1.0f,
          "soft clip is bounded");
    check(dsp::softClip(-0.4f) < 0.0f && std::fabs(dsp::softClip(-0.4f) + dsp::softClip(0.4f)) < 1.0e-6f,
          "soft clip is symmetric");

    float panLeft = 0.0f, panRight = 0.0f;
    dsp::panGains(0.0f, panLeft, panRight);
    check(std::fabs(panLeft - panRight) < 1.0e-6f, "centre panning is even");
    check(std::fabs(panLeft * panLeft + panRight * panRight - 1.0f) < 1.0e-5f,
          "panning holds constant power");
    dsp::panGains(-1.0f, panLeft, panRight);
    check(panLeft > 0.99f && panRight < 0.01f, "hard left silences the right");
}

void testWav() {
    std::printf("wav output\n");
    const std::string path = "bravebeats_test_output.wav";
    std::vector<float> samples;
    for (int i = 0; i < 480; ++i) {
        samples.push_back(std::sin(static_cast<float>(i) * 0.1f));
        samples.push_back(-std::sin(static_cast<float>(i) * 0.1f));
    }
    check(WavWriter::write(path, samples, 2, 48000, 24), "a 24 bit file is written");

    FILE *file = std::fopen(path.c_str(), "rb");
    check(file != nullptr, "the file exists afterwards");
    if (file) {
        unsigned char header[44] = {0};
        const std::size_t read = std::fread(header, 1, sizeof(header), file);
        std::fclose(file);
        check(read == sizeof(header), "the header is complete");
        check(header[0] == 'R' && header[1] == 'I' && header[2] == 'F' && header[3] == 'F',
              "the RIFF tag is present");
        check(header[8] == 'W' && header[9] == 'A' && header[10] == 'V' && header[11] == 'E',
              "the WAVE tag is present");
        const int channels = header[22] | (header[23] << 8);
        const int rate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
        const int bits = header[34] | (header[35] << 8);
        check(channels == 2, "the channel count is stored");
        check(rate == 48000, "the sample rate is stored");
        check(bits == 24, "the bit depth is stored");
    }
    std::remove(path.c_str());

    check(!WavWriter::write(path, samples, 2, 48000, 12), "an unsupported bit depth is refused");
    std::remove(path.c_str());
}

music::Composition composeFor(uint64_t seed, double seconds) {
    music::ComposerSettings settings;
    settings.seed = seed;
    settings.durationSeconds = seconds;
    return music::compose(settings);
}

void testComposition() {
    std::printf("composition\n");
    for (uint64_t seed : {1ull, 42ull, 4096ull, 20260828ull}) {
        const music::Composition piece = composeFor(seed, 60.0);

        check(!piece.notes.empty(), "seed " + std::to_string(seed) + " produces notes");
        check(piece.tempo >= 60.0 && piece.tempo <= 220.0, "the tempo is usable");
        check(piece.pulsesPerBar == 12 || piece.pulsesPerBar == 16, "the meter is one of the two");

        bool sorted = true, sane = true, inWindow = true;
        int voicesSeen[static_cast<int>(music::Voice::Count)] = {0};
        double previous = -1.0;
        for (const music::Note &note : piece.notes) {
            sorted = sorted && note.time >= previous;
            previous = note.time;
            sane = sane && std::isfinite(note.time) && note.time >= 0.0;
            sane = sane && note.velocity >= 0.0f && note.velocity <= 1.0f;
            sane = sane && std::isfinite(note.pitch) && note.pitch > 0.0f;
            sane = sane && std::isfinite(note.duration) && note.duration >= 0.0f;
            sane = sane && note.pan >= -1.5f && note.pan <= 1.5f;
            inWindow = inWindow && note.time <= piece.totalSeconds + 0.001;
            voicesSeen[static_cast<int>(note.voice)]++;
        }
        check(sorted, "notes come out in time order");
        check(sane, "every note is inside its limits");
        check(inWindow, "no note is scheduled past the end");

        // A piece missing its timeline or its drums is not the thing we set out
        // to generate, so treat silence from a part as a failure
        for (int v = 0; v < static_cast<int>(music::Voice::Count); ++v) {
            const music::Voice voice = static_cast<music::Voice>(v);
            check(voicesSeen[v] > 0,
                  std::string("seed ") + std::to_string(seed) + " plays the " + music::voiceName(voice));
        }

        int barsInSections = 0;
        for (const music::Section &section : piece.sections) {
            check(section.bars >= 1, "every section has at least one bar");
            barsInSections += section.bars;
        }
        check(barsInSections == piece.totalBars, "the sections account for every bar");
    }

    // The seed is the whole contract, so identical settings must give an
    // identical piece and different seeds must not
    const music::Composition first = composeFor(777ull, 45.0);
    const music::Composition second = composeFor(777ull, 45.0);
    check(first.notes.size() == second.notes.size(), "the same seed gives the same note count");
    bool same = first.notes.size() == second.notes.size();
    for (std::size_t i = 0; same && i < first.notes.size(); ++i) {
        same = first.notes[i].time == second.notes[i].time &&
               first.notes[i].voice == second.notes[i].voice &&
               first.notes[i].velocity == second.notes[i].velocity &&
               first.notes[i].pitch == second.notes[i].pitch;
    }
    check(same, "the same seed reproduces the piece exactly");
    check(composeFor(778ull, 45.0).notes.size() != first.notes.size() ||
              composeFor(778ull, 45.0).tempo != first.tempo,
          "a different seed gives a different piece");

    // Settings the caller pins must survive
    music::ComposerSettings pinned;
    pinned.seed = 5ull;
    pinned.durationSeconds = 30.0;
    pinned.tempo = 100.0;
    pinned.meterPulses = 16;
    pinned.scaleName = "phrygian";
    pinned.root = 55;
    const music::Composition forced = music::compose(pinned);
    check(std::fabs(forced.tempo - 100.0) < 1.0e-9, "a pinned tempo is used");
    check(forced.pulsesPerBar == 16, "a pinned meter is used");
    check(forced.scaleName == "phrygian", "a pinned scale is used");
    check(forced.rootMidi == 55, "a pinned root is used");

    const music::Composition tiny = composeFor(3ull, 4.0);
    check(!tiny.notes.empty(), "a very short piece still has notes");
}

void testRender() {
    std::printf("rendering\n");
    music::ComposerSettings settings;
    settings.seed = 4242ull;
    settings.durationSeconds = 8.0;
    const music::Composition piece = music::compose(settings);

    engine::RenderSettings render;
    render.sampleRate = 24000;  // the tests do not need full rate
    render.tailSeconds = 1.0;

    engine::Renderer rendererA;
    std::vector<float> audioA;
    const engine::RenderStats stats = rendererA.render(piece, render, audioA);

    check(!audioA.empty(), "the render produces samples");
    check(stats.nonFinite == 0, "no sample comes out non-finite");
    check(stats.peak <= render.targetPeak + 1.0e-4f, "the peak lands on the target");
    check(stats.rms > 0.005f, "the render is not silent");
    check(audioA.size() % 2 == 0, "the buffer is whole stereo frames");

    bool finite = true, bounded = true;
    for (float sample : audioA) {
        finite = finite && std::isfinite(sample);
        bounded = bounded && std::fabs(sample) <= 1.0f;
    }
    check(finite, "every sample is finite");
    check(bounded, "every sample is inside full scale");

    // Same seed, same audio, down to the sample
    engine::Renderer rendererB;
    std::vector<float> audioB;
    rendererB.render(piece, render, audioB);
    check(audioA.size() == audioB.size(), "two renders are the same length");
    bool identical = audioA.size() == audioB.size();
    for (std::size_t i = 0; identical && i < audioA.size(); ++i) {
        identical = audioA[i] == audioB[i];
    }
    check(identical, "the same piece renders to identical audio");

    // Turning normalisation off must leave the mix where it was
    engine::RenderSettings raw = render;
    raw.targetPeak = 0.0f;
    engine::Renderer rendererC;
    std::vector<float> audioC;
    const engine::RenderStats rawStats = rendererC.render(piece, raw, audioC);
    check(rawStats.peak > 0.0f && rawStats.peak <= 1.0f, "the unnormalised peak is sane");
    check(rawStats.nonFinite == 0, "the unnormalised render is clean too");

    // Soloing one part has to be quieter than the whole ensemble
    music::Composition solo = piece;
    std::vector<music::Note> bellOnly;
    for (const music::Note &note : solo.notes) {
        if (note.voice == music::Voice::Bell) bellOnly.push_back(note);
    }
    solo.notes.swap(bellOnly);
    engine::Renderer rendererD;
    std::vector<float> audioD;
    const engine::RenderStats soloStats = rendererD.render(solo, raw, audioD);
    check(soloStats.rms > 0.0f, "a soloed part still makes sound");
    check(soloStats.rms < rawStats.rms, "one part is quieter than the ensemble");
}

}  // namespace

int main() {
    std::printf("bravebeats self-checks\n\n");
    testEuclid();
    testScales();
    testRandom();
    testDsp();
    testWav();
    testComposition();
    testRender();
    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
