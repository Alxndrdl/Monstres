// @ts-check
// Visual non-regression: the rendered board must match the reference images
// in visual.spec.js-snapshots/ (one per browser). After an intended visual
// change, update them with `npm run test:update` and review the new images.
const { test, expect } = require('@playwright/test');
const { openGame } = require('./helpers');

test('board for seed 42', async ({ page }) => {
  await openGame(page, '?seed=42&new&frozen');
  await expect(page.locator('#canvas')).toHaveScreenshot('board-seed42.png');
});

test('board after a few moves and a fight', async ({ page }) => {
  await openGame(page, '?seed=42&new&frozen');
  await page.evaluate(() => {
    for (const a of ['right', 'right', 'up', 'up', 'up', 'attack', 'attack']) window.monstres.step(a);
  });
  await expect(page.locator('#canvas')).toHaveScreenshot('board-seed42-moves.png');
});
