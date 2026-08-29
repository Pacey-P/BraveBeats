/** Options shared by every way of starting the generator. */
export interface BraveBeatsOptions {
  /**
   * Decides the whole piece: meter, tempo, scale, every pattern and the
   * arrangement. The same seed always gives the same music. Integers up to
   * 2^53 are safe.
   */
  seed?: number;
  /** Beats per minute. Omit, or pass 0, to let the seed choose. */
  tempo?: number;
  /** 12 for a 12/8 feel, 16 for 4/4. Omit to let the seed choose. */
  meter?: 12 | 16;
  /** Scale name, e.g. `"minor-pentatonic"`. Omit to let the seed choose. */
  scale?: ScaleName;
  /** Root note as a MIDI number, roughly 50 to 58 works well. */
  root?: number;
  /** Reverb size, 0 to 1. */
  room?: number;
}

/** Scales the generator knows. Read `scaleNames()` for the list at runtime. */
export type ScaleName =
  | 'minor-pentatonic'
  | 'major-pentatonic'
  | 'dorian'
  | 'phrygian'
  | 'aeolian'
  | 'mixolydian'
  | 'hexatonic'
  | 'in-sen';

/** Where the module's WebAssembly comes from. */
export interface WasmSource {
  /**
   * URL of `bravebeats.wasm`. Defaults to the copy sitting next to this
   * module, which is what a bundler will resolve for you.
   */
  wasmUrl?: string | URL;
  /** Already-loaded bytes, if your asset pipeline hands them over directly. */
  wasmBinary?: ArrayBuffer | Uint8Array;
}

/** What the generator decided, once a piece exists. */
export interface CompositionInfo {
  seed: number;
  tempo: number;
  scale: string;
  root: number;
  /** 12 in a 12/8 feel, 16 in 4/4. */
  pulsesPerBar: number;
  barSeconds: number;
  totalBars: number;
  totalSeconds: number;
  /** Length of one turn of the loop, or 0 for a fixed piece. */
  loopSeconds: number;
  noteCount: number;
  sections: Array<{ name: string; bars: number }>;
}
