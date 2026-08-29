/**
 * Source for the AudioWorklet processor.
 *
 * It is a string rather than a file because an AudioWorklet is loaded from a
 * URL, and every bundler wants different configuration to emit one. Handing
 * the browser a Blob URL built at runtime means this module drops into a game
 * project with no build setup at all.
 *
 * The processor cannot fetch anything: `fetch` does not exist in an
 * AudioWorkletGlobalScope. The WebAssembly bytes are posted in through
 * `processorOptions` instead, and the node outputs silence for the handful of
 * blocks it takes to compile them.
 */
export const workletSource = /* js */ `
const IMPORTS = {
  env: { emscripten_notify_memory_growth: () => {} },
  wasi_snapshot_preview1: { fd_close: () => 0, fd_write: () => 0, fd_seek: () => 0 },
};

class BraveBeatsProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const settings = options.processorOptions;
    this.exports = null;
    this.engine = 0;
    this.pendingIntensity = settings.intensity;
    this.gain = settings.gain;
    this.stopped = false;

    this.port.onmessage = (event) => {
      const message = event.data;
      if (message.type === 'intensity') {
        this.pendingIntensity = message.value;
        if (this.exports) this.exports.bb_set_intensity(this.engine, message.value);
      } else if (message.type === 'gain') {
        this.gain = message.value;
      } else if (message.type === 'stop') {
        this.stopped = true;
      }
    };

    WebAssembly.instantiate(settings.wasmBinary, IMPORTS).then(({ instance }) => {
      const exports = instance.exports;
      if (exports._initialize) exports._initialize();
      this.engine = exports.bb_create(
        settings.seed,
        settings.tempo,
        settings.meter,
        settings.scaleIndex,
        settings.root,
        sampleRate,
        settings.loopBars,
        0,
        settings.maxBlock,
      );
      if (!this.engine) {
        this.port.postMessage({ type: 'error', message: 'bb_create returned null' });
        return;
      }
      exports.bb_set_intensity(this.engine, this.pendingIntensity);
      this.exports = exports;

      // An AudioWorkletGlobalScope has no TextDecoder. These are scale and
      // section names, plain ASCII, so read them a byte at a time
      const readString = (pointer) => {
        if (!pointer) return '';
        const bytes = new Uint8Array(exports.memory.buffer);
        let text = '';
        for (let at = pointer; at < bytes.length && bytes[at] !== 0; at += 1) {
          text += String.fromCharCode(bytes[at]);
        }
        return text;
      };
      const sections = [];
      for (let i = 0; i < exports.bb_section_count(this.engine); i += 1) {
        sections.push({
          name: readString(exports.bb_section_name(this.engine, i)),
          bars: exports.bb_section_bars(this.engine, i),
        });
      }
      this.port.postMessage({
        type: 'ready',
        info: {
          seed: settings.seed,
          tempo: exports.bb_tempo(this.engine),
          scale: readString(exports.bb_scale_of(this.engine)),
          root: exports.bb_root(this.engine),
          pulsesPerBar: exports.bb_pulses_per_bar(this.engine),
          barSeconds: exports.bb_bar_seconds(this.engine),
          totalBars: exports.bb_total_bars(this.engine),
          totalSeconds: exports.bb_total_seconds(this.engine),
          loopSeconds: exports.bb_loop_seconds(this.engine),
          noteCount: exports.bb_note_count(this.engine),
          sections,
        },
      });
    }).catch((error) => {
      this.port.postMessage({ type: 'error', message: String(error) });
    });
  }

  process(inputs, outputs) {
    const output = outputs[0];
    const frames = output[0].length;

    // Silence until the module is compiled, and after an explicit stop
    if (!this.exports || this.stopped) {
      for (let channel = 0; channel < output.length; channel += 1) output[channel].fill(0);
      return !this.stopped;
    }

    this.exports.bb_render(this.engine, frames);
    // Rebuilt every block: growing the module's memory detaches older views
    const memory = this.exports.memory.buffer;
    const left = new Float32Array(memory, this.exports.bb_left(this.engine), frames);
    const right = new Float32Array(memory, this.exports.bb_right(this.engine), frames);

    if (this.gain === 1) {
      output[0].set(left);
      if (output.length > 1) output[1].set(right);
    } else {
      for (let i = 0; i < frames; i += 1) output[0][i] = left[i] * this.gain;
      if (output.length > 1) {
        for (let i = 0; i < frames; i += 1) output[1][i] = right[i] * this.gain;
      }
    }
    return true;
  }
}

registerProcessor('bravebeats', BraveBeatsProcessor);
`;
