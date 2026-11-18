// @ts-check
// Functional tests in real browsers: the game as a player uses it.
const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');
const { test, expect } = require('@playwright/test');
const { openGame, state, walk, center } = require('./helpers');

// The character creation (create.js), from its first step: a name, then
// through the steps; the world's seed; the arrival's flight skipped.
async function createCharacter(page, { name, seed, starter = 0, woman = false } = {}) {
  await expect(page.locator('#create')).toBeVisible();
  await page.locator('#create-name').fill(name);
  await page.locator('#create-next').click();
  if (woman) await page.locator('#create-sex .choice').nth(1).click();
  await page.locator('#create-next').click();
  await page.locator('#create-next').click();
  await page.locator('#create-starters .starter').nth(starter).click();
  await page.locator('#create-next').click();
  if (seed !== undefined) await page.locator('#create-seed').fill(seed);
  await expect(page.locator('#create-next')).toHaveText('Commencer l’aventure');
  await page.locator('#create-next').click();
  await expect(page.locator('#create')).toBeHidden();
  await expect(page.locator('#skip-intro')).toBeVisible();
  await page.locator('#skip-intro').click();
  await expect(page.locator('#skip-intro')).toBeHidden();
}

test('the game starts in a 3D world', async ({ page }) => {
  await openGame(page, '?seed=42&new&frozen');
  const s = await state(page);
  expect(s.tick).toBe(0);
  expect(s.player).toMatchObject({ x: 0, z: 0, onGround: true });
  expect(s.player.y).toBe(s.player.ground);  // standing on the hills
  expect(s.objects).toBeGreaterThan(1500);  // nature, village, ruins, castle, cave
  expect(s.creatures[0].species).toBe('companion');
  const bySpecies = {};
  for (const c of s.creatures) bySpecies[c.species] = (bySpecies[c.species] || 0) + 1;
  expect(bySpecies.villager).toBe(17);  // with Isidore, the old magician, and the west's eight
  expect(bySpecies.companion).toBeGreaterThanOrEqual(1);  // the companion (and now and then a wild Pompon)
  expect(bySpecies.crow).toBeGreaterThan(0);
  expect(bySpecies.fish).toBeGreaterThan(0);
  // Wild monsters only around the player (the rest of the world is empty until it goes there).
  expect(bySpecies.slime + bySpecies.mushroom + bySpecies.bird).toBeGreaterThan(15);
  expect(bySpecies.slime + bySpecies.mushroom + bySpecies.bird).toBeLessThan(80);
  await expect(page.locator('#message')).toBeHidden();
});

test('the companion follows the player', async ({ page }) => {
  await openGame(page, '?seed=42&new&frozen');
  await walk(page, 0.3, 1, 120);  // about 10 m
  await page.evaluate(() => window.monstres.step(120));  // then wait 2 s
  const { player, creatures } = await state(page);
  const companion = creatures[0];
  expect(Math.hypot(companion.x - player.x, companion.z - player.z)).toBeLessThan(2.5);
});

test('wild monsters move around on their own', async ({ page }) => {
  await openGame(page, '?seed=42&new&frozen');
  const wild = (s) => s.creatures.filter((c) => ['slime', 'mushroom', 'bird'].includes(c.species));
  const before = wild(await state(page));
  await page.evaluate(() => window.monstres.step(600));  // 10 s
  const after = wild(await state(page));
  const moved = after.filter((c, i) => Math.hypot(c.x - before[i].x, c.z - before[i].z) > 0.5);
  expect(moved.length).toBeGreaterThan(8);
});

test.describe('keyboard', () => {
  test('Z/W or the up arrow walks forward', async ({ page }) => {
    await openGame(page, '?seed=42&new');
    await page.keyboard.down('KeyW');  // physical key: Z on AZERTY
    await expect.poll(async () => (await state(page)).player.z).toBeLessThan(-1);
    await page.keyboard.up('KeyW');

    const z = (await state(page)).player.z;
    await page.keyboard.down('ArrowDown');
    await expect.poll(async () => (await state(page)).player.z).toBeGreaterThan(z + 1);
    await page.keyboard.up('ArrowDown');
  });

  test('space jumps', async ({ page }) => {
    await openGame(page, '?seed=42&new');
    await page.keyboard.press('Space');
    await expect.poll(async () => { const p = (await state(page)).player; return p.y - p.ground; }).toBeGreaterThan(0.3);
    await expect.poll(async () => (await state(page)).player.onGround).toBe(true);
  });

  test('movement is relative to the camera', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    await page.evaluate(() => window.monstres.setCamera(Math.PI / 2, 0.35));  // looking towards -X
    await page.keyboard.down('KeyW');
    await page.evaluate(() => window.monstres.step(30));
    await page.keyboard.up('KeyW');
    const { player } = await state(page);
    expect(player.x).toBeLessThan(-2);
    expect(Math.abs(player.z)).toBeLessThan(0.01);
  });
});

test.describe('on-screen controls', () => {
  test('dragging the joystick up walks forward, releasing it stops', async ({ page }) => {
    await openGame(page, '?seed=42&new');
    const joystick = await center(page.locator('#joystick'));
    await page.mouse.move(joystick.x, joystick.y);
    await page.mouse.down();
    await page.mouse.move(joystick.x, joystick.y - 60, { steps: 5 });
    await expect.poll(async () => (await state(page)).player.z).toBeLessThan(-1);
    await page.mouse.up();

    const stopped = (await state(page)).player.z;
    await page.waitForTimeout(300);
    expect((await state(page)).player.z).toBeCloseTo(stopped, 3);
    await expect(page.locator('#knob')).toHaveCSS('transform', 'none');
  });

  test('the jump button jumps', async ({ page }) => {
    await openGame(page, '?seed=42&new');
    await page.locator('#jump').click();
    await expect.poll(async () => { const p = (await state(page)).player; return p.y - p.ground; }).toBeGreaterThan(0.3);
  });

  test('dragging on the view turns the camera', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    const viewport = page.viewportSize();
    const cx = viewport.width / 2;
    const cy = viewport.height / 3;
    await page.mouse.move(cx, cy);
    await page.mouse.down();
    await page.mouse.move(cx + 100, cy + 40, { steps: 5 });
    await page.mouse.up();
    const { camera } = await state(page);
    expect(camera.yaw).toBeLessThan(-0.3);    // dragged right: turned right
    expect(camera.pitch).toBeGreaterThan(0.4); // dragged down: looks further down
  });

  test('keyboard hints are shown on a computer', async ({ page, browserName }) => {
    test.skip(browserName === 'firefox', 'headless Firefox reports no mouse (pointer: none)');
    await openGame(page, '?seed=42&new&frozen');
    await expect(page.locator('#move-hint')).toBeVisible();
    await expect(page.locator('#move-hint kbd')).toHaveCount(4);  // on the joystick's sides
    await expect(page.locator('#jump kbd')).toHaveText('Espace');  // in the jump button
    await expect(page.locator('#jump kbd')).toBeVisible();
  });

  test('keyboard hints appear once a key is pressed', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    await page.keyboard.press('KeyW');
    await expect(page.locator('#move-hint')).toBeVisible();
    await expect(page.locator('#jump kbd')).toBeVisible();
  });
});

test.describe('on a phone in landscape', () => {
  test.use({ hasTouch: true, isMobile: true, viewport: { width: 844, height: 390 } });
  test.skip(({ browserName }) => browserName === 'firefox', 'Firefox has no mobile emulation');

  test('the controls are shown, keyboard hints are not', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    await expect(page.locator('#joystick')).toBeVisible();
    await expect(page.locator('#jump')).toBeVisible();
    await expect(page.locator('#move-hint')).toBeHidden();
    await expect(page.locator('#rotate')).toBeHidden();
  });

  test('tapping the jump button jumps', async ({ page }) => {
    await openGame(page, '?seed=42&new');
    await page.locator('#jump').tap();
    await expect.poll(async () => { const p = (await state(page)).player; return p.y - p.ground; }).toBeGreaterThan(0.3);
  });

  test('a button works while the other thumb holds the joystick', async ({ page, browserName }) => {
    test.skip(browserName !== 'chromium', 'two fingers at once: through Chromium\'s DevTools protocol');
    await openGame(page, '?seed=42&new&frozen');
    await expect(page.locator('.team-icon[data-slot="0"]')).toHaveAttribute('aria-pressed', 'true');
    const stick = await center(page.locator('#joystick'));
    const icon = await center(page.locator('.team-icon[data-slot="0"]'));
    const cdp = await page.context().newCDPSession(page);
    const thumb = { x: stick.x, y: stick.y - 30, id: 1 };
    const finger = { x: icon.x, y: icon.y, id: 2 };
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [thumb] });
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [thumb, finger] });
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [finger] });
    // Pompon goes back into the crystal, the joystick still held.
    await expect.poll(async () => (await state(page)).active).toBe(-1);
    await expect(page.locator('#joystick')).toHaveClass(/active/);
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [thumb] });
  });
});

test.describe('on a phone in portrait', () => {
  test.use({ hasTouch: true, isMobile: true, viewport: { width: 390, height: 844 } });
  test.skip(({ browserName }) => browserName === 'firefox', 'Firefox has no mobile emulation');

  test('asks to turn the phone', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    await expect(page.locator('#rotate')).toBeVisible();
    await expect(page.locator('#rotate')).toContainText('Tournez votre téléphone');
  });
});

test('a computer with a tall window is not asked to turn', async ({ page }) => {
  await page.setViewportSize({ width: 500, height: 800 });
  await openGame(page, '?seed=42&new&frozen');
  await expect(page.locator('#rotate')).toBeHidden();
});

