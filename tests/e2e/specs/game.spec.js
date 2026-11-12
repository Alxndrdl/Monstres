// @ts-check
// Functional tests in real browsers: the game as a player uses it.
const { test, expect } = require('@playwright/test');
const { openGame, state, walk, center } = require('./helpers');

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
  expect(bySpecies).toMatchObject({ companion: 1, villager: 8 });
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
    await expect.poll(async () => { const p = (await state(page)).player; return p.y - p.ground; }).toBeGreaterThan(0.3);
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

test.describe('settings menu', () => {
  test('the gear opens the menu with the save buttons, Escape closes it', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    const menu = page.locator('#settings');
    await expect(menu).toBeHidden();
    await expect(page.locator('#export')).toBeHidden();  // only in the menu
    await page.locator('#settings-button').click();
    await expect(menu).toBeVisible();
    await expect(page.locator('#export')).toBeVisible();
    await expect(page.locator('#import')).toBeVisible();
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
    await expect(panel.locator('.name')).toHaveText('Pompon');
    await expect(panel.locator('.facts')).toHaveText(/^Niveau 3 · T’attend · à 1 m$/);  // just behind the player
    await expect(panel.locator('.tags .badge')).toHaveText(['Bête', 'Feu']);
    await expect(panel.locator('.variants')).toHaveText('Variantes : Mâle · Pelage de braise · Oreilles pointues · Une queue');
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
    await expect(page.getByRole('tab')).toHaveText(['Personnage', 'Équipe', 'Quêtes', 'Bestiaire', 'Carte']);
    const tick = (await state(page)).tick;
    await page.waitForTimeout(500);
    expect((await state(page)).tick).toBe(tick);  // paused
    await page.keyboard.press('Escape');
    await expect(menu).toBeHidden();
    await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(tick);  // time goes on
  });

  test('the character sheet shows the magician, the name is kept', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    await page.locator('#menu-button').click();
    await page.getByRole('tab', { name: 'Personnage' }).click();
    await expect(page.locator('#wizard-rank')).toHaveText('Apprenti magicien · niveau 1');
    await expect(page.locator('#wizard-stats')).toContainText('Points de vie');
    await expect(page.locator('#wizard-where')).toContainText('Prairie');
    await expect(page.locator('#wizard-portrait')).toHaveAttribute('src', /^data:image\/png/);
    const before = (await state(page)).player;
    const name = page.locator('#wizard-name');
    await name.fill('Zoé');
    await name.press('d');  // typing letters does not walk
    await name.press('Enter');
    expect((await state(page)).player).toEqual(before);
    await expect.poll(() => page.evaluate(() => window.monstres.wizard().name)).toBe('Zoéd');
    await page.evaluate(() => window.monstres.saveNow());

    await openGame(page, '?frozen');  // the same game, loaded again
    await page.locator('#menu-button').click();
    await page.getByRole('tab', { name: 'Personnage' }).click();
    await expect(page.locator('#wizard-name')).toHaveValue('Zoéd');
  });

  test('the bestiary lists the monsters, each with its card', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    await page.locator('#menu-button').click();
    await page.getByRole('tab', { name: 'Bestiaire' }).click();
    const entries = page.locator('.bestiary-entry');
    await expect(entries).toHaveCount(6);  // villagers are not monsters
    await expect(entries.locator('strong')).toHaveText(['Pompon', 'Gloubi', 'Champi', 'Piou', 'Croa', 'Bloup']);
    await page.locator('.bestiary-entry[data-species="mushroom"]').click();
    const detail = page.locator('#bestiary-detail');
    await expect(detail.locator('h2')).toHaveText('Champi');
    await expect(detail.locator('.badge')).toHaveText(['Végétal', 'Nature']);
    await expect(detail).toContainText('Défense');
    await expect(detail).toContainText(/Dans ce monde : \d+ individu/);
    await expect(detail.locator('img')).toHaveAttribute('src', /^data:image\/png/);
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
    await expect(entry.locator('.dna-count')).toHaveText(`${seen}/${seenSpecies.variants.length}`);
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
    await expect(page.locator('#variant-info h3')).toHaveText('Mâle');  // the sex comes first
    // The chosen variant is shown beside the grid, not below it.
    const grid = await page.locator('.dna-groups').boundingBox();
    const info = await page.locator('#variant-info').boundingBox();
    expect(info.x).toBeGreaterThanOrEqual(grid.x + grid.width);
    expect(info.y).toBeLessThan(grid.y + 40);
    await expect(page.locator('#variant-info .status')).toHaveText('Capturée');
    await page.locator('.dna-back').click();
    await expect(page.locator('#bestiary-detail h2')).toHaveText('Pompon');
  });

  test('the map shows where the player is, nearby or the whole world', async ({ page }) => {
    await openGame(page, '?seed=42&new&frozen');
    await page.keyboard.press('m');  // shortcut
    await expect(page.getByRole('tab', { name: 'Carte' })).toHaveAttribute('aria-selected', 'true');
    await expect(page.locator('#map-where')).toHaveText('Tu te trouves : Prairie');
    const painted = () => page.evaluate(() => {
      const c = document.getElementById('map-canvas');
      const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
      const colors = new Set();
      for (let i = 0; i < d.length; i += 4 * 97) colors.add(`${d[i] >> 4},${d[i + 1] >> 4},${d[i + 2] >> 4}`);
      return colors.size;
    });
    expect(await painted()).toBeGreaterThan(20);  // terrain, path, trees...
    await page.getByRole('button', { name: 'Globale' }).click();
    await expect(page.getByRole('button', { name: 'Globale' })).toHaveAttribute('aria-pressed', 'true');
    expect(await painted()).toBeGreaterThan(20);
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

    // A special attack: a projectile, and the button waits for its cooldown.
    await page.locator('#attack-2').dispatchEvent('pointerdown');
    await page.evaluate(() => window.monstres.step(40));
    const after = await page.evaluate(() => window.monstres.battle());
    expect(after.enemy.hp).toBeLessThan(battle.enemy.hp);
    expect(after.ally.fighter.cooldowns[1]).toBeGreaterThan(0);
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
    await page.evaluate((i) => window.monstres.engage(i), BIRD);
    // Fight until it is weak enough (or beaten: then try again).
    const result = await page.evaluate(() => {
      const m = window.monstres;
      for (let i = 0; i < 400; i++) {
        const b = m.battle();
        if (b.phase !== 'fight') return b.outcome.result;
        if (b.capture === 'ok') return 'weak';
        m.input.move(0, b.enemy.hp * 4 > b.enemy.maxHp ? 1 : 0);
        if (b.enemy.fighter.state === 'windup') m.input.parry();  // its attacks are shown first
        else if (b.enemy.hp * 2 > b.enemy.maxHp && b.ally.fighter.cooldowns[1] === 0) m.input.attack(2);
        else if (b.enemy.hp * 3 > b.enemy.maxHp) m.input.attack(1);
        m.step(4);
      }
      return 'timeout';
    });
    expect(result).toBe('weak');
    await expect(page.locator('#action-capture')).toBeEnabled();
    await page.locator('#action-capture').click();
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
    await expect(page.locator('#team-detail .move')).toHaveCount(3);
    await expect(page.locator('#team-detail .move').first()).toContainText('Coup de bec');
    await page.locator('#team-list li').first().locator('button').click();
    await expect(page.locator('#team-detail h2')).toHaveText('Pompon');
    await expect(page.locator('#team-detail')).toContainText('Boule de braise');
    await expect(page.locator('#team-detail')).toContainText('Niveau 3');

    // Letting the bird go: asked twice; it comes out of the crystal and leaves.
    await page.locator('#team-list li').nth(1).locator('button').click();
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
    await expect(page.locator('.fighter.enemy')).toBeHidden();  // no hit points: no one is hurt
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
  });
});

test.describe('the bigger world', () => {
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
    await expect(page.locator('#map-where')).toContainText('Choisis un téléporteur');
    await page.evaluate(() => window.monstresMenu.close());
    await page.evaluate((id) => { window.monstres.teleportTo(id); window.monstres.step(1); }, castle.id);
    expect((await state(page)).player.zone).toBe('castle');
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
      Math.hypot(c.x, c.z) < 18).length;
    expect(near(await state(page))).toBeGreaterThan(1);
    const castle = await page.evaluate(() => window.monstres.map().castle);
    await page.evaluate(([x, z]) => { window.monstres.teleport(x, z); window.monstres.step(31); }, [castle.x, castle.z]);
    const far = await state(page);
    expect(near(far)).toBe(0);
    expect(far.player.zone).toBe('castle');
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
