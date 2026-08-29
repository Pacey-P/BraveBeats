#!/usr/bin/env bash
# Compiles the C++ generator to a standalone WebAssembly module.
#
# Standalone means no Emscripten JavaScript glue: the output is a plain .wasm
# with four trivial imports, which is what lets the same binary run on the main
# thread, in a worker and inside an AudioWorklet.
#
# Needs Emscripten on PATH: https://emscripten.org/docs/getting_started
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
out="$here/../src/bravebeats.wasm"

if ! command -v em++ > /dev/null; then
  echo "build-wasm: em++ not found. Install and activate the Emscripten SDK first." >&2
  exit 1
fi

exported='[
  "_bb_create","_bb_destroy","_bb_set_intensity","_bb_get_intensity","_bb_render",
  "_bb_left","_bb_right","_bb_max_block","_bb_loop_seconds","_bb_total_seconds",
  "_bb_tempo","_bb_bar_seconds","_bb_pulses_per_bar","_bb_total_bars","_bb_root",
  "_bb_note_count","_bb_scale_of","_bb_scale_count","_bb_scale_name",
  "_bb_section_count","_bb_section_name","_bb_section_bars"
]'

em++ -std=c++14 -O3 \
  -I "$root/src" -I "$root/third_party/synthdrums606/include" \
  "$root/src/wasm/bravebeats_c.cpp" \
  -o "$out" \
  -sSTANDALONE_WASM --no-entry \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=16MB \
  -sEXPORTED_FUNCTIONS="$(echo "$exported" | tr -d ' \n')" \
  -fno-exceptions -fno-rtti

echo "build-wasm: wrote $out ($(du -h "$out" | cut -f1))"
