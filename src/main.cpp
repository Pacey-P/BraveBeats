// bravebeats - a procedural tribal music generator
//
// Picks a groove, an ensemble and an arc from a seed, plays the result on
// synthesised drums and hand-built melodic voices, and writes a stereo WAV

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "bravebeats/core/wav.hpp"
#include "bravebeats/engine/renderer.hpp"
#include "bravebeats/music/composer.hpp"

namespace {

struct Options {
    bravebeats::music::ComposerSettings composer;
    bravebeats::engine::RenderSettings render;
    std::string output = "tribal.wav";
    std::string solo;  // render a single part, for checking the mix
    int bitDepth = 24;
    bool listScales = false;
    bool quiet = false;
};

void printUsage(const char *program) {
    std::printf(
        "bravebeats - procedural tribal music generator\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "  -o, --out PATH       output WAV file (default tribal.wav)\n"
        "  -s, --seed N         seed for the whole piece (default 20260828)\n"
        "  -d, --duration SEC   length in seconds, rounded to whole bars (default 150)\n"
        "  -t, --tempo BPM      tempo, 0 lets the seed choose (default 0)\n"
        "  -m, --meter N        12 for a 12/8 feel, 16 for 4/4, 0 for either\n"
        "      --scale NAME     scale name, empty lets the seed choose\n"
        "      --root MIDI      root note as a MIDI number, e.g. 55\n"
        "  -r, --rate HZ        sample rate (default 48000)\n"
        "  -b, --bits N         16 or 24 bit output (default 24)\n"
        "      --room AMOUNT    reverb size, 0 to 1 (default 0.80)\n"
        "      --peak AMOUNT    peak level of the final mix, 0 to leave it alone\n"
        "      --solo PART      render only one part, e.g. bell or balafon\n"
        "      --list-scales    print the available scales and exit\n"
        "  -q, --quiet          only print errors\n"
        "  -h, --help           show this message\n"
        "\n"
        "The seed decides everything: meter, tempo, scale, every pattern and\n"
        "the whole arrangement. The same seed always renders the same piece.\n",
        program);
}

bool needsValue(int index, int argc, const char *flag) {
    if (index + 1 < argc) return true;
    std::fprintf(stderr, "bravebeats: %s needs a value\n", flag);
    return false;
}

bool parse(int argc, char **argv, Options &options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto matches = [&arg](const char *shortFlag, const char *longFlag) {
            return arg == shortFlag || arg == longFlag;
        };

        if (matches("-h", "--help")) { printUsage(argv[0]); std::exit(0); }
        else if (arg == "--list-scales") { options.listScales = true; }
        else if (matches("-q", "--quiet")) { options.quiet = true; }
        else if (matches("-o", "--out")) {
            if (!needsValue(i, argc, arg.c_str())) return false;
            options.output = argv[++i];
        } else if (matches("-s", "--seed")) {
            if (!needsValue(i, argc, arg.c_str())) return false;
            options.composer.seed = std::strtoull(argv[++i], nullptr, 10);
        } else if (matches("-d", "--duration")) {
            if (!needsValue(i, argc, arg.c_str())) return false;
            options.composer.durationSeconds = std::atof(argv[++i]);
        } else if (matches("-t", "--tempo")) {
            if (!needsValue(i, argc, arg.c_str())) return false;
            options.composer.tempo = std::atof(argv[++i]);
        } else if (matches("-m", "--meter")) {
            if (!needsValue(i, argc, arg.c_str())) return false;
            options.composer.meterPulses = std::atoi(argv[++i]);
        } else if (arg == "--scale") {
            if (!needsValue(i, argc, arg.c_str())) return false;
            options.composer.scaleName = argv[++i];
        } else if (arg == "--solo") {
            if (!needsValue(i, argc, arg.c_str())) return false;
            options.solo = argv[++i];
        } else if (arg == "--root") {
            if (!needsValue(i, argc, arg.c_str())) return false;
            options.composer.root = std::atoi(argv[++i]);
        } else if (matches("-r", "--rate")) {
            if (!needsValue(i, argc, arg.c_str())) return false;
            options.render.sampleRate = std::atoi(argv[++i]);
        } else if (matches("-b", "--bits")) {
            if (!needsValue(i, argc, arg.c_str())) return false;
            options.bitDepth = std::atoi(argv[++i]);
        } else if (arg == "--room") {
            if (!needsValue(i, argc, arg.c_str())) return false;
            options.render.reverbSize = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--peak") {
            if (!needsValue(i, argc, arg.c_str())) return false;
            options.render.targetPeak = static_cast<float>(std::atof(argv[++i]));
        } else {
            std::fprintf(stderr, "bravebeats: unknown option '%s'\n", arg.c_str());
            printUsage(argv[0]);
            return false;
        }
    }
    return true;
}

