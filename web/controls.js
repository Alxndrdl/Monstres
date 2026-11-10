// On-screen and keyboard controls. Talks to the game through window.monstres.input
// (defined in src/platform/web/post.js) once the game is ready.
//
//   joystick (bottom left)   move, relative to the camera
//   jump button (bottom right)
//   drag anywhere else       look around
//   keyboard                 ZQSD / WASD / arrows to move, Space to jump
(() => {
  'use strict';

  const LOOK_SPEED = { mouse: 0.006, pen: 0.006, touch: 0.009 };  // radians per CSS pixel
  const DEAD_ZONE = 0.12;

  let game = null;
  const keys = new Set();
  const joystick = { pointer: null, x: 0, y: 0 };
  const lookPointers = new Map();  // pointerId -> {x, y}

  // ---- Sending the current direction to the game ----
  const pressed = (...codes) => codes.some((c) => keys.has(c));

  function sendMove() {
    if (!game) return;
    let x = joystick.x;
    let y = joystick.y;
    if (joystick.pointer === null) {
      x = (pressed('KeyD', 'ArrowRight') ? 1 : 0) - (pressed('KeyA', 'ArrowLeft') ? 1 : 0);
      y = (pressed('KeyW', 'ArrowUp') ? 1 : 0) - (pressed('KeyS', 'ArrowDown') ? 1 : 0);
      const length = Math.hypot(x, y);
      if (length > 1) {
        x /= length;
        y /= length;
      }
    }
    game.input.move(x, y);
  }

  function onReady() {
    game = window.monstres;
    sendMove();
  }
  if (window.monstres && window.monstres.isReady) onReady();
  else window.addEventListener('monstres-ready', onReady, { once: true });

  // ---- Joystick ----
  const base = document.getElementById('joystick');
  const knob = document.getElementById('knob');

  function updateJoystick(event) {
    const rect = base.getBoundingClientRect();
    const radius = rect.width / 2;
    let dx = event.clientX - (rect.left + radius);
    let dy = event.clientY - (rect.top + radius);
    const distance = Math.hypot(dx, dy);
    if (distance > radius) {
      dx *= radius / distance;
      dy *= radius / distance;
    }
    knob.style.transform = `translate(${dx}px, ${dy}px)`;
    const strength = Math.min(distance / radius, 1);
    joystick.x = strength < DEAD_ZONE ? 0 : dx / radius;
    joystick.y = strength < DEAD_ZONE ? 0 : -dy / radius;  // screen up = forward
    sendMove();
  }

  function releaseJoystick(event) {
    if (event.pointerId !== joystick.pointer) return;
    joystick.pointer = null;
    joystick.x = 0;
    joystick.y = 0;
    knob.style.transform = '';
    base.classList.remove('active');
    sendMove();
  }

  base.addEventListener('pointerdown', (event) => {
    if (joystick.pointer !== null) return;
    event.preventDefault();
    joystick.pointer = event.pointerId;
    base.setPointerCapture(event.pointerId);
    base.classList.add('active');
    updateJoystick(event);
  });
  base.addEventListener('pointermove', (event) => {
    if (event.pointerId === joystick.pointer) updateJoystick(event);
  });
  base.addEventListener('pointerup', releaseJoystick);
  base.addEventListener('pointercancel', releaseJoystick);
  base.addEventListener('lostpointercapture', releaseJoystick);

  // ---- Jump button ----
  const jumpButton = document.getElementById('jump');
  jumpButton.addEventListener('pointerdown', (event) => {
    event.preventDefault();  // no focus, no text selection
    jumpButton.classList.add('pressed');
    if (game) game.input.jump();
  });
  for (const type of ['pointerup', 'pointercancel', 'pointerleave']) {
    jumpButton.addEventListener(type, () => jumpButton.classList.remove('pressed'));
  }
  jumpButton.addEventListener('click', (event) => event.preventDefault());

  // ---- Look around: drag on the 3D view (several fingers are fine) ----
  const canvas = document.getElementById('canvas');
  canvas.addEventListener('pointerdown', (event) => {
    event.preventDefault();
    canvas.setPointerCapture(event.pointerId);
    lookPointers.set(event.pointerId, { x: event.clientX, y: event.clientY });
  });
  canvas.addEventListener('pointermove', (event) => {
    const last = lookPointers.get(event.pointerId);
    if (!last) return;
    const speed = LOOK_SPEED[event.pointerType] || LOOK_SPEED.mouse;
    const dx = event.clientX - last.x;
    const dy = event.clientY - last.y;
    last.x = event.clientX;
    last.y = event.clientY;
    // Drag right: turn right. Drag down: look further down.
    if (game && (dx || dy)) game.input.look(-dx * speed, dy * speed);
  });
  for (const type of ['pointerup', 'pointercancel', 'lostpointercapture']) {
    canvas.addEventListener(type, (event) => lookPointers.delete(event.pointerId));
  }

  // ---- Keyboard ----
  // KeyboardEvent.code is the physical key: KeyW is Z on AZERTY, W on QWERTY.
  const MOVE_KEYS = new Set(['KeyW', 'KeyA', 'KeyS', 'KeyD', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight']);

  window.addEventListener('keydown', (event) => {
    if (event.ctrlKey || event.metaKey || event.altKey) return;
    if (MOVE_KEYS.has(event.code)) {
      event.preventDefault();
      keys.add(event.code);
      sendMove();
    } else if (event.code === 'Space') {
      event.preventDefault();
      if (!event.repeat && game) game.input.jump();
      jumpButton.classList.add('pressed');
    }
  });
  window.addEventListener('keyup', (event) => {
    if (MOVE_KEYS.has(event.code)) {
      keys.delete(event.code);
      sendMove();
    } else if (event.code === 'Space') {
      jumpButton.classList.remove('pressed');
    }
  });
  window.addEventListener('blur', () => {
    keys.clear();
    sendMove();
  });

  // ---- Keyboard hints: on computers, or as soon as a keyboard is used ----
  const hasMouse = window.matchMedia('(hover: hover) and (pointer: fine)');
  let keyboardUsed = false;
  const showHints = () =>
    document.documentElement.classList.toggle('has-keyboard', hasMouse.matches || keyboardUsed);
  showHints();
  hasMouse.addEventListener('change', showHints);
  window.addEventListener('keydown', () => {
    if (!keyboardUsed) {
      keyboardUsed = true;
      showHints();
    }
  });

  // Show the real letters of the keyboard layout (ZQSD on AZERTY, WASD on QWERTY).
  const moveHint = document.getElementById('move-hint');
  function setMoveLetters(letters) {
    moveHint.querySelectorAll('kbd').forEach((kbd, i) => {
      kbd.textContent = letters[i];
    });
  }
  const lang = (navigator.language || '').toLowerCase();
  setMoveLetters(lang.startsWith('fr') || lang.startsWith('be') ? 'ZQSD' : 'WASD');
  if (navigator.keyboard && navigator.keyboard.getLayoutMap) {
    navigator.keyboard.getLayoutMap().then((map) => {
      const letters = ['KeyW', 'KeyA', 'KeyS', 'KeyD'].map((code) => (map.get(code) || '').toUpperCase());
      if (letters.every((l) => l.length === 1)) setMoveLetters(letters);
    }).catch(() => {});
  }
})();