test.describe('saves', () => {
  test('the game resumes from its save after a reload', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    await walk(page, 0.5, 1, 40);
    const saved = await state(page);
    expect(await page.evaluate(() => window.monstres.saveNow())).toBe(true);

    await openGame(page, '?frozen');
    const resumed = await state(page);
    expect(resumed.player).toEqual(saved.player);
    expect(resumed.tick).toBe(saved.tick);
  });

  test('a save can be exported to a file and imported back into another slot', async ({ page }) => {
    await openGame(page, '?seed=7&new&frozen');
    await walk(page, -1, 0, 30);
    const exported = (await state(page)).player;

    const downloadPromise = page.waitForEvent('download');
    await page.evaluate(() => window.monstres.exportSave({ forceDownload: true }));
    const download = await downloadPromise;
    expect(download.suggestedFilename()).toBe('monstres.sav');
    const file = await download.path();

    await openGame(page, '?seed=99&new&frozen');
    expect((await state(page)).player).not.toEqual(exported);
    await page.locator('#settings-button').click();
    await page.locator('#saves-open').click();
    const slots = page.locator('#saves-list .save-slot');
    await expect(slots).toHaveCount(3);
    // The file goes into the second slot (empty); the game played does not change.
    const chooser = page.waitForEvent('filechooser');
    await slots.nth(1).locator('.save-import').click();
    await (await chooser).setFiles(file);
    await expect(page.locator('#saves-note')).toContainText('importée dans l’emplacement 2');
    await expect(slots.nth(1)).toContainText('Niveau 1');
    expect((await state(page)).player).not.toEqual(exported);
    // Played: the world is the exported one.
    await slots.nth(1).locator('.save-play').click();
    await expect(page.locator('#saves')).toBeHidden();
    await expect.poll(async () => (await state(page)).player).toEqual(exported);
  });

  test('an invalid save file is refused', async ({ page }) => {
    await openGame(page, '?seed=7&new&frozen');
    await walk(page, 1, 0, 10);
    const before = await state(page);
    await page.locator('#settings-button').click();
    await page.locator('#saves-open').click();
    const chooser = page.waitForEvent('filechooser');
    await page.locator('#saves-list .save-slot').nth(2).locator('.save-import').click();
    await (await chooser).setFiles({
      name: 'bad.sav', mimeType: 'application/octet-stream', buffer: Buffer.from('not a save'),
    });
    await expect(page.locator('#saves-note')).toContainText('pas une sauvegarde Monstres valide');
    await expect(page.locator('#saves-list .save-slot.empty')).toHaveCount(2);
    await page.locator('#saves-close').click();
    expect(await state(page)).toEqual(before);
  });

  test('three slots: the game played, and a new game with a name and a seed', async ({ page }) => {
    test.setTimeout(90000);  // a character created
    await openGame(page, '?seed=42&new');
    await page.locator('#settings-button').click();
    await page.locator('#saves-open').click();
    await expect(page.locator('#settings')).toBeHidden();  // the window takes its place
    const slots = page.locator('#saves-list .save-slot');
    // The game played: its level, size and date; it can be neither played again nor removed.
    await expect(slots.nth(0)).toContainText('en cours');
    await expect(slots.nth(0)).toContainText('Magicien sans nom');
    await expect(slots.nth(0)).toContainText('Niveau 1');
    await expect(slots.nth(0).locator('.save-facts')).toContainText(/\d+ Ko/);
    await expect(slots.nth(0).locator('.save-facts')).toContainText(new RegExp(`${new Date().getFullYear()}`));
    await expect(slots.nth(0).locator('.save-play')).toBeDisabled();
    await expect(slots.nth(0).locator('.save-delete')).toBeDisabled();
    await expect(slots.nth(1)).toContainText('Vide');
    // Time stands still while the window is open.
    const tick = (await state(page)).tick;
    await page.waitForTimeout(300);
    expect((await state(page)).tick).toBe(tick);

    // A new game in the second slot (the character creation): a name is needed; the seed makes the world.
    await slots.nth(1).locator('.save-new').click();
    await expect(page.locator('#saves')).toBeHidden();
    await page.locator('#create-next').click();
    await expect(page.locator('#create-note')).toContainText('Choisis un nom');
    const before = (await state(page)).player;
    await page.locator('#create-name').fill('Zoé');
    await page.locator('#create-name').press('d');  // typing letters does not walk
    expect((await state(page)).player).toEqual(before);
    await createCharacter(page, { name: 'Zoéd', seed: '1234' });
    expect(await page.evaluate(() => window.monstres.wizard().name)).toBe('Zoéd');
    expect((await state(page)).player).toMatchObject({ x: 0, z: 0 });
    await page.locator('#settings-button').click();
    await page.locator('#saves-open').click();
    await expect(slots.nth(1)).toContainText('Zoéd');
    await expect(slots.nth(1)).toContainText('en cours');
  });

  test('three slots: the same seed, the same world; left, reloaded and removed', async ({ page }) => {
    test.setTimeout(90000);  // a reload
    await openGame(page, '?seed=42&new');
    // Two new games (the creation itself: the test above), with the same seed.
    const world = await page.evaluate(async () => {
      const m = window.monstres;
      await m.newGame(1, 'Zoéd', '1234');
      m.skipIntro();
      const path = m.map().path.slice(0, 6);
      await m.newGame(2, 'Léo', '1234');
      m.skipIntro();
      return path;
    });
    expect(await page.evaluate(() => window.monstres.map().path.slice(0, 6))).toEqual(world);

    // The name is shown on the character sheet, and cannot be changed there.
    await page.locator('#menu-button').click();
    await page.getByRole('tab', { name: 'Personnage' }).click();
    await expect(page.locator('#wizard-name')).toHaveText('Léo');
    await expect(page.locator('#tab-character input')).toHaveCount(0);
    await page.keyboard.press('Escape');

    // After a reload, the game played is still the third one.
    await page.evaluate(() => window.monstres.saveNow());
    await openGame(page, '');
    expect(await page.evaluate(() => window.monstres.wizard().name)).toBe('Léo');
    const list = await page.evaluate(() => window.monstres.saves());
    expect(list.map((x) => !!x.current)).toEqual([false, false, true]);
    expect(list.map((x) => x.name)).toEqual(['', 'Zoéd', 'Léo']);
    expect(list[1].seed).toBe('1234');

    // The second one is removed, after a confirmation.
    const slots = page.locator('#saves-list .save-slot');
    await page.locator('#settings-button').click();
    await page.locator('#saves-open').click();
    await slots.nth(1).locator('.save-delete').click();
    await expect(page.locator('#saves-note')).toContainText('définitive');
    await expect(slots.nth(1)).toContainText('Zoéd');  // not yet
    await slots.nth(1).locator('.save-delete').click();
    await expect(slots.nth(1)).toContainText('Vide');
    // Back to the first one.
    await slots.nth(0).locator('.save-play').click();
    await expect(page.locator('#saves')).toBeHidden();
    expect(await page.evaluate(() => window.monstres.wizard().name)).toBe('');
    expect(await page.evaluate(() => window.monstres.saves().map((x) => !!x.empty))).toEqual([false, true, false]);
  });
});

test('the first visit begins with the character creation, then the arrival', async ({ page }) => {
  test.setTimeout(120000);
  await page.goto('/');  // no save yet
  await page.evaluate(() => window.monstres.ready);
  expect(await page.evaluate(() => window.monstres.needsCreation())).toBe(true);
  await expect(page.locator('#create')).toBeVisible();
  await expect(page.locator('#create-cancel')).toBeHidden();  // it must be done
  await expect(page.locator('#settings-button')).toBeHidden();
  // The look: a woman, long blond hair, blue eyes, a wand, a red robe, a hood.
  await page.locator('#create-name').fill('Zoé');
  await page.locator('#create-next').click();
  await page.locator('#create-sex .choice').nth(1).click();
  await page.locator('#create-hair .choice').nth(1).click();
  await page.locator('#create-hair-color .swatch').nth(4).click();
  await page.locator('#create-eyes .swatch').nth(3).click();
  await page.locator('#create-skin .swatch').nth(2).click();
  await page.locator('#create-next').click();
  await page.locator('#create-staff .choice').nth(1).click();
  await page.locator('#create-robe .swatch').nth(1).click();
  await page.locator('#create-headwear .choice').nth(1).click();
  // Turning and zooming the magician does not choose anything.
  const view = page.locator('#create-view');
  const box = await view.boundingBox();
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
  await page.mouse.move(box.x + box.width / 2 + 80, box.y + box.height / 2, { steps: 4 });
  await page.mouse.up();
  await page.locator('#create-zoom-in').click();
  // The three monsters to start with: Zapou.
  await page.locator('#create-next').click();
  await expect(page.locator('#create-starters .starter strong')).toHaveText(['Pompon', 'Zapou', 'Ploufi']);
  await page.locator('#create-starters .starter').nth(1).click();
  await expect(page.locator('#create-starter-text')).toContainText('Zapou, une femelle');
  // The world: a seed drawn at random, or drawn again with the die.
  await page.locator('#create-next').click();
  const drawn = await page.locator('#create-seed').inputValue();
  expect(drawn).toMatch(/^\d+$/);
  await page.locator('#create-dice').click();
  await page.locator('#create-seed').fill('77');
  await page.locator('#create-next').click();
  // The arrival's flight: nothing but the world, and a button to skip it.
  await expect(page.locator('#create')).toBeHidden();
  await expect(page.locator('#skip-intro')).toBeVisible();
  await expect(page.locator('#settings-button')).toBeHidden();
  expect((await state(page)).intro).toBe(true);
  await page.locator('#skip-intro').click();
  await expect(page.locator('#skip-intro')).toBeHidden();
  await expect(page.locator('#settings-button')).toBeVisible();
  // Isidore waits in the village.
  await expect(page.locator('#toast')).toContainText('Isidore');
  const game = await page.evaluate(() => ({
    wizard: window.monstres.wizard(), team: window.monstres.team(), saves: window.monstres.saves(),
    placeholder: window.monstres.state().placeholder, seed: window.monstres.saves()[0].seed,
  }));
  expect(game.placeholder).toBe(false);
  expect(game.wizard.name).toBe('Zoé');
  expect(game.wizard.look).toEqual({ woman: true, skin: 2, hair: 1, hairColor: 4, eyes: 3, staff: 1, robe: 1, headwear: 1 });
  expect(game.team.map((m) => m.species)).toEqual(['spark']);
  expect(game.team[0].variants).toContain('sex.female');
  expect(game.seed).toBe('77');
  expect(game.saves[0].name).toBe('Zoé');
  // Kept: the next visit loads it, without the creation.
  await page.evaluate(() => window.monstres.saveNow());
  await page.reload();
  await page.evaluate(() => window.monstres.ready);
  await expect(page.locator('#create')).toBeHidden();
  expect(await page.evaluate(() => window.monstres.wizard().look.robe)).toBe(1);
});

