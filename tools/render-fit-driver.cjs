// Development-only portrait fitting driver. No runtime Playwright dependency.
// It uses the standalone head-only canvas, never the game/network/creator UI.
const path = require('path');
const fs = require('fs');
const readline = require('readline');
const root = path.resolve(__dirname, '..');
const { chromium } = require(path.join(root, '.cache/browser/node_modules/playwright-core'));

async function openSession() {
  const browser = await chromium.launch({
    executablePath: process.env.CHROMIUM || '/tmp/chromium', headless: true,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--use-angle=swiftshader-webgl', '--enable-unsafe-swiftshader'],
  });
  const page = await browser.newPage({ viewport: { width: 480, height: 640 }, deviceScaleFactor: 1 });
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
  const base = process.env.GTC_URL || 'http://127.0.0.1:1453';
  await page.goto(process.env.FIT_URL || new URL('/fit.html', base).toString(), { waitUntil: 'networkidle' });
  await page.waitForFunction(() => window.fitHarness?.ready === true);
  return { browser, page, errors };
}

async function render(session, candidates, keys) {
  for (const key of keys) {
    const candidate = candidates[key];
    if (!candidate) throw new Error(`Missing candidate ${key}`);
    await session.page.evaluate(({ key, candidate }) => window.fitHarness.apply(key, candidate), { key, candidate });
    await session.page.waitForFunction(() => window.fitHarness.assetsReady(), null, { timeout: 30000 });
    await session.page.evaluate(() => {
      window.fitHarness.draw();
      return new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    });
    await session.page.screenshot({ path: path.join(root, 'build', `render-fit-${key}.png`) });
  }
  if (session.errors.length) {
    const message = session.errors.join('\n');
    session.errors.length = 0;
    throw new Error(message);
  }
}

async function main() {
  const session = await openSession();
  if (process.argv.includes('--serve')) {
    process.stdout.write(`${JSON.stringify({ ready: true })}\n`);
    const lines = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    for await (const line of lines) {
      if (!line.trim()) continue;
      let response;
      try {
        const request = JSON.parse(line);
        await render(session, request.candidates, request.keys);
        response = { ok: true };
      } catch (error) {
        response = { ok: false, error: error.stack || error.message };
      }
      process.stdout.write(`${JSON.stringify(response)}\n`);
    }
  } else {
    const file = process.env.FIT_FILE || path.join(root, 'build', 'render-fit-trial.json');
    const candidates = JSON.parse(fs.readFileSync(file, 'utf8'));
    const only = (process.env.FIT_ONLY || '').split(',').filter(Boolean);
    const keys = only.length ? only : Object.keys(candidates);
    await render(session, candidates, keys);
    console.log('rendered', keys.join(','));
  }
  await session.browser.close();
}

main().catch(error => { console.error(error); process.exit(1); });
