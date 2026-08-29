// Drives the demo in a real browser and checks that sound comes out.
//
// The worklet path cannot be covered by the Node tests: an AudioWorklet only
// exists in a browser, and its global scope is missing things the main thread
// takes for granted, which is exactly where this has broken before.
//
// Usage: npm run demo (in one shell), then node browser-check.mjs
import { chromium } from 'playwright';

const url = process.env.DEMO_URL ?? 'http://localhost:8080/';
const browser = await chromium.launch({
  executablePath: process.env.CHROMIUM ?? '/opt/pw-browsers/chromium',
  args: ['--autoplay-policy=no-user-gesture-required'],
});
const page = await browser.newPage();

const problems = [];
page.on('pageerror', (error) => problems.push(`page error: ${error.message}`));
page.on('console', (message) => {
  const text = message.text();
  if (message.type() === 'error' && !text.includes('favicon')) {
    problems.push(`console error: ${text}`);
  }
});

const fail = (message) => { problems.push(message); };

await page.goto(url, { waitUntil: 'networkidle' });

const scaleOptions = await page.locator('#scale option').count();
console.log(`scales offered: ${scaleOptions - 1}`);
if (scaleOptions < 2) fail('the module did not load its scale list');

// Measures what the node is actually putting out, over about a second
async function measurePeak() {
  return page.evaluate(() => new Promise((resolve) => {
    const analyser = window.__ctx.createAnalyser();
    analyser.fftSize = 2048;
    window.__track.node.connect(analyser);
    const data = new Float32Array(analyser.fftSize);
    let peak = 0;
    let ticks = 0;
    const tick = () => {
      analyser.getFloatTimeDomainData(data);
      for (const value of data) peak = Math.max(peak, Math.abs(value));
      if (++ticks < 40) setTimeout(tick, 25);
      else { analyser.disconnect(); resolve(peak); }
    };
    tick();
  }));
}

await page.click('#play');
await page.waitForFunction(
  () => document.getElementById('status').textContent.includes('playing'),
  { timeout: 30000 },
).catch(async () => fail(`never started: ${await page.locator('#status').textContent()}`));

const info = (await page.locator('#info').textContent()).replace(/\s+/g, ' ').trim();
console.log(`playing: ${info}`);

const loud = await measurePeak();
console.log(`peak at full intensity: ${loud.toFixed(4)}`);
if (loud < 0.02) fail(`the worklet produced no audio (peak ${loud})`);

// The point of the adaptive node: turning intensity down thins the ensemble
await page.fill('#intensity', '0.12');
await page.dispatchEvent('#intensity', 'input');
await page.waitForTimeout(2500);
const quiet = await measurePeak();
console.log(`peak at intensity 0.12: ${quiet.toFixed(4)}`);
if (!(quiet < loud)) fail(`lowering intensity did not thin the mix (${quiet} vs ${loud})`);

await page.fill('#seed', '4242');
await page.click('#play');
await page.waitForFunction(
  () => document.getElementById('status').textContent.includes('playing'),
  { timeout: 30000 },
).catch(() => fail('reseeding did not restart playback'));
console.log('reseeded and playing again');

await page.click('#stop');
await browser.close();

if (problems.length) {
  console.error('\nFAILED:');
  for (const problem of problems) console.error(`  ${problem}`);
  process.exit(1);
}
console.log('\nbrowser check passed');