test('the game still starts offline once installed', async ({ page, context, browserName }) => {
  test.skip(browserName === 'webkit', 'service worker offline mode is unreliable in Playwright WebKit');
  await openGame(page, '?seed=42&new&frozen');
  await page.evaluate(() => navigator.serviceWorker.ready);
  await page.reload();  // this load goes through the service worker and fills the cache
  await page.evaluate(() => window.monstres.ready);

  await context.setOffline(true);
  await openGame(page, '?frozen');
  expect((await state(page)).objects).toBeGreaterThan(150);
  await expect(page.locator('#joystick')).toBeVisible();  // controls.js came from the cache too
});

test('the game still starts from the cache when the server answers 404', async ({ page, browserName }) => {
  test.skip(browserName === 'webkit', 'service worker offline mode is unreliable in Playwright WebKit');
  // A server of its own, which can take the site down: then every file,
  // sw.js too, answers 404 (a route() would not reach the worker's requests).
  const site = process.env.MONSTRES_SITE || path.resolve(__dirname, '../../../build/web-release/site');
  const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
    '.webmanifest': 'application/manifest+json', '.png': 'image/png' };
  let up = true;
  const server = http.createServer((req, res) => {
    const name = decodeURIComponent(new URL(req.url, 'http://x').pathname).replace(/\/$/, '/index.html');
    const file = path.join(site, path.normalize(name));
    if (!up || !file.startsWith(site) || !fs.existsSync(file)) {
      res.writeHead(404, { 'Content-Type': 'text/plain' }).end('Not Found');
      return;
    }
    res.writeHead(200, { 'Content-Type': types[path.extname(file)] || 'application/octet-stream' });
    fs.createReadStream(file).pipe(res);
  });
  await new Promise((done) => server.listen(0, '127.0.0.1', done));
  const base = `http://127.0.0.1:${server.address().port}/`;
  try {
    await openGame(page, `${base}?seed=42&new&frozen`);
    await page.evaluate(() => navigator.serviceWorker.ready);
    await page.reload();  // this load goes through the service worker and fills the cache
    await page.evaluate(() => window.monstres.ready);

    up = false;
    await openGame(page, `${base}?frozen`);
    expect((await state(page)).objects).toBeGreaterThan(150);
    await expect(page.locator('#joystick')).toBeVisible();
  } finally {
    server.closeAllConnections();
    server.close();
  }
});

test.describe('settings menu', () => {
  test('the gear opens the menu with the saves button, Escape closes it', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    const menu = page.locator('#settings');
    await expect(menu).toBeHidden();
    await expect(page.locator('#saves-open')).toBeHidden();  // only in the menu
    await page.locator('#settings-button').click();
    await expect(menu).toBeVisible();
    await expect(page.locator('#saves-open')).toBeVisible();
    await page.keyboard.press('Escape');
    await expect(menu).toBeHidden();
    // Clicking on the game view closes it too.
    await page.locator('#settings-button').click();
    await expect(menu).toBeVisible();
    await page.mouse.click(400, 150);
    await expect(menu).toBeHidden();
  });

  test('debug: the slider sets the time of day, which can stand still', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    expect((await state(page)).hour).toBeCloseTo(8, 3);  // a new game starts in the morning
    await page.locator('#settings-button').click();
    const slider = page.locator('#hour');
    await expect(slider).toBeVisible();
    await slider.fill('22.5');
    await expect(page.locator('#hour-value')).toHaveText('22:30');
    expect((await state(page)).hour).toBeCloseTo(22.5, 3);
    // Time goes on: one hour per minute of play.
    await page.evaluate(() => window.monstres.step(3600));
    expect((await state(page)).hour).toBeCloseTo(23.5, 3);
    // Unless it is held.
    await page.locator('#hold-hour').check();
    await page.evaluate(() => window.monstres.step(600));
    expect((await state(page)).hour).toBeCloseTo(23.5, 3);
    await page.locator('#hold-hour').uncheck();
    await page.evaluate(() => window.monstres.step(600));
    expect((await state(page)).hour).toBeCloseTo(23.666, 2);
  });

  test('debug: the weather slider tries every weather, then the natural one again', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    const start = (await state(page)).weather;
    expect(start.choice).toBe(-1);
    expect(['clear', 'fewClouds']).toContain(start.kind);  // a new game starts in the sun
    await page.locator('#settings-button').click();
    const slider = page.locator('#weather');
    const expected = [
      ['clear', 'Ensoleillé'], ['fewClouds', 'Quelques nuages'], ['overcast', 'Couvert'], ['rain', 'Pluie'],
      ['storm', 'Tempête, orage'], ['fog', 'Brouillard'], ['snow', 'Neige'],
    ];
    for (let k = 0; k < expected.length; ++k) {
      await slider.fill(String(k + 1));
      await expect(page.locator('#weather-value')).toHaveText(expected[k][1]);
      const w = (await state(page)).weather;
      expect(w.kind).toBe(expected[k][0]);
      expect(w.choice).toBe(k);
    }
    const rain = await page.evaluate(() => { window.monstres.setWeather(3); return window.monstres.state().weather; });
    expect(rain.rain).toBeGreaterThan(0.5);
    expect(rain.cover).toBe(1);
    // A storm afar: fine weather here.
    await slider.fill('8');
    await expect(page.locator('#weather-value')).toHaveText('Orage au loin');
    expect((await state(page)).weather.kind).toBe('clear');
    await slider.fill('0');
    await expect(page.locator('#weather-value')).toHaveText(/^Naturelle/);
    const back = (await state(page)).weather;
    expect(back.choice).toBe(-1);
    expect(back.kind).toBe(start.kind);
  });

  test('the FPS counter can be shown, and stays shown after a reload', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    const fps = page.locator('#fps');
    await expect(fps).toBeHidden();
    await page.locator('#settings-button').click();
    await page.locator('#show-fps').check();
    await expect(fps).toBeVisible();
    await expect(fps).toHaveText(/^\d+ FPS$/);
    await openGame(page, '?seed=42&new&frozen');
    await expect(fps).toBeVisible();
    await page.locator('#settings-button').click();
    await expect(page.locator('#show-fps')).toBeChecked();
    await page.locator('#show-fps').uncheck();
    await expect(fps).toBeHidden();
  });
});

test.describe('creature info', () => {
  test('clicking on the companion shows who it is, clicking elsewhere hides it', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    const panel = page.locator('#creature');
    await expect(panel).toBeHidden();
    const at = await page.evaluate(() => window.monstres.creatureOnScreen(0));
    expect(at).not.toBeNull();
    await page.mouse.click(at.x, at.y);
    await expect(panel).toBeVisible();
    // The essentials: its name, its level and whose it is; no kind, no description.
    await expect(panel.locator('.name')).toHaveText('Pompon niv. 3 (à toi)');
    await expect(panel.locator('.kind')).toBeHidden();
    await expect(panel.locator('.facts')).toHaveText(/^T’attend · à 1 m$/);  // just behind the player
    await expect(page.locator('#context .context-title')).toHaveText('Pompon niv. 3 (à toi)');
    await expect(page.locator('#context .context-note')).toBeHidden();
    await expect(panel.locator('.tags .badge')).toHaveText(['Bête', 'Feu']);
    await expect(panel.locator('.variants span')).toHaveText(['♂', 'Pelage de braise', 'Oreilles pointues', 'Une queue']);
    expect((await page.evaluate(() => window.monstres.selected())).index).toBe(0);

    await page.mouse.click(220, 300);  // grass, no creature there
    await expect(panel).toBeHidden();
    expect(await page.evaluate(() => window.monstres.selected())).toBeNull();
  });

  test('dragging to look around does not select anything', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    const at = await page.evaluate(() => window.monstres.creatureOnScreen(0));
    await page.mouse.move(at.x, at.y);
    await page.mouse.down();
    await page.mouse.move(at.x + 80, at.y, { steps: 5 });
    await page.mouse.up();
    await expect(page.locator('#creature')).toBeHidden();
  });

  test('the close button hides the panel', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    const at = await page.evaluate(() => window.monstres.creatureOnScreen(0));
    await page.mouse.click(at.x, at.y);
    await expect(page.locator('#creature')).toBeVisible();
    await page.locator('#creature-close').click();
    await expect(page.locator('#creature')).toBeHidden();
  });
});

