// OWNERSHIP=Claude
// The page. It draws whatever state the server sends and asks the server about
// every move; it decides no chess itself, so there is no position here to get
// out of step with the engine's.
//
// The move list is a tree: the server sends every node with its parent and its
// children and says which one the cursor is on, and the page lays that out —
// the main line inline, side lines indented under the move they replace. Moving
// around it is the same kind of request as a move, so a reload finds the cursor
// exactly where it was.
//
// State arrives two ways: a GET on load for an immediate first paint, and an SSE
// stream that pushes every later change. In Play mode that stream is also how
// the engine's thinking and its move arrive, and in Analyse mode every
// iteration of the running search: the page never waits for a search, it is
// simply sent a new state whenever there is one.
import { Chessground } from './vendor/chessground.min.js';
import { playForTransition, initMuteToggle } from './sound.js';

const sessionId = new URLSearchParams(location.search).get('id') || 'main';
const el = id => document.getElementById(id);

let state = null;              // last state the server sent
let pendingPromotion = null;   // {orig, dest, color, queued?} while the chooser is open

// Moves queued while the engine thinks (#26): played one at a time as its own real
// move lands, so the server is never asked to accept a move mid-search. `movable.color`
// stays the human's colour even while thinking (see render()), so turnColor differs
// from it and chessground routes the drag into its premove path instead of `onUserMove`;
// we take over from there — one premove slot isn't enough for a queue, so each one is
// captured into `queue` and replayed locally instead of left for chessground to hold.
let queue = [];
let queueSending = false;      // true while the front of the queue is in flight
let lastBoardKey = null;       // (fen, queue) last actually drawn, so an unrelated
                                // render (a search depth ticking up) doesn't re-set the
                                // position and animate it away from the queue and back

// ------------------------------------------------------------------- clocks

// Presets a click resolves to base/increment in ms before it ever reaches the
// server — the server only ever sees "clock: baseMs+incMs" or "depth: value",
// the same shape either way it got there.
const CLOCK_PRESETS = [
  { label: '1+0', baseMs: 60000, incMs: 0 },
  { label: '1+1', baseMs: 60000, incMs: 1000 },
  { label: '2+1', baseMs: 120000, incMs: 1000 },
  { label: '3+2', baseMs: 180000, incMs: 2000 },
  { label: '5+0', baseMs: 300000, incMs: 0 },
  { label: '5+5', baseMs: 300000, incMs: 5000 },
  { label: '10+0', baseMs: 600000, incMs: 0 },
  { label: '10+5', baseMs: 600000, incMs: 5000 },
  { label: '15+10', baseMs: 900000, incMs: 10000 },
];
const HYPERBULLET_PRESETS = [
  { label: '1s+0', baseMs: 1000, incMs: 0 },
  { label: '1s+1', baseMs: 1000, incMs: 1000 },
  { label: '10s+0', baseMs: 10000, incMs: 0 },
  { label: '30s+0', baseMs: 30000, incMs: 0 },
];

// Local-only settings-form state: which sub-form is showing, whether Custom's
// fields are open, and whether the picker is forced open over the collapsed
// summary while a game is on. None of this is server state — it resyncs to
// what the server actually applied whenever that changes (renderPlayPanel/
// renderWatchPanel, via lastPlayClockOn/lastWatchClockOn), which is also what
// resets it after a successful Apply.
let playExpanded = false, watchExpanded = false;
let playKindChoice = 'depth', watchKindChoice = 'depth';
let playCustomOpen = false, watchCustomOpen = false;
let lastPlayClockOn = null, lastWatchClockOn = null;

// The last state's live clock, anchored to when this page applied it, so
// tickClocks() can interpolate locally between server pushes without asking
// for one every tick itself ("the server owns the clocks; the page only
// interpolates").
let clockAnchor = null;

function hasMoves(s) {
  const root = s.tree.nodes.find(n => n.id === 0);
  return !!root && root.children.length > 0;
}

// Whether a game is on for the purposes of collapsing the settings picker:
// once a move has been played and the game isn't over, changing a setting
// would restart it, so the picker gives way to a summary instead.
function settingsLocked(s) {
  return hasMoves(s) && s.outcome.state === 'ongoing';
}

function matchPreset(baseMs, incMs) {
  return [...CLOCK_PRESETS, ...HYPERBULLET_PRESETS].find(p => p.baseMs === baseMs && p.incMs === incMs);
}

function formatClockValue(baseMs) {
  return baseMs < 60000 ? `${Math.round(baseMs / 1000)}s` : `${Math.round(baseMs / 60000)}`;
}

// "3+2", or "5+0 / 3+0" when White and Black differ (only reachable via
// Custom — a preset always gives both the same clock).
function clockLabel(white, black) {
  const same = white.baseMs === black.baseMs && white.incMs === black.incMs;
  const preset = same && matchPreset(white.baseMs, white.incMs);
  const fmt = c => `${formatClockValue(c.baseMs)}+${Math.round(c.incMs / 1000)}`;
  if (preset) return preset.label;
  return same ? fmt(white) : `${fmt(white)} / ${fmt(black)}`;
}

function playSettingSummary(play) {
  return play.clockOn ? `Clock: ${clockLabel(play.clockWhite, play.clockBlack)}` : `Fixed depth: ${play.limit.value}`;
}

function watchSettingSummary(w) {
  return w.clockOn ? `Clock: ${clockLabel(w.clockWhite, w.clockBlack)}`
                   : `White: depth ${w.white.value} · Black: depth ${w.black.value}`;
}

// Draws one settings form's preset grid plus its Custom button, rebuilt from
// scratch each render like the move list and the analysis line are — cheap
// for a handful of buttons, and it keeps "which one is active" a draw-time
// question rather than state to keep in sync by hand.
function renderPresets(container, presets, white, black, customOpen, onPick, onCustom) {
  container.replaceChildren();
  const same = white.baseMs === black.baseMs && white.incMs === black.incMs;
  const active = same && matchPreset(white.baseMs, white.incMs);
  for (const preset of presets) {
    const button = document.createElement('button');
    button.type = 'button';
    button.textContent = preset.label;
    button.classList.toggle('active', !customOpen && preset === active);
    button.addEventListener('click', () => onPick(preset));
    container.append(button);
  }
  const custom = document.createElement('button');
  custom.type = 'button';
  custom.textContent = 'Custom';
  custom.classList.toggle('active', customOpen);
  custom.addEventListener('click', onCustom);
  container.append(custom);
}

