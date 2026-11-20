// The game's sound (the Web Audio API): what the game tells (window.monstres
// .soundFrame(): the events since the last frame, where the camera is, the
// ambience, the sources around; see src/audio/sounds.hpp) played with the
// recipes of sound_recipes.js, in three groups with their own volume
// (settings menu: music, effects, ambience; kept by the page):
// - effects: the monsters' cries, attacks, spells, quests, the interface;
// - ambience: wind, rain, birds and crickets, the waterfall, the river, a
//   fire... (those placed in the world come from where they are);
// - music: the season's theme (slower at night, with drums in a battle).
// A group at 0 % makes no sound at all (nothing is built for it); all at
// 0 %, the game stops looking for sounds and the audio stays asleep. The
// browser only lets sound start after a first touch or key.
// In a cave (the burrow, a building: less) everything echoes.
// window.MonstresAudio: cry(species, kind, variant) (the bestiary's
// button), canPlay(group), stats() (the frame profiler).
(() => {
  'use strict';

  const R = window.MonstresSoundRecipes;
  const { $, whenReady, prefs } = window.MonstresUtil;
  const GROUPS = ['music', 'effects', 'ambience'];
  const DEFAULTS = { music: 50, effects: 80, ambience: 70 };  // %
  const MAX_VOICES = 32;       // sounds playing at once (effects): more are dropped
  const HEARD_FROM = 70;       // meters: placed sounds farther are not played
  const SPEED_OF_SOUND = 343;  // thunder comes later

  const volume = {};
  for (const g of GROUPS) volume[g] = Math.min(100, Math.max(0, Number(prefs.get(`volume.${g}`, DEFAULTS[g])) || 0));
  const wanted = () => GROUPS.some((g) => volume[g] > 0);
  const gainOf = (percent) => (percent / 100) ** 2;  // what the ear finds even

  let ctx = null;
  let game = null;
  let master = null;
  const bus = {};      // a gain per group
  const send = {};     // and its share to the echo
  let echo = null;     // ConvolverNode
  let echoPlace = 'outside';
  let lastFrame = null;
  const total = { events: 0, played: 0, cries: 0 };  // since the page opened (tests)
  const stats = { events: 0, played: 0, culled: 0, dropped: 0, bars: 0, ms: 0, frames: 0, since: performance.now(), report: null };
  let voiceEnds = [];  // when each effect playing ends (ctx time)

  // ---- The audio context: made at the first touch, asleep when unused ----
  function build() {
    const Ctx = window.AudioContext || window.webkitAudioContext;
    if (!Ctx) return false;
    ctx = new Ctx({ latencyHint: 'interactive' });
    // Awake again (the first time, back to the page): the game forgets what
    // happened while asleep (no burst of old sounds).
    ctx.addEventListener('statechange', () => {
      if (ctx.state === 'running' && game && wanted()) game.setSound(true);
    });
    master = ctx.createDynamicsCompressor();  // no clipping when many sounds add up
    master.threshold.value = -12;
    master.ratio.value = 4;
    master.connect(ctx.destination);
    echo = ctx.createConvolver();
    const wet = ctx.createGain();
    wet.gain.value = 0.8;
    echo.connect(wet).connect(master);
    for (const g of GROUPS) {
      bus[g] = ctx.createGain();
      bus[g].gain.value = gainOf(volume[g]);
      bus[g].connect(master);
      send[g] = ctx.createGain();
      send[g].gain.value = 0;
      bus[g].connect(send[g]);  // to the echo once inside (setPlace)
    }
    return true;
  }
  function awake() { return ctx && ctx.state === 'running'; }
  function wake() {
    if (!wanted()) return;
    if (!ctx && !build()) return;
    if (ctx.state !== 'running' && !document.hidden) ctx.resume().catch(() => {});
    if (game) game.setSound(true);
  }
  function sleep() {
    stopMusic();
    stopAmbience();
    if (game) game.setSound(false);
    if (ctx && ctx.state === 'running') ctx.suspend().catch(() => {});
  }
  // Sound can only start after the player touches the page.
  const gestures = ['pointerdown', 'keydown', 'touchend'];
  const onGesture = () => {
    wake();
    if (awake()) for (const type of gestures) window.removeEventListener(type, onGesture, true);
  };
  for (const type of gestures) window.addEventListener(type, onGesture, true);
  document.addEventListener('visibilitychange', () => {
    if (!ctx) return;
    if (document.hidden) ctx.suspend().catch(() => {});
    else if (wanted()) ctx.resume().catch(() => {});
  });

  // ---- The volumes (settings menu) ----
  function setVolume(group, percent) {
    volume[group] = percent;
    prefs.set(`volume.${group}`, percent);
    if (ctx) bus[group].gain.setTargetAtTime(gainOf(percent), ctx.currentTime, 0.05);
    if (group === 'music' && percent === 0) stopMusic();
    if (group === 'ambience' && percent === 0) stopAmbience();
    if (wanted()) wake();
    else sleep();
  }
  for (const g of GROUPS) {
    const input = $(`volume-${g}`);
    const output = $(`volume-${g}-value`);
    if (!input) continue;
    input.value = volume[g];
    output.textContent = `${volume[g]} %`;
    input.addEventListener('input', () => {
      output.textContent = `${input.value} %`;
      setVolume(g, Number(input.value));
    });
  }

  // ---- The echo: caves, the burrow, buildings ----
  const ECHOES = { cave: [3.2, 2.2, 0.55], burrow: [1.6, 3, 0.35], building: [0.8, 4, 0.15] };  // seconds, decay, share
  function setPlace(place) {
    if (place === echoPlace || !ctx) return;
    echoPlace = place;
    const e = ECHOES[place];
    for (const g of GROUPS) {
      try { send[g].disconnect(); } catch { /* not connected */ }
      send[g].gain.value = 0;
    }
    if (!e) return;  // outside: no echo (and no convolver working for nothing)
    echo.buffer = R.impulse(ctx, e[0], e[1]);
    for (const g of GROUPS) {
      send[g].gain.value = e[2] * (g === 'music' ? 0.4 : 1);
      send[g].connect(echo);
    }
  }

  // ---- Effects ----
  // Where a sound goes: a panner when it is placed, straight to its group otherwise.
  function voiceOut(group, at, seconds) {
    const out = ctx.createGain();
    let tail = out;
    if (at) {
      const p = ctx.createPanner();
      p.panningModel = 'equalpower';
      p.distanceModel = 'inverse';
      p.refDistance = 3;
      p.rolloffFactor = 1.1;
      p.maxDistance = 200;
      if (p.positionX) {
        p.positionX.value = at.x;
        p.positionY.value = at.y;
        p.positionZ.value = at.z;
      } else {
        p.setPosition(at.x, at.y, at.z);
      }
      out.connect(p);
      tail = p;
    }
    tail.connect(bus[group]);
    setTimeout(() => tail.disconnect(), (seconds + 1) * 1000);
    return out;
  }
  function busy() {
    const now = ctx.currentTime;
    voiceEnds = voiceEnds.filter((t) => t > now);
    return voiceEnds.length >= MAX_VOICES;
  }
  function distanceTo(e) {
    const l = lastFrame && lastFrame.listener;
    return l && e.x !== undefined ? Math.hypot(e.x - l.x, e.y - l.y, e.z - l.z) : 0;
  }
  // Plays one of the game's events (sounds.hpp: SoundEvent).
  function play(e) {
    ++stats.events;
    ++total.events;
    const group = e.sound === 'thunder' ? 'ambience' : 'effects';
    if (volume[group] === 0) return;
    const placed = e.x !== undefined;
    const far = placed ? distanceTo(e) : 0;
    if (placed && far > HEARD_FROM && e.sound !== 'thunder') {
      ++stats.culled;
      return;
    }
    if (busy()) {
      ++stats.dropped;
      return;
    }
    const t = ctx.currentTime + 0.01 + (e.sound === 'thunder' ? Math.min(6, far / SPEED_OF_SOUND) : 0);
    const at = placed && e.sound !== 'thunder' ? e : null;
    const out = voiceOut(group, at, 4);
    const length = e.sound === 'cry' ? R.cry(ctx, out, t, e.species, e.cry, e.variant)
      : R.effects[e.sound] ? R.effects[e.sound](ctx, out, t, e) : 0;
    voiceEnds.push(t + (length || 0.5));
    ++stats.played;
    ++total.played;
    if (e.sound === 'cry') ++total.cries;
  }

  // ---- Ambience: loops (wind, rain...), little sounds dropped now and then,
  // the sources placed around ----
  const loops = {};    // name: { set, stop }
  const sources = new Map();  // key: { kind, loop, out, at, next }
  let nextDrop = {};
  function loopLevel(name, amount) {
    if (amount <= 0.001 && !loops[name]) return;
    if (!loops[name]) loops[name] = R.loops[name](ctx, bus.ambience);
    loops[name].set(amount, ctx.currentTime);
  }
  function stopAmbience() {
    if (!ctx) return;
    for (const name of Object.keys(loops)) {
      loops[name].stop(ctx.currentTime);
      delete loops[name];
    }
    for (const s of sources.values()) s.loop.stop(ctx.currentTime);
    sources.clear();
    nextDrop = {};
  }
  function drop(name, every, at) {  // every: seconds on average (0: never)
    const now = ctx.currentTime;
    const key = at ? `${name}@${at.x | 0},${at.z | 0}` : name;
    if (!every) {
      delete nextDrop[key];
      return;
    }
    if (nextDrop[key] === undefined) nextDrop[key] = now + Math.random() * every;
    if (now < nextDrop[key]) return;
    nextDrop[key] = now + every * (0.4 + 1.2 * Math.random());
    const length = R.drops[name](ctx, voiceOut('ambience', at, 2), now + 0.01);
    voiceEnds.push(now + length);
  }
  const SOURCE_LEVEL = { waterfall: 1, river: 0.6, waterWheel: 0.7, windmill: 0.5, fire: 0.6 };
  function updateAmbience(a, list) {
    const out = a.place === 'outside';
    const wet = Math.max(a.rain, a.snow);
    loopLevel('wind', out ? 0.12 + 0.88 * a.wind : 0);
    loopLevel('rain', a.rain);
    loopLevel('snow', a.snow);
    loopLevel('storm', a.storm);
    loopLevel('cave', a.place === 'cave' || a.place === 'burrow' ? 1 : 0);
    const day = 1 - a.night;
    const birds = { spring: 1.4, summer: 2.5, autumn: 6, winter: 0 }[a.season] || 0;
    drop('bird', out && day > 0.5 && wet < 0.3 && birds && !a.battle ? birds : 0);
    const crickets = { spring: 2.5, summer: 0.9, autumn: 1.8, winter: 0 }[a.season] || 0;
    drop('cricket', out && a.night > 0.6 && wet < 0.3 && crickets ? crickets : 0);
    drop('drip', a.place === 'cave' || a.place === 'burrow' ? 2.5 : 0);
    // The sources: those around now (a key by place: they do not move).
    const seen = new Set();
    for (const s of list) {
      const key = `${s.kind}@${Math.round(s.x)},${Math.round(s.z)}`;
      seen.add(key);
      let src = sources.get(key);
      if (!src) {
        const o = voiceOutLasting(s);
        src = { kind: s.kind, out: o.out, panner: o.panner, loop: R.loops[s.kind](ctx, o.out), at: s };
        sources.set(key, src);
      }
      src.loop.set(SOURCE_LEVEL[s.kind] || 0.5, ctx.currentTime);
      if (s.kind === 'fire') drop('crackle', 0.35, s);
      if (s.kind === 'windmill' || s.kind === 'waterWheel') drop('creak', 2.2, s);
    }
    for (const [key, src] of sources) {
      if (seen.has(key)) continue;
      src.loop.stop(ctx.currentTime);
      setTimeout(() => src.panner.disconnect(), 3000);
      sources.delete(key);
    }
  }
  // A lasting placed output (a source's loop): it stays until the source goes.
  function voiceOutLasting(at) {
    const out = ctx.createGain();
    const p = ctx.createPanner();
    p.panningModel = 'equalpower';
    p.distanceModel = 'inverse';
    p.refDistance = at.kind === 'waterfall' ? 10 : 4;
    p.rolloffFactor = 1.2;
    if (p.positionX) {
      p.positionX.value = at.x;
      p.positionY.value = at.y;
      p.positionZ.value = at.z;
    } else {
      p.setPosition(at.x, at.y, at.z);
    }
    out.connect(p).connect(bus.ambience);
    return { out, panner: p };
  }

  // ---- Music: the season's theme, a bar at a time, a little ahead ----
  let music = null;  // { season, out, bar, next }
  function stopMusic() {
    if (!music || !ctx) return;
    const old = music;
    old.out.gain.setTargetAtTime(0, ctx.currentTime, 0.6);
    setTimeout(() => old.out.disconnect(), 4000);
    music = null;
  }
  function updateMusic(a) {
    if (volume.music === 0) return;
    if (music && music.season !== a.season) stopMusic();  // another season: the old one fades away
    if (!music) {
      const out = ctx.createGain();
      out.gain.value = 0;
      out.gain.setTargetAtTime(1, ctx.currentTime, 1.0);
      out.connect(bus.music);
      music = { season: a.season, out, bar: 0, next: ctx.currentTime + 0.2 };
    }
    // Quieter in a cave, during a capture.
    const level = (a.place === 'cave' || a.place === 'burrow' ? 0.6 : 1) * (a.capture ? 0.5 : 1);
    music.out.gain.setTargetAtTime(level, ctx.currentTime, 0.8);
    while (music.next < ctx.currentTime + 1.2) {
      music.next += R.themeBar(ctx, music.out, music.next, music.season, music.bar++, { night: a.night > 0.6, battle: a.battle });
      ++stats.bars;
    }
  }

  // ---- Every frame: the game's sounds ----
  let frames = 0;
  function update() {
    requestAnimationFrame(update);
    if (!game || !awake()) return;
    const start = performance.now();
    const slow = frames++ % 6 === 0;  // ten times a second is enough for what changes slowly
    const f = game.soundFrame(slow && volume.ambience > 0);
    lastFrame = f;
    const l = f.listener;
    const L = ctx.listener;
    const fx = -Math.sin(l.yaw), fz = -Math.cos(l.yaw);
    if (L.positionX) {
      const t = ctx.currentTime;
      L.positionX.setTargetAtTime(l.x, t, 0.02);
      L.positionY.setTargetAtTime(l.y, t, 0.02);
      L.positionZ.setTargetAtTime(l.z, t, 0.02);
      L.forwardX.setTargetAtTime(fx, t, 0.02);
      L.forwardY.setTargetAtTime(0, t, 0.02);
      L.forwardZ.setTargetAtTime(fz, t, 0.02);
      L.upX.value = 0;
      L.upY.value = 1;
      L.upZ.value = 0;
    } else {
      L.setPosition(l.x, l.y, l.z);
      L.setOrientation(fx, 0, fz, 0, 1, 0);
    }
    setPlace(f.ambience.place);
    for (const e of f.events) play(e);
    if (slow) {
      if (volume.ambience > 0) updateAmbience(f.ambience, f.sources);
      if (volume.music > 0) updateMusic(f.ambience);
    }
    stats.ms += performance.now() - start;
    ++stats.frames;
  }

  // ---- For the page ----
  // A monster's cry, now (the bestiary): false when effects are silent.
  function cry(species, kind = 'call', variant = Math.floor(Math.random() * 3)) {
    if (volume.effects === 0) return false;
    wake();
    if (!ctx) return false;
    const go = () => {
      const t = ctx.currentTime + 0.02;
      voiceEnds.push(t + R.cry(ctx, voiceOut('effects', null, 3), t, species, kind, variant));
      ++total.cries;
    };
    if (ctx.state === 'running') go();
    else ctx.resume().then(go).catch(() => {});
    return true;
  }
  // For the frame profiler: the last half second.
  function report() {
    const now = performance.now();
    const span = (now - stats.since) / 1000;
    if (span >= 0.5 || !stats.report) {
      stats.report = {
        state: !wanted() ? 'off' : ctx ? ctx.state : 'waiting',
        voices: ctx ? voiceEnds.filter((t) => t > ctx.currentTime).length : 0,
        loops: Object.keys(loops).length + sources.size,
        eventsPerSecond: stats.events / span,
        playedPerSecond: stats.played / span,
        culled: stats.culled,
        dropped: stats.dropped,
        bars: stats.bars,
        ms: stats.frames ? stats.ms / stats.frames : 0,
        latencyMs: ctx ? 1000 * ((ctx.baseLatency || 0) + (ctx.outputLatency || 0)) : 0,
        sampleRate: ctx ? ctx.sampleRate : 0,
        echo: echoPlace !== 'outside',
        place: echoPlace,
        music: music ? music.season : null,
        total: { ...total },
      };
      Object.assign(stats, { events: 0, played: 0, culled: 0, dropped: 0, bars: 0, ms: 0, frames: 0, since: now });
    }
    return stats.report;
  }

  window.MonstresAudio = { cry, canPlay: (group) => volume[group] > 0, stats: report, species: R.species };
  whenReady((g) => {
    if (typeof g.soundFrame !== 'function') return;
    game = g;
    if (awake()) game.setSound(true);
    requestAnimationFrame(update);
  });
})();