test.describe('grimoire', () => {
  test('the book opens the menus and stops time, Escape closes it', async ({ page }) => {
    await openGame(page, '?seed=42&new');
    const menu = page.locator('#game-menu');
    await expect(menu).toBeHidden();
    await page.locator('#menu-button').click();
    await expect(menu).toBeVisible();
    await expect(page.getByRole('tab')).toHaveText(['Personnage', 'Équipe', 'Sac', 'Sorts', 'Quêtes', 'Bestiaire', 'Carte']);
    const tick = (await state(page)).tick;
    await page.waitForTimeout(500);
    expect((await state(page)).tick).toBe(tick);  // paused
    await page.keyboard.press('Escape');
    await expect(menu).toBeHidden();
    await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(tick);  // time goes on
  });

  test('the character sheet shows the magician', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    await page.locator('#menu-button').click();
    await page.getByRole('tab', { name: 'Personnage' }).click();
    await expect(page.locator('#wizard-rank')).toHaveText('Apprenti magicien · niveau 1');
    await expect(page.locator('#wizard-mana-text')).toHaveText('Mana : 20 / 20');  // its only stat shown
    await expect(page.locator('#wizard-record')).toContainText('Monstres capturés');
    await expect(page.locator('#wizard-record')).toContainText('Combats gagnés');
    await expect(page.locator('#wizard-record')).toContainText('Monstres battus');
    await expect(page.locator('#wizard-where')).toContainText('Prairie');
    await expect(page.locator('#wizard-portrait')).toHaveAttribute('src', /^data:image\/png/);
    await expect(page.locator('#wizard-name')).toHaveText('Magicien sans nom');  // chosen with a new game (saves)
  });

  test('the bestiary lists the monsters, each with its card', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    await page.locator('#menu-button').click();
    await page.getByRole('tab', { name: 'Bestiaire' }).click();
    const entries = page.locator('.bestiary-entry');
    await expect(entries).toHaveCount(13);  // villagers are not monsters
    await expect(entries.locator('strong')).toHaveText(['Pompon', 'Gloubi', 'Champi', 'Piou', 'Croa', 'Bloup', 'Zapou', 'Ploufi', 'Cristi',
      'Chipou', 'Hulou', 'Laini', 'Taupi']);
    await page.locator('.bestiary-entry[data-species="mushroom"]').click();
    const detail = page.locator('#bestiary-detail');
    await expect(detail.locator('h2')).toHaveText('Champi');
    await expect(detail.locator('.badge')).toHaveText(['Végétal', 'Nature']);
    await expect(detail).toContainText('Défense');
    await expect(detail).toContainText(/Taille : .* · poids : .* · vitesse : .* m\/s/);  // averages
    await expect(detail.locator('img')).toHaveAttribute('src', /^data:image\/png/);
    // Its habitat: a map in place of its card, where it lives blinking.
    await page.locator('#habitat-button').click();
    await expect(page.locator('#tab-bestiary')).toBeVisible();
    await expect(page.locator('#bestiary-detail h2')).toHaveText('Habitat de Champi');
    await expect(page.locator('#habitat-map')).toBeVisible();
    await expect(page.locator('.habitat-legend')).toHaveText(/Toi.*Téléporteur.*Habitat/);
    // Another species: its habitat at once.
    await page.locator('.bestiary-entry[data-species="bird"]').click();
    await expect(page.locator('#bestiary-detail h2')).toHaveText('Habitat de Piou');
    await page.locator('.bestiary-entry[data-species="mushroom"]').click();
    await page.locator('#bestiary-detail .dna-back').click();
    await expect(page.locator('#bestiary-detail h2')).toHaveText('Champi');
    // The map tab: its tools over it, its legend shown on demand.
    await page.getByRole('tab', { name: 'Carte' }).click();
    await expect(page.locator('#map-legend')).toBeHidden();
    await page.locator('#map-legend-toggle').click();
    await expect(page.locator('#map-legend')).toBeVisible();
    await expect(page.locator('#map-legend')).not.toContainText('Habitat');
    await page.locator('#map-legend-toggle').click();
  });

  test('the crow has two elements', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    await page.keyboard.press('b');
    await page.locator('.bestiary-entry[data-species="crow"]').click();
    await expect(page.locator('#bestiary-detail .badge')).toHaveText(['Oiseau', 'Ombre', 'Air']);
  });

  test('the variants of a species: unknown, seen (greyed) and captured', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    await page.evaluate(() => window.monstres.step(1));  // the monsters near the start are seen
    const bestiary = await page.evaluate(() => window.monstres.bestiary());
    const companion = bestiary.find((e) => e.species === 'companion');
    expect(companion.variants.filter((v) => v.status === 'captured').map((v) => v.id))
      .toEqual(['sex.male', 'fur.ember', 'ears.pointed', 'tails.one']);
    // A species with seen variants, but none captured.
    const seenSpecies = bestiary.find((e) => e.species !== 'companion' && e.variants.some((v) => v.status === 'seen'));
    expect(seenSpecies).toBeTruthy();

    await page.keyboard.press('b');
    const entry = page.locator(`.bestiary-entry[data-species="${seenSpecies.species}"]`);
    const seen = seenSpecies.variants.filter((v) => v.status === 'seen').length;
    // The count: variants captured at least once (none yet); the seen ones in its record.
    await expect(entry.locator('.dna-count')).toHaveText(`0/${seenSpecies.variants.length}`);
    await entry.click();
    await expect(page.locator('#bestiary-detail .record')).toContainText(`${seen} / ${seenSpecies.variants.length}`);
    await entry.click();
    await page.locator('#dna-button').click();
    const cells = page.locator('.variant-cell');
    await expect(cells).toHaveCount(seenSpecies.variants.length);
    await expect(page.locator('.variant-cell[data-status="unknown"] .unknown').first()).toHaveText('?');
    await expect(page.locator('.variant-cell[data-status="unknown"]')).toHaveCount(seenSpecies.variants.length - seen);
    const greyed = page.locator('.variant-cell[data-status="seen"]').first();
    await expect(greyed.locator('img')).toHaveCSS('filter', 'grayscale(1)');
    await greyed.click();
    await expect(page.locator('#variant-info .status')).toHaveText('Aperçue, pas encore capturée');
    await expect(page.locator('#variant-info h3')).not.toBeEmpty();
    await expect(page.locator('#variant-info .badge').first()).toHaveText(/Commune|Peu commune|Rare|Très rare/);
    await page.locator('.variant-cell[data-status="unknown"]').first().click();
    await expect(page.locator('#variant-info')).toContainText('Variante encore inconnue');

    // The companion's are captured: shown as they are.
    await page.locator('.bestiary-entry[data-species="companion"]').click();
    const mine = page.locator('.variant-cell[data-status="captured"]');
    await expect(mine).toHaveCount(4);
    await expect(mine.first().locator('img')).toHaveCSS('filter', 'none');
    await mine.first().click();
    await expect(page.locator('#variant-info h3')).toHaveText('♂ Mâle');  // the sex comes first
    // The chosen variant is shown beside the grid, not below it.
    const grid = await page.locator('.dna-groups').boundingBox();
    const info = await page.locator('#variant-info').boundingBox();
    expect(info.x).toBeGreaterThanOrEqual(grid.x + grid.width);
    expect(info.y).toBeLessThan(grid.y + 40);
    await expect(page.locator('#variant-info .status')).toHaveText('Capturée');
    await page.locator('.dna-back').click();
    await expect(page.locator('#bestiary-detail h2')).toHaveText('Pompon');
  });

  test('the map: the player up close, then the whole continent, zooming out; no creatures', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    await page.keyboard.press('m');  // shortcut
    await expect(page.getByRole('tab', { name: 'Carte' })).toHaveAttribute('aria-selected', 'true');
    await expect(page.locator('#map-tools, .map-tools')).toBeVisible();  // over the map
    const painted = () => page.evaluate(() => {
      const c = document.getElementById('map-canvas');
      const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
      const colors = new Set();
      for (let i = 0; i < d.length; i += 4 * 97) colors.add(`${d[i] >> 4},${d[i + 1] >> 4},${d[i + 2] >> 4}`);
      return colors.size;
    });
    expect(await painted()).toBeGreaterThan(20);  // terrain, path, trees...
    const scale = () => page.locator('#map-scale').textContent();
    const close = await scale();
    // Zooming out, down to the whole continent (then the button gives up).
    const out = page.getByRole('button', { name: 'Dézoomer' });
    for (let i = 0; i < 20 && !(await out.isDisabled()); ++i) await out.click();
    await expect(out).toBeDisabled();
    expect(await scale()).toMatch(/km$/);
    expect(await scale()).not.toBe(close);
    expect(await painted()).toBeGreaterThan(20);  // sea, beaches, snow, desert...
    const continent = await page.evaluate(() => window.monstres.continent());
    expect(continent.ready).toBe(true);
    expect(continent.heights.length).toBe(continent.pointsX * continent.pointsZ);
    expect(continent.landmarks.map((m) => m.region)).toContain('capital');
    // The wheel zooms back in; the button recenters on the player.
    const box = await page.locator('#map-canvas').boundingBox();
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.wheel(0, -1500);
    await expect(out).toBeEnabled();
    await page.getByRole('button', { name: 'Recentrer sur toi' }).click();
    await page.keyboard.press('m');
    await expect(page.locator('#game-menu')).toBeHidden();
  });
});

