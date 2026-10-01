// OWNERSHIP=Claude
// Sound effects, following lichess.org's own default ("standard") sound
// theme as closely as its assets allow. Tracing lila's ui/site/src/sound.ts:
// a plain move plays Move, a capture plays Capture, and *every* result
// (win, loss or draw) plays GenericNotify — Victory/Defeat/Draw are all
// symlinked to GenericNotify.mp3 in lila's own standard theme, and
// Check/Checkmate are symlinked to a silent file, i.e. lichess plays no
// separate check/checkmate ding beyond whatever the move/capture sound
// already covered. We reuse GenericNotify for "a clocked game's first move
// just landed" too, since lila has no distinct sound for that at all — see
// issues #30 and #31.
const FILES = {
  move: './vendor/sound/standard/Move.mp3',
  capture: './vendor/sound/standard/Capture.mp3',
  notify: './vendor/sound/standard/GenericNotify.mp3',
};

const MUTE_KEY = 'ascaniusfish-sound-muted';

function isMuted() {
  try { return localStorage.getItem(MUTE_KEY) === '1'; }
  catch { return false; }
}

export function setMuted(muted) {
  try { localStorage.setItem(MUTE_KEY, muted ? '1' : '0'); }
  catch { /* private browsing or blocked storage: mute state just won't persist */ }
}

function play(name) {
  if (isMuted()) return;
  new Audio(FILES[name]).play().catch(() => {}); // browsers refuse autoplay before any user gesture
}

function clockOn(s) {
  return s.mode === 'play' ? s.play.clockOn : s.mode === 'watch' ? s.watch.clockOn : false;
}

// Called from apply() with the previous and next state (as sent by
// state_json()). `prev` is null on the very first state a page load
// receives, which must stay silent rather than announcing a "game start"
// for a game that was already running before this tab opened it.
//
// The "game start" chime marks the clock actually starting, not the game
// being set up: a fresh session, a page reload, loading a FEN/PGN, and
// Analyse's "New game" (no clock at all there) all replace the game — bumping
// `gameSerial` — well before anyone has moved, so that field would fire the
// chime at exactly the moments it must stay silent. What it should follow
// instead is the tree leaving the root (ply 0 -> 1) in a clocked game, which
// is the one moment `Session::clock_ticking()` (gui/session.hpp) starts
// charging either side's time.
export function playForTransition(prev, next) {
  if (!prev) return;
  if (next.tree.cursor !== prev.tree.cursor) {
    const node = next.tree.nodes.find(n => n.id === next.tree.cursor);
    if (node && node.parent >= 0) play(node.san.includes('x') ? 'capture' : 'move');
  }
  if (prev.ply === 0 && next.ply >= 1 && next.gameSerial === prev.gameSerial && clockOn(next))
    play('notify');
  if (prev.outcome.state === 'ongoing' && next.outcome.state !== 'ongoing') play('notify');
}

// The checkbox reads "Sound", so ticked means sound on.
export function initMuteToggle(checkbox) {
  checkbox.checked = !isMuted();
  checkbox.addEventListener('change', event => setMuted(!event.target.checked));
}

// The low-time warning (#58): two short beeps, synthesized rather than a file,
// since lila's standard theme has no low-time sound we ship. It obeys the
// Sound switch above and has a switch of its own, both per-browser.
const LOWTIME_KEY = 'ascaniusfish-lowtime-off';
let audio = null;

function lowTimeOn() {
  try { return localStorage.getItem(LOWTIME_KEY) !== '1'; }
  catch { return true; }
}

export function playLowTime() {
  if (isMuted() || !lowTimeOn()) return;
  try {
    audio = audio || new AudioContext();
    for (const start of [0, 0.18]) {
      const osc = audio.createOscillator();
      const gain = audio.createGain();
      const at = audio.currentTime + start;
      osc.frequency.value = 880;
      gain.gain.setValueAtTime(0.0001, at);
      gain.gain.exponentialRampToValueAtTime(0.25, at + 0.01);
      gain.gain.exponentialRampToValueAtTime(0.0001, at + 0.12);
      osc.connect(gain).connect(audio.destination);
      osc.start(at);
      osc.stop(at + 0.13);
    }
  } catch { /* no Web Audio: the red clock is still there */ }
}

export function initLowTimeToggle(checkbox) {
  checkbox.checked = lowTimeOn();
  checkbox.addEventListener('change', event => {
    try { localStorage.setItem(LOWTIME_KEY, event.target.checked ? '0' : '1'); }
    catch { /* won't persist, as with the mute switch */ }
  });
}
