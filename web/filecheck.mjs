// Verifies bravebeats.html plays when opened straight from disk (file://),
// which is the whole point of the standalone build.
import { chromium } from 'playwright';

const page_url = process.argv[2] ?? `file://${new URL('../bravebeats.html', import.meta.url).pathname}`;
const browser = await chromium.launch({
  executablePath: '/opt/pw-browsers/chromium',
  args: ['--autoplay-policy=no-user-gesture-required'],
});
const page = await browser.newPage();
const problems = [];
page.on('pageerror', (e) => problems.push('page error: ' + e.message));
page.on('console', (m) => {
  const text = m.text();
  const isFavicon = text.includes('favicon') || text.includes('404');
  if (m.type() === 'error' && !isFavicon) problems.push('console: ' + text);
});
// Beyond the page itself the file is self-contained, so nothing else should
// be fetched - no wasm, no module, no fonts
page.on('request', (r) => {
  const url = r.url();
  if (url === page_url || url.startsWith('data:') || url.startsWith('blob:')) return;
  if (url.endsWith('/favicon.ico')) return;
  problems.push('unexpected request: ' + url);
});

console.log(`loading ${page_url}`);
await page.goto(page_url, { waitUntil: 'networkidle' });

await page.waitForFunction(() => document.querySelectorAll('#scale option').length > 1, { timeout: 15000 })
  .catch(() => problems.push('the scale list never populated (wasm did not load)'));
console.log('scales offered:', (await page.locator('#scale option').count()) - 1);

await page.click('#play');
await page.waitForFunction(
  () => document.getElementById('status').textContent.includes('playing'),
  { timeout: 30000 },
).catch(async () => problems.push('never started: ' + await page.locator('#status').textContent()));

console.log('info:', (await page.locator('#info').textContent()).replace(/\s+/g, ' ').trim());
const fallbackShown = await page.locator('#fallback-note').isVisible();
console.log('using main-thread fallback:', fallbackShown);

async function peak() {
  return page.evaluate(() => new Promise((resolve) => {
    const data = new Float32Array(window.__analyser.fftSize);
    let best = 0, ticks = 0;
    const tick = () => {
      window.__analyser.getFloatTimeDomainData(data);
      for (const v of data) best = Math.max(best, Math.abs(v));
      if (++ticks < 30) setTimeout(tick, 30); else resolve(best);
    };
    tick();
  }));
}
const loud = await peak();
console.log('output peak at full intensity:', loud.toFixed(4));
if (loud < 0.05) problems.push(`no audio came out (peak ${loud})`);

await page.fill('#intensity', '0.1');
await page.dispatchEvent('#intensity', 'input');
await page.waitForTimeout(2500);
const quiet = await peak();
console.log('output peak at intensity 0.10:', quiet.toFixed(4));
if (!(quiet < loud)) problems.push(`lowering intensity did not thin the mix (${quiet} vs ${loud})`);

await page.click('#stop');
await browser.close();

if (problems.length) {
  console.error('\nFAILED:');
  for (const p of problems) console.error('  ' + p);
  process.exit(1);
}
console.log('\nfile:// check passed');