test.describe('battles', () => {
  // Seed 42: the nearest bird pecks about 10 m from the start.
  let BIRD = -1;
  test.beforeEach(async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    BIRD = await page.evaluate(() => {
      const s = window.monstres.state();
      let best = -1;
      let bestDistance = 1e9;
      s.creatures.forEach((c, i) => {
        const d = Math.hypot(c.x - s.player.x, c.z - s.player.z);
        if (c.species === 'bird' && d < bestDistance) { best = i; bestDistance = d; }
      });
      // A few steps from it (birds run away from closer).
      const b = s.creatures[best];
      if (bestDistance > 10) {
        window.monstres.teleport(b.x + 6, b.z + 6);
        window.monstres.step(1);
      }
      return best;
    });
    expect(BIRD).toBeGreaterThan(0);
  });
  async function lookAtBird(page) {
    await page.evaluate((i) => {
      const s = window.monstres.state();
      const c = s.creatures[i];
      window.monstres.setCamera(Math.atan2(-(c.x - s.player.x), -(c.z - s.player.z)), 0.35);
    }, BIRD);
  }

  test('the context menu starts a battle; the battle buttons replace the jump', async ({ page }) => {
    await expect(page.locator('#context')).toBeHidden();
    await lookAtBird(page);
    const at = await page.evaluate((i) => window.monstres.creatureOnScreen(i), BIRD);
    await page.mouse.click(at.x, at.y);
    await expect(page.locator('#context')).toBeVisible();
    await expect(page.locator('#context .context-title')).toContainText('Piou');
    await expect(page.locator('#attack-1')).toBeHidden();
    await page.locator('#action-fight').click();

    await expect(page.locator('.fighter.enemy')).toBeVisible();  // (#battle-hud itself has no size)
    await expect(page.locator('.fighter.enemy strong')).toHaveText('Piou');
    await expect(page.locator('.fighter.ally strong')).toHaveText('Pompon');
    for (const id of ['#attack-1', '#attack-2', '#attack-3', '#parry']) await expect(page.locator(id)).toBeVisible();
    await expect(page.locator('#jump')).toHaveAttribute('aria-label', 'Esquiver');
    await expect(page.locator('#action-capture')).toBeDisabled();  // not weakened yet
    const battle = await page.evaluate(() => window.monstres.battle());
    expect(battle.phase).toBe('fight');
    expect(battle.enemy.index).toBe(BIRD);

    // A special attack: a projectile (the Piou may dodge it), and the button waits for its cooldown.
    await page.locator('#attack-2').dispatchEvent('pointerdown');
    await page.evaluate(() => window.monstres.step(40));
    const after = await page.evaluate(() => window.monstres.battle());
    expect(after.ally.fighter.attackTick).toBeGreaterThan(battle.tick);
    expect(after.ally.fighter.cooldowns[1]).toBeGreaterThan(0);
    // The foe's icon, aimed at; tapped again: no one is aimed at, Pompon moves freely.
    await expect(page.locator('.foe-icon')).toHaveCount(1);
    await expect(page.locator('.foe-icon')).toHaveClass(/targeted/);
    await page.locator('.foe-icon').click();
    await expect(page.locator('.fighter.enemy strong')).toHaveText('Mode libre');
    expect((await page.evaluate(() => window.monstres.battle())).target).toBe(-1);
    await page.locator('.foe-icon').click();
    await expect(page.locator('.fighter.enemy strong')).toHaveText('Piou');
    await expect(page.locator('#attack-2')).toHaveClass(/waiting/);

    // Fleeing: Pompon comes back to the player, the battle ends.
    await page.locator('#action-flee').click();
    await page.evaluate(() => window.monstres.step(400));
    await expect(page.locator('.fighter.enemy')).toBeHidden();
    await expect(page.locator('#attack-1')).toBeHidden();
    await expect(page.locator('#jump')).toHaveAttribute('aria-label', 'Sauter');
    expect((await page.evaluate(() => window.monstres.battle())).outcome.result).toBe('fled');
  });

  test('the team bar recalls the companion into the crystal and summons it back', async ({ page }) => {
    const icon = page.locator('.team-icon[data-slot="0"]');
    await expect(page.locator('.team-icon')).toHaveCount(1);
    await expect(icon).toHaveAttribute('aria-pressed', 'true');
    await icon.click();
    await expect(icon).toHaveAttribute('aria-pressed', 'false');
    expect((await state(page)).active).toBe(-1);
    expect(await page.evaluate(() => window.monstres.pick(...Object.values(window.monstres.creatureOnScreen(0) || { x: 0, y: 0 })))).not.toBe(0);
    // Without a companion out, no battle.
    await lookAtBird(page);
    const at = await page.evaluate((i) => window.monstres.creatureOnScreen(i), BIRD);
    await page.mouse.click(at.x, at.y);
    await expect(page.locator('#action-fight')).toBeDisabled();
    await expect(page.locator('#context .context-note')).toContainText('Invoque');
    await icon.click();
    await expect(icon).toHaveAttribute('aria-pressed', 'true');
    expect((await state(page)).active).toBe(0);
    await expect(page.locator('#action-fight')).toBeEnabled();
  });

  test('a weakened monster is captured and joins the team, and can be let go', async ({ page }) => {
    // Mercy first: the blow that would beat it leaves it 1 hit point.
    await page.evaluate((i) => {
      const m = window.monstres;
      m.equipSpell(0, 3);
      m.step(1);
      m.engage(i);
      m.step(1);
      m.cast(0);
      m.step(1);
    }, BIRD);
    // Fight until it is weak enough.
    const result = await page.evaluate(() => {
      const m = window.monstres;
      for (let i = 0; i < 600; i++) {
        const b = m.battle();
        if (b.phase !== 'fight') return b.outcome.result;
        if (b.capture === 'ok') return 'weak';
        // Its attacks are shown first: parried. Embers, bites when close.
        if (b.enemy.fighter.state === 'windup') m.input.parry();
        else if (b.ally.fighter.cooldowns[1] === 0) m.input.attack(2);
        else m.input.attack(1);
        m.step(4);
      }
      return 'timeout';
    });
    expect(result).toBe('weak');
    await expect(page.locator('#action-capture')).toBeEnabled();
    await page.locator('#action-capture').click();
    // The seal: a circle of runes around it, lit in an order to touch again.
    const seal = page.locator('#seal');
    await expect(seal).toBeVisible();
    await expect(seal.locator('.seal-title')).toHaveText('Capture · Piou');
    await expect(seal.locator('.rune')).toHaveCount(6);
    await expect(page.locator('#action-capture')).toBeHidden();  // the rest waits
    const untilInput = () => page.evaluate(() => {
      const m = window.monstres;
      for (let i = 0; i < 400 && m.battle().seal && m.battle().seal.stage !== 'input'; i++) m.step(1);
      return m.battle().seal;
    });
    let s0 = await untilInput();
    expect(s0.bar).toBe(50);
    await expect(seal.locator('.seal-text')).toContainText('touche les runes dans le même ordre (0/5)');
    // A mistake: the bar moves towards the escape.
    await seal.locator('.rune').nth((s0.order[0] + 1) % 6).click();
    await expect(seal.locator('.seal-text')).toContainText('Raté');
    // The right order: towards the capture (the rest of the seal: test_battle.cpp).
    s0 = await untilInput();
    for (const k of s0.order) await seal.locator('.rune').nth(k).click();
    await expect(seal.locator('.seal-text')).toContainText('Bravo');
    expect((await page.evaluate(() => window.monstres.battle().seal)).power).toBe(35);
    // The seal held from here on (as if the next rounds were right).
    await page.evaluate(() => {
      const m = window.monstres;
      for (let i = 0; i < 2000 && m.battle().seal; i++) {
        const s = m.battle().seal;
        if (s.stage === 'input') m.rune(s.order[s.done]);
        else m.step(1);
      }
    });
    await expect(seal).toBeHidden();
    await expect(page.locator('#battle-banner')).toContainText('Piou capturé');
    // The experience Pompon earned, on its bar.
    await expect(page.locator('#battle-banner .xp-card')).toHaveCount(1);
    await expect(page.locator('#battle-banner .xp-card')).toContainText('Pompon');
    await expect(page.locator('#battle-banner .xp-gain')).toContainText('XP');
    await expect(page.locator('.fighter.enemy')).toBeHidden();

    // The team bar: Pompon is out; the bird takes its place, then goes back in.
    await expect(page.locator('.team-icon')).toHaveCount(2);
    await expect(page.locator('.team-icon[data-slot="0"]')).toHaveAttribute('aria-pressed', 'true');
    await page.locator('.team-icon[data-slot="1"]').click();
    await expect(page.locator('.team-icon[data-slot="1"]')).toHaveAttribute('aria-pressed', 'true');
    let s = await state(page);
    expect(s.active).toBe(1);
    expect(s.creatures[0].species).toBe('bird');
    await page.locator('.team-icon[data-slot="1"]').click();
    await expect(page.locator('.team-icon[aria-pressed="true"]')).toHaveCount(0);
    s = await state(page);
    expect(s.active).toBe(-1);
    await page.locator('.team-icon[data-slot="0"]').click();
    expect((await state(page)).creatures[0].species).toBe('companion');

    await page.keyboard.press('e');  // the team, in the grimoire
    await expect(page.getByRole('tab', { name: 'Équipe' })).toHaveAttribute('aria-selected', 'true');
    await expect(page.locator('#team-list li')).toHaveCount(2);
    await page.locator('#team-list li').nth(1).locator('button').click();
    await expect(page.locator('#team-detail h2')).toHaveText('Piou');
    await expect(page.locator('#team-detail .move')).toHaveCount(4);  // three attacks, and the parry
    await expect(page.locator('#team-detail .move').first()).toContainText('Coup de bec');
    await expect(page.locator('#team-detail .move').last()).toContainText('Parer');
    await expect(page.locator('#team-detail .move').nth(2)).toContainText('se charge 1 s');
    await page.locator('#team-list li').first().locator('button').click();
    await expect(page.locator('#team-detail h2')).toHaveText('Pompon');
    await expect(page.locator('#team-detail')).toContainText('Boule de braise');
    await expect(page.locator('#team-detail')).toContainText('Niveau 3');

    // Letting the bird go: a window in the middle asks; "keep" first, then "release".
    await page.locator('#team-list li').nth(1).locator('button').click();
    await page.locator('#release').click();
    await expect(page.locator('#release-dialog')).toBeVisible();
    await expect(page.locator('#release-dialog .release-warning')).toContainText('ne reviendra pas');
    await page.locator('#release-cancel').click();
    await expect(page.locator('#release-dialog')).toBeHidden();
    await expect(page.locator('#game-menu')).toBeVisible();
    await page.locator('#release').click();
    await expect(page.locator('#release-confirm')).toContainText('Piou');
    const before = (await state(page)).creatures.length;
    await page.locator('#release-confirm').click();
    await expect(page.locator('#game-menu')).toBeHidden();
    await page.evaluate(() => window.monstres.step(2));
    await expect(page.locator('.team-icon')).toHaveCount(1);
    const after = await state(page);
    expect(after.creatures.length).toBe(before + 1);
    expect(after.creatures[after.creatures.length - 1].species).toBe('bird');
    // Pompon alone cannot be let go.
    await page.keyboard.press('e');
    await expect(page.locator('#release')).toHaveCount(0);
    await expect(page.locator('#team-detail .release-box')).toContainText('seul monstre');
  });

  test('a beaten monster lies knocked out; the victory shows the experience', async ({ page }) => {
    await page.evaluate((i) => window.monstres.engage(i), BIRD);
    const result = await page.evaluate(() => {
      const m = window.monstres;
      for (let i = 0; i < 600; i++) {
        const b = m.battle();
        if (b.phase !== 'fight') return b.outcome.result;
        m.input.move(0, 1);
        m.input.attack(1 + (i % 3));
        m.step(4);
      }
      return 'timeout';
    });
    expect(result).toBe('victory');
    await expect(page.locator('#battle-banner')).toContainText('Victoire');
    await expect(page.locator('#battle-banner .xp-card')).toHaveCount(1);
    const outcome = (await page.evaluate(() => window.monstres.battle())).outcome;
    expect(outcome.gains[0].xp).toBeGreaterThan(0);
    expect((await state(page)).creatures[BIRD].mind).toBe('knockedOut');
    await page.evaluate((i) => window.monstres.select(i), BIRD);
    expect((await page.evaluate(() => window.monstres.selected())).engage).toBe('knockedOut');
  });

  test('training: the companion attacks the magician, who shields itself', async ({ page }) => {
    await page.evaluate(() => { window.monstres.setCamera(Math.PI, 0.3); window.monstres.step(30); });
    const at = await page.evaluate(() => window.monstres.creatureOnScreen(0));
    await page.mouse.click(at.x, at.y);
    await expect(page.locator('#action-train')).toBeEnabled();
    await page.locator('#action-train').click();
    await page.evaluate(() => window.monstres.step(1));
    expect((await page.evaluate(() => window.monstres.battle())).phase).toBe('training');
    await expect(page.locator('#attack-1')).toBeVisible();
    // The same screen as against a monster: the magician at the top (behind
    // its shield, no hit points), the companion's hit points at the bottom,
    // the mana beside them.
    await expect(page.locator('.fighter.enemy')).toBeVisible();
    await expect(page.locator('.fighter.enemy .hp-text')).toContainText('bouclier');
    await expect(page.locator('.fighter.enemy .hp-bar')).toBeHidden();
    await expect(page.locator('.fighter.ally')).toBeVisible();
    await expect(page.locator('.fighter.ally strong')).toHaveText('Pompon');
    await expect(page.locator('.fighter.ally .hp-text')).toContainText('PV');
    await expect(page.locator('#mana')).toBeVisible();
    const [ally, mana] = await Promise.all(['.fighter.ally', '#mana'].map((s) => page.locator(s).boundingBox()));
    expect(mana.x).toBeGreaterThan(ally.x + ally.width - 1);  // just right of it
    expect(Math.abs(mana.y + mana.height - (ally.y + ally.height))).toBeLessThan(2);  // on the same line
    const hp = (await page.evaluate(() => window.monstres.team()))[0].hp;
    await page.evaluate(() => {
      const m = window.monstres;
      for (let i = 0; i < 12; i++) {
        m.input.move(0, 1);
        m.step(6);
        m.input.move(0, 0);
        m.input.attack(1);
        m.step(4);
      }
    });
    const b = await page.evaluate(() => window.monstres.battle());
    expect(b.shield.parriedTick).toBeGreaterThan(0);
    expect(b.shield.lastDamage).toBeGreaterThan(0);
    expect((await page.evaluate(() => window.monstres.team()))[0].hp).toBe(hp);
    await page.locator('#action-stop').click();
    await page.evaluate(() => window.monstres.step(1));
    expect((await page.evaluate(() => window.monstres.battle())).phase).toBe('none');
    await expect(page.locator('#attack-1')).toBeHidden();
    await expect(page.locator('.fighter.ally')).toBeHidden();
  });
});