// Fills a Custom form's four fields (in seconds — the unit the fields use)
// from the clock they'd apply to, leaving one being typed alone.
function fillCustomForm(prefix, white, black) {
  const set = (id, seconds) => { if (document.activeElement !== el(id)) el(id).value = seconds; };
  set(`${prefix}-custom-white-base`, Math.round(white.baseMs / 1000));
  set(`${prefix}-custom-white-inc`, Math.round(white.incMs / 1000));
  set(`${prefix}-custom-black-base`, Math.round(black.baseMs / 1000));
  set(`${prefix}-custom-black-inc`, Math.round(black.incMs / 1000));
}

function applyPlayPreset(preset) {
  clearQueueSilently();
  command('/api/play', { kind: 'clock', baseMs: preset.baseMs, incMs: preset.incMs });
}

// Watch's settings are scoped per side server-side, so a preset (both sides
// alike) is two calls, white then black.
function applyWatchClock(side, baseMs, incMs) {
  return post('/api/watch', { side, kind: 'clock', baseMs, incMs })
    .then(apply)
    .catch(err => { showMessage(err.message); if (state) render(state); });
}

function applyWatchPreset(preset) {
  return applyWatchClock('white', preset.baseMs, preset.incMs)
    .then(() => applyWatchClock('black', preset.baseMs, preset.incMs));
}

// Brings the local clock display up to date with a just-applied state: the
// two remaining times and whether either is ticking, anchored to now so
// tickClocks() can count down from here without another request.
function updateClockAnchor(s) {
  clockAnchor = {
    whiteMs: s.clock.whiteMs,
    blackMs: s.clock.blackMs,
    running: s.clock.running,
    turn: s.turn,
    at: performance.now(),
  };
}

function formatClock(ms) {
  const total = Math.max(0, Math.ceil(ms / 1000));
  const m = Math.floor(total / 60), sec = total % 60;
  return `${m}:${String(sec).padStart(2, '0')}`;
}

// Runs on its own timer rather than from render(): a running clock has to
// keep moving between server states, not just when one arrives.
function tickClocks() {
  if (!state || el('clock-top').hidden || !clockAnchor) return;
  const elapsed = clockAnchor.running ? performance.now() - clockAnchor.at : 0;
  let whiteMs = clockAnchor.whiteMs, blackMs = clockAnchor.blackMs;
  if (clockAnchor.running) {
    if (clockAnchor.turn === 'white') whiteMs = Math.max(0, whiteMs - elapsed);
    else blackMs = Math.max(0, blackMs - elapsed);
  }
  const top = state.orientation === 'white' ? 'black' : 'white';
  drawClockChip(el('clock-top'), top === 'white' ? whiteMs : blackMs);
  drawClockChip(el('clock-bottom'), top === 'white' ? blackMs : whiteMs);
}

function drawClockChip(chip, ms) {
  chip.textContent = formatClock(ms);
  chip.classList.toggle('low', ms > 0 && ms < 10000);
  chip.classList.toggle('out', ms <= 0);
}

setInterval(tickClocks, 100);

const board = Chessground(el('board'), {
  orientation: 'white',
  coordinates: true,
  autoCastle: true,            // move the rook along locally; the server confirms it anyway
  disableContextMenu: true,    // a right-click drops the queue instead (see below)
  highlight: { lastMove: true, check: true },
  animation: { enabled: false, duration: 180 },   // the first state is a jump, not a move
  draggable: { showGhost: true },
  movable: { free: false, color: 'both', showDests: true, events: { after: onUserMove } },
  premovable: { enabled: true, showDests: true, events: { set: onPremoveSet } },
  drawable: { enabled: true },
});

// A right-click always means "forget the queue", never "start drawing" — capture phase
// so this runs before chessground's own listener on the same element.
el('board').addEventListener('pointerdown', event => {
  if (event.button === 2 && queue.length) {
    event.preventDefault();
    event.stopImmediatePropagation();
    clearQueue();
  }
}, true);

// ---------------------------------------------------------------- server calls

async function post(path, body) {
  const res = await fetch(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id: sessionId, ...body }),
  });
  const data = await res.json();
  if (!res.ok) throw new Error(data.error || `request failed (${res.status})`);
  return data;
}

// Runs a command, showing its error instead of letting the board drift.
async function command(path, body) {
  try {
    clearMessage();
    apply(await post(path, body));
  } catch (err) {
    showMessage(err.message);
    if (state) render(state);   // whatever was refused, the board still shows the truth
  }
}

// The same, for a command whose refusal is not news: holding the right arrow at
// the end of a line is how you find the end of a line, not a mistake to report.
async function nudge(path, body) {
  try {
    apply(await post(path, body));
  } catch (err) {
    if (state) render(state);
  }
}

// ---------------------------------------------------------------- user actions

// Nearest square first: the promotion square itself, then stepping away from
// the edge along the file.
const PROMOTION_PIECES = [['q', 'queen'], ['r', 'rook'], ['b', 'bishop'], ['n', 'knight']];
const PROMO_ROLE = Object.fromEntries(PROMOTION_PIECES);

function onUserMove(orig, dest) {
  if (state && state.promotions.includes(orig + dest)) {
    pendingPromotion = { orig, dest, color: state.turn };
    openPromotion();
    return;
  }
  command('/api/move', { uci: orig + dest });
}

