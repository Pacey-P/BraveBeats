# bravebeats (TypeScript)

The generator compiled to WebAssembly, with a typed API for the browser.
Same C++ as the command line tool, so a seed means the same music in both.

## Just want to hear it?

Open `bravebeats.html` at the repository root. The module and the WebAssembly
are inlined into that one file, so it needs no build, no server and no network.

## Install

There is no published package yet. Copy `web/` into your project, or point
your package manager at the directory:

```sh
npm install /path/to/BraveBeats/web
```

Then build it once (needs nothing but TypeScript; the `.wasm` is committed):

```sh
npm install && npm run build
```

## Two ways to use it

### A finished piece

For a menu, a title screen, anywhere the music does not need to react.

```ts
import { renderTrack } from 'bravebeats';

const context = new AudioContext();
const { buffer, info } = await renderTrack(context, { seed: 20260828, duration: 90 });

const source = context.createBufferSource();
source.buffer = buffer;
source.loop = true;
source.connect(context.destination);
source.start();

console.log(`${info.tempo.toFixed(0)} bpm, ${info.scale}, ${info.totalBars} bars`);
```

### A live track that follows the game

This is the one worth having. It loops seamlessly and you drive how much of
the ensemble is playing.

```ts
import { createAdaptive } from 'bravebeats';

const context = new AudioContext();
const track = await createAdaptive(context, { seed: 7, loopBars: 8, intensity: 0.2 });
track.connect(context.destination);
await track.ready;

// Anywhere in your game loop. Cheap enough to set every frame
track.intensity = threatLevel;   // 0 to 1
```

Layers have their own entry points, so intensity brings the ensemble in the
order a real one would assemble:

| intensity | what joins |
| --- | --- |
| 0.00 | drone |
| 0.10 | bell (the timeline) |
| 0.30 | kick, low drum |
| 0.42 | shaker, balafon |
| 0.52 | mid drum |
| 0.60 | slap, kalimba |
| 0.66 | chant |
| 0.68 | high drum |
| 0.76 | clap |

Changes land on the next notes, never part-way through a stroke, so moving the
value quickly is safe. The audio itself never restarts: voices and both effects
run continuously across the loop point, so there is no seam and no gap when the
density changes.

`AudioContext` must be resumed from a user gesture before anything is audible.
That is a browser rule, not this module's.

### When a worklet cannot be loaded

A worklet module has to come from a real origin, so a page opened from disk
with `file://` cannot use one - which also covers an Electron app loading its
window from a file. By default `createAdaptive` throws there, with a message
saying what to do about it. Pass `fallbackToScriptProcessor: true` and it
renders the same audio on the main thread instead:

```ts
const track = await createAdaptive(context, { fallbackToScriptProcessor: true });
if (track.usingFallback) console.warn('running on the main thread; may stutter');
```

It is off by default because you want to know when the good path is
unavailable rather than quietly shipping a version that can glitch.

## API

| | |
| --- | --- |
| `renderTrack(context, options)` | renders a piece to an `AudioBuffer` |
| `renderToChannels(options)` | the same, as plain `Float32Array`s; works in Node too |
| `createAdaptive(context, options)` | a live `AdaptiveTrack` that loops and follows intensity |
| `scaleNames()` | the scales this build knows |

Options: `seed`, `tempo`, `meter` (12 or 16), `scale`, `root`, `duration`,
`loopBars`, `sampleRate`, `peak`. Leave any of them out and the seed decides.

`AdaptiveTrack` has `intensity` (get and set), `setGain()`, `connect()`,
`stop()`, `node` (the underlying `AudioWorkletNode`) and `ready`, a promise
resolving to what the generator chose.

### Loading the WebAssembly

By default the module fetches `bravebeats.wasm` from beside itself, which is
what a bundler resolves for you. Override it if your assets live elsewhere:

```ts
await createAdaptive(context, { wasmUrl: '/assets/bravebeats.wasm' });
await renderToChannels({ wasmBinary: myArrayBuffer });   // or hand over bytes
```

In Node there is no `fetch` for `file:` URLs, so pass `wasmBinary` there:

```ts
const wasmBinary = await readFile('node_modules/bravebeats/dist/bravebeats.wasm');
const { left, right } = await renderToChannels({ seed: 1, duration: 30, wasmBinary });
```

## How it fits together

The module is built with `-sSTANDALONE_WASM`, so there is no Emscripten
JavaScript glue: the output is a plain 170 KB `.wasm` with four trivial
imports. That is what lets the same binary run on the main thread and inside
an `AudioWorklet`, which has no `fetch` and no module loader of its own.

The worklet source ships as a string and is turned into a Blob URL at runtime.
It is not a file because an `AudioWorklet` is loaded from a URL and every
bundler wants different configuration to emit one; a Blob needs none.

The main thread reads the `.wasm` and posts the bytes into the worklet through
`processorOptions`. The node outputs silence for the few blocks it takes to
compile, then renders straight from the module's memory into the output
buffers, with no copies in between.

## Development

```sh
npm run build         # TypeScript to dist/
npm test              # renders in Node and compares against the native binary
npm run demo          # serves demo/ at http://localhost:8080
node browser-check.mjs   # drives the demo in Chromium (needs playwright)
npm run build:standalone # rebuilds bravebeats.html (run by npm run build)
npm run check:standalone # opens bravebeats.html from disk in Chromium
npm run build:wasm    # rebuilds the .wasm (needs Emscripten)
```

`npm test` needs the native binary built first (`cmake --build build` in the
repository root); it renders the same seed both ways and checks they match.
They are not bit-identical, because the two toolchains use different `libm`
implementations, but the difference sits about 70 dB below the signal.

Install Playwright first with `npm install -D playwright` (it is not a default
dependency because it downloads a browser). `browser-check.mjs` covers what the Node tests cannot: an `AudioWorklet` only
exists in a browser, and its global scope is missing things the main thread
takes for granted.
