// The saves (settings menu, top right): three slots, each one a game. For
// each: the magician's name and level, the file's size, when it was saved;
// play it, export it to a file, remove it. In an empty slot: a new game (the
// character creation, create.js) or a file brought in. Time stands still while the window is open. The rules are in
// the game (src/platform/web/main.cpp): the save played is never removed
// nor written over.
(() => {
  'use strict';

  const { $, el, whenReady } = window.MonstresUtil;
  let game = null;
  const dialog = $('saves');
  const list = $('saves-list');
  const note = $('saves-note');
  const fileInput = $('import-file');
  let slotFor = -1;     // the empty slot a file goes to
  let confirming = -1;  // the slot whose removal is being confirmed

  const sizeText = (bytes) => (bytes >= 1024 * 1024
    ? `${(bytes / (1024 * 1024)).toLocaleString('fr-FR', { maximumFractionDigits: 1 })} Mo`
    : `${Math.max(1, Math.round(bytes / 1024))} Ko`);
  const dateText = (ms) => new Date(ms).toLocaleString('fr-FR', { dateStyle: 'short', timeStyle: 'short' });
  const say = (text) => { note.textContent = text; };
  const button = (label, className, action, disabled = false, title = '') => {
    const b = el('button', { type: 'button', className: `button ${className}`, textContent: label, disabled, title });
    b.addEventListener('click', action);
    return b;
  };

  const isOpen = () => !dialog.hidden;

  // Another world is played: the window closes on it.
  function played(ok) {
    if (ok) close();
    else say('Cette sauvegarde ne peut pas être chargée.');
  }

  function card(s) {
    const title = el('h3', { textContent: `Emplacement ${s.slot + 1}` });
    if (s.empty) {
      return el('section', { className: 'save-slot empty' }, [
        title,
        el('p', { className: 'save-name', textContent: 'Vide' }),
        el('div', { className: 'save-actions' }, [
          button('Nouvelle partie', 'primary save-new', () => {
            close();
            window.monstresCreate.open(s.slot);  // the character creation
          }),
          button('Importer', 'save-import', () => {
            slotFor = s.slot;
            fileInput.click();
          }),
        ]),
      ]);
    }
    if (s.current) title.append(el('span', { className: 'badge save-current', textContent: 'en cours' }));
    const facts = s.valid
      ? [`Niveau ${s.level}`, sizeText(s.bytes), dateText(s.time)]
      : ['Illisible', sizeText(s.bytes), dateText(s.time)];
    const mine = 'C’est la partie en cours : joue d’abord une autre sauvegarde.';
    const remove = confirming === s.slot
      ? button('Confirmer la suppression', 'danger save-delete', async () => {
        confirming = -1;
        say(await game.deleteSave(s.slot) ? `Emplacement ${s.slot + 1} supprimé.` : 'Suppression impossible.');
        render();
      })
      : button('Supprimer', 'danger save-delete', () => {
        confirming = s.slot;
        say('La suppression est définitive : exporte la sauvegarde d’abord pour en garder une copie.');
        render();
      }, s.current, s.current ? mine : '');
    return el('section', { className: `save-slot${s.current ? ' current' : ''}` }, [
      title,
      el('p', { className: 'save-name', textContent: s.valid ? (s.name || 'Magicien sans nom') : 'Sauvegarde abîmée' }),
      el('ul', { className: 'save-facts' }, facts.map((t) => el('li', { textContent: t }))),
      el('div', { className: 'save-actions' }, [
        button('Jouer', 'primary save-play', async () => played(await game.playSave(s.slot)), s.current || !s.valid,
          s.current ? 'C’est la partie en cours.' : ''),
        button('Exporter', 'save-export', () => game.exportSave({ slot: s.slot })),
        remove,
      ]),
    ]);
  }

  function render() {
    list.replaceChildren(...game.saves().map(card));
  }

  function open() {
    if (!game || typeof game.saves !== 'function') return;
    game.setPaused(true);  // the game played is saved: what is shown is up to date
    confirming = -1;
    say('');
    dialog.hidden = false;
    render();
  }

  function close() {
    if (!isOpen()) return;
    dialog.hidden = true;
    if (document.activeElement && dialog.contains(document.activeElement)) document.activeElement.blur();
    // The grimoire, if it is open, keeps the time still.
    if (game && !(window.monstresMenu && window.monstresMenu.isOpen())) game.setPaused(false);
  }

  $('saves-open').addEventListener('click', open);
  $('saves-close').addEventListener('click', close);
  dialog.addEventListener('pointerdown', (event) => {
    if (event.target === dialog) close();  // beside the window
  });
  window.addEventListener('keydown', (event) => {
    if (!isOpen()) return;
    event.stopPropagation();  // typing a name does not walk
    if (event.key === 'Escape') close();
  }, true);
  window.addEventListener('keyup', (event) => { if (isOpen()) event.stopPropagation(); }, true);

  fileInput.addEventListener('change', async () => {
    const file = fileInput.files[0];
    fileInput.value = '';
    if (!file || !game) return;
    const ok = await game.importSave(file, slotFor);
    say(ok ? `Sauvegarde importée dans l’emplacement ${slotFor + 1}.` : 'Ce fichier n’est pas une sauvegarde Monstres valide.');
    render();
  });

  whenReady((g) => { game = g; });
  window.monstresSaves = { open, close, isOpen };
})();
