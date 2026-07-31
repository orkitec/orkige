// Orkige web data loader - copied verbatim into a browser export as game.js
// and pulled in by the shell page before the player module.
//
// The export ships its whole payload (engine media, the project, the
// orkige_project.txt marker) as ONE game pak - the engine's own zip, the same
// archive RenderSystem::mountPak reads. This script fetches it and hands the
// bytes to the module's filesystem as /game.pak; the player then writes out
// the small tree that is read through fopen (marker, manifest, scenes,
// scripts, config, shader/font media) and MOUNTS the bulk game media straight
// out of the pak, exactly as the Android player reads an uncompressed APK in
// place.
//
// Only the documented runtime methods are used - FS_createDataFile plus the
// run-dependency pair, which a build with -sFORCE_FILESYSTEM exports precisely
// so a package fetched at runtime can install itself. Nothing here reaches
// into the module's internals.
(function () {
  var PAK_URL = 'game.pak';
  var PAK_PATH = 'game.pak';   // placed at the module filesystem root
  var DEPENDENCY = 'orkige-game-pak';

  var module = window.Module = window.Module || {};
  module.preRun = module.preRun || [];

  var bytes = null;      // the fetched payload, once it has arrived
  var install = null;    // set once the runtime is waiting for it

  function place() {
    // '/' + PAK_PATH: the module's base directory, which is where the player
    // looks for the pak (SDL_GetBasePath() is the filesystem root here)
    module.FS_createDataFile('/', PAK_PATH, bytes, true, false, false);
    module.removeRunDependency(DEPENDENCY);
  }

  // preRun runs before main(): hold the runtime there until the pak is in
  // place, so the player never boots against an empty filesystem
  module.preRun.push(function () {
    module.addRunDependency(DEPENDENCY);
    if (bytes) {
      place();
    } else {
      install = place;
    }
  });

  function failed(reason) {
    // the page owns the visible failure (the shell defines showError); a
    // vanished server otherwise leaves a silent launch-background page
    if (typeof window.showError === 'function') {
      window.showError('could not load the game data (' + PAK_URL + '): ' +
        reason);
    } else if (window.console) {
      console.error('orkige: could not load ' + PAK_URL + ': ' + reason);
    }
  }

  fetch(PAK_URL).then(function (response) {
    if (!response.ok) {
      throw new Error('HTTP ' + response.status);
    }
    return response.arrayBuffer();
  }).then(function (buffer) {
    bytes = new Uint8Array(buffer);
    if (install) {
      install();
    }
  }).catch(function (error) {
    failed(error && error.message ? error.message : String(error));
  });
})();
