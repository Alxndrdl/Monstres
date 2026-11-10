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

module.exports = { openGame, state };
