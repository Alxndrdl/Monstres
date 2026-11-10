// @ts-check
// Visual non-regression: the rendered page must match the reference images in
// visual.spec.js-snapshots/ (one per browser). After an intended visual change,
// update them with `npm run test:update` and review the new images.
const { test, expect } = require('@playwright/test');
const { openGame, walk } = require('./helpers');

test('start view for seed 42', async ({ page }) => {
  await openGame(page, '?seed=42&new&frozen');
  await expect(page).toHaveScreenshot('start-seed42.png');
});

test('after walking and turning the camera', async ({ page }) => {
  await openGame(page, '?seed=42&new&frozen');
  await walk(page, 0.4, 1, 90);
  await page.evaluate(() => window.monstres.setCamera(2.2, 0.7));
  await expect(page).toHaveScreenshot('walked-seed42.png');
});

test.describe('phone', () => {
  test.use({ hasTouch: true, isMobile: true, viewport: { width: 390, height: 844 } });
  test.skip(({ browserName }) => browserName === 'firefox', 'Firefox has no mobile emulation');

  test('start view on a phone', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    await expect(page).toHaveScreenshot('start-seed42-phone.png');
  });
});
