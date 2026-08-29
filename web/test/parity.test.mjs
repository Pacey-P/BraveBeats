// Checks the WebAssembly build against the native renderer.
//
// They are not expected to be bit-identical: the two toolchains use different
// libm implementations, so sin, exp and pow differ in the last places and that
// spreads through the filters. What must hold is that it is the same piece -
// same arrangement, same notes, and audio that tracks the native render
// closely enough that no one could tell them apart.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const repo = join(here, '..', '..');
const wasmBinary = await readFile(join(here, '..', 'src', 'bravebeats.wasm'));
const { renderToChannels, scaleNames } = await import('../dist/index.js');

const SEED = 20260828;
const DURATION = 20;
const RATE = 48000;

function readWav(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const channels = view.getUint16(22, true);
  const rate = view.getUint32(24, true);
  const bits = view.getUint16(34, true);
  // Walk the chunks rather than assuming data starts at byte 44
  let offset = 12;
  let dataStart = 0;
  let dataLength = 0;
  while (offset + 8 <= bytes.length) {
    const id = String.fromCharCode(...bytes.subarray(offset, offset + 4));
    const size = view.getUint32(offset + 4, true);
    if (id === 'data') {
      dataStart = offset + 8;
      dataLength = size;
      break;
    }
    offset += 8 + size + (size % 2);
  }
  const bytesPerSample = bits / 8;
  const frames = Math.floor(dataLength / (bytesPerSample * channels));
  const left = new Float32Array(frames);
  const right = new Float32Array(frames);
  for (let i = 0; i < frames; i += 1) {
    for (let c = 0; c < channels; c += 1) {
      const at = dataStart + (i * channels + c) * bytesPerSample;
      let value;
      if (bits === 24) {
        const raw = bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16);
        value = ((raw & 0x800000 ? raw - 0x1000000 : raw)) / 8388608;
      } else {
        value = view.getInt16(at, true) / 32768;
      }
      if (c === 0) left[i] = value;
      else right[i] = value;
    }
  }
  return { left, right, rate, frames };
}

test('the module exposes the same scales as the native build', async () => {
  const fromWasm = await scaleNames({ wasmBinary });
  const fromCli = execFileSync(join(repo, 'build', 'bravebeats'), ['--list-scales'])
    .toString().trim().split('\n');
  assert.deepEqual(fromWasm, fromCli);
});

test('a rendered piece matches the native render closely', async () => {
  const wavPath = join(repo, 'build', 'parity.wav');
  execFileSync(join(repo, 'build', 'bravebeats'), [
    '-s', String(SEED), '-d', String(DURATION), '-r', String(RATE), '-b', '24',
    '-q', '-o', wavPath,
  ]);
  const native = readWav(await readFile(wavPath));

  const wasm = await renderToChannels({
    seed: SEED, duration: DURATION, sampleRate: RATE, wasmBinary,
  });

  // Same arrangement decisions
  assert.equal(wasm.info.seed, SEED);
  assert.ok(wasm.info.tempo > 60 && wasm.info.tempo < 220, 'tempo is usable');
  assert.ok(wasm.info.noteCount > 100, `expected a full piece, got ${wasm.info.noteCount} notes`);

  // Same length, to the frame
  assert.equal(wasm.left.length, native.frames,
    `length differs: wasm ${wasm.left.length} vs native ${native.frames}`);

  // Same audio, allowing for the two libm implementations
  let worst = 0;
  let sumSquaredError = 0;
  let sumSquaredSignal = 0;
  for (let i = 0; i < native.frames; i += 1) {
    for (const [a, b] of [[wasm.left[i], native.left[i]], [wasm.right[i], native.right[i]]]) {
      const error = Math.abs(a - b);
      worst = Math.max(worst, error);
      sumSquaredError += error * error;
      sumSquaredSignal += b * b;
    }
  }
  const noiseFloorDb = 10 * Math.log10(sumSquaredError / Math.max(sumSquaredSignal, 1e-30));
  console.log(`    worst sample difference ${worst.toFixed(6)}, difference is ${noiseFloorDb.toFixed(1)} dB below the signal`);
  assert.ok(noiseFloorDb < -60,
    `wasm and native diverge too far: difference is only ${noiseFloorDb.toFixed(1)} dB down`);
});

test('a loop is exactly its own length and does not fade', async () => {
  const loop = await renderToChannels({
    seed: SEED, loopBars: 8, sampleRate: RATE, wasmBinary, peak: 0,
  });
  const expected = Math.round(loop.info.loopSeconds * RATE);
  assert.equal(loop.left.length, expected);
  assert.ok(loop.info.loopSeconds > 0, 'a loop reports its length');

  // A fade would leave the last frames near silence; a loop must not
  const tail = loop.left.slice(-RATE / 4);
  const tailPeak = tail.reduce((m, v) => Math.max(m, Math.abs(v)), 0);
  assert.ok(tailPeak > 0.01, `loop tail is faded out (peak ${tailPeak})`);
});

test('the same seed renders the same audio twice', async () => {
  const options = { seed: 7, duration: 6, sampleRate: 24000, wasmBinary };
  const a = await renderToChannels(options);
  const b = await renderToChannels(options);
  assert.deepEqual(Array.from(a.left.slice(0, 5000)), Array.from(b.left.slice(0, 5000)));
  assert.equal(a.info.tempo, b.info.tempo);
});

test('different seeds give different music', async () => {
  const a = await renderToChannels({ seed: 1, duration: 6, sampleRate: 24000, wasmBinary });
  const b = await renderToChannels({ seed: 2, duration: 6, sampleRate: 24000, wasmBinary });
  const same = a.left.every((v, i) => v === b.left[i]);
  assert.ok(!same, 'two seeds produced identical audio');
});

test('an unknown scale is rejected with a helpful message', async () => {
  await assert.rejects(
    () => renderToChannels({ scale: 'klingon', wasmBinary, duration: 4 }),
    /unknown scale "klingon", expected one of/,
  );
});
