import type { WasmSource } from './types.js';

/** The raw functions the WebAssembly module exports. */
export interface BraveBeatsExports {
  memory: WebAssembly.Memory;
  _initialize?: () => void;
  bb_create(
    seed: number,
    tempo: number,
    meterPulses: number,
    scaleIndex: number,
    root: number,
    sampleRate: number,
    loopBars: number,
    durationSeconds: number,
    maxBlock: number,
  ): number;
  bb_destroy(engine: number): void;
  bb_set_intensity(engine: number, intensity: number): void;
  bb_get_intensity(engine: number): number;
  bb_render(engine: number, frames: number): void;
  bb_left(engine: number): number;
  bb_right(engine: number): number;
  bb_max_block(engine: number): number;
  bb_loop_seconds(engine: number): number;
  bb_total_seconds(engine: number): number;
  bb_tempo(engine: number): number;
  bb_bar_seconds(engine: number): number;
  bb_pulses_per_bar(engine: number): number;
  bb_total_bars(engine: number): number;
  bb_root(engine: number): number;
  bb_note_count(engine: number): number;
  bb_scale_of(engine: number): number;
  bb_scale_count(): number;
  bb_scale_name(index: number): number;
  bb_section_count(engine: number): number;
  bb_section_name(engine: number, index: number): number;
  bb_section_bars(engine: number, index: number): number;
}

/**
 * The module is built standalone, so the only imports it asks for are a memory
 * growth notification and three stdio stubs it never actually calls. Supplying
 * them here keeps the module free of any Emscripten JavaScript glue, which is
 * what lets the same binary run on the main thread, in a worker and inside an
 * AudioWorklet.
 */
export function wasmImports(): WebAssembly.Imports {
  const unused = () => 0;
  return {
    env: { emscripten_notify_memory_growth: () => {} },
    wasi_snapshot_preview1: { fd_close: unused, fd_write: unused, fd_seek: unused },
  };
}

export async function loadWasmBytes(source: WasmSource = {}): Promise<Uint8Array> {
  if (source.wasmBinary) {
    return source.wasmBinary instanceof Uint8Array
      ? source.wasmBinary
      : new Uint8Array(source.wasmBinary);
  }
  const url = source.wasmUrl ?? defaultWasmUrl();
  const response = await fetch(url instanceof URL ? url.href : url);
  if (!response.ok) {
    throw new Error(`bravebeats: could not fetch ${String(url)} (${response.status})`);
  }
  return new Uint8Array(await response.arrayBuffer());
}

/**
 * Where `bravebeats.wasm` sits relative to this module, which is what a bundler
 * resolves for you. A build with no module URL to work from - a plain script
 * bundle, for one - has nowhere to look, and says so rather than fetching
 * something surprising.
 */
function defaultWasmUrl(): URL {
  const base = import.meta.url;
  if (!base) {
    throw new Error(
      'bravebeats: this build cannot locate bravebeats.wasm on its own. ' +
      'Pass wasmUrl or wasmBinary.',
    );
  }
  return new URL('./bravebeats.wasm', base);
}

export async function instantiate(bytes: Uint8Array): Promise<BraveBeatsExports> {
  const { instance } = await WebAssembly.instantiate(
    bytes as unknown as BufferSource,
    wasmImports(),
  );
  const exports = instance.exports as unknown as BraveBeatsExports;
  // The reactor entry point runs the static initialisers
  exports._initialize?.();
  return exports;
}

/** Reads a NUL-terminated string out of the module's memory. */
export function readCString(exports: BraveBeatsExports, pointer: number): string {
  if (!pointer) return '';
  const bytes = new Uint8Array(exports.memory.buffer);
  let end = pointer;
  while (bytes[end] !== 0 && end < bytes.length) end += 1;
  return new TextDecoder().decode(bytes.subarray(pointer, end));
}

export function scaleNamesFrom(exports: BraveBeatsExports): string[] {
  const names: string[] = [];
  for (let i = 0; i < exports.bb_scale_count(); i += 1) {
    names.push(readCString(exports, exports.bb_scale_name(i)));
  }
  return names;
}

export function scaleIndexOf(exports: BraveBeatsExports, name?: string): number {
  if (!name) return -1;
  const index = scaleNamesFrom(exports).indexOf(name);
  if (index < 0) {
    throw new Error(
      `bravebeats: unknown scale "${name}", expected one of ${scaleNamesFrom(exports).join(', ')}`,
    );
  }
  return index;
}
