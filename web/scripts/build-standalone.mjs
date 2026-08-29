// Builds bravebeats.html: one file you can open straight from disk.
//
// The module and the WebAssembly are both inlined, so the page fetches
// nothing and works with no server, no build and no network.
import { readFile, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { join } from 'node:path';
import { build } from 'esbuild';

const here = fileURLToPath(new URL('.', import.meta.url));
const web = join(here, '..');
const repo = join(web, '..');

const bundled = await build({
  entryPoints: [join(web, 'src', 'index.ts')],
  bundle: true,
  format: 'iife',
  globalName: 'BraveBeats',
  target: 'es2022',
  write: false,
  minify: true,
  legalComments: 'none',
  // A plain script bundle has no module URL. The page always hands over the
  // inlined bytes, and loadWasmBytes reports it clearly if anything ever does not
  define: { 'import.meta.url': '""' },
});
const script = bundled.outputFiles[0].text;

const wasm = await readFile(join(web, 'src', 'bravebeats.wasm'));
const wasmBase64 = wasm.toString('base64');

const template = await readFile(join(web, 'demo', 'standalone.template.html'), 'utf8');
const page = template
  .replace('/*__BUNDLE__*/', () => script)
  .replace('__WASM_BASE64__', () => wasmBase64);

const out = join(repo, 'bravebeats.html');
await writeFile(out, page);

const kb = (n) => `${Math.round(n / 1024)} KB`;
console.log(`build-standalone: wrote ${out} (${kb(page.length)})`);
console.log(`  module ${kb(script.length)}, wasm ${kb(wasm.length)} inlined as ${kb(wasmBase64.length)} of base64`);
