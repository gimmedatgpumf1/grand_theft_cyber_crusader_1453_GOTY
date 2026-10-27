// Development-only render fitting driver. No runtime dependency on Playwright.
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
  const page = await browser.newPage({ viewport: { width: 1280, height: 720 }, deviceScaleFactor: 1 });
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
  await page.goto(process.env.GTC_URL || 'http://127.0.0.1:1453', { waitUntil: 'networkidle' });
  await page.waitForFunction(() => document.getElementById('citizen-id').textContent !== 'CITIZEN --');
  await page.keyboard.press('KeyC');
  await page.waitForSelector('#creator:not(.hidden)');
  await page.locator('[data-creator-focus="face"]').click();
  await page.waitForTimeout(600);
  return { browser, page, errors, setupKey: '' };
}

async function render(session, candidates, keys) {
  for (const key of keys) {
    const candidate = candidates[key];
    if (!candidate) throw new Error(`Missing candidate ${key}`);
    const fields = candidate.fields || {};
    const setupKey = JSON.stringify([candidate.preset || key, fields]);
    if (session.setupKey !== setupKey) {
      await session.page.locator(`[data-preset="${candidate.preset || key}"]`).click();
      await session.page.evaluate(fields => {
        for (const [name, value] of Object.entries(fields)) {
          const input = document.getElementById(`creator-${name}`);
          if (!input) throw new Error(`Unknown appearance field ${name}`);
          if (input.type === 'checkbox') input.checked = Boolean(value);
          else input.value = String(value);
        }
        if (Object.keys(fields).length) {
          document.querySelector('[data-appearance]').dispatchEvent(new Event('change', { bubbles: true }));
        }
      }, fields);
      session.setupKey = setupKey;
    }
    await session.page.evaluate(candidate => {
      for (const [name, value] of Object.entries(candidate.morphs)) {
        const input = document.getElementById(`morph-${name}`);
        if (!input) throw new Error(`Unknown morph ${name}`);
        input.value = String(value);
        document.getElementById(`morph-${name}-value`).textContent = String(value);
      }
      const input = document.querySelector('[data-morph]');
      input.dispatchEvent(new Event('input', { bubbles: true }));
    }, candidate);
    if (typeof candidate.male === 'boolean') {
      const expected = candidate.male ? 'male' : 'female';
      const actual = await session.page.locator('#creator-male').inputValue();
      if (actual !== expected) throw new Error(`${key}: expected ${expected} creator geometry, got ${actual || 'empty select'}`);
    }
    await session.page.waitForTimeout(250);
    await session.page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
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