test.describe('the bigger world', () => {
  test('Bastien offers a battle: rules, ready, his unknown second monster, forfeit', async ({ page }) => {
    await openGame(page, '?seed=3&new&frozen');
    const i = await page.evaluate(() => {
      const m = window.monstres;
      for (let i = 1; i < m.state().creatures.length; i++) {
        m.select(i);
        const c = m.selected();
        if (c && c.npc === 4) { m.teleport(c.x + 1.5, c.z); m.step(1); return i; }
      }
      return -1;
    });
    expect(i).toBeGreaterThan(0);
    await page.evaluate((i) => { window.monstres.select(i); window.monstresDialogue.open(window.monstres.selected()); }, i);
    await expect(page.locator('#dialogue .dialogue-name')).toHaveText('Bastien');
    // First, his lesson: a battle against him.
    await expect(page.locator('#dialogue .dialogue-text')).toContainText('initiation');
    await page.locator('#accept-quest').click();
    await expect.poll(() => page.evaluate(() => window.monstres.quests().find((q) => q.id === 'combat').stage)).toBe('active');
    await page.locator('#challenge').click();
    await page.evaluate(() => window.monstres.step(1));
    // The rules, while both sides take their places; his team: Champi, and a "?".
    await expect(page.locator('#rules')).toBeVisible();
    await expect(page.locator('#rules .rules-title')).toHaveText('Combat contre Bastien');
    await expect(page.locator('#context')).toBeHidden();
    await expect(page.locator('.foe-icon')).toHaveCount(2);
    await expect(page.locator('.foe-icon .foe-unknown')).toHaveCount(1);
    await page.locator('#rules-ready').click();
    await page.evaluate(() => window.monstres.step(1));
    await expect(page.locator('#rules')).toBeHidden();
    expect((await page.evaluate(() => window.monstres.battle())).phase).toBe('fight');
    await expect(page.locator('.fighter.enemy strong')).toHaveText('Champi');
    await expect(page.locator('#action-capture')).toHaveCount(0);  // his monsters cannot be caught
    // Forfeit: over at once, he wins.
    await page.locator('#action-forfeit').click();
    await page.evaluate(() => window.monstres.step(1));
    await expect(page.locator('#battle-banner')).toContainText('forfait');
    expect((await page.evaluate(() => window.monstres.battle())).outcome.result).toBe('forfeit');
  });

  test('spells: chosen in the grimoire, cast with a button; Odette heals the team', async ({ page }) => {
    await openGame(page, '?seed=3&new&frozen');
    // The grimoire's tab: the adventure and combat slots; the first adventure one is shown.
    await page.keyboard.press('p');
    await expect(page.locator('#tab-spells')).toBeVisible();
    await expect(page.locator('.spell-entry')).toHaveCount(4);
    await expect(page.locator('#spell-slot-2')).toHaveAttribute('aria-current', 'true');
    await expect(page.locator('#spell-detail h2')).toHaveText('Soin');
    // Emptied (the menu stops time: it happens at once).
    await page.locator('#spell-edit').click();
    await expect(page.locator('.spell-choice')).toHaveCount(4);  // none, heal, detection, way out (the shield is for battles)
    await page.locator('#spell-choice-none').click();
    await page.locator('#spell-confirm').click();
    await expect(page.locator('#spell-slot-2')).toHaveAttribute('data-spell', 'none');
    expect((await page.evaluate(() => window.monstres.spells())).adventure).toEqual(['none', 'none']);
    // The combat ones: heal in the second slot swaps with the shield (greyed: already chosen).
    await page.locator('#spell-slot-1').click();
    await page.locator('#spell-edit').click();
    await expect(page.locator('#spell-choice-heal')).toHaveClass(/taken/);
    await page.locator('#spell-choice-heal').click();
    await expect(page.locator('#spell-detail')).toContainText('échangés');
    await page.locator('#spell-confirm').click();
    expect((await page.evaluate(() => window.monstres.spells())).combat).toEqual(['shield', 'heal']);
    await page.keyboard.press('Escape');
    // No adventure spell: no button.
    await expect(page.locator('#spell-0')).toBeHidden();
    await expect(page.locator('#spell-1')).toBeHidden();
    // Odette heals the team (here: nothing to heal, she says so).
    const i = await page.evaluate(() => {
      const m = window.monstres;
      for (let i = 1; i < m.state().creatures.length; i++) {
        m.select(i);
        const c = m.selected();
        if (c && c.npc === 5) { m.teleport(c.x + 1.5, c.z); m.step(1); return i; }
      }
      return -1;
    });
    expect(i).toBeGreaterThan(0);
    await page.evaluate((i) => { window.monstres.select(i); window.monstresDialogue.open(window.monstres.selected()); }, i);
    await expect(page.locator('#dialogue .dialogue-name')).toHaveText('Odette');
    // Her lesson first (the Heal spell was cast already, on a hurt monster: done at once).
    await expect(page.locator('#dialogue .dialogue-text')).toContainText('sort de Soin');
    await page.locator('#accept-quest').click();
    await expect(page.locator('#dialogue .dialogue-text')).toContainText('lance le sort de Soin');
    await page.evaluate((i) => { window.monstres.select(i); window.monstresDialogue.open(window.monstres.selected()); }, i);
    await expect(page.locator('#dialogue .dialogue-text')).toContainText('pleine forme');
  });

  test('monsters of the same kind join in; their icons, the one aimed at', async ({ page }) => {
    await openGame(page, '?seed=2&new&frozen');  // slimes close together, not far from the start
    // Three slimes close together, in front of the player.
    const foes = await page.evaluate(() => {
      const m = window.monstres;
      const s = m.state();
      const slimes = s.creatures.map((c, i) => [c, i]).filter(([c]) => c.species === 'slime');
      for (const [c, i] of slimes) {
        const near = slimes.filter(([d]) => Math.hypot(d.x - c.x, d.z - c.z) < 7);
        if (near.length >= 2) {
          m.teleport(c.x + 5, c.z);
          m.step(1);
          // Moving may bring new monsters around (their indices change): the slime again, by where it is.
          const after = m.state().creatures;
          let best = -1;
          after.forEach((d, k) => {
            if (d.species === 'slime' && (best < 0 || Math.hypot(d.x - c.x, d.z - c.z) < Math.hypot(after[best].x - c.x, after[best].z - c.z))) best = k;
          });
          m.engage(best);
          m.step(1);
          return (m.battle().foes || []).length;
        }
      }
      return 0;
    });
    test.skip(foes < 2, 'no group of slimes in this world');
    await expect(page.locator('.foe-icon')).toHaveCount(foes);
    await expect(page.locator('.foe-icon.targeted')).toHaveCount(1);
    await page.locator('.foe-icon').nth(1).click();
    expect((await page.evaluate(() => window.monstres.battle())).target).toBe(1);
    await expect(page.locator('.foe-icon').nth(1)).toHaveClass(/targeted/);
  });

  test('mana, Detection, Mercy; traits and gifts of a monster; the record of the bestiary', async ({ page }) => {
    await openGame(page, '?seed=3&new&frozen');
    await expect(page.locator('#mana .mana-text')).toHaveText('20/20');
    // The character sheet: the mana left of its most.
    await page.keyboard.press('c');
    await expect(page.locator('#wizard-mana-text')).toContainText('20 / 20');
    // Detection in the second adventure slot, cast: 6 mana.
    await page.keyboard.press('p');
    await page.locator('#spell-slot-3').click();
    await page.locator('#spell-edit').click();
    await page.locator('#spell-choice-detect').click();
    await expect(page.locator('#spell-detail')).toContainText('6 mana');
    await page.locator('#spell-confirm').click();
    await page.keyboard.press('Escape');
    await expect(page.locator('#spell-1')).toBeVisible();
    await page.evaluate(() => { window.monstres.cast(1); window.monstres.step(1); });
    const s = await page.evaluate(() => window.monstres.spells());
    expect(s.manaNow).toBe(14);
    expect(s.detect).toBeGreaterThan(59);
    await expect(page.locator('#mana .mana-text')).toHaveText('14/20');
    await expect(page.locator('#spell-1')).toHaveClass(/active/);
    // Pompon's sheet: size, weight, age; its gifts as a web.
    await page.keyboard.press('e');
    await expect(page.locator('#team-detail .measures')).toContainText(/Taille : [\d,]+ cm · poids : [\d,]+ kg · âge :/);
    await expect(page.locator('#team-detail .gift-web .web-label')).toHaveCount(5);
    await expect(page.locator('#team-detail .gift-web')).toHaveAttribute('aria-label', /Force \d+, Dextérité \d+/);
    const team = await page.evaluate(() => window.monstres.team());
    for (const g of team[0].gifts) {
      expect(g).toBeGreaterThanOrEqual(5);
      expect(g).toBeLessThanOrEqual(15);
    }
    // The bestiary: the record of a species, the builds among the variants.
    await page.keyboard.press('b');
    await page.locator('.bestiary-entry[data-species="slime"]').click();
    await expect(page.locator('#bestiary-detail .record')).toContainText('Monstres capturés');
    await page.locator('#dna-button').click();
    await expect(page.locator('.dna-group h3').last()).toHaveText('Gabarits');
    await page.keyboard.press('Escape');
    // Mercy, in a battle: active until it holds a blow back.
    await page.evaluate(() => {
      const m = window.monstres;
      m.equipSpell(0, 3);
      m.step(1);
      const st = m.state();
      const i = st.creatures.findIndex((c, k) => k > 0 && ['slime', 'mushroom', 'bird'].includes(c.species));
      m.teleport(st.creatures[i].x + 4, st.creatures[i].z);
      m.step(1);
      m.engage(i);
      m.step(2);
      m.cast(0);
      m.step(1);
    });
    const b = await page.evaluate(() => window.monstres.spells());
    expect(b.mercy).toBe(true);
    expect(b.manaNow).toBe(9);
    await expect(page.locator('#spell-0')).toHaveAttribute('data-spell', 'mercy');
  });

  test('a gift tells its range and modifiers; variants tell their effects; Lison and the giant Champi', async ({ page }) => {
    await openGame(page, '?seed=3&new&frozen');
    await page.keyboard.press('e');
    // Beside the web: the detail of the first gift, then of the one chosen.
    // (Hover is not simulated here: WebKit's pointer lands beside the label.)
    const tip = page.locator('#team-detail .gift-info');
    await expect(tip).toBeVisible();
    await expect(tip).toContainText('Force');
    await expect(tip).toContainText('Minimum 5 · maximum 15');
    await expect(tip).toContainText('Aucun modificateur');
    await page.locator('.web-hit').nth(1).focus();
    await expect(tip).toContainText('Dextérité');
    // No more fight-stat bars on the sheet; the list and the sheet scroll each on their own.
    await expect(page.locator('#team-detail > .stats')).toHaveCount(0);
    const scroll = await page.evaluate(() => ['team-detail', 'tab-team'].map((id) => getComputedStyle(document.getElementById(id)).overflowY));
    expect(scroll).toEqual(['auto', 'hidden']);
    // The bestiary: the omega's effects.
    await page.keyboard.press('b');
    await page.locator('.bestiary-entry[data-species="mushroom"]').click();
    await page.locator('#dna-button').click();
    await expect(page.locator('.variant-cell[data-id="omega"]')).toHaveCount(1);
    const bestiary = await page.evaluate(() => window.monstres.bestiary());
    const horn = bestiary.find((e) => e.species === 'slime').variants.find((v) => v.id === 'top.horn');
    expect(horn.mods).toEqual([{ stat: 'charisma', value: -1 }, { stat: 'defense', value: 2 }]);
    await page.keyboard.press('Escape');
    // Lison tells of the giant Champi: the quest, in the journal.
    const i = await page.evaluate(() => {
      const m = window.monstres;
      for (let i = 1; i < m.state().creatures.length; i++) {
        m.select(i);
        const c = m.selected();
        if (c && c.npc === 3) { m.teleport(c.x + 1.5, c.z); m.step(1); return i; }
      }
      return -1;
    });
    expect(i).toBeGreaterThan(0);
    await page.evaluate((i) => { window.monstres.select(i); window.monstresDialogue.open(window.monstres.selected()); }, i);
    await expect(page.locator('#dialogue .dialogue-name')).toHaveText('Lison');
    await expect(page.locator('#dialogue .dialogue-text')).toContainText('Champi gigantesque');
    await page.locator('#accept-quest').click();
    await expect(page.locator('#dialogue .dialogue-text')).toContainText('costaud');
    const quest = (await page.evaluate(() => window.monstres.quests())).find((q) => q.id === 'giantChampi');
    expect(quest.stage).toBe('active');
    await page.keyboard.press('Escape');
    await page.keyboard.press('j');
    await expect(page.locator('.quest-entry[data-quest="giantChampi"]')).toBeVisible();
    await page.locator('.quest-entry[data-quest="giantChampi"]').click();
    await expect(page.locator('#quest-detail')).toContainText('Capturer le Champi géant');
  });

  test('talking to Mirabelle gives a quest, found in the journal', async ({ page }) => {
    await openGame(page, '?seed=3&new&frozen');
    // Next to her, then her menu (the context menu is driven by a tap: select through the page).
    const i = await page.evaluate(() => {
      const m = window.monstres;
      for (let i = 1; i < m.state().creatures.length; i++) {
        m.select(i);
        const c = m.selected();
        if (c && c.npc === 1) { m.teleport(c.x + 2, c.z); m.step(1); return i; }
      }
      return -1;
    });
    expect(i).toBeGreaterThan(0);
    await page.evaluate((i) => window.monstresDialogue.open(window.monstres.selected()), i);
    await expect(page.locator('#dialogue .dialogue-name')).toHaveText('Mirabelle');
    await expect(page.locator('#dialogue .dialogue-text')).toContainText('émeraude');
    await page.locator('#accept-quest').click();
    await page.evaluate(() => window.monstres.step(1));
    await expect(page.locator('#toast')).toContainText('Nouvelle quête');
    await page.keyboard.press('Escape');
    await page.keyboard.press('j');
    await expect(page.locator('#quest-detail h2')).toHaveText('Le Piou de Mirabelle');
    await expect(page.locator('#quest-detail .step')).toHaveCount(2);
    // Showing her Pompon: not even a Piou.
    await page.keyboard.press('Escape');
    await page.evaluate(() => window.monstresDialogue.open(window.monstres.selected()));
    await page.getByRole('button', { name: 'Montrer un monstre' }).click();
    await page.locator('#present-0').click();
    await expect(page.locator('#dialogue .dialogue-text')).toContainText('même pas un Piou');
  });

  test('teleporters: found by walking near, then a trip through the map', async ({ page }) => {
    await openGame(page, '?seed=3&new&frozen');
    const tps = await page.evaluate(() => window.monstres.teleporters().list);
    const castle = tps.find((t) => t.place === 'castle');
    const village = tps.find((t) => t.place === 'village');
    await page.evaluate(([x, z]) => { window.monstres.teleport(x, z); window.monstres.step(2); }, [castle.x, castle.z]);
    await expect(page.locator('#toast')).toContainText('Téléporteur découvert');
    await page.evaluate(([x, z]) => { window.monstres.teleport(x, z); window.monstres.step(2); }, [village.x, village.z]);
    const here = await page.evaluate(() => window.monstres.teleporters());
    expect(here.list.find((t) => t.place === 'castle').reachable).toBe(true);
    await page.evaluate(() => window.monstresMenu.open('map', { teleport: true }));
    await expect(page.locator('#map-hint')).toContainText('Choisis un téléporteur');
    await page.evaluate(() => window.monstresMenu.close());
    await page.evaluate((id) => { window.monstres.teleportTo(id); window.monstres.step(1); }, castle.id);
    expect((await state(page)).player.zone).toBe('castle');
  });

  test('after a trip, the teleporter arrived at is the one selected', async ({ page }) => {
    await openGame(page, '?seed=3&new&frozen');
    const tps = await page.evaluate(() => window.monstres.teleporters().list);
    const castle = tps.find((t) => t.place === 'castle');
    const village = tps.find((t) => t.place === 'village');
    await page.evaluate(([x, z]) => { window.monstres.teleport(x, z); window.monstres.step(2); }, [castle.x, castle.z]);
    await page.evaluate(([x, z]) => { window.monstres.teleport(x, z); window.monstres.step(2); }, [village.x, village.z]);
    // The village's teleporter selected, its map opened, the castle chosen on it.
    await page.evaluate((i) => window.dispatchEvent(new CustomEvent('monstres-object', { detail: { index: i } })), village.object);
    await expect(page.locator('#context .context-title')).toContainText('Village');
    await page.locator('#action-teleport').click();
    await expect(page.locator('#map-hint')).toBeVisible();
    const target = (await page.evaluate(() => window.monstresMenu.teleportTargets())).find((t) => t.id === castle.id);
    const box = await page.locator('#map-canvas').boundingBox();
    await page.mouse.click(box.x + target.x, box.y + target.y);
    await page.evaluate(() => window.monstres.step(1));
    expect((await state(page)).player.zone).toBe('castle');
    await expect(page.locator('#context .context-title')).toContainText('Corbeval');
  });

  test('a signpost of the village says where the places are', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    const sign = await page.evaluate(() => window.monstres.map().objects.find((o) => o[0] === 'sign'));
    await page.evaluate(([x, z]) => {
      window.monstres.teleport(x, z + 4);
      window.monstres.setCamera(0, 0.25);
      window.monstres.step(2);
    }, [sign[1], sign[2]]);
    const at = await page.evaluate(() => {
      for (let y = 60; y < 560; y += 6) {
        for (let x = 60; x < 760; x += 6) if (window.monstres.pickObject(x, y) >= 0 && window.monstres.object(window.monstres.pickObject(x, y)).kind === 'sign') return { x, y };
      }
      return null;
    });
    expect(at).not.toBeNull();
    await page.mouse.click(at.x, at.y);
    await expect(page.locator('#sign')).toBeVisible();
    await expect(page.locator('#sign .sign-title')).toHaveText('Bienvenue au village !');
    await expect(page.locator('#sign')).toContainText('Château de Corbeval');
    await expect(page.locator('#sign')).toContainText('Grotte aux Cristaux');
    await page.locator('#sign-close').click();
    await expect(page.locator('#sign')).toBeHidden();
  });

  test('far from the start, the monsters of the start are gone, others live there', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    const near = (s) => s.creatures.filter((c, i) => i > 0 && ['slime', 'mushroom', 'bird'].includes(c.species) &&
      Math.hypot(c.x, c.z) < 30).length;
    expect(near(await state(page))).toBeGreaterThan(1);
    // Far into the wilds, east of the valley.
    await page.evaluate(() => { window.monstres.teleport(420, 300); window.monstres.step(31); });
    const far = await state(page);
    expect(near(far)).toBe(0);
    expect(far.player.zone).toBe('plains');
    expect(far.creatures.filter((c) => Math.hypot(c.x - far.player.x, c.z - far.player.z) < 60).length).toBeGreaterThan(3);
  });

  test('the frame detail shows where the time goes', async ({ page }) => {
    await openGame(page, '?seed=42&new');
    await page.locator('#settings-button').click();
    await page.locator('#show-fps').check();
    await page.locator('#show-profile').check();
    await expect(page.locator('#fps .profile-section').first()).toBeVisible({ timeout: 10_000 });
    await expect(page.locator('#fps')).toContainText('Logique du jeu');
    await expect(page.locator('#fps')).toContainText('Triangles');
    const p = await page.evaluate(() => window.monstres.profile());
    expect(p.triangles).toBeGreaterThan(1000);
    expect(p.drawCalls).toBeGreaterThan(10);
  });
});