// Fires instead of `onUserMove` when the piece dragged is the human's own but it is
// not really their move (see the `premovable` config above) — this is a queue entry,
// not a move to send. chessground would hold it as its own single premove; we take it
// over immediately so a second one can be queued on top of it.
function onPremoveSet(orig, dest) {
  const piece = board.state.pieces.get(orig);
  board.cancelPremove();
  if (!piece) return;
  if (piece.role === 'pawn' && (dest[1] === '1' || dest[1] === '8')) {
    pendingPromotion = { orig, dest, color: piece.color, queued: true };
    openPromotion();
    return;
  }
  enqueueMove(orig, dest, '');
}

// Lays the four pieces over the destination file, in board orientation. A
// promotion square is always a board edge (rank 1 or 8), so which way the
// stack points — regardless of colour or a flipped board — falls out of
// whether that edge is the top or bottom row.
function openPromotion() {
  const { row, col } = squareCoords(pendingPromotion.dest, state.orientation);
  const step = row === 0 ? 1 : -1;
  const box = el('promotion');
  box.replaceChildren();
  PROMOTION_PIECES.forEach(([code, role], i) => {
    const piece = document.createElement('piece');
    piece.className = `${role} ${pendingPromotion.color}`;
    piece.style.top = `${(row + step * i) * 12.5}%`;
    piece.style.left = `${col * 12.5}%`;
    piece.dataset.piece = code;
    box.append(piece);
  });
  box.hidden = false;
}

// Board-relative row/col (0 = top/left) for a square, in the given orientation.
function squareCoords(square, orientation) {
  const file = 'abcdefgh'.indexOf(square[0]);
  const rank = Number(square[1]) - 1;
  return orientation === 'white'
    ? { row: 7 - rank, col: file }
    : { row: rank, col: 7 - file };
}

function finishPromotion(piece) {
  const move = pendingPromotion;
  pendingPromotion = null;
  el('promotion').hidden = true;
  el('promotion').replaceChildren();
  if (!move) return;
  if (!piece) return render(state);   // cancelled: put the pawn back
  if (move.queued) enqueueMove(move.orig, move.dest, piece);
  else command('/api/move', { uci: move.orig + move.dest + piece });
}

// ------------------------------------------------------------------- move queue

// Queues one more move. The queue is replayed onto the board by render()'s
// replayQueue(), not here, so a promotion resolved mid-queue and a move dropped
// straight in end up drawn the same way.
function enqueueMove(orig, dest, promo) {
  queue.push({ orig, dest, promo });
  render(state);
}

function clearQueue() {
  if (!clearQueueSilently()) return;
  render(state);
}

// The same, for callers about to render anyway (a command's own response will).
function clearQueueSilently() {
  if (queue.length === 0) return false;
  queue = [];
  board.cancelPremove();
  return true;
}

// Whether the queue is still worth anything: only in Play mode, only while the game
// goes on. render() calls this on every state so a checkmate, a resignation or a mode
// switch drops the queue as soon as its own new state arrives, with no extra wiring.
function queueValidFor(s) {
  return s.mode === 'play' && s.outcome.state === 'ongoing';
}

// A move once it's actually the human's turn: sent for real, and only then — the
// server must never be asked to accept a move mid-search (#26). Checked against the
// position's own dests first, so an entry that turned out illegal is simply dropped,
// not sent for the server to refuse.
function advanceQueue(s) {
  if (queueSending || queue.length === 0 || !queueValidFor(s)) return;
  if (s.play.thinking || s.turn !== s.play.humanColor) return;
  const mv = queue[0];
  const legal = (s.dests[mv.orig] || []).includes(mv.dest) &&
                (!mv.promo || s.promotions.includes(mv.orig + mv.dest));
  if (!legal) {
    queue = [];
    board.cancelPremove();
    showMessage('Queued move is no longer legal and was dropped.');
    return;
  }
  queueSending = true;
  post('/api/move', { uci: mv.orig + mv.dest + mv.promo })
    .then(after => {
      queue.shift();
      queueSending = false;
      apply(after);
    })
    .catch(err => {
      queue = [];
      queueSending = false;
      showMessage(err.message);
      render(state);
    });
}

function queueKey() {
  return queue.map(mv => mv.orig + mv.dest + mv.promo).join(',');
}

// Draws the queue on top of whatever render() just set the board to: each move is
// actually played locally (chessground's own `move`, plus the rook or the captured
// pawn for a castle or an en-passant queued mid-chain) so "the position after them" is
// what is on screen, and an arrow over each one is what marks it as not really played.
function replayQueue() {
  for (const mv of queue) applyQueuedMoveVisually(mv);
  board.setAutoShapes(queue.map(mv => ({ orig: mv.orig, dest: mv.dest, brush: 'yellow' })));
}

function fileIndex(square) {
  return 'abcdefgh'.indexOf(square[0]);
}

function applyQueuedMoveVisually(mv) {
  const piece = board.state.pieces.get(mv.orig);
  if (!piece) return;
  const wasOccupied = board.state.pieces.has(mv.dest);
  board.move(mv.orig, mv.dest);
  if (mv.promo) {
    board.setPieces(new Map([[mv.dest, { role: PROMO_ROLE[mv.promo], color: piece.color }]]));
  } else if (piece.role === 'king' && Math.abs(fileIndex(mv.orig) - fileIndex(mv.dest)) === 2) {
    const rank = mv.orig[1];
    const kingside = fileIndex(mv.dest) > fileIndex(mv.orig);
    board.move((kingside ? 'h' : 'a') + rank, (kingside ? 'f' : 'd') + rank);
  } else if (piece.role === 'pawn' && mv.orig[0] !== mv.dest[0] && !wasOccupied) {
    board.setPieces(new Map([[mv.dest[0] + mv.orig[1], undefined]]));
  }
}

el('promotion').addEventListener('click', event => {
  const piece = event.target.closest('piece');
  finishPromotion(piece ? piece.dataset.piece : '');
});

el('new').addEventListener('click', () => { clearQueueSilently(); command('/api/reset', {}); });
el('undo').addEventListener('click', () => { clearQueueSilently(); command('/api/undo', {}); });
el('flip').addEventListener('click', () => command('/api/flip', {}));
el('resign').addEventListener('click', () => command('/api/resign', {}));

