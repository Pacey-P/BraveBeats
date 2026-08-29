# BraveBeats

A procedural tribal music generator. Give it a seed and it writes a piece:
a polyrhythmic drum-circle groove with interlocking hand drums, an iron bell
timeline, a balafon ostinato, kalimba counter-lines, a flute calling over the
top, sung voices and a low drone underneath. Everything is synthesised at
render time — there are no samples anywhere in this repository.

Percussion is played on [606-Inspired Synth Drums][drums] by Matthew Fecher,
vendored under `third_party/`.

[drums]: https://github.com/analogcode/606-Inspired-Synth-Drums

There are two ways to use it: a command line tool that writes a WAV, and a
TypeScript module for the browser that can also follow your game.

## Try it

Open [`bravebeats.html`](bravebeats.html) in a browser. That is the whole
thing: press play, drag the intensity slider and watch the ensemble fill in
and thin out. No build, no server, no network - the generator and its
WebAssembly are both baked into that one 249 KB file.

Browsers will not load an audio worklet into a page opened straight off disk,
so the file falls back to running on the main thread and says so. It sounds
identical but can stutter if the browser gets busy. For the smooth path, serve
the folder over http:

```sh
python3 -m http.server 8000    # then open http://localhost:8000/bravebeats.html
```

For a WAV on the command line:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/bravebeats -s 20260828 -d 60 -o tribal.wav
```

Needs a C++14 compiler and nothing else. There are no dependencies to fetch.

## Command line

```
  -o, --out PATH       output WAV file (default tribal.wav)
  -s, --seed N         seed for the whole piece (default 20260828)
  -d, --duration SEC   length in seconds, rounded to whole bars (default 150)
  -t, --tempo BPM      tempo, 0 lets the seed choose (default 0)
  -m, --meter N        12 for a 12/8 feel, 16 for 4/4, 0 for either
      --scale NAME     scale name, empty lets the seed choose
      --root MIDI      root note as a MIDI number, e.g. 55
  -r, --rate HZ        sample rate (default 48000)
  -b, --bits N         16 or 24 bit output (default 24)
      --room AMOUNT    reverb size, 0 to 1 (default 0.80)
      --peak AMOUNT    peak level of the final mix, 0 to leave it alone
      --solo PART      render only one part, e.g. bell or balafon
      --list-scales    print the available scales and exit
  -q, --quiet          only print errors