test('what the page sends to the game is bounded, and nothing breaks the security policy', async ({ page }) => {
  const problems = [];
  page.on('console', (m) => { if (/Content.Security|Refused to/i.test(m.text())) problems.push(m.text()); });
  page.on('pageerror', (e) => problems.push(String(e)));
  await openGame(page, '?seed=42&new&frozen');
  const before = await state(page);
  // A file far too big for a save is not even read.
  const big = await page.evaluate(() => window.monstres.importSave(new File([new Uint8Array(9 * 1024 * 1024)], 'x.sav'), 1));
  expect(big).toBe(false);
  // A portrait has a reasonable size, whatever is asked.
  expect(await page.evaluate(() => window.monstres.portrait(0, 99999, 0).width)).toBe(1024);
  // Not numbers: ignored (the player and the camera stay where they are).
  await page.evaluate(() => {
    window.monstres.input.move(NaN, NaN);
    window.monstres.input.look(NaN, Infinity);
    window.monstres.setCamera(NaN, 0);
    window.monstres.teleport(NaN, 5);
    window.monstres.step(5);
  });
  const after = await state(page);
  expect(after.player.x).toBe(before.player.x);
  expect(after.player.z).toBe(before.player.z);
  expect(Number.isFinite(after.camera.yaw)).toBe(true);
  expect(Number.isFinite(after.camera.pitch)).toBe(true);
  expect(problems).toEqual([]);
});

