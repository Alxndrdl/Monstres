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

test('the village', async ({ page }) => {
  await openGame(page, '?seed=42&new&frozen');
  await walk(page, 0, 1, 130);  // north along the path
  await page.evaluate(() => {
    window.monstres.step(60);
    window.monstres.setCamera(0.3, 0.32);
  });
  await expect(page).toHaveScreenshot('village-seed42.png');
});

test('the ruins of the tower', async ({ page }) => {
  await openGame(page, '?seed=42&new&frozen');
  await page.evaluate(() => window.monstres.setCamera(Math.PI, 0.25));  // facing south
  await walk(page, 0, 1, 480);
  await page.evaluate(() => {
    window.monstres.step(30);
    window.monstres.setCamera(Math.PI - 1.2, 0.2);
  });
  await expect(page).toHaveScreenshot('ruins-seed42.png');
});

test.describe('phone', () => {
  test.skip(({ browserName }) => browserName === 'firefox', 'Firefox has no mobile emulation');

  test.describe('landscape', () => {
    test.use({ hasTouch: true, isMobile: true, viewport: { width: 844, height: 390 } });
    test('start view on a phone', async ({ page }) => {
      await openGame(page, '?seed=42&new&frozen');
      await expect(page).toHaveScreenshot('start-seed42-phone-landscape.png');
    });
  });

  test.describe('portrait', () => {
    test.use({ hasTouch: true, isMobile: true, viewport: { width: 390, height: 844 } });
    test('asks to turn the phone', async ({ page }) => {
      await openGame(page, '?seed=42&new&frozen');
      await page.evaluate(() => document.getAnimations().forEach((a) => a.pause()));
      await expect(page).toHaveScreenshot('rotate-phone-portrait.png');
    });
  });
});
