// C surface over the generator, for WebAssembly hosts
//
// Deliberately small and free of strings on the way in: a host picks a scale
// by index rather than by name, so nothing has to marshal text into the
// module. Audio comes back through two planar buffers the host reads straight
// out of the module's memory, which is the shape an AudioWorklet wants

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "bravebeats/engine/renderer.hpp"
#include "bravebeats/music/composer.hpp"

namespace {

struct Engine {
    bravebeats::music::Composition composition;
    bravebeats::engine::Renderer renderer;
    std::vector<float> left;
    std::vector<float> right;
    std::string scaleName;
    int maxBlock = 0;
};

// Kept alive so a host reading the returned pointer always sees valid memory
std::string gScratch;

}  // namespace

extern "C" {

// A seed arrives as a double because that is what a JavaScript number is.
// Values up to 2^53 survive the trip exactly, which is more seeds than anyone
// is going to use
Engine *bb_create(double seed,
                  double tempo,
                  int meterPulses,
                  int scaleIndex,
                  int root,
                  int sampleRate,
                  int loopBars,
                  double durationSeconds,
                  int maxBlock) {
    bravebeats::music::ComposerSettings settings;
    settings.seed = static_cast<uint64_t>(seed < 0.0 ? 0.0 : seed);
    settings.tempo = tempo;
    settings.meterPulses = meterPulses;
    settings.root = root;
    settings.loopBars = loopBars > 0 ? loopBars : 0;
    settings.durationSeconds = durationSeconds > 0.0 ? durationSeconds : 150.0;

    const std::vector<bravebeats::music::Scale> &scales = bravebeats::music::scaleLibrary();
    if (scaleIndex >= 0 && scaleIndex < static_cast<int>(scales.size())) {
        settings.scaleName = scales[static_cast<std::size_t>(scaleIndex)].name;
    }

    Engine *engine = new (std::nothrow) Engine();
    if (engine == nullptr) return nullptr;

    engine->composition = bravebeats::music::compose(settings);
    engine->scaleName = engine->composition.scaleName;
    engine->maxBlock = maxBlock > 0 ? maxBlock : 1024;
    engine->left.assign(static_cast<std::size_t>(engine->maxBlock), 0.0f);
    engine->right.assign(static_cast<std::size_t>(engine->maxBlock), 0.0f);

    bravebeats::engine::RenderSettings render;
    render.sampleRate = sampleRate > 0 ? sampleRate : 48000;
    // A live host has no end to normalise against, so the limiter ceiling is
    // the working level and nothing is scaled afterwards
    render.targetPeak = 0.0f;
    engine->renderer.prepare(engine->composition, render);
    return engine;
}

void bb_destroy(Engine *engine) { delete engine; }

void bb_set_intensity(Engine *engine, float intensity) {
    if (engine) engine->renderer.setIntensity(intensity);
}

float bb_get_intensity(const Engine *engine) {
    return engine ? engine->renderer.intensity() : 0.0f;
}

// Fills the module-side buffers. Never ask for more than maxBlock frames
void bb_render(Engine *engine, int frames) {
    if (!engine) return;
    if (frames > engine->maxBlock) frames = engine->maxBlock;
    if (frames <= 0) return;
    engine->renderer.renderBlock(engine->left.data(), engine->right.data(), frames);
}

float *bb_left(Engine *engine) { return engine ? engine->left.data() : nullptr; }
float *bb_right(Engine *engine) { return engine ? engine->right.data() : nullptr; }
int bb_max_block(const Engine *engine) { return engine ? engine->maxBlock : 0; }

double bb_loop_seconds(const Engine *engine) { return engine ? engine->composition.loopSeconds : 0.0; }
double bb_total_seconds(const Engine *engine) { return engine ? engine->composition.totalSeconds : 0.0; }
double bb_tempo(const Engine *engine) { return engine ? engine->composition.tempo : 0.0; }
double bb_bar_seconds(const Engine *engine) { return engine ? engine->composition.barSeconds : 0.0; }
int bb_pulses_per_bar(const Engine *engine) { return engine ? engine->composition.pulsesPerBar : 0; }
int bb_total_bars(const Engine *engine) { return engine ? engine->composition.totalBars : 0; }
int bb_root(const Engine *engine) { return engine ? engine->composition.rootMidi : 0; }
int bb_note_count(const Engine *engine) {
    return engine ? static_cast<int>(engine->composition.notes.size()) : 0;
}
const char *bb_scale_of(const Engine *engine) {
    return engine ? engine->scaleName.c_str() : "";
}

int bb_scale_count() { return static_cast<int>(bravebeats::music::scaleLibrary().size()); }

const char *bb_scale_name(int index) {
    const std::vector<bravebeats::music::Scale> &scales = bravebeats::music::scaleLibrary();
    if (index < 0 || index >= static_cast<int>(scales.size())) return "";
    gScratch = scales[static_cast<std::size_t>(index)].name;
    return gScratch.c_str();
}

// Section names and lengths, so a host can show or follow the arrangement
int bb_section_count(const Engine *engine) {
    return engine ? static_cast<int>(engine->composition.sections.size()) : 0;
}

const char *bb_section_name(Engine *engine, int index) {
    if (!engine || index < 0 || index >= static_cast<int>(engine->composition.sections.size())) return "";
    gScratch = engine->composition.sections[static_cast<std::size_t>(index)].name;
    return gScratch.c_str();
}

int bb_section_bars(const Engine *engine, int index) {
    if (!engine || index < 0 || index >= static_cast<int>(engine->composition.sections.size())) return 0;
    return engine->composition.sections[static_cast<std::size_t>(index)].bars;
}

}  // extern "C"
