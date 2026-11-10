// @ts-check

/**
 * Opens the game and waits until main() has finished starting.
 * @param {import('@playwright/test').Page} page
 * @param {string} query e.g. '?seed=42&new&frozen'
 */
async function openGame(page, query = '') {
  await page.goto(query);
  await page.evaluate(() => window.monstres.ready);
}

/** @param {import('@playwright/test').Page} page */
function state(page) {
  return page.evaluate(() => window.monstres.state());
}

/**
 * With ?frozen: sets the joystick, runs `ticks` ticks, releases the joystick.
 * @param {import('@playwright/test').Page} page
 */
function walk(page, x, y, ticks) {
  return page.evaluate(([mx, my, n]) => {
    window.monstres.input.move(mx, my);
    window.monstres.step(n);
    window.monstres.input.move(0, 0);
  }, [x, y, ticks]);
}

/** Center of an element, in page coordinates. */
async function center(locator) {
  const box = await locator.boundingBox();
  if (!box) throw new Error('element not visible');
  return { x: box.x + box.width / 2, y: box.y + box.height / 2 };
}

module.exports = { openGame, state, walk, center };
