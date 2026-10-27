// Development-only visual regression capture. Runtime has no Playwright dependency.
const path = require('path');
const fs = require('fs');
const root = path.resolve(__dirname, '..');
const { chromium } = require(path.join(root, '.cache/browser/node_modules/playwright-core'));

(async () => {
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
  await page.waitForTimeout(800);
  const keys = (process.env.PRESETS || 'marilyn,ibrahim,xi,putin,trump,taylor,obama').split(',');
  for (const key of keys) {
    await page.locator(`[data-preset="${key}"]`).click();
    await page.waitForTimeout(1100);
    await page.screenshot({ path: path.join(root, 'build', `creator-${key}.png`) });
    console.log('captured', key);
  }
  await page.locator('[data-preset="marilyn"]').click();
  await page.waitForTimeout(700);
  await page.mouse.move(520, 430);
  await page.mouse.down();
  await page.mouse.move(690, 455, { steps: 8 });
  await page.mouse.up();
  await page.waitForTimeout(500);
  await page.screenshot({ path: path.join(root, 'build', 'creator-rotated.png') });
  await page.mouse.wheel(0, -460);
  await page.waitForTimeout(500);
  await page.screenshot({ path: path.join(root, 'build', 'creator-zoom.png') });
  await page.locator('[data-creator-tab="nose"]').click();
  await page.locator('#morph-noseWidth').evaluate(input => {
    input.value = 55;
    input.dispatchEvent(new Event('input', { bubbles: true }));
    input.dispatchEvent(new Event('change', { bubbles: true }));
  });
  await page.waitForTimeout(700);
  const slider = await page.locator('#morph-noseWidth-value').textContent();
  await page.locator('[data-preset="marilyn"]').click();
  await page.locator('[data-creator-focus="face"]').click();
  await page.locator('[data-creator-tab="style"]').click();
  await page.locator('#creator-hair').selectOption('15');
  await page.waitForTimeout(700);
  await page.screenshot({ path: path.join(root, 'build', 'creator-updo.png') });
  const result = { errors, slider, presets: keys, extraHair: [15] };
  fs.writeFileSync(path.join(root, 'build', 'creator-visuals.json'), JSON.stringify(result, null, 2));
  console.log(JSON.stringify(result));
  await browser.close();
  if (errors.length || slider !== '55') process.exitCode = 1;
})().catch(error => { console.error(error); process.exit(1); });
