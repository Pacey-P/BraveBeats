# BraveBeats

A procedural tribal music generator. Give it a seed and it writes a piece:
a polyrhythmic drum-circle groove with interlocking hand drums, an iron bell
timeline, a balafon ostinato, kalimba counter-lines, a flute calling over the
top, sung voices and a low drone underneath. Everything is synthesised at
render time — there are no samples anywhere in this repository.

Percussion is played on [606-Inspired Synth Drums][drums] by Matthew Fecher,
vendored under `third_party/`.

[drums]: https://github.com/analogcode/606-Inspired-Synth-Drums

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Needs a C++14 compiler and nothing else. There are no dependencies to fetch.

## Use

```sh
./build/bravebeats -s 20260828 -d 180 -o tribal.wav
```

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

## Layout

```
src/bravebeats/
  core/      random, dsp, oscillators, wav writing
  music/     euclidean rhythms, scales, arrangement, the composer
  voices/    the drum kit wrapper and the synthesised melodic voices
  engine/    the offline renderer and mix bus
tests/       self-checks
third_party/ vendored 606 drum DSP (MIT)
```

`music/composer.hpp` turns a seed into a list of notes and touches no audio at
all. `engine/renderer.hpp` takes that list and plays it. The split means you
can inspect or change what is being played without going near the DSP.

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
