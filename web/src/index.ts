/**
 * bravebeats - a procedural tribal music generator.
 *
 * Two ways in:
 *
 *   renderTrack()    renders a finished piece to an AudioBuffer. Use it for a
 *                    menu or a title screen, where the music does not react.
 *
 *   createAdaptive() gives you a live node that loops seamlessly and follows
 *                    an intensity you set from the game. Use it for anything
 *                    that should thin out when the player is safe and fill in
 *                    when they are not.
 */
import type { BraveBeatsOptions, CompositionInfo, WasmSource } from './types.js';
import {
  instantiate,
  loadWasmBytes,
  readCString,
  scaleIndexOf,
  scaleNamesFrom,
  type BraveBeatsExports,
} from './wasm.js';
import { workletSource } from './worklet-source.js';

export type { BraveBeatsOptions, CompositionInfo, ScaleName, WasmSource } from './types.js';

const DEFAULT_SEED = 20260828;
const MAX_BLOCK = 1024;

function optionValues(options: BraveBeatsOptions, exports: BraveBeatsExports) {
  return {
    seed: Math.max(0, Math.floor(options.seed ?? DEFAULT_SEED)),
    tempo: options.tempo ?? 0,
    meter: options.meter ?? 0,
    scaleIndex: scaleIndexOf(exports, options.scale),
    root: options.root ?? 0,
  };
}

function readInfo(exports: BraveBeatsExports, engine: number, seed: number): CompositionInfo {
  const sections: CompositionInfo['sections'] = [];
  for (let i = 0; i < exports.bb_section_count(engine); i += 1) {
    sections.push({
      name: readCString(exports, exports.bb_section_name(engine, i)),
      bars: exports.bb_section_bars(engine, i),
    });
  }
  return {
    seed,
    tempo: exports.bb_tempo(engine),
    scale: readCString(exports, exports.bb_scale_of(engine)),
    root: exports.bb_root(engine),
    pulsesPerBar: exports.bb_pulses_per_bar(engine),
    barSeconds: exports.bb_bar_seconds(engine),
    totalBars: exports.bb_total_bars(engine),
    totalSeconds: exports.bb_total_seconds(engine),
    loopSeconds: exports.bb_loop_seconds(engine),
    noteCount: exports.bb_note_count(engine),
    sections,
  };
}

/** The scales this build knows about. */
export async function scaleNames(source?: WasmSource): Promise<string[]> {
  const exports = await instantiate(await loadWasmBytes(source));
  return scaleNamesFrom(exports);
}

export interface RenderOptions extends BraveBeatsOptions, WasmSource {
  /** Seconds of music, before the reverb tail. Rounded to whole bars. */
  duration?: number;
  sampleRate?: number;
  /** Bars in a seamless loop. Set this and the piece repeats with no seam. */
  loopBars?: number;
  /** Peak level of the result, 0 to 1. Pass 0 to leave levels untouched. */
  peak?: number;
}

export interface RenderedTrack {
  left: Float32Array;
  right: Float32Array;
  sampleRate: number;
  info: CompositionInfo;
}

/**
 * Renders to plain channel data. Works anywhere, including Node, so this is
 * also what the tests use.
 */
export async function renderToChannels(options: RenderOptions = {}): Promise<RenderedTrack> {
  const exports = await instantiate(await loadWasmBytes(options));
  const values = optionValues(options, exports);
  const sampleRate = options.sampleRate ?? 48000;
  const loopBars = options.loopBars ?? 0;
  const duration = options.duration ?? 150;

  const engine = exports.bb_create(
    values.seed, values.tempo, values.meter, values.scaleIndex, values.root,
    sampleRate, loopBars, duration, MAX_BLOCK,
  );
  if (!engine) throw new Error('bravebeats: could not create the engine');

  try {
    const info = readInfo(exports, engine, values.seed);
    // A loop is exactly its own length. A fixed piece gets a tail so the last
    // reverb has somewhere to go
    const seconds = loopBars > 0 ? info.loopSeconds : info.totalSeconds + 4;
    const frames = Math.max(1, Math.round(seconds * sampleRate));

    const left = new Float32Array(frames);
    const right = new Float32Array(frames);

    for (let offset = 0; offset < frames; offset += MAX_BLOCK) {
      const block = Math.min(MAX_BLOCK, frames - offset);
      exports.bb_render(engine, block);
      const memory = exports.memory.buffer;
      left.set(new Float32Array(memory, exports.bb_left(engine), block), offset);
      right.set(new Float32Array(memory, exports.bb_right(engine), block), offset);
    }

    // A fixed piece opens and closes; a loop must not, or the seam would dip
    if (loopBars === 0) {
      applyFades(left, right, sampleRate);
    }
    const peak = options.peak ?? 0.89;
    if (peak > 0) normalise(left, right, peak);

    return { left, right, sampleRate, info };
  } finally {
    exports.bb_destroy(engine);
  }
}

