// Export product illustrations from the existing local website, using demo data.
// Supply an existing Playwright install via NODE_PATH; no dependencies are installed.
const { chromium } = require('playwright');
const path = require('node:path');
const fs = require('node:fs');

async function main() {
  const base = process.env.LUNA_PREVIEW_URL || 'http://127.0.0.1:8773/design/preview/';
  const url = new URL(base);
  if (!['127.0.0.1', 'localhost', '[::1]'].includes(url.hostname)) {
    throw new Error('Serve the preview on localhost before exporting images.');
  }
  const executablePath = process.env.LUNA_PREVIEW_BROWSER;
  const browser = await chromium.launch({ headless: true, ...(executablePath ? { executablePath } : {}) });
  const output = path.resolve(__dirname, '../docs/images');
  fs.mkdirSync(output, { recursive: true });
  try {
    const page = await browser.newPage({ viewport: { width: 1280, height: 930 },
      timezoneId: 'Asia/Shanghai', reducedMotion: 'reduce' });
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.clock.setFixedTime(new Date('2026-10-08T20:08:00+08:00'));
    await page.goto(base);
    await page.locator('#digital-hm').waitFor();
    await page.evaluate(() => document.fonts.ready);
    await page.selectOption('#scenario', 'normal');
    const capture = async name => {
      await page.locator('#display').screenshot({ path: path.join(output, name + '.png'), animations: 'disabled' });
      console.log('Exported ' + name + '.png');
    };
    await capture('clock');
    for (const [name, button] of [['music', '音乐卡'], ['weather', '天气卡'],
      ['codex', '额度卡'], ['computer', '电脑卡']]) {
      await page.getByRole('button', { name: button, exact: true }).click();
      await capture(name);
      if (name === 'music') {
        await page.locator('[data-action=volume-open]').click();
        await capture('music-volume');
      }
    }
    await page.locator('#display').click({ position: { x: 360, y: 100 } });
    await page.locator('#idle-screen').waitFor({ state: 'visible' });
    await capture('standby');
    if (errors.length) throw new Error(errors.join('\n'));
  } finally {
    await browser.close();
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
