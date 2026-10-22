import {createPiScene} from './model.js';

const forge = createPiScene(document.getElementById('sprite'));
window.piForge = forge;
window.renderPi = pose => {
  forge.render(pose);
  return document.getElementById('sprite').toDataURL('image/png');
};
window.piReady = true;
forge.render({track: 'right', step: 0, blink: 0});
