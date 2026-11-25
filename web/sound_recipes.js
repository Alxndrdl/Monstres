// The sounds themselves, made with the Web Audio API (no sound files): the
// monsters' cries, the effects (attacks, spells, quests...), the background
// sounds and the four seasons' themes. Each recipe plays into a node it is
// given, at a time it is given, and returns how long it lasts; audio.js
// decides what plays, when, where and how loud.
(() => {
  'use strict';

  const midi = (n) => 440 * 2 ** ((n - 69) / 12);

  // ---- Small tools ----
  const noises = new WeakMap();  // one buffer of white noise per context
  function noise(ctx) {
    let b = noises.get(ctx);
    if (!b) {
      b = ctx.createBuffer(1, ctx.sampleRate * 2, ctx.sampleRate);
      const d = b.getChannelData(0);
      let s = 12345;
      for (let i = 0; i < d.length; ++i) {
        s = (s * 1103515245 + 12345) >>> 0;
        d[i] = (s / 4294967296) * 2 - 1;
      }
      noises.set(ctx, b);
    }
    return b;
  }
  function noiseSource(ctx, loop = true) {
    const src = ctx.createBufferSource();
    src.buffer = noise(ctx);
    src.loop = loop;
    src.loopStart = Math.random();
    return src;
  }
  // A gain shaped like a note: up in `a` seconds to `peak`, down to nothing after `d` more.
  function envelope(ctx, out, t, a, d, peak) {
    const g = ctx.createGain();
    g.gain.setValueAtTime(0.0001, t);
    g.gain.linearRampToValueAtTime(peak, t + a);
    g.gain.exponentialRampToValueAtTime(0.0001, t + a + d);
    g.connect(out);
    return g;
  }
  function osc(ctx, type, f, t, stop) {
    const o = ctx.createOscillator();
    o.type = type;
    o.frequency.setValueAtTime(f, t);
    o.start(t);
    o.stop(stop);
    return o;
  }
  function filter(ctx, type, f, q = 1) {
    const n = ctx.createBiquadFilter();
    n.type = type;
    n.frequency.value = f;
    n.Q.value = q;
    return n;
  }
  // A tone: an oscillator from f0 to f1 in a note's envelope.
  function tone(ctx, out, t, { type = 'sine', f0, f1 = f0, a = 0.01, d = 0.2, peak = 0.3, glide = 'exp' }) {
    const g = envelope(ctx, out, t, a, d, peak);
    const o = osc(ctx, type, f0, t, t + a + d + 0.05);
    if (f1 !== f0) {
      if (glide === 'exp') o.frequency.exponentialRampToValueAtTime(Math.max(1, f1), t + a + d);
      else o.frequency.linearRampToValueAtTime(f1, t + a + d);
    }
    o.connect(g);
    return a + d;
  }
  // A burst of noise through a filter.
  function hiss(ctx, out, t, { type = 'bandpass', f0 = 1000, f1 = f0, q = 1, a = 0.005, d = 0.15, peak = 0.3 }) {
    const g = envelope(ctx, out, t, a, d, peak);
    const f = filter(ctx, type, f0, q);
    if (f1 !== f0) f.frequency.exponentialRampToValueAtTime(Math.max(20, f1), t + a + d);
    const src = noiseSource(ctx, false);
    src.connect(f).connect(g);
    src.start(t, Math.random());
    src.stop(t + a + d + 0.05);
    return a + d;
  }
  // A bell: inharmonic partials, each fading at its own pace.
  function bell(ctx, out, t, f, peak = 0.2, d = 1.2) {
    [[1, 1], [2.76, 0.5], [5.4, 0.25], [8.93, 0.12]].forEach(([r, k]) => tone(ctx, out, t, { f0: f * r, a: 0.002, d: d / Math.sqrt(r), peak: peak * k }));
    return d;
  }

  // ---- The monsters' cries ----
  // Each species: its voice (wave, pitch, timbre) and the shape of its call.
  // syllables: [start (s), length (s), pitch from, pitch to] (pitches: × base).
  const VOICES = {
    companion: { wave: 'triangle', base: 640, lp: 3200, vib: [0, 0], call: [[[0, 0.11, 1, 1.35], [0.15, 0.16, 1.3, 0.9]],
      [[0, 0.09, 1.1, 1.5], [0.12, 0.09, 1.2, 1.5], [0.24, 0.14, 1.3, 1.0]], [[0, 0.32, 0.8, 1.25]]] },
    slime: { wave: 'sine', base: 230, lp: 1400, vib: [17, 0.12], call: [[[0, 0.22, 0.9, 1.5]], [[0, 0.12, 1, 1.3], [0.16, 0.18, 1.2, 0.8]],
      [[0, 0.35, 1.3, 0.7]]] },
    mushroom: { wave: 'square', base: 360, lp: 1100, vib: [0, 0], call: [[[0, 0.14, 1, 1.15], [0.18, 0.12, 1.1, 0.95]],
      [[0, 0.25, 0.9, 1.2]], [[0, 0.1, 1.2, 1.2], [0.13, 0.1, 1, 1], [0.26, 0.12, 1.15, 1.15]]] },
    bird: { wave: 'sine', base: 2300, lp: 9000, vib: [0, 0], call: [[[0, 0.06, 1, 1.4], [0.09, 0.06, 1.1, 1.5], [0.18, 0.1, 1.3, 0.9]],
      [[0, 0.12, 0.8, 1.6]], [[0, 0.05, 1.4, 1.1], [0.07, 0.05, 1.4, 1.1], [0.14, 0.05, 1.4, 1.1], [0.21, 0.08, 1.5, 1.0]]] },
    crow: { wave: 'sawtooth', base: 470, lp: 1700, vib: [38, 0.05], rasp: 0.35, call: [[[0, 0.28, 1.05, 0.9]],
      [[0, 0.2, 1, 0.9], [0.3, 0.22, 1, 0.85]], [[0, 0.35, 1.1, 0.8]]] },
    fish: { wave: 'sine', base: 520, lp: 2000, vib: [0, 0], bubble: true, call: [[[0, 0.1, 1.6, 0.7]],
      [[0, 0.08, 1.4, 0.8], [0.12, 0.08, 1.6, 0.8]], [[0, 0.16, 1.2, 0.5]]] },
    spark: { wave: 'square', base: 900, lp: 4000, vib: [24, 0.06], crackle: 0.25, call: [[[0, 0.07, 1, 1.4], [0.1, 0.12, 1.3, 1.1]],
      [[0, 0.06, 1.2, 1.6], [0.08, 0.06, 1.2, 1.6], [0.16, 0.1, 1.4, 1.0]], [[0, 0.2, 0.9, 1.5]]] },
    axolotl: { wave: 'triangle', base: 480, lp: 1800, vib: [5, 0.04], call: [[[0, 0.3, 0.95, 1.2]],
      [[0, 0.16, 1, 1.25], [0.22, 0.2, 1.15, 0.95]], [[0, 0.4, 1.2, 0.9]]] },
    cristi: { bell: true, base: 1250, call: [[[0, 0.5, 1, 1]], [[0, 0.3, 1, 1], [0.18, 0.5, 1.5, 1.5]], [[0, 0.3, 1.33, 1.33], [0.2, 0.6, 1, 1]]] },
    raccoon: { wave: 'sawtooth', base: 760, lp: 2600, vib: [0, 0], call: [[[0, 0.05, 1, 1.2], [0.07, 0.05, 1.05, 1.25], [0.14, 0.05, 1, 1.2], [0.21, 0.08, 1.1, 0.9]],
      [[0, 0.18, 1.2, 0.9]], [[0, 0.05, 1, 1.3], [0.08, 0.05, 1, 1.3], [0.16, 0.12, 1.2, 0.8]]] },
    owl: { wave: 'sine', base: 360, lp: 900, vib: [0, 0], breath: 0.06, call: [[[0, 0.22, 1, 0.95], [0.4, 0.42, 1, 0.9]],
      [[0, 0.5, 1.05, 0.92]], [[0, 0.16, 1.1, 1.0], [0.24, 0.16, 1.1, 1.0], [0.55, 0.45, 1, 0.9]]] },
    sheep: { wave: 'sawtooth', base: 300, lp: 1500, vib: [7, 0.06], vowel: 900, call: [[[0, 0.55, 1, 0.95]],
      [[0, 0.3, 1.1, 1.05], [0.38, 0.45, 1, 0.92]], [[0, 0.7, 0.92, 0.9]]] },
    mole: { wave: 'triangle', base: 950, lp: 3000, vib: [0, 0], sniff: true, call: [[[0.2, 0.08, 1, 1.2], [0.32, 0.1, 1.1, 0.95]],
      [[0.15, 0.14, 1, 1.3]], [[0.25, 0.06, 1.2, 1.2], [0.34, 0.06, 1.2, 1.2]]] },
    // The north's: a gravelly rumble, a bleat, a squeak.
    golem: { wave: 'sawtooth', base: 120, lp: 600, vib: [9, 0.08], rasp: 0.5, call: [[[0, 0.35, 1, 0.85]],
      [[0, 0.18, 0.9, 1.05], [0.24, 0.3, 1, 0.8]], [[0, 0.5, 1.1, 0.75]]] },
    goat: { wave: 'sawtooth', base: 420, lp: 2000, vib: [11, 0.1], vowel: 1100, call: [[[0, 0.42, 1, 0.97]],
      [[0, 0.22, 1.1, 1.05], [0.3, 0.35, 1.05, 0.93]], [[0, 0.55, 0.95, 0.92]]] },
    ermine: { wave: 'triangle', base: 1600, lp: 6000, vib: [0, 0], call: [[[0, 0.05, 1, 1.25], [0.07, 0.05, 1.1, 1.3]],
      [[0, 0.09, 0.9, 1.5]], [[0, 0.04, 1.2, 1.2], [0.06, 0.04, 1.25, 1.25], [0.12, 0.04, 1.3, 1.3], [0.18, 0.07, 1.4, 1.0]]] },
  };
  const fallbackVoice = VOICES.slime;

  function syllable(ctx, out, t, v, len, from, to, peak, harsh) {
    const g = envelope(ctx, out, t, Math.min(0.03, len * 0.3), len * 0.85, peak);
    let node = g;
    if (v.lp) {
      const f = filter(ctx, 'lowpass', v.lp * (harsh ? 1.8 : 1), 0.9);
      f.connect(g);
      node = f;
    }
    if (v.vowel) {  // a vowel: a resonance on top
      const f = filter(ctx, 'peaking', v.vowel, 3);
      f.gain.value = 9;
      f.connect(node);
      node = f;
    }
    const o = osc(ctx, v.wave, v.base * from, t, t + len + 0.1);
    o.frequency.exponentialRampToValueAtTime(v.base * to, t + len);
    if (v.vib[0]) {  // vibrato (a slime wobbles, a sheep bleats)
      const lfo = osc(ctx, 'sine', v.vib[0], t, t + len + 0.1);
      const depth = ctx.createGain();
      depth.gain.value = v.base * from * v.vib[1];
      lfo.connect(depth).connect(o.frequency);
    }
    o.connect(node);
    if (v.rasp || harsh) hiss(ctx, out, t, { f0: v.base * 2.5, q: 2, a: 0.01, d: len, peak: peak * (v.rasp || 0.25) });
    if (v.crackle) for (let k = 0; k < 4; ++k) hiss(ctx, out, t + Math.random() * len, { type: 'highpass', f0: 3000, d: 0.02, peak: peak * v.crackle * 2 });
  }

  // kind: 'call' (variant 0..2), 'attack' (sharper, louder), 'hurt' (falls, quick).
  function cry(ctx, out, t, species, kind = 'call', variant = 0) {
    const v = VOICES[species] || fallbackVoice;
    const shape = v.call[Math.abs(variant) % v.call.length];
    let syllables = shape;
    let pitch = 1;
    let peak = 0.28;
    let harsh = false;
    if (kind === 'attack') {
      syllables = [[0, 0.16, 0.95, 1.45]];
      pitch = 1.08;
      peak = 0.36;
      harsh = true;
    } else if (kind === 'hurt') {
      syllables = [[0, 0.07, 1.5, 1.6], [0.08, 0.2, 1.55, 0.75]];
      pitch = 1.12;
      peak = 0.32;
    }
    let end = 0;
    if (v.bell) {  // Cristi: a crystal rings
      for (const [s, len, from] of syllables) bell(ctx, out, t + s, v.base * from * pitch, peak * 0.7, kind === 'hurt' ? 0.4 : len + 0.5);
      return syllables.reduce((m, [s, len]) => Math.max(m, s + len + 0.5), 0);
    }
    const voice = { ...v, base: v.base * pitch };
    if (v.sniff && kind === 'call') for (let k = 0; k < 2; ++k) hiss(ctx, out, t + k * 0.08, { type: 'highpass', f0: 2500, d: 0.05, peak: 0.12 });
    if (v.breath) hiss(ctx, out, t, { f0: v.base * 2, q: 1.5, a: 0.05, d: 0.6, peak: v.breath });
    for (const [s, len, from, to] of syllables) {
      syllable(ctx, out, t + s, voice, len, from, to, peak, harsh);
      if (v.bubble) tone(ctx, out, t + s + len * 0.6, { f0: v.base * 2.2, f1: v.base * 3.5, d: 0.05, peak: peak * 0.4 });
      end = Math.max(end, s + len);
    }
    return end + 0.1;
  }

  // ---- Effects ----
  const ELEMENT_IMPACT = {
    fire: (ctx, out, t, k) => { hiss(ctx, out, t, { type: 'lowpass', f0: 2500, f1: 400, d: 0.35, peak: 0.35 * k });
      for (let i = 0; i < 5; ++i) hiss(ctx, out, t + Math.random() * 0.3, { type: 'highpass', f0: 2000, d: 0.02, peak: 0.2 * k }); return 0.4; },
    water: (ctx, out, t, k) => { tone(ctx, out, t, { f0: 700, f1: 250, d: 0.12, peak: 0.25 * k });
      hiss(ctx, out, t + 0.02, { type: 'highpass', f0: 1500, d: 0.3, peak: 0.25 * k }); return 0.35; },
    electric: (ctx, out, t, k) => { for (let i = 0; i < 6; ++i) tone(ctx, out, t + i * 0.03, { type: 'square', f0: 400 + Math.random() * 1600, d: 0.03, peak: 0.12 * k });
      hiss(ctx, out, t, { type: 'highpass', f0: 4000, d: 0.2, peak: 0.25 * k }); return 0.25; },
    nature: (ctx, out, t, k) => { tone(ctx, out, t, { f0: 180, f1: 70, d: 0.15, peak: 0.35 * k });
      hiss(ctx, out, t, { f0: 1800, q: 0.7, d: 0.25, peak: 0.2 * k }); return 0.3; },
    air: (ctx, out, t, k) => hiss(ctx, out, t, { f0: 400, f1: 2500, q: 1.5, a: 0.05, d: 0.35, peak: 0.3 * k }),
    shadow: (ctx, out, t, k) => { tone(ctx, out, t, { type: 'sawtooth', f0: 160, f1: 60, d: 0.4, peak: 0.2 * k });
      tone(ctx, out, t, { type: 'sawtooth', f0: 165, f1: 58, d: 0.4, peak: 0.15 * k }); return 0.45; },
    light: (ctx, out, t, k) => bell(ctx, out, t, 1400, 0.2 * k, 0.8),
    arcane: (ctx, out, t, k) => { for (let i = 0; i < 4; ++i) tone(ctx, out, t + i * 0.04, { f0: midi(84 + i * 3), d: 0.3, peak: 0.1 * k }); return 0.5; },
    normal: (ctx, out, t, k) => { tone(ctx, out, t, { f0: 150, f1: 55, d: 0.14, peak: 0.4 * k });
      hiss(ctx, out, t, { type: 'lowpass', f0: 1500, d: 0.08, peak: 0.25 * k }); return 0.2; },
  };

  // Spells: each its little tune.
  function arpeggio(ctx, out, t, notes, step, { type = 'sine', d = 0.35, peak = 0.16 } = {}) {
    notes.forEach((n, i) => tone(ctx, out, t + i * step, { type, f0: midi(n), d, peak }));
    return notes.length * step + d;
  }
  const SPELLS = {
    heal: (ctx, out, t) => { arpeggio(ctx, out, t, [72, 76, 79, 84], 0.07, { type: 'triangle', d: 0.6 });
      return hiss(ctx, out, t, { f0: 3000, f1: 6000, q: 2, a: 0.1, d: 0.5, peak: 0.06 }); },
    shield: (ctx, out, t) => { tone(ctx, out, t, { type: 'triangle', f0: 220, f1: 330, a: 0.05, d: 0.5, peak: 0.2 }); return bell(ctx, out, t + 0.05, 990, 0.12, 0.6); },
    mercy: (ctx, out, t) => arpeggio(ctx, out, t, [67, 71, 74], 0.0, { type: 'sine', d: 1.0, peak: 0.1 }),
    detect: (ctx, out, t) => { for (let i = 0; i < 3; ++i) tone(ctx, out, t + i * 0.32, { f0: 1760, f1: 1700, d: 0.25, peak: 0.14 / (i + 1) }); return 1.0; },
    wayOut: (ctx, out, t) => tone(ctx, out, t, { type: 'triangle', f0: 400, f1: 1200, glide: 'lin', a: 0.05, d: 0.6, peak: 0.12 }),
    swift: (ctx, out, t) => { hiss(ctx, out, t, { f0: 600, f1: 6000, q: 1.5, a: 0.02, d: 0.35, peak: 0.16 }); return arpeggio(ctx, out, t + 0.1, [72, 79, 86], 0.05, { d: 0.4 }); },
    home: (ctx, out, t) => { hiss(ctx, out, t, { f0: 300, f1: 4000, q: 2, a: 0.2, d: 0.6, peak: 0.2 }); return arpeggio(ctx, out, t + 0.3, [79, 84, 88, 91], 0.06, { d: 0.6 }); },
    replay: (ctx, out, t) => arpeggio(ctx, out, t, [76, 79, 76], 0.06),
    guard: (ctx, out, t) => bell(ctx, out, t, 660, 0.14, 0.6),
    boost: (ctx, out, t) => arpeggio(ctx, out, t, [72, 79, 84], 0.05, { type: 'square', d: 0.2, peak: 0.07 }),
    newOrder: (ctx, out, t) => arpeggio(ctx, out, t, [84, 79, 76, 72], 0.04, { d: 0.2 }),
    steady: (ctx, out, t) => tone(ctx, out, t, { type: 'triangle', f0: 523, a: 0.1, d: 0.8, peak: 0.12 }),
    hint: (ctx, out, t) => tone(ctx, out, t, { f0: 2093, d: 0.4, peak: 0.1 }),
  };

  const EFFECTS = {
    swing: (ctx, out, t, e) => hiss(ctx, out, t, { f0: 600, f1: e.variant ? 2600 : 1800, q: 1.2, a: 0.02, d: e.variant ? 0.3 : 0.14, peak: 0.22 }),
    hit: (ctx, out, t, e) => ELEMENT_IMPACT.normal(ctx, out, t, 0.6 + 0.6 * (e.strength ?? 0.5)),
    parry: (ctx, out, t) => { tone(ctx, out, t, { f0: 1250, d: 0.3, peak: 0.15 }); return tone(ctx, out, t, { f0: 1730, d: 0.25, peak: 0.1 }); },
    impact: (ctx, out, t, e) => (ELEMENT_IMPACT[e.element] || ELEMENT_IMPACT.normal)(ctx, out, t, 0.7),
    spell: (ctx, out, t, e) => (SPELLS[e.spell] || SPELLS.hint)(ctx, out, t),
    summon: (ctx, out, t) => { hiss(ctx, out, t, { f0: 800, f1: 5000, q: 3, a: 0.1, d: 0.25, peak: 0.12 }); return tone(ctx, out, t + 0.28, { f0: 900, f1: 1400, d: 0.08, peak: 0.2 }); },
    recall: (ctx, out, t) => { tone(ctx, out, t, { f0: 1400, f1: 600, d: 0.2, peak: 0.15 }); return hiss(ctx, out, t, { f0: 5000, f1: 800, q: 3, d: 0.3, peak: 0.1 }); },
    teleport: (ctx, out, t) => { tone(ctx, out, t, { type: 'sawtooth', f0: 110, f1: 880, a: 0.3, d: 0.5, peak: 0.08 });
      hiss(ctx, out, t, { f0: 300, f1: 6000, q: 4, a: 0.4, d: 0.4, peak: 0.15 }); return arpeggio(ctx, out, t + 0.5, [84, 91, 96], 0.05, { d: 0.5 }); },
    door: (ctx, out, t) => { tone(ctx, out, t, { f0: 110, f1: 70, d: 0.18, peak: 0.3 }); return hiss(ctx, out, t, { type: 'lowpass', f0: 900, d: 0.15, peak: 0.25 }); },
    rescue: (ctx, out, t) => { hiss(ctx, out, t, { f0: 5000, f1: 300, q: 2, a: 0.1, d: 0.8, peak: 0.15 }); return arpeggio(ctx, out, t + 0.6, [72, 67, 64, 60], 0.12, { type: 'triangle', d: 0.6 }); },
    heal: (ctx, out, t) => arpeggio(ctx, out, t, [60, 64, 67, 72, 76], 0.08, { type: 'triangle', d: 1.0, peak: 0.12 }),
    battleStart: (ctx, out, t) => { tone(ctx, out, t, { f0: 90, f1: 45, d: 0.4, peak: 0.45 });
      return arpeggio(ctx, out, t + 0.1, [57, 64], 0.14, { type: 'sawtooth', d: 0.35, peak: 0.08 }); },
    victory: (ctx, out, t) => { arpeggio(ctx, out, t, [72, 76, 79], 0.12, { type: 'square', d: 0.18, peak: 0.07 });
      return arpeggio(ctx, out, t + 0.38, [84, 84], 0.0, { type: 'triangle', d: 0.9, peak: 0.12 }); },
    defeat: (ctx, out, t) => arpeggio(ctx, out, t, [67, 63, 60, 55], 0.22, { type: 'triangle', d: 0.6, peak: 0.12 }),
    fled: (ctx, out, t) => { for (let i = 0; i < 4; ++i) hiss(ctx, out, t + i * 0.09, { type: 'lowpass', f0: 600, d: 0.05, peak: 0.2 }); return tone(ctx, out, t, { f0: 800, f1: 300, d: 0.4, peak: 0.1 }); },
    runeRight: (ctx, out, t, e) => bell(ctx, out, t, midi([72, 74, 76, 79, 81, 84, 86, 88][(e.variant ?? 0) % 8]), 0.16, 0.7),
    runeWrong: (ctx, out, t) => { tone(ctx, out, t, { type: 'sawtooth', f0: 180, d: 0.25, peak: 0.12 }); return tone(ctx, out, t, { type: 'sawtooth', f0: 191, d: 0.25, peak: 0.12 }); },
    caught: (ctx, out, t) => { arpeggio(ctx, out, t, [72, 76, 79, 84, 88], 0.06, { d: 0.5 }); return bell(ctx, out, t + 0.35, midi(96), 0.15, 1.4); },
    escaped: (ctx, out, t) => { hiss(ctx, out, t, { type: 'highpass', f0: 2000, d: 0.12, peak: 0.3 }); return tone(ctx, out, t + 0.05, { type: 'triangle', f0: 600, f1: 150, d: 0.4, peak: 0.15 }); },
    questAccepted: (ctx, out, t) => arpeggio(ctx, out, t, [76, 83], 0.12, { type: 'triangle', d: 0.6, peak: 0.13 }),
    questStep: (ctx, out, t) => bell(ctx, out, t, midi(88), 0.12, 0.6),
    questDone: (ctx, out, t) => { arpeggio(ctx, out, t, [72, 76, 79, 84], 0.1, { type: 'triangle', d: 0.5, peak: 0.13 }); return bell(ctx, out, t + 0.42, midi(84), 0.12, 1.4); },
    item: (ctx, out, t, e) => (e.variant === 0  // coins
      ? (tone(ctx, out, t, { type: 'square', f0: 1318, d: 0.06, peak: 0.06 }), tone(ctx, out, t + 0.07, { type: 'square', f0: 1760, d: 0.25, peak: 0.06 }))
      : tone(ctx, out, t, { f0: 500, f1: 1000, d: 0.12, peak: 0.18 })),
    levelUp: (ctx, out, t) => { arpeggio(ctx, out, t, [60, 64, 67, 72, 76, 79, 84], 0.05, { type: 'triangle', d: 0.5, peak: 0.1 });
      return hiss(ctx, out, t, { f0: 4000, f1: 9000, q: 2, a: 0.2, d: 0.6, peak: 0.05 }); },
    thunder: (ctx, out, t, e) => { const k = e.strength ?? 0.5;
      if (k > 0.7) hiss(ctx, out, t, { type: 'highpass', f0: 1500, d: 0.15, peak: 0.4 * k });
      return hiss(ctx, out, t, { type: 'lowpass', f0: 300 + 500 * k, f1: 60, a: 0.05, d: 2.5 + 2 * (1 - k), peak: 0.6 * k }); },
  };

  // ---- Background sounds (loops: start(), set(level), stop()) ----
  // Each returns { set(amount 0..1, at), stop(at) }; amount 0 is silent.
  function noiseLoop(ctx, out, chain, level = 1) {
    const src = noiseSource(ctx);
    const g = ctx.createGain();
    g.gain.value = 0;
    let node = src;
    for (const n of chain) {
      node.connect(n);
      node = n;
    }
    node.connect(g).connect(out);
    src.start();
    return {
      set(amount, at) { g.gain.setTargetAtTime(amount * level, at, 0.6); },
      stop(at) { g.gain.setTargetAtTime(0, at, 0.3); src.stop(at + 2); },
    };
  }
  // An LFO moving a parameter around its value.
  function wobble(ctx, param, rate, depth) {
    const lfo = ctx.createOscillator();
    lfo.frequency.value = rate;
    const g = ctx.createGain();
    g.gain.value = depth;
    lfo.connect(g).connect(param);
    lfo.start();
    return lfo;
  }
  const LOOPS = {
    wind: (ctx, out) => { const f = filter(ctx, 'bandpass', 500, 0.7); wobble(ctx, f.frequency, 0.13, 260); return noiseLoop(ctx, out, [f], 0.5); },
    rain: (ctx, out) => noiseLoop(ctx, out, [filter(ctx, 'highpass', 900), filter(ctx, 'lowpass', 7000)], 0.25),
    snow: (ctx, out) => noiseLoop(ctx, out, [filter(ctx, 'lowpass', 500)], 0.12),
    storm: (ctx, out) => noiseLoop(ctx, out, [filter(ctx, 'lowpass', 140)], 0.6),
    cave: (ctx, out) => { const f = filter(ctx, 'lowpass', 120); return noiseLoop(ctx, out, [f], 0.25); },
    // Placed sources (through a panner).
    waterfall: (ctx, out) => noiseLoop(ctx, out, [filter(ctx, 'lowpass', 2200), filter(ctx, 'highshelf', 3000)], 0.9),
    river: (ctx, out) => { const f = filter(ctx, 'bandpass', 1100, 0.6); wobble(ctx, f.frequency, 0.4, 300); return noiseLoop(ctx, out, [f], 0.35); },
    waterWheel: (ctx, out) => { const f = filter(ctx, 'bandpass', 700, 0.8); wobble(ctx, f.frequency, 0.6, 250); return noiseLoop(ctx, out, [f], 0.4); },
    windmill: (ctx, out) => { const f = filter(ctx, 'bandpass', 300, 2); wobble(ctx, f.frequency, 0.25, 120); return noiseLoop(ctx, out, [f], 0.3); },
    fire: (ctx, out) => { const f = filter(ctx, 'lowpass', 900); wobble(ctx, f.frequency, 3, 300); return noiseLoop(ctx, out, [f], 0.25); },
  };
  // Little sounds dropped now and then into the background: a bird, a
  // cricket, a drop of water in a cave, a crackle of the fire.
  const DROPS = {
    bird: (ctx, out, t) => { const f = 2200 + Math.random() * 1800; const n = 2 + Math.floor(Math.random() * 3);
      for (let i = 0; i < n; ++i) tone(ctx, out, t + i * 0.11, { f0: f * (1 + 0.15 * Math.random()), f1: f * 1.3, d: 0.07, peak: 0.05 }); return n * 0.11; },
    cricket: (ctx, out, t) => { for (let i = 0; i < 6; ++i) tone(ctx, out, t + i * 0.045, { f0: 4600, d: 0.025, peak: 0.025 }); return 0.3; },
    drip: (ctx, out, t) => tone(ctx, out, t, { f0: 1200 + Math.random() * 900, f1: 2600, d: 0.06, peak: 0.08 }),
    crackle: (ctx, out, t) => hiss(ctx, out, t, { type: 'highpass', f0: 2500, d: 0.015, peak: 0.25 }),
    creak: (ctx, out, t) => tone(ctx, out, t, { type: 'sawtooth', f0: 140, f1: 110, a: 0.05, d: 0.4, peak: 0.05 }),
  };

  // ---- Music: a theme for each season ----
  // scale: semitones of the mode; root: MIDI; chords: per bar, degrees of the
  // scale; melody: per bar, eight eighths (a degree, + octave, or null).
  const THEMES = {
    spring: { bpm: 96, root: 62, scale: [0, 2, 4, 6, 7, 9, 11], lead: 'flute', pad: 'soft', pluck: 'pluck',
      chords: [[0, 2, 4], [4, 6, 8], [5, 7, 9], [3, 5, 7]],
      melody: [[4, null, 5, 4, 2, null, 0, null], [1, null, 2, 4, 4, null, null, null],
               [5, 4, 5, 7, 6, null, 4, null], [3, null, 2, 1, 2, null, null, null]] },
    summer: { bpm: 108, root: 60, scale: [0, 2, 4, 5, 7, 9, 11], lead: 'marimba', pad: 'warm', pluck: 'marimba',
      chords: [[0, 2, 4], [3, 5, 7], [4, 6, 8], [0, 2, 4]],
      melody: [[0, 2, 4, null, 7, null, 4, 2], [3, null, 5, 7, 5, null, 3, null],
               [4, null, 6, 8, 7, 6, 4, null], [2, null, 1, null, 0, null, null, null]] },
    autumn: { bpm: 84, root: 57, scale: [0, 2, 3, 5, 7, 9, 10], lead: 'cello', pad: 'dark', pluck: 'harp',
      chords: [[0, 2, 4], [6, 8, 10], [5, 7, 9], [6, 8, 10]],
      melody: [[4, null, 3, 2, 0, null, null, 2], [1, null, 2, 3, 1, null, null, null],
               [2, null, 3, 4, 5, null, 4, 2], [3, null, 1, null, null, null, null, null]] },
    winter: { bpm: 70, root: 64, scale: [0, 2, 3, 5, 7, 8, 10], lead: 'celesta', pad: 'air', pluck: 'celesta',
      chords: [[0, 2, 4], [5, 7, 9], [2, 4, 6], [6, 8, 10]],
      melody: [[7, null, null, 4, null, null, 2, null], [5, null, null, null, 4, null, null, null],
               [2, null, null, 4, 6, null, null, null], [4, null, null, null, null, null, null, null]] },
  };
  // The instruments (vel: 0..1).
  const INSTRUMENTS = {
    flute: (ctx, out, t, f, dur, vel) => { const g = envelope(ctx, out, t, 0.06, dur, 0.09 * vel);
      const o = osc(ctx, 'sine', f, t, t + dur + 0.2); wobble(ctx, o.frequency, 5, f * 0.006); o.connect(g);
      hiss(ctx, out, t, { f0: f * 2, q: 4, a: 0.03, d: 0.1, peak: 0.01 * vel }); },
    marimba: (ctx, out, t, f, dur, vel) => { tone(ctx, out, t, { f0: f, d: 0.5, peak: 0.12 * vel }); tone(ctx, out, t, { f0: f * 4, d: 0.08, peak: 0.04 * vel }); },
    cello: (ctx, out, t, f, dur, vel) => { const g = envelope(ctx, out, t, 0.12, dur + 0.2, 0.07 * vel); const lp = filter(ctx, 'lowpass', f * 4, 0.7);
      const o = osc(ctx, 'sawtooth', f, t, t + dur + 0.5); wobble(ctx, o.frequency, 5.5, f * 0.005); o.connect(lp).connect(g); },
    celesta: (ctx, out, t, f, dur, vel) => { bell(ctx, out, t, f, 0.08 * vel, 1.6); },
    pluck: (ctx, out, t, f, dur, vel) => { tone(ctx, out, t, { type: 'triangle', f0: f, d: 0.6, peak: 0.07 * vel }); },
    harp: (ctx, out, t, f, dur, vel) => { const g = envelope(ctx, out, t, 0.004, 1.2, 0.06 * vel); const lp = filter(ctx, 'lowpass', f * 3);
      lp.frequency.exponentialRampToValueAtTime(f * 1.2, t + 1); osc(ctx, 'sawtooth', f, t, t + 1.3).connect(lp).connect(g); },
    bass: (ctx, out, t, f, dur, vel) => { const g = envelope(ctx, out, t, 0.02, dur, 0.12 * vel); const lp = filter(ctx, 'lowpass', 500);
      osc(ctx, 'triangle', f, t, t + dur + 0.1).connect(lp).connect(g); },
    drum: (ctx, out, t, f, dur, vel) => { tone(ctx, out, t, { f0: 120, f1: 45, d: 0.18, peak: 0.25 * vel }); },
    snare: (ctx, out, t, f, dur, vel) => { hiss(ctx, out, t, { type: 'highpass', f0: 1800, d: 0.12, peak: 0.1 * vel }); },
  };
  const PADS = { soft: ['triangle', 2200], warm: ['sawtooth', 1200], dark: ['sawtooth', 700], air: ['sine', 4000] };
  function pad(ctx, out, t, freqs, dur, kind, vel) {
    const [type, cutoff] = PADS[kind] || PADS.soft;
    const g = envelope(ctx, out, t, dur * 0.35, dur * 0.75, 0.035 * vel);
    const lp = filter(ctx, 'lowpass', cutoff, 0.5);
    lp.connect(g);
    for (const f of freqs) {
      osc(ctx, type, f, t, t + dur * 1.15).connect(lp);
      osc(ctx, type, f * 1.004, t, t + dur * 1.15).connect(lp);  // a little chorus
    }
  }
  // The notes of bar `bar` (from 0, looping) of a season's theme, its
  // `mood`: { night, battle } (night: slower, fewer notes; battle: drums,
  // quicker). Calls play(time, fn) for each sound, from `t` on.
  function themeBar(ctx, out, t, season, bar, mood) {
    const th = THEMES[season] || THEMES.summer;
    const beat = 60 / (th.bpm * (mood.battle ? 1.3 : mood.night ? 0.85 : 1));
    const n = th.chords.length;
    const chord = th.chords[bar % n];
    const note = (deg, oct = 0) => midi(th.root + th.scale[((deg % 7) + 7) % 7] + 12 * (Math.floor(deg / 7) + oct));
    const barLen = beat * 4;
    pad(ctx, out, t, chord.map((d) => note(d, -1)), barLen, th.pad, mood.battle ? 0.7 : 1);
    INSTRUMENTS.bass(ctx, out, t, note(chord[0], -2), beat * 1.8, 1);
    INSTRUMENTS.bass(ctx, out, t + beat * 2, note(chord[0] + (bar % 2 ? 4 : 0), -2), beat * 1.8, 0.8);
    // Arpeggios of the chord (not every bar at night).
    if (!mood.night || bar % 2 === 0) {
      for (let i = 0; i < 4; ++i) INSTRUMENTS[th.pluck](ctx, out, t + i * beat, note(chord[i % 3]), beat, 0.6);
    }
    // The tune: every other time through, an octave up and a little different.
    const pass = Math.floor(bar / n);
    const line = th.melody[bar % n];
    line.forEach((deg, i) => {
      if (deg === null) return;
      if (mood.night && i % 2) return;
      const ornament = pass % 2 === 1 && i === 7 ? 1 : 0;
      let len = 1;
      while (i + len < line.length && line[i + len] === null && len < 4) ++len;
      INSTRUMENTS[th.lead](ctx, out, t + i * beat / 2, note(deg + ornament, pass % 2), len * beat / 2, 0.9);
    });
    if (mood.battle) {
      for (let i = 0; i < 4; ++i) {
        INSTRUMENTS.drum(ctx, out, t + i * beat, 0, 0, i % 2 ? 0.6 : 1);
        if (i % 2) INSTRUMENTS.snare(ctx, out, t + i * beat, 0, 0, 1);
      }
    }
    return barLen;
  }

  // Impulse responses for the echo (caves: long; buildings: short).
  function impulse(ctx, seconds, decay) {
    const len = Math.floor(ctx.sampleRate * seconds);
    const b = ctx.createBuffer(2, len, ctx.sampleRate);
    for (let c = 0; c < 2; ++c) {
      const d = b.getChannelData(c);
      for (let i = 0; i < len; ++i) d[i] = (Math.random() * 2 - 1) * (1 - i / len) ** decay;
    }
    return b;
  }

  window.MonstresSoundRecipes = {
    species: Object.keys(VOICES), cry, effects: EFFECTS, loops: LOOPS, drops: DROPS, themeBar, seasons: Object.keys(THEMES), impulse,
  };
})();
