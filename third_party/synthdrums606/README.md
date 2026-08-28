# 606-Inspired Synth Drums (vendored)

Header-only C++ drum synthesis by Matthew Fecher (AnalogMatthew), used here for
every percussion sound in the generator.

- Upstream: https://github.com/analogcode/606-Inspired-Synth-Drums
- Commit: `78b6b0ddb3b011a4f61a396f8cde1781ddcbc8d2` (2026-08-21)
- Licence: MIT, see `LICENSE`

The headers in `include/` are the upstream `Source/` directory copied without
modification. They are pure DSP and contain no samples; every drum sound is
synthesised at render time.

`src/bravebeats/voices/drumkit.hpp` is the only place that talks to them. It
re-casts the kit as a drum circle rather than a drum machine: the toms become
tuned hand drums, the snare is played short and snappy as a rim slap, the
closed hat stands in for a seed rattle, and the open hat, tuned well down and
cut short, rings as the iron bell that carries the timeline.

To update, replace the contents of `include/` with the upstream `Source/`
directory and re-run the tests.