el('sides').addEventListener('click', event => {
  const button = event.target.closest('button');
  if (button) { clearQueueSilently(); command('/api/play', { side: button.dataset.side }); }
});

el('play-depth-form').addEventListener('submit', event => {
  event.preventDefault();
  clearQueueSilently();
  command('/api/play', { kind: 'depth', value: Number(el('play-depth-value').value) });
});

el('play-kind').addEventListener('click', event => {
  const button = event.target.closest('button');
  if (!button) return;
  playKindChoice = button.dataset.kind;
  render(state);
});

el('play-setting-change').addEventListener('click', () => {
  playExpanded = true;
  render(state);
});

el('play-custom-form').addEventListener('submit', event => {
  event.preventDefault();
  clearQueueSilently();
  command('/api/play', {
    kind: 'clock',
    baseMs: Number(el('play-custom-white-base').value) * 1000,
    incMs: Number(el('play-custom-white-inc').value) * 1000,
    blackBaseMs: Number(el('play-custom-black-base').value) * 1000,
    blackIncMs: Number(el('play-custom-black-inc').value) * 1000,
  });
});

el('fen-form').addEventListener('submit', event => {
  event.preventDefault();
  clearQueueSilently();
  command('/api/fen', { fen: el('fen').value.trim() });
});

el('copy-fen').addEventListener('click', () => {
  el('fen').select();
  navigator.clipboard?.writeText(el('fen').value);
  showMessage('FEN copied.');
});

for (const form of document.querySelectorAll('.watch-side'))
  form.addEventListener('submit', event => {
    event.preventDefault();
    command('/api/watch', {
      side: form.dataset.side,
      kind: 'depth',
      value: Number(form.querySelector('.watch-depth-value').value),
    });
  });

el('watch-kind').addEventListener('click', event => {
  const button = event.target.closest('button');
  if (!button) return;
  watchKindChoice = button.dataset.kind;
  render(state);
});

el('watch-setting-change').addEventListener('click', () => {
  watchExpanded = true;
  render(state);
});

el('watch-custom-form').addEventListener('submit', event => {
  event.preventDefault();
  const whiteBase = Number(el('watch-custom-white-base').value) * 1000;
  const whiteInc = Number(el('watch-custom-white-inc').value) * 1000;
  const blackBase = Number(el('watch-custom-black-base').value) * 1000;
  const blackInc = Number(el('watch-custom-black-inc').value) * 1000;
  applyWatchClock('white', whiteBase, whiteInc).then(() => applyWatchClock('black', blackBase, blackInc));
});

el('watch-run').addEventListener('click', () => {
  if (state) command('/api/watch', { action: state.watch.running ? 'pause' : 'start' });
});
el('watch-step').addEventListener('click', () => command('/api/watch', { action: 'step' }));

el('analysis-toggle').addEventListener('click', () => {
  if (state) command('/api/analyse', { on: !state.analysis.on });
});

el('analysis-line').addEventListener('click', event => {
  const button = event.target.closest('button.pv-move');
  if (button) command('/api/line', { moves: button.dataset.line });
});

el('settings-toggle').addEventListener('click', () => openSettings(el('settings').hidden));

el('set-evalbar').addEventListener('change', event =>
  command('/api/settings', { evalBar: event.target.checked }));
el('set-engine-line').addEventListener('change', event =>
  command('/api/settings', { engineLine: event.target.checked }));
initMuteToggle(el('set-sound'));

// Anywhere else closes it, as a menu does; the gear itself is its own toggle.
document.addEventListener('pointerdown', event => {
  if (el('settings').hidden) return;
  if (event.target.closest('#settings, #settings-toggle')) return;
  openSettings(false);
});

el('modes').addEventListener('click', event => {
  const button = event.target.closest('button');
  if (button) { clearQueueSilently(); command('/api/mode', { mode: button.dataset.mode }); }
});

// Unlike the mode selector's own Analyse button, which always starts blank
// (see render()'s `to-analyse` visibility below), this carries the game that
// just ended over into Analyse mode.
el('to-analyse').addEventListener('click', () => {
  clearQueueSilently();
  command('/api/mode', { mode: 'analyse', keepGame: true });
});

el('nav').addEventListener('click', event => {
  const button = event.target.closest('button');
  if (button) { clearQueueSilently(); nudge('/api/nav', { where: button.dataset.nav }); }
});

el('moves').addEventListener('click', event => {
  const button = event.target.closest('.move');
  if (button) { clearQueueSilently(); command('/api/goto', { node: Number(button.dataset.node) }); }
});

el('promote').addEventListener('click', () => { clearQueueSilently(); command('/api/promote', {}); });
el('delete-move').addEventListener('click', () => { clearQueueSilently(); command('/api/delete', {}); });

el('pgn-form').addEventListener('submit', event => {
  event.preventDefault();
  clearQueueSilently();
  command('/api/pgn', { pgn: el('pgn').value });
});

el('copy-pgn').addEventListener('click', () => {
  el('pgn').select();
  navigator.clipboard?.writeText(el('pgn').value);
  showMessage('PGN copied.');
});

// Which key does what. Every one of these has a button beside the move list, so
// nothing is only reachable from the keyboard.
const NAV_KEYS = {
  ArrowLeft: 'back',
  ArrowRight: 'forward',
  ArrowUp: 'prev',
  ArrowDown: 'next',
  Home: 'start',
  End: 'end',
};

document.addEventListener('keydown', event => {
  if (event.key === 'Escape' && pendingPromotion) return finishPromotion('');
  if (event.key === 'Escape' && !el('settings').hidden) return openSettings(false);
  if (event.key === 'Escape' && queue.length) return clearQueue();
  // A shortcut must never fire into a FEN or a PGN being typed: there Home,
  // End and the arrows are the text field's own.
  if (typingSomewhere() || event.ctrlKey || event.metaKey || event.altKey) return;
  const where = NAV_KEYS[event.key];
  if (!where) return;
  event.preventDefault();
  clearQueueSilently();
  nudge('/api/nav', { where });
});

