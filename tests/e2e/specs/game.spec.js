// @ts-check
// Functional tests in real browsers: the game as a player uses it.
const { test, expect } = require('@playwright/test');
const { openGame, state, walk, center } = require('./helpers');

test('the game starts in a 3D world', async ({ page }) => {
  await openGame(page, '?seed=42&new&frozen');
  const s = await state(page);
  expect(s.tick).toBe(0);
  expect(s.player).toMatchObject({ x: 0, y: 0, z: 0, onGround: true });
  expect(s.objects).toBeGreaterThan(150);  // nature + village
  expect(s.creatures[0].species).toBe('companion');
  const bySpecies = {};
  for (const c of s.creatures) bySpecies[c.species] = (bySpecies[c.species] || 0) + 1;
  expect(bySpecies).toMatchObject({ companion: 1, villager: 8, crow: 5, fish: 4 });
  expect(bySpecies.slime + bySpecies.mushroom + bySpecies.bird).toBe(16);
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
    await expect.poll(async () => (await state(page)).player.y).toBeGreaterThan(0.3);
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
    await expect.poll(async () => (await state(page)).player.y).toBeGreaterThan(0.3);
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
    await expect(page.locator('#move-hint')).toContainText('pour se déplacer');
    await expect(page.getByText('pour sauter')).toBeVisible();
  });

  test('keyboard hints appear once a key is pressed', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    await page.keyboard.press('KeyW');
    await expect(page.locator('#move-hint')).toBeVisible();
    await expect(page.getByText('pour sauter')).toBeVisible();
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
    await expect.poll(async () => (await state(page)).player.y).toBeGreaterThan(0.3);
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

  test('a save can be exported to a file and imported back', async ({ page }) => {
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
    await page.locator('#import-file').setInputFiles(file);
    await expect.poll(async () => (await state(page)).player).toEqual(exported);
  });

  test('an invalid save file is refused', async ({ page }) => {
    await openGame(page, '?seed=7&new&frozen');
    await walk(page, 1, 0, 10);
    const before = await state(page);
    page.once('dialog', (dialog) => dialog.accept());
    await page.locator('#import-file').setInputFiles({
      name: 'bad.sav', mimeType: 'application/octet-stream', buffer: Buffer.from('not a save'),
    });
    await page.waitForTimeout(200);
    expect(await state(page)).toEqual(before);
  });
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