bool validate(const Options &options) {
    if (options.composer.durationSeconds < 1.0 || options.composer.durationSeconds > 3600.0) {
        std::fprintf(stderr, "bravebeats: duration must be between 1 and 3600 seconds\n");
        return false;
    }
    if (options.composer.tempo != 0.0 &&
        (options.composer.tempo < 30.0 || options.composer.tempo > 220.0)) {
        std::fprintf(stderr, "bravebeats: tempo must be between 30 and 220 bpm\n");
        return false;
    }
    if (options.composer.meterPulses != 0 && options.composer.meterPulses != 12 &&
        options.composer.meterPulses != 16) {
        std::fprintf(stderr, "bravebeats: meter must be 12, 16 or 0\n");
        return false;
    }
    if (options.render.targetPeak < 0.0f || options.render.targetPeak > 1.0f) {
        std::fprintf(stderr, "bravebeats: peak must be between 0 and 1\n");
        return false;
    }
    if (options.render.sampleRate < 8000 || options.render.sampleRate > 192000) {
        std::fprintf(stderr, "bravebeats: sample rate must be between 8000 and 192000\n");
        return false;
    }
    if (options.bitDepth != 16 && options.bitDepth != 24) {
        std::fprintf(stderr, "bravebeats: bit depth must be 16 or 24\n");
        return false;
    }
    if (!options.composer.scaleName.empty() &&
        bravebeats::music::findScale(options.composer.scaleName) == nullptr) {
        std::fprintf(stderr, "bravebeats: unknown scale '%s', try --list-scales\n",
                     options.composer.scaleName.c_str());
        return false;
    }
    if (!options.solo.empty()) {
        bool known = false;
        for (int v = 0; v < static_cast<int>(bravebeats::music::Voice::Count); ++v) {
            if (options.solo == bravebeats::music::voiceName(static_cast<bravebeats::music::Voice>(v))) {
                known = true;
                break;
            }
        }
        if (!known) {
            std::fprintf(stderr, "bravebeats: unknown part '%s'\n", options.solo.c_str());
            std::fprintf(stderr, "bravebeats: parts are");
            for (int v = 0; v < static_cast<int>(bravebeats::music::Voice::Count); ++v) {
                std::fprintf(stderr, " %s", bravebeats::music::voiceName(static_cast<bravebeats::music::Voice>(v)));
            }
            std::fprintf(stderr, "\n");
            return false;
        }
    }
    if (options.composer.root != 0 &&
        (options.composer.root < 24 || options.composer.root > 84)) {
        std::fprintf(stderr, "bravebeats: root note must be a MIDI number between 24 and 84\n");
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    Options options;
    if (!parse(argc, argv, options)) return 2;

    if (options.listScales) {
        for (const auto &scale : bravebeats::music::scaleLibrary()) {
            std::printf("%s\n", scale.name.c_str());
        }
        return 0;
    }
    if (!validate(options)) return 2;

    bravebeats::music::Composition composition = bravebeats::music::compose(options.composer);

    if (!options.solo.empty()) {
        std::vector<bravebeats::music::Note> kept;
        for (const auto &note : composition.notes) {
            if (options.solo == bravebeats::music::voiceName(note.voice)) kept.push_back(note);
        }
        composition.notes.swap(kept);
    }

    if (!options.quiet) {
        std::printf("seed      %llu\n", static_cast<unsigned long long>(composition.seed));
        std::printf("tempo     %.1f bpm, %d/%d feel, %d pulses to the bar\n",
                    composition.tempo, composition.beatsPerBar,
                    composition.subdivision == 3 ? 8 : 4, composition.pulsesPerBar);
        std::printf("scale     %s on MIDI %d\n", composition.scaleName.c_str(), composition.rootMidi);
        std::printf("form     ");
        for (const auto &section : composition.sections) {
            std::printf(" %s(%d)", section.name.c_str(), section.bars);
        }
        std::printf("\n");
        std::printf("notes     %zu across %d bars\n", composition.notes.size(), composition.totalBars);
        std::fflush(stdout);
    }

    bravebeats::engine::Renderer renderer;
    std::vector<float> audio;
    const bravebeats::engine::RenderStats stats =
        renderer.render(composition, options.render, audio);

    if (stats.nonFinite > 0) {
        std::fprintf(stderr, "bravebeats: %ld non-finite samples were replaced with silence\n",
                     stats.nonFinite);
    }

    if (!bravebeats::WavWriter::write(options.output, audio, 2,
                                      options.render.sampleRate, options.bitDepth)) {
        std::fprintf(stderr, "bravebeats: could not write '%s'\n", options.output.c_str());
        return 1;
    }

    if (!options.quiet) {
        std::printf("wrote     %s (%.1f s, %d Hz, %d bit)\n",
                    options.output.c_str(), stats.seconds, options.render.sampleRate,
                    options.bitDepth);
        std::printf("levels    peak %.3f, rms %.4f, limiter held back %.1f dB at its hardest\n",
                    stats.peak, stats.rms,
                    -bravebeats::dsp::gainToDb(stats.limiterMinGain));
    }
    return 0;
}