test('at the cave\'s mouth, "Entrer" leads inside in a fade to black, "Sortir" back out; the map stays outside', async ({ page }) => {
  await openGame(page, '?seed=5&new&frozen');
  const map = await page.evaluate(() => window.monstres.map());
  const cave = map.cave;
  const fx = cave.mouthX - cave.x, fz = cave.mouthZ - cave.z;
  const l = Math.hypot(fx, fz);
  // Inside the arch, in front of its dark hollow.
  await page.evaluate(([x, z]) => { window.monstres.teleport(x, z); window.monstres.step(1); },
    [cave.mouthX - fx / l * 2.0, cave.mouthZ - fz / l * 2.0]);
  const context = page.locator('#context');
  await expect(context).toBeVisible();
  await expect(context).toContainText('Grotte aux Cristaux');
  await context.getByRole('menuitem', { name: 'Entrer' }).click();
  await expect.poll(async () => (await state(page)).player.place).toBe('cave');
  await expect(page.locator('#fade')).not.toHaveClass(/on/);
  const inside = await state(page);
  expect(inside.player.zone).toBe('cave');
  // On the map: at the mouth.
  expect(Math.hypot(inside.player.mapX - cave.mouthX, inside.player.mapZ - cave.mouthZ)).toBeLessThan(4);
  // Cristis wait in the cave.
  expect(inside.creatures.filter((c) => c.species === 'cristi').length).toBeGreaterThanOrEqual(7);
  // Back to the way out.
  await page.evaluate(() => { window.monstres.input.move(0, -1); window.monstres.step(40); window.monstres.input.move(0, 0); });
  await expect(context).toContainText('Sortir');
  await context.getByRole('menuitem', { name: 'Sortir' }).click();
  await expect.poll(async () => (await state(page)).player.place).toBe('outside');
});

test.describe('the west', () => {
  // Next to villager `npc`, its dialogue open.
  async function talkTo(page, npc) {
    const i = await page.evaluate((n) => {
      const m = window.monstres;
      for (let i = 1; i < m.state().creatures.length; i++) {
        m.select(i);
        const c = m.selected();
        if (c && c.npc === n) { m.teleport(c.x + 1.2, c.z); m.step(1); return i; }
      }
      return -1;
    }, npc);
    expect(i).toBeGreaterThan(0);
    await page.evaluate((i) => { window.monstres.select(i); window.monstresDialogue.open(window.monstres.selected()); }, i);
  }

  test('Anselme\'s bundles found, his cart up again: he sells potions, kept in the bag', async ({ page }) => {
    await openGame(page, '?seed=5&new&frozen');
    await talkTo(page, 10);
    await expect(page.locator('#dialogue .dialogue-name')).toHaveText('Anselme');
    await expect(page.locator('#dialogue .dialogue-text')).toContainText('charrette');
    await page.locator('#accept-quest').click();
    // The three bundles, picked up as the magician walks by.
    const spots = await page.evaluate(() => window.monstres.quests().find((q) => q.id === 'merchant').steps.spots);
    expect(spots.length).toBe(3);
    for (const s of spots) await page.evaluate(([x, z]) => { window.monstres.teleport(x, z); window.monstres.step(12); }, [s.x, s.z]);
    expect((await page.evaluate(() => window.monstres.bag())).items.find((it) => it.id === 'goods').count).toBe(3);
    await talkTo(page, 10);
    await page.locator('#complete-quest').click();
    await expect(page.locator('#dialogue .dialogue-text')).toContainText('potions');
    // His shop.
    await talkTo(page, 10);
    await expect(page.locator('#dialogue .dialogue-text')).toContainText('pièces la fiole');
    await page.locator('#buy-potion').click();
    await expect.poll(async () => (await page.evaluate(() => window.monstres.bag())).items.find((it) => it.id === 'manaPotion').count).toBe(3);
    // The bag, in the grimoire.
    await page.keyboard.press('Escape');
    await page.keyboard.press('i');
    await expect(page.locator('#tab-bag')).toBeVisible();
    await expect(page.locator('#bag-coins')).toContainText('25 pièces');
    await page.locator('#bag-manaPotion').click();
    await expect(page.locator('#bag-detail')).toContainText('Potion de mana');
    await expect(page.locator('#bag-drink')).toBeDisabled();  // its mana is full
    await page.locator('#bag-discard-one').click();
    await expect.poll(async () => (await page.evaluate(() => window.monstres.bag())).items.find((it) => it.id === 'manaPotion').count).toBe(2);
  });

  test('the farm: Capucine, Violette the healer; the farm\'s panel lists the team', async ({ page }) => {
    await openGame(page, '?seed=5&new&frozen');
    await talkTo(page, 11);
    await expect(page.locator('#dialogue .dialogue-name')).toHaveText('Capucine');
    await expect(page.locator('#dialogue .dialogue-text')).toContainText('exploitation');
    await talkTo(page, 12);
    await expect(page.locator('#dialogue .dialogue-name')).toHaveText('Violette');
    await expect(page.locator('#dialogue .dialogue-text')).toContainText('pleine forme');
    await page.evaluate(() => window.monstresDialogue.openFarm());
    await expect(page.locator('#farm-panel')).toBeVisible();
    await expect(page.locator('#farm-team li')).toHaveCount(1);
    await expect(page.locator('#farm-kept')).toContainText('Personne');
    await expect(page.locator('#farm-swap')).toBeDisabled();
    await page.keyboard.press('Escape');
    await expect(page.locator('#farm-panel')).toBeHidden();
  });

  test('a villager\'s icons are its buttons; a trainer\'s monster says whose it is', async ({ page }) => {
    await openGame(page, '?seed=5&new&frozen');
    // Next to villager `npc` (or the first monster with that tag), touched: its menu on the right.
    const touch = (npc, tag) => page.evaluate(([npc, tag]) => {
      const m = window.monstres;
      for (let i = 1; i < m.state().creatures.length; i++) {
        m.select(i);
        const c = m.selected();
        if (c && (tag ? c.tag === tag : c.npc === npc && c.species === 'villager')) {
          m.teleport(c.x + 1.2, c.z);
          m.step(1);
          m.select(i);
          window.dispatchEvent(new CustomEvent('monstres-selection', { detail: { index: i } }));
          return m.selected();
        }
      }
      return null;
    }, [npc, tag]);
    const odette = await touch(5);
    expect(odette.marks).toContain('heal');
    await expect(page.locator('#action-heal')).toBeVisible();
    await expect(page.locator('#action-heal')).toBeDisabled();  // nothing to heal
    await expect(page.locator('#context .context-note')).toContainText('pleine forme');
    if (odette.marks.includes('newQuest')) await expect(page.locator('#action-talk')).toHaveClass(/newQuest/);
    else await expect(page.locator('#action-talk')).toHaveCount(0);
    await touch(4);
    await expect(page.locator('#action-fight')).toBeVisible();
    // Bastien's monster.
    await touch(0, 'rival');
    await expect(page.locator('#context .context-title')).toContainText('(à Bastien)');
  });

  test('Romane, in the woods, offers a battle; the map shows the west', async ({ page }) => {
    await openGame(page, '?seed=5&new&frozen');
    await talkTo(page, 15);
    await expect(page.locator('#dialogue .dialogue-name')).toHaveText('Romane');
    await expect(page.locator('#challenge')).toBeVisible();
    const map = await page.evaluate(() => window.monstres.map().west);
    expect(map.road.length).toBeGreaterThan(100);
    expect(map.farm.length).toBe(2);
  });
});