function typingSomewhere() {
  const at = document.activeElement;
  if (!at) return false;
  return at.isContentEditable || ['INPUT', 'TEXTAREA', 'SELECT'].includes(at.tagName);
}

// --------------------------------------------------------------------- drawing

const MODE_NOTES = {
  analyse: 'Free play: move for both sides, set up any position, step through the game ' +
           'with the arrow keys. A move played from an earlier position starts a side line. ' +
           'Turn the engine on to see its eval and its best line for whatever is on the board.',
  play:    'You against Ascaniusfish. Load a FEN to start from a position of your own.',
  watch:   'Ascaniusfish against itself, one engine process per side. Start plays the game on; ' +
           'Pause takes effect after the move being thought about, and Step plays exactly one. ' +
           'Stepping back into the game pauses it — the engines only ever play from the end of a line.',
};

const OUTCOME_TEXT = {
  white_wins: 'White wins',
  black_wins: 'Black wins',
  draw: 'Draw',
};

// Every state the server hands out is numbered (`seq`), and states reach the
// page from two places at once: the answer to a request and the SSE stream. The
// answer to a move is built before the new search has started, while its first
// iteration can be pushed a millisecond later — inside the turn this page spends
// on `res.json()`. So the older state can be the one that arrives last, and
// applying it would drop a line the page had already been given and leave the
// panel empty until the next iteration, seconds later (#23). Numbers decide it
// instead of arrival order.
let appliedSeq = -1;

function apply(next) {
  if (typeof next.seq === 'number' && next.seq <= appliedSeq) return;
  appliedSeq = typeof next.seq === 'number' ? next.seq : appliedSeq;
  const first = state === null;
  playForTransition(state, next);
  state = next;
  updateClockAnchor(next);
  advanceQueue(next);   // may send the front of the queue for real, before it is drawn
  render(next);
  // Animating from chessground's default start position into the real one on
  // page load looks like a move that never happened; animate from here on.
  if (first) board.set({ animation: { enabled: true } });
}

function render(s) {
  const playing = s.mode === 'play';
  const over = s.outcome.state !== 'ongoing';
  if (!queueValidFor(s)) clearQueueSilently();
  // In Play mode your pieces stay yours even while the engine thinks, so they can
  // still be picked up: turnColor then differs from movable.color and chessground
  // treats the drag as a premove instead of a move (#26), rather than the board
  // freezing. The server is still the only thing that ever plays a move on its own
  // turn — a direct move attempted out of turn is refused there as before. In Watch
  // mode the board is yours whenever neither engine is thinking. Elsewhere both
  // colours are free.
  const movableColor = over ? undefined
                      : playing ? s.play.humanColor
                      : s.mode === 'watch' ? (s.watch.thinking ? undefined : 'both')
                      : 'both';

  // Setting the same fen again would be a no-op for chessground's own diffing, but
  // replaying the queue on top of it is not free, and doing that on every state a
  // running search pushes (only its depth/score changed, not the position) would
  // fight chessground's animation right back to the real squares and out to the
  // queued ones each time. Only touch the board when what it should show changed.
  const boardKey = s.fen + '\u0000' + queueKey();
  if (boardKey !== lastBoardKey) {
    lastBoardKey = boardKey;
    board.set({
      fen: s.fen.split(' ')[0],
      orientation: s.orientation,
      turnColor: s.turn,
      lastMove: s.lastMove || null,
      check: s.check || false,
      movable: { color: movableColor, dests: movableColor ? destsMap(s.dests) : new Map() },
    });
    replayQueue();
  } else {
    board.set({
      orientation: s.orientation,
      movable: { color: movableColor, dests: movableColor ? destsMap(s.dests) : new Map() },
    });
  }

  for (const button of el('modes').children)
    button.classList.toggle('active', button.dataset.mode === s.mode);
  el('mode-note').textContent = MODE_NOTES[s.mode] || '';

  el('status').textContent = statusLine(s);
  el('status').classList.toggle('over', over);
  el('undo').disabled = (!s.tree.canBack && !s.play.resigned) || s.play.thinking;
  el('resign').hidden = !playing;
  el('resign').disabled = over;
  el('to-analyse').hidden = !over || s.mode === 'analyse';

  renderClockVisibility(s);
  renderPlayPanel(s);
  renderWatchPanel(s);
  renderEngine(s);
  renderSettings(s);

  // Leave a FEN or a PGN the user is in the middle of typing alone.
  if (document.activeElement !== el('fen')) el('fen').value = s.fen;
  if (document.activeElement !== el('pgn')) el('pgn').value = s.pgn;
  el('download-pgn').href = `/api/pgn?id=${encodeURIComponent(sessionId)}`;

  renderMoves(s);
  if (pendingPromotion) openPromotion();
}

// -------------------------------------------------------------------- clock

// Two chips above and below the board, flipping with orientation like a real
// chess site's; hidden entirely outside a clocked Play or Watch game. Only
// visibility is decided here — tickClocks() draws the numbers on its own
// timer — but a visibility change moves how much height the board may have,
// so it also re-fits it, the same as a resize.
function renderClockVisibility(s) {
  const on = (s.mode === 'play' && s.play.clockOn) || (s.mode === 'watch' && s.watch.clockOn);
  const changed = el('clock-top').hidden === on;
  el('clock-top').hidden = el('clock-bottom').hidden = !on;
  if (changed) fitBoard();
  if (on) tickClocks();
}

// ------------------------------------------------------------------ play panel