```

A reverb tail of about four seconds is added after the last note, and lengths
are rounded up to whole bars with a four-bar minimum, so very short requests
come back a little longer than asked.

The seed decides everything — meter, tempo, scale, every pattern, the whole
arrangement — so the same seed always renders the same piece, sample for
sample. Anything you pin on the command line overrides the seed's choice for
that one thing and leaves the rest alone.

```sh
./build/bravebeats -s 7 -m 12 -t 96 --scale minor-pentatonic --root 50
./build/bravebeats -s 7 --solo bell --peak 0 -o bell.wav   # one part, raw level
```

## How the music is built

### Rhythm

Parts are generated as Euclidean rhythms. Spreading `k` onsets as evenly as
possible over `n` steps reproduces a surprising number of traditional
timelines, and Bjorklund's algorithm in `music/euclid.hpp` gives them exactly:
`E(3,8)` is the tresillo, `E(5,8)` the cinquillo, and `E(7,12)` the West
African bell pattern that most of these grooves hang off. The tests pin all
three.

The ensemble is built the way a drum circle is, not the way a drum machine is:

| Part | Role | Played on |
| --- | --- | --- |
| bell | the timeline everything else is heard against | open hat, tuned down |
| kick | floor drum on the ground pulse | bass drum |
| low / mid / high drum | interlocking tuned hand drums | toms |
| slap | the sharp answer at the end of a phrase | snare, short and snappy |
| clap | hands around the circle | analog clap |
| shaker | seed rattle on the subdivision | closed hat, tuned down |

The important part is that **layers do not all share the bar**. The bell locks
to the bar, but the mid drum runs on a cycle of 8, 9 or 10 pulses against a bar
of 12, and the shaker often runs on 5 or 7. Those cycles do not divide the bar,
so the parts drift against each other and only come back around every few bars.
That drift is the whole point — it is what a written-out 12/8 loop cannot do.

Timing is not quantised. Every part carries a constant push or lay-back of a
few milliseconds (the bell leads, the low drum sits behind), each stroke gets
its own small drift, inner pulses are pushed slightly late, and quiet ghost
strokes appear and drop out with the intensity.

### Pitch

One mode for the whole piece, weighted towards gapped pentatonic scales because
they leave more room under the drums. The balafon plays a walking ostinato that
is nudged every fourth bar so it never quite repeats; the kalimba takes the
pulses the balafon leaves alone; the flute calls at the ends of phrases and
through the quiet stretches; the voices hold a slow modal progression, sung an
octave below the balafon so the two do not mask each other.

### Form

A fixed arc — invocation, first call, gathering, circle, ascent, peak, hollow,
return, embers — stretched to whatever length you asked for. Layers have no
on/off switches in the arrangement: each one joins when the section intensity
passes its own entry point and plays quietly for a while after it arrives, so
the ensemble fills in and thins out on its own. The *hollow* drops the drums
entirely and leaves the drone and the flute.

## How the sound is made

Percussion comes from the vendored 606 voices. Everything else is written here,
in `src/bravebeats/voices/`:

- **balafon and kalimba** (`struck.hpp`) — modal synthesis. A struck bar rings
  on a small set of inharmonic partials, and the ratios are most of what
  separates wood from metal: a tuned balafon bar is carved underneath to pull
  its second mode up to two octaves, giving `1 : 4 : 9.2`, while a kalimba tine
  clamped at one end gives the much wider `1 : 6.27 : 17.55`. The balafon's
  gourds carry a membrane that rattles on loud strokes, so it does too.
- **flute** (`wind.hpp`) — a tone plus a lot of moving air, with the breath
  noise filtered around the note itself and thinning as the note settles.
  Vibrato only opens up after the note has been held a moment.
- **drone** (`wind.hpp`) — a buzzed-lip source through three moving resonances,
  with circular breathing showing up as periodic pushes of pressure rather than
  a break in the sound.
- **chant** (`chant.hpp`) — formant synthesis. A vowel is mostly three
  resonances over a buzzing glottal source, so that is how it is built, sliding
  from one vowel towards another while the note is held. Three slightly
  detuned copies run at once, because a group of singers never lands on one
  pitch.

Each part is summed on its own before it reaches the mix, so the sends differ:
the floor drum stays dry and close while the pipe and the voices sit back in
the room. Drums run through a shared compressor and a little drive; the master
ends on a lookahead limiter whose ceiling is a guarantee, not an aim.

## In a game

`web/` is the generator compiled to WebAssembly with a typed API, so a seed
means the same music there as on the command line. Full documentation is in
[`web/README.md`](web/README.md).

```ts
import { createAdaptive } from 'bravebeats';

const track = await createAdaptive(new AudioContext(), { seed: 7, loopBars: 8 });
track.connect(context.destination);

track.intensity = threatLevel;   // 0 to 1, safe to set every frame
```

The track loops with no seam, and intensity decides how much of the ensemble
is playing: the drone and the bell hold the bottom, and the drums, balafon,
voices and hands join as it rises. Nothing restarts when it changes, because
the voices and both effects run continuously across the loop point; only which
notes get played changes. There is also `renderTrack()`, which hands back a
finished `AudioBuffer` for music that does not need to react.

The module is a plain 170 KB `.wasm` with no Emscripten glue, which is what
lets it run inside an `AudioWorklet`. The `.wasm` is committed, so you do not
need Emscripten unless you change the C++.

`bravebeats.html` at the root is that same module and wasm inlined into one
file, rebuilt by `npm run build` in `web/`. It is the quickest way to hear what
a seed sounds like before wiring anything up.

## Layout

```
src/bravebeats/
  core/      random, dsp, oscillators, wav writing
  music/     euclidean rhythms, scales, arrangement, the composer
  voices/    the drum kit wrapper and the synthesised melodic voices
  engine/    the renderer and mix bus
src/wasm/    the C surface the WebAssembly build exposes
web/         the TypeScript module, its tests and a browser demo
tests/       self-checks
third_party/ vendored 606 drum DSP (MIT)
```

`music/composer.hpp` turns a seed into a list of notes and touches no audio at
all. `engine/renderer.hpp` takes that list and plays it, either straight
through for a finished piece or a block at a time for a live host. The split
means you can inspect or change what is being played without going near the
DSP, and it is what makes the adaptive mode possible: the notes are all written
down in advance, and intensity decides which of them are heard.

## Tests

```sh
cmake --build build && ./build/bravebeats_tests
```

200 checks covering the Euclidean patterns against the traditional rhythms they
should reproduce, scale and RNG behaviour, filter and limiter stability, the
WAV header, and the determinism promise — that one seed gives one piece, and
that it renders to identical audio every time.

## Licence

MIT, see `LICENSE`. The vendored drum DSP is MIT, copyright Matthew Fecher.
