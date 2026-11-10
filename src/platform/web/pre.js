// Runs before the game starts (emcc --pre-js). Same scope as the generated code.

// URL parameters become main()'s argv:  ?seed=42&new&frozen
Module.arguments = Module.arguments || [];
{
  const params = new URLSearchParams(location.search);
  if (params.has('seed')) Module.arguments.push('--seed', params.get('seed'));
  if (params.has('new')) Module.arguments.push('--new');
  if (params.has('frozen')) Module.arguments.push('--frozen');
}

// Mount /save on IndexedDB and wait until it is loaded before main() runs.
Module.preRun = Module.preRun || [];
Module.preRun.push(() => {
  FS.mkdir('/save');
  FS.mount(IDBFS, {}, '/save');
  addRunDependency('monstres-load-save');
  FS.syncfs(true, (err) => {
    if (err) console.error('Lecture de la sauvegarde impossible', err);
    removeRunDependency('monstres-load-save');
  });
});

// Ask the browser not to evict our data when space runs low.
if (navigator.storage && navigator.storage.persist) {
  navigator.storage.persist().catch(() => {});
}