function renderPlayPanel(s) {
  const play = s.play;
  el('play-panel').hidden = s.mode !== 'play';
  if (s.mode !== 'play') return;

  for (const button of el('sides').children)
    button.classList.toggle('active', button.dataset.side === play.side);

  // The picker resyncs to what actually got applied whenever that changes —
  // including resetting back out of "Change settings" and out of Custom —
  // and otherwise leaves whatever the user is mid-editing alone.
  if (play.clockOn !== lastPlayClockOn) {
    lastPlayClockOn = play.clockOn;
    playKindChoice = play.clockOn ? 'clock' : 'depth';
    playExpanded = false;
    playCustomOpen = false;
  }

  const collapsed = settingsLocked(s) && !playExpanded;
  el('play-setting-summary').hidden = !collapsed;
  el('play-setting-form').hidden = collapsed;
  if (collapsed) el('play-setting-text').textContent = playSettingSummary(play);

  for (const button of el('play-kind').children)
    button.classList.toggle('active', button.dataset.kind === playKindChoice);
  el('play-depth-form').hidden = playKindChoice !== 'depth';
  el('play-clock-form').hidden = playKindChoice !== 'clock';

  // Don't fight a value being typed.
  if (document.activeElement !== el('play-depth-value')) el('play-depth-value').value = play.limit.value;

  renderPresets(el('play-presets'), CLOCK_PRESETS, play.clockWhite, play.clockBlack, playCustomOpen,
    preset => applyPlayPreset(preset),
    () => { playCustomOpen = !playCustomOpen; render(state); });
  el('play-custom-form').hidden = !playCustomOpen;
  fillCustomForm('play', play.clockWhite, play.clockBlack);

  el('engine').textContent = engineLine(s);
  el('engine').classList.toggle('bad', !!play.error);
  el('engine').classList.toggle('thinking', play.thinking && !play.error);
}

function engineLine(s) {
  const play = s.play;
  if (play.error) return `Engine: ${play.error}`;
  const colour = play.engineColor === 'white' ? 'White' : 'Black';
  if (play.thinking) {
    const search = play.search;
    if (!search) return `Thinking as ${colour}\u2026`;
    const score = scoreText(search.score);
    return `Thinking as ${colour}\u2026 depth ${search.depth}` + (score ? `, ${score}` : '');
  }
  if (s.outcome.state !== 'ongoing') return `Playing ${colour} \u2014 game over`;
  return `Playing ${colour} \u2014 your move`;
}

// A UCI score object as text. Every score the page shows is white's view, so
// "+1.20" means white is better whoever moved, and "#-3" is black mating in 3.
function scoreText(score) {
  if (!score) return '';
  if (score.kind === 'mate') return '#' + score.value;
  return (score.value >= 0 ? '+' : '') + (score.value / 100).toFixed(2);
}

// ----------------------------------------------------------------- watch panel

function renderWatchPanel(s) {
  const w = s.watch;
  el('watch-panel').hidden = s.mode !== 'watch';
  if (s.mode !== 'watch') return;

  if (w.clockOn !== lastWatchClockOn) {
    lastWatchClockOn = w.clockOn;
    watchKindChoice = w.clockOn ? 'clock' : 'depth';
    watchExpanded = false;
    watchCustomOpen = false;
  }

  const collapsed = settingsLocked(s) && !watchExpanded;
  el('watch-setting-summary').hidden = !collapsed;
  el('watch-setting-form').hidden = collapsed;
  if (collapsed) el('watch-setting-text').textContent = watchSettingSummary(w);

  for (const button of el('watch-kind').children)
    button.classList.toggle('active', button.dataset.kind === watchKindChoice);
  el('watch-depth-form').hidden = watchKindChoice !== 'depth';
  el('watch-clock-form').hidden = watchKindChoice !== 'clock';

  for (const form of document.querySelectorAll('.watch-side')) {
    const limit = w[form.dataset.side];
    const value = form.querySelector('.watch-depth-value');
    // Don't fight a value being typed.
    if (document.activeElement !== value) value.value = limit.value;
  }

  renderPresets(el('watch-presets'), [...CLOCK_PRESETS, ...HYPERBULLET_PRESETS], w.clockWhite, w.clockBlack,
    watchCustomOpen,
    preset => applyWatchPreset(preset),
    () => { watchCustomOpen = !watchCustomOpen; render(state); });
  el('watch-custom-form').hidden = !watchCustomOpen;
  fillCustomForm('watch', w.clockWhite, w.clockBlack);

  const over = s.outcome.state !== 'ongoing';
  // Start and Step only make sense at the end of a line, which is the only
  // place the engines play from; the server refuses them anywhere else.
  el('watch-run').textContent = w.running ? 'Pause' : 'Start';
  el('watch-run').disabled = !w.running && (over || !w.atTip);
  el('watch-step').disabled = w.running || w.thinking || over || !w.atTip;

  el('watch-state').textContent = watchLine(s);
  el('watch-state').classList.toggle('bad', !!w.error);
  el('watch-state').classList.toggle('thinking', w.thinking && !w.error);
}

function watchLine(s) {
  const w = s.watch;
  if (w.error) return `Engine: ${w.error}`;
  if (s.outcome.state !== 'ongoing') return 'The game is over.';
  const mover = w.mover === 'white' ? 'White' : 'Black';
  if (w.thinking) {
    const search = w.search;
    if (!search) return `${mover} is thinking\u2026`;
    const score = scoreText(search.score);
    return `${mover} is thinking\u2026 depth ${search.depth}` + (score ? `, ${score}` : '');
  }
  if (!w.atTip) return 'Paused \u2014 go to the end of the line to play on.';
  if (w.running || w.stepping) return `Starting ${mover}\u2026`;
  return `Paused \u2014 ${mover} to move.`;
}

// --------------------------------------------------------------------- engine