function applyFades(left: Float32Array, right: Float32Array, sampleRate: number): void {
  const fadeIn = Math.round(0.05 * sampleRate);
  const fadeOut = Math.round(3.2 * sampleRate);
  const frames = left.length;
  for (let i = 0; i < fadeIn && i < frames; i += 1) {
    const gain = i / fadeIn;
    left[i] *= gain;
    right[i] *= gain;
  }
  for (let i = 0; i < fadeOut && i < frames; i += 1) {
    const index = frames - 1 - i;
    const gain = (i / fadeOut) ** 2;
    left[index] *= gain;
    right[index] *= gain;
  }
}

function normalise(left: Float32Array, right: Float32Array, target: number): void {
  let peak = 0;
  for (let i = 0; i < left.length; i += 1) {
    peak = Math.max(peak, Math.abs(left[i]), Math.abs(right[i]));
  }
  if (peak < 1e-4) return;
  const makeup = target / peak;
  for (let i = 0; i < left.length; i += 1) {
    left[i] *= makeup;
    right[i] *= makeup;
  }
}

/** Renders a finished piece into an AudioBuffer ready to play or loop. */
export async function renderTrack(
  context: BaseAudioContext,
  options: RenderOptions = {},
): Promise<{ buffer: AudioBuffer; info: CompositionInfo }> {
  const track = await renderToChannels({ ...options, sampleRate: context.sampleRate });
  const buffer = context.createBuffer(2, track.left.length, track.sampleRate);
  // set() rather than copyToChannel(): it types cleanly across TypeScript
  // versions and is available on every AudioBuffer
  buffer.getChannelData(0).set(track.left);
  buffer.getChannelData(1).set(track.right);
  return { buffer, info: track.info };
}

export interface AdaptiveOptions extends BraveBeatsOptions, WasmSource {
  /** Bars in the loop. Longer loops repeat less obviously. */
  loopBars?: number;
  /** Where the ensemble starts, 0 to 1. */
  intensity?: number;
  /** Output gain applied inside the worklet. */
  gain?: number;
}

/**
 * A live, looping track whose density follows `intensity`.
 *
 * Layers have their own entry points, so raising intensity brings the ensemble
 * in roughly in the order a real one would assemble: bell and drone first,
 * then the floor drum, the hand drums, the balafon, and the hands and voices
 * at the top. Changes take effect on the next notes, never mid-stroke, so it
 * is safe to drive this from gameplay every frame.
 */
export class AdaptiveTrack {
  readonly node: AudioWorkletNode;
  readonly ready: Promise<CompositionInfo>;
  private currentIntensity: number;

  private constructor(node: AudioWorkletNode, intensity: number) {
    this.node = node;
    this.currentIntensity = intensity;
    this.ready = new Promise<CompositionInfo>((resolve, reject) => {
      node.port.onmessage = (event) => {
        const message = event.data;
        if (message.type === 'ready') resolve(message.info as CompositionInfo);
        else if (message.type === 'error') reject(new Error(`bravebeats: ${message.message}`));
      };
    });
  }

  static async create(
    context: AudioContext,
    options: AdaptiveOptions = {},
  ): Promise<AdaptiveTrack> {
    const bytes = await loadWasmBytes(options);
    // The exports are needed once, on this side, to turn a scale name into the
    // index the module expects
    const exports = await instantiate(bytes);
    const values = optionValues(options, exports);
    const intensity = options.intensity ?? 1;

    await registerProcessorOnce(context);

    const node = new AudioWorkletNode(context, 'bravebeats', {
      numberOfInputs: 0,
      numberOfOutputs: 1,
      outputChannelCount: [2],
      processorOptions: {
        ...values,
        loopBars: options.loopBars ?? 8,
        intensity,
        gain: options.gain ?? 1,
        maxBlock: MAX_BLOCK,
        // Copied so the worklet owns its own bytes
        wasmBinary: bytes.slice().buffer,
      },
    });
    return new AdaptiveTrack(node, intensity);
  }

  /** How much of the ensemble is playing, 0 to 1. */
  get intensity(): number {
    return this.currentIntensity;
  }

  set intensity(value: number) {
    const clamped = Math.min(1, Math.max(0, value));
    this.currentIntensity = clamped;
    this.node.port.postMessage({ type: 'intensity', value: clamped });
  }

  /** Output gain, applied inside the worklet. */
  setGain(value: number): void {
    this.node.port.postMessage({ type: 'gain', value });
  }

  connect(destination: AudioNode): AudioNode {
    return this.node.connect(destination);
  }

  /** Silences the node and lets it be collected. */
  stop(): void {
    this.node.port.postMessage({ type: 'stop' });
    this.node.disconnect();
  }
}

/** Shorthand for {@link AdaptiveTrack.create}. */
export function createAdaptive(
  context: AudioContext,
  options?: AdaptiveOptions,
): Promise<AdaptiveTrack> {
  return AdaptiveTrack.create(context, options);
}

// registerProcessor throws if the same name is added to a context twice, and a
// game may well want more than one track
const registered = new WeakSet<BaseAudioContext>();

async function registerProcessorOnce(context: BaseAudioContext): Promise<void> {
  if (registered.has(context)) return;
  const blob = new Blob([workletSource], { type: 'application/javascript' });
  const url = URL.createObjectURL(blob);
  try {
    await context.audioWorklet.addModule(url);
    registered.add(context);
  } finally {
    URL.revokeObjectURL(url);
  }
}
