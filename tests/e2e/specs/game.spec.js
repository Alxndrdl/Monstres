// @ts-check
// Functional tests in real browsers: the game as a player uses it.
const { test, expect } = require('@playwright/test');
const { openGame, state } = require('./helpers');

test('the game starts with WebGL2 and shows the HUD', async ({ page }) => {
  await openGame(page, '?seed=42&new&frozen');
  const s = await state(page);
  expect(s.tick).toBe(0);
  expect(s.player).toMatchObject({ x: 8, y: 6, hp: 10, score: 0 });
  expect(s.monsters).toHaveLength(3);
  await expect(page.locator('#hud')).toHaveText('PV 10/10 · Score 0');
});

test('arrow keys move the player', async ({ page }) => {
  await openGame(page, '?seed=42&new');
  await page.keyboard.press('ArrowRight');
  await expect.poll(async () => (await state(page)).player.x).toBe(9);
  await page.keyboard.press('ArrowDown');
  await expect.poll(async () => (await state(page)).player.y).toBe(7);
});

test('the game resumes from its save after a reload', async ({ page }) => {
  await openGame(page, '?seed=42&new&frozen');
  await page.evaluate(() => {
    window.monstres.step('right');
    window.monstres.step('right');
    window.monstres.step('down');
  });
  const saved = await state(page);
  expect(await page.evaluate(() => window.monstres.saveNow())).toBe(true);

  await openGame(page, '?frozen');
  expect(await state(page)).toEqual(saved);
});

test('a save can be exported to a file and imported back', async ({ page }) => {
  await openGame(page, '?seed=7&new&frozen');
  await page.evaluate(() => window.monstres.step('left'));
  const exported = await state(page);

  const downloadPromise = page.waitForEvent('download');
  await page.evaluate(() => window.monstres.exportSave({ forceDownload: true }));
  const download = await downloadPromise;
  expect(download.suggestedFilename()).toBe('monstres.sav');
  const file = await download.path();

  await openGame(page, '?seed=99&new&frozen');
  expect(await state(page)).not.toEqual(exported);
  await page.locator('#import-file').setInputFiles(file);
  await expect.poll(() => state(page)).toEqual(exported);
});

test('an invalid save file is refused', async ({ page }) => {
  await openGame(page, '?seed=7&new&frozen');
  const before = await state(page);
  page.once('dialog', (dialog) => dialog.accept());
  await page.locator('#import-file').setInputFiles({
    name: 'bad.sav', mimeType: 'application/octet-stream', buffer: Buffer.from('not a save'),
  });
  await page.waitForTimeout(200);
  expect(await state(page)).toEqual(before);
});

test('the game still starts offline once installed', async ({ page, context, browserName }) => {
  test.skip(browserName === 'webkit', 'service worker offline mode is unreliable in Playwright WebKit');
  await openGame(page, '?seed=42&new&frozen');
  await page.evaluate(async () => {
    await navigator.serviceWorker.ready;
  });
  await page.reload();  // this load goes through the service worker and fills the cache
  await page.evaluate(() => window.monstres.ready);

  await context.setOffline(true);
  await openGame(page, '?frozen');
  expect((await state(page)).player).toMatchObject({ x: 8, y: 6 });
});