// The eval gauge and the engine-line box, in every mode (#25). Both draw from
// `eval`, the one score and line the server says belongs to the position on the
// board — while an engine is thinking that is its search, and after it has moved
// it is what the move you are looking at was played on. `source` is 'none' for a
// position no search has ever been at, and then the bar and the box say nothing
// at all: an equal bar would be a claim, and a stale one a lie.
function renderEngine(s) {
  const analysing = s.mode === 'analyse';
  const a = s.analysis;
  const ev = s.eval;
  const known = ev.source !== 'none';
  const showLine = s.settings.engineLine;
  const panel = el('analysis-panel');

  const bar = el('evalbar');
  bar.hidden = !s.settings.evalBar;
  bar.classList.toggle('flipped', s.orientation === 'black');
  bar.classList.toggle('idle', !ev.score);
  bar.querySelector('.evalbar-fill').style.height =
    `${(ev.score ? whiteShare(ev.score) : 0.5) * 100}%`;

  // Analyse mode's panel also holds the engine's on/off switch, so it stays
  // whatever the gear says. In Play and Watch the panel is only the line box, so
  // with that switch off there is nothing for it to hold and the two modes look
  // exactly as they did before this existed.
  panel.hidden = !analysing && !showLine;
  if (panel.hidden) return;

  panel.classList.toggle('running', (a.running || s.play.thinking || s.watch.thinking) && !a.error);
  panel.classList.toggle('has-line', showLine && (known || a.running));
  el('analysis-toggle').hidden = !analysing;
  el('analysis-toggle').textContent = a.on ? 'Turn engine off' : 'Turn engine on';
  el('analysis-score').textContent = s.settings.evalBar && known ? scoreText(ev.score) : '';

  // With the line switched off the stats go too, and so does the height they
  // hold open: an Analyse panel that is only its on/off button should look like
  // one, not like a box with something missing from it.
  el('analysis-stats').hidden = !showLine;
  el('analysis-stats').textContent = showLine ? engineStats(s) : '';
  el('analysis-stats').classList.toggle('bad', showLine && analysing && !!a.error);

  renderLine(s, showLine ? ev : null, analysing);
}

function engineStats(s) {
  const a = s.analysis;
  const ev = s.eval;
  // Every mode shows the engine's error in its own panel; here it belongs to the
  // analysis, which has no other line to say it in.
  if (a.error && s.mode === 'analyse') return `Engine: ${a.error}`;
  if (ev.source === 'none') {
    if (s.outcome.state !== 'ongoing') return 'The game is over — nothing to search.';
    if (s.mode !== 'analyse') return 'No search of this position yet.';
    return a.on ? 'Starting the search\u2026' : 'The engine is off.';
  }
  // Where the number came from: the search running now, one kept from an earlier
  // visit to this position (#24), or the search that played the move the cursor
  // is on. A kept result deeper than the search running behind it says how far
  // that one has got, and the marker goes when it catches up.
  const behind = ev.source === 'stored' && a.running && ev.liveDepth < ev.depth;
  const depth = ev.source === 'stored'
    ? `depth ${ev.depth} saved` + (behind ? `, search at ${ev.liveDepth}` : '')
    : ev.source === 'move' ? `depth ${ev.depth} for this move`
    : `depth ${ev.depth}`;
  const nps = ev.nps ? `${Math.round(ev.nps / 1000).toLocaleString()} knps` : '';
  return [depth, `${ev.nodes.toLocaleString()} nodes`, nps]
    .filter(Boolean).join(' \u00b7 ');
}

// Where the bar sits, as white's share of it. The centipawn score goes through
// a sigmoid: this engine's king-safety terms reach the thousands, so a linear
// scale would sit pinned at one end for most of a game. Both ends stop short
// of full, so a bar that does run out really does mean mate.
function whiteShare(score) {
  if (score.kind === 'mate') return score.value >= 0 ? 0.97 : 0.03;
  return Math.min(0.97, Math.max(0.03, 1 / (1 + Math.exp(-score.value / 400))));
}

// The best line, numbered from the position on the board. In Analyse mode each
// move is a button: clicking it plays the line up to and including that move. In
// Play and Watch it is a line to read, not one to walk into — the engine is in
// the middle of a game there, and clicking a move would open a side line in it.
function renderLine(s, ev, clickable) {
  const box = el('analysis-line');
  box.replaceChildren();
  if (!ev) return;
  let fullmove = s.fullmove;
  let white = s.turn === 'white';
  ev.line.forEach((move, ply) => {
    if (white || ply === 0) {
      const number = document.createElement('span');
      number.className = 'move-number';
      number.textContent = white ? `${fullmove}.` : `${fullmove}\u2026`;
      box.append(number, ' ');
    }
    const chip = document.createElement(clickable ? 'button' : 'span');
    if (clickable) {
      chip.type = 'button';
      chip.dataset.line = ev.line.slice(0, ply + 1).map(m => m.uci).join(' ');
    }
    chip.className = 'pv-move';
    chip.textContent = move.san;
    chip.title = move.uci;
    box.append(chip, ' ');
    if (!white) fullmove++;
    white = !white;
  });
}

// ------------------------------------------------------------------- settings

// The gear's switches. The server holds them, so this only draws them; a second
// tab on the same game sees a switch flipped here as it sees a move.
function renderSettings(s) {
  el('set-evalbar').checked = s.settings.evalBar;
  el('set-engine-line').checked = s.settings.engineLine;
}

function openSettings(open) {
  el('settings').hidden = !open;
  el('settings-toggle').setAttribute('aria-expanded', open ? 'true' : 'false');
  el('settings-toggle').classList.toggle('active', open);
}

function destsMap(dests) {
  return new Map(Object.entries(dests));
}

function statusLine(s) {
  if (s.outcome.state !== 'ongoing') {
    const you = s.mode === 'play' && s.outcome.state !== 'draw'
              ? (s.outcome.state === s.play.humanColor + '_wins' ? ' — you win' : ' — you lose')
              : '';
    return `${OUTCOME_TEXT[s.outcome.state]} — ${s.outcome.reason}${you}`;
  }
  const side = s.turn === 'white' ? 'White' : 'Black';
  const check = s.check ? ', in check' : '';
  return `${side} to move (move ${s.fullmove}${check}) — ${s.legalMoves.length} legal moves`;
}

// ----------------------------------------------------------------- move tree

function renderMoves(s) {
  const tree = s.tree;
  const busy = s.play.thinking;
  for (const button of el('nav').children) {
    const where = button.dataset.nav;
    const can = { back: tree.canBack, start: tree.canBack, forward: tree.canForward,
                  end: tree.canForward, prev: tree.canPrev, next: tree.canNext }[where];
    button.disabled = busy || !can;
  }
  el('promote').disabled = busy || !tree.canPromote;
  el('delete-move').disabled = busy || !tree.canBack;

  const box = el('moves');
  box.replaceChildren();
  const byId = new Map(tree.nodes.map(node => [node.id, node]));
  if (!byId.get(0)?.children.length) {
    const empty = document.createElement('p');
    empty.className = 'note';
    empty.textContent = 'No moves yet.';
    box.append(empty);
    return;
  }
  // Numbering the first move even when it is black's is what a start position
  // with black to move needs: "12\u2026 Nc6" rather than a bare "Nc6".
  drawAfter(byId, 0, box, newLine(box), true, tree.cursor);
  keepCurrentInView(box);
}

// Keep the current move visible inside the move list. `scrollIntoView` would do
// this too, but it scrolls every scrollable ancestor it has, the document among
// them: in a long game every move played scrolled the board off the screen.
// Nothing but this box's own scrollTop moves here.
function keepCurrentInView(box) {
  const chip = box.querySelector('.move.current');
  if (!chip) return;
  const list = box.getBoundingClientRect();
  const move = chip.getBoundingClientRect();
  if (move.top < list.top) box.scrollTop -= list.top - move.top;
  else if (move.bottom > list.bottom) box.scrollTop += move.bottom - list.bottom;
}

function newLine(container) {
  const line = document.createElement('div');
  line.className = 'move-line';
  container.append(line);
  return line;
}

// Draws everything after `id` into `container`, starting in `line`. Each move
// after the first of a branch point gets an indented block of its own, and the
// main line then picks up again in a fresh line below them — which is what
// makes a variation read as an aside rather than as part of the game.
function drawAfter(byId, id, container, line, forceNumber, cursor) {
  let force = forceNumber;
  while (byId.get(id).children.length) {
    const kids = byId.get(id).children;
    line.append(...moveChip(byId.get(kids[0]), force, cursor));
    force = false;
    if (kids.length > 1) {
      for (const alt of kids.slice(1)) {
        const block = document.createElement('div');
        block.className = 'variation';
        const head = newLine(block);
        head.append(...moveChip(byId.get(alt), true, cursor));
        drawAfter(byId, alt, block, head, false, cursor);
        container.append(block);
      }
      line = newLine(container);      // the main line resumes below the asides
      force = true;                   // so a black move needs its number again
    }
    id = kids[0];
  }
}

// One move: its number when it needs one, the move itself as a button, and
// whatever is attached to it — an engine score, a PGN comment.
function moveChip(node, forceNumber, cursor) {
  const parts = [];
  if (node.white || forceNumber) {
    const number = document.createElement('span');
    number.className = 'move-number';
    number.textContent = node.white ? `${node.moveNumber}.` : `${node.moveNumber}\u2026`;
    parts.push(number, ' ');
  }
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'move' + (node.id === cursor ? ' current' : '');
  button.dataset.node = node.id;
  button.textContent = node.san;
  button.title = node.uci;
  // An engine move carries what its search found: eval and depth reached.
  if (node.note) {
    const note = document.createElement('em');
    note.textContent = `${scoreText(node.note.score)}/${node.note.depth}`;
    note.title = `${node.note.nodes} nodes in ${node.note.time} ms`;
    button.append(' ', note);
  }
  parts.push(button, ' ');
  if (node.comment) {
    const comment = document.createElement('span');
    comment.className = 'move-comment';
    comment.textContent = node.comment;
    parts.push(comment, ' ');
  }
  return parts;
}

function showMessage(text) {
  el('message').textContent = text;
  el('message').hidden = false;
}

function clearMessage() {
  el('message').hidden = true;
}

// ------------------------------------------------------------------- live link

function connect() {
  const events = new EventSource(`/api/events?id=${encodeURIComponent(sessionId)}`);
  events.addEventListener('state', event => {
    setLink('live', 'live');
    apply(JSON.parse(event.data));
  });
  // A fresh stream may be a restarted server, whose numbering starts over;
  // nothing is in flight at that moment, so there is no order left to keep.
  events.addEventListener('open', () => {
    appliedSeq = -1;
    setLink('live', 'live');
  });
  events.addEventListener('error', () => setLink('reconnecting…', 'down'));
}

function setLink(text, kind) {
  el('link').textContent = text;
  el('link').className = kind;
}

// The narrow stacked layout in style.css; keep this query in step with it.
const stacked = window.matchMedia('(max-width: 860px)');

function fitBoard() {
  // chessground needs a pixel size, and a whole number of pixels per square
  // keeps the piece SVGs from shimmering. The frame is what gets the size, not
  // the board inside it, so the promotion overlay stays on the squares too.
  const frame = document.querySelector('.board-frame');
  const room = Math.min(document.querySelector('.board-fit').clientWidth, boardRoom());
  const size = `${Math.max(256, Math.floor(room / 8) * 8)}px`;
  if (frame.style.width === size) return;    // the observer hearing our own change
  frame.style.width = frame.style.height = size;
  board.redrawAll();
}

// How tall the board may be. The page does not scroll, so a board taller than
// the column would be cut off rather than scrolled to: it gets the column's
// height less the status line under it, which is why that line is held to a
// fixed height. Stacked, the column is as tall as its content, so measuring it
// would chase the board's own size — there the page scrolls and only the width
// decides.
function outerHeight(node) {
  const style = getComputedStyle(node);
  return node.offsetHeight + (parseFloat(style.marginTop) || 0) + (parseFloat(style.marginBottom) || 0);
}

function boardRoom() {
  if (stacked.matches) return Infinity;
  const column = document.querySelector('.board-column');
  return column.clientHeight - outerHeight(el('status')) - outerHeight(el('clock-top')) - outerHeight(el('clock-bottom'));
}

const boardFit = new ResizeObserver(fitBoard);
boardFit.observe(document.querySelector('.board-fit'));      // the width, eval bar and all
boardFit.observe(document.querySelector('.board-column'));   // the height the window leaves
stacked.addEventListener('change', fitBoard);

setLink('connecting…', 'down');
fetch(`/api/state?id=${encodeURIComponent(sessionId)}`)
  .then(res => res.json())
  .then(apply)
  .catch(() => showMessage('The engine server is not answering. Is ascaniusfish_gui still running?'))
  .finally(connect);
