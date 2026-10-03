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
import { playForTransition, initMuteToggle, initLowTimeToggle, playLowTime } from './sound.js';

const sessionId = new URLSearchParams(location.search).get('id') || 'main';
const el = id => document.getElementById(id);

// The server already turned a valid "?token=" on this load into a cookie
// (gui_server.cpp's check_auth); every request from here on rides that
// cookie, so the token has no further reason to sit in the address bar where
// a screenshot or a shared link would carry it along (#27).
{
    const params = new URLSearchParams(location.search);
    if(params.has('token'))
    {
        params.delete('token');
        const query = params.toString();
        history.replaceState(null, '', location.pathname + (query ? `?${query}` : ''));
    }
}

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

// Local-only settings-form state: which sub-form is showing and whether
// Custom's fields are open. None of this is server state — it resyncs to
// what the server actually applied whenever that changes (renderPlayPanel/
// renderWatchPanel, via lastPlayClockOn/lastWatchClockOn), which is also what
// resets it after a successful Apply.
let adjudicateOpen = false;
// The plan view: which side's plans are drawn, the piece (by its square) clicked
// to stay picked, and the one the pointer is over in the list.
let planSide = 'both', planPinned = null, planHover = null;
// The eval terms whose parts are folded away (by row name); all open at first.
const termsFolded = new Set();
// Whether parts worth 0 for both sides are left out (the panel's checkbox).
// Remembered in this browser only; a view preference, not the game's state.
let termsHideZero = false;
try { termsHideZero = localStorage.getItem('termsHideZero') === '1'; } catch (e) {}
let playKindChoice = 'clock', watchKindChoice = 'clock';   // Clock is the default (#58)
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

// Whether the settings picker gives way to a one-line summary: once a move has
// been played it only opens while the game is paused, since a setting is
// changed between moves and the server refuses it otherwise.
function settingsLocked(s) {
  return hasMoves(s) && !s.paused;
}

// Standing still, by a pause or because it is over: where the engine may
// analyse the game without leaving it.
function halted(s) {
  return s.paused || s.outcome.state !== 'ongoing';
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
// from the clock the game is set to (1+1 until another is picked). Only when
// Custom is opened: filling them on every render would put a field back while
// the next one is being typed.
function fillCustomForm(prefix, white, black) {
  const set = (id, seconds) => { el(id).value = seconds; };
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

// Whole seconds, rounded up, until the last ten: then tenths, rounded down,
// as lichess shows them (#58) — "0:09.4", so the last second is never "0:01".
function formatClock(ms) {
  ms = Math.max(0, ms);
  if (ms < 10000) return `0:0${(Math.floor(ms / 100) / 10).toFixed(1)}`;
  const total = Math.ceil(ms / 1000);
  const m = Math.floor(total / 60), sec = total % 60;
  return `${m}:${String(sec).padStart(2, '0')}`;
}

// The low-time warning sounds once when *your* clock in Play drops below 10 s,
// never for an engine's (#58). Re-armed only once that clock is back above
// 15 s, so a 1+1 game hovering around the mark is not a beep every move.
const LOW_TIME_MS = 10000, LOW_TIME_REARM_MS = 15000;
let lowTimeArmed = true;

function watchLowTime(whiteMs, blackMs) {
  if (state.mode !== 'play' || !state.play.clockOn || state.outcome.state !== 'ongoing') return;
  const mine = state.play.humanColor === 'white' ? whiteMs : blackMs;
  if (mine >= LOW_TIME_REARM_MS) lowTimeArmed = true;
  else if (lowTimeArmed && mine > 0 && mine < LOW_TIME_MS && clockAnchor.running &&
           clockAnchor.turn === state.play.humanColor) {
    lowTimeArmed = false;
    playLowTime();
  }
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
  watchLowTime(whiteMs, blackMs);
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
  drawable: {
    enabled: true,
    // The plan view's arrows (renderPlans): a colour per side, and the one piece
    // looked at in the accent colour over faint circles on its other routes.
    brushes: {
      planWhite: { key: 'plw', color: '#3f8fd8', opacity: 0.75, lineWidth: 8 },
      planBlack: { key: 'plb', color: '#d8503f', opacity: 0.75, lineWidth: 8 },
      planPick:  { key: 'plp', color: '#e68f00', opacity: 0.95, lineWidth: 10 },
      planVia:   { key: 'plv', color: '#e68f00', opacity: 0.35, lineWidth: 6 },
    },
  },
  events: { select: key => pickPlanAt(key) },
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
  if (s.play.thinking || s.paused || s.turn !== s.play.humanColor) return;
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
// what is on screen, and its two squares in chessground's premove colour are what mark
// it as not really played.
function replayQueue() {
  for (const mv of queue) applyQueuedMoveVisually(mv);
  const marked = new Map();
  for (const mv of queue) for (const sq of [mv.orig, mv.dest]) marked.set(sq, 'current-premove');
  board.set({ highlight: { custom: marked } });
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
el('pause').addEventListener('click', () => {
  if (state) { clearQueueSilently(); command('/api/pause', { on: !state.paused }); }
});
el('adjudicate').addEventListener('click', () => { adjudicateOpen = !adjudicateOpen; render(state); });
el('adjudicate-choices').addEventListener('click', event => {
  const button = event.target.closest('button');
  if (!button) return;
  adjudicateOpen = false;
  clearQueueSilently();
  command('/api/adjudicate', { result: button.dataset.result });
});

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

for (const id of ['analysis-line', 'analysis-lines'])
  el(id).addEventListener('click', event => {
    const button = event.target.closest('button.pv-move');
    if (button) command('/api/line', { moves: button.dataset.line });
  });

// The analysis's number of lines (#69): one more or fewer, up to every legal
// move, which "All" asks for in every position to come.
const MPV_ALL = 256;   // the server's ANALYSIS_LINES_MAX
function setLines(n) {
  if (state && n !== state.analysis.lines) command('/api/settings', { lines: n });
}
el('mpv-less').addEventListener('click', () => {
  if (state) setLines(Math.max(1, Math.min(state.analysis.lines, state.legalMoves.length) - 1));
});
el('mpv-more').addEventListener('click', () => {
  if (state) setLines(Math.min(MPV_ALL, state.analysis.lines + 1));
});
el('mpv-all').addEventListener('click', () => setLines(MPV_ALL));

el('settings-toggle').addEventListener('click', () => openSettings(el('settings').hidden));
el('share-toggle').addEventListener('click', () => openShare(el('share').hidden));

el('set-evalbar').addEventListener('change', event =>
  command('/api/settings', { evalBar: event.target.checked }));
el('set-engine-line').addEventListener('change', event =>
  command('/api/settings', { engineLine: event.target.checked }));
el('set-plans').addEventListener('change', event =>
  command('/api/settings', { dreamer: event.target.checked }));
el('set-eval-terms').addEventListener('change', event =>
  command('/api/settings', { advanced: event.target.checked }));
el('plans-filter').addEventListener('click', event => {
  const button = event.target.closest('button[data-side]');
  if (!button) return;
  planSide = button.dataset.side;
  for (const b of el('plans-filter').children) b.classList.toggle('active', b === button);
  if (state) renderPlans(state);
});
el('plans-list').addEventListener('click', event => {
  const row = event.target.closest('.plans-row[data-from]');
  if (!row) return;
  planPinned = planPinned === row.dataset.from ? null : row.dataset.from;
  if (state) renderPlans(state);
});
el('plans-list').addEventListener('pointerover', event => {
  const row = event.target.closest('.plans-row[data-from]');
  const from = row ? row.dataset.from : null;
  if (from === planHover) return;
  planHover = from;
  if (state) drawPlanShapes(state);
});
el('plans-list').addEventListener('pointerleave', () => {
  planHover = null;
  if (state) drawPlanShapes(state);
});
el('terms-hide-zero').checked = termsHideZero;
el('terms-hide-zero').addEventListener('change', event => {
  termsHideZero = event.target.checked;
  try { localStorage.setItem('termsHideZero', termsHideZero ? '1' : '0'); } catch (e) {}
  if (state) renderTerms(state);
});
el('terms-list').addEventListener('click', event => {
  const row = event.target.closest('.terms-parent[data-name]');
  if (!row) return;
  const name = row.dataset.name;
  if (!termsFolded.delete(name)) termsFolded.add(name);
  if (state) renderTerms(state);
});
el('set-tb').addEventListener('change', event =>
  command('/api/settings', { tb: event.target.checked }));
el('set-tb-limit').addEventListener('change', event =>
  command('/api/settings', { tbLimit: Number(event.target.value) }));
el('set-nne').addEventListener('change', event =>
  command('/api/settings', { nne: event.target.checked }));
el('set-narrow').addEventListener('change', event =>
  command('/api/settings', { narrow: event.target.checked }));
el('set-narrow-deeper').addEventListener('change', event =>
  command('/api/settings', { narrowDeeper: event.target.checked }));
el('tb-moves').addEventListener('click', event => {
  const row = event.target.closest('button.tb-move');
  if (row) command('/api/line', { moves: row.dataset.uci });
});
initMuteToggle(el('set-sound'));
initLowTimeToggle(el('set-lowtime'));

// Anywhere else closes it, as a menu does; the gear itself is its own toggle.
document.addEventListener('pointerdown', event => {
  if (!event.target.closest('#settings, #settings-toggle')) openSettings(false);
  if (!event.target.closest('#share, #share-toggle')) openShare(false);
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
  if (event.key === 'Escape' && !el('share').hidden) return openShare(false);
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
  const movableColor = over || (playing && s.paused) ? undefined
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

  el('status').textContent = statusLine(s);
  el('status').classList.toggle('over', over);
  el('undo').disabled = (!s.tree.canBack && !s.play.resigned) || s.play.thinking;
  el('pause').hidden = !playing || over;
  el('pause').textContent = s.paused ? 'Resume' : 'Pause';
  el('resign').hidden = !playing;
  el('resign').disabled = over;
  el('adjudicate').hidden = s.mode === 'analyse';
  el('adjudicate').disabled = over;
  if (over || s.mode === 'analyse') adjudicateOpen = false;
  el('adjudicate').classList.toggle('active', adjudicateOpen);
  el('adjudicate-choices').hidden = !adjudicateOpen;
  el('to-analyse').hidden = !over || s.mode === 'analyse';

  renderMaterial(s);
  renderClockVisibility(s);
  renderPlayPanel(s);
  renderWatchPanel(s);
  renderEngine(s);
  renderPlans(s);
  renderTerms(s);
  renderSettings(s);

  // Leave a FEN or a PGN the user is in the middle of typing alone.
  if (document.activeElement !== el('fen')) el('fen').value = s.fen;
  if (document.activeElement !== el('pgn')) el('pgn').value = s.pgn;
  el('download-pgn').href = `/api/pgn?id=${encodeURIComponent(sessionId)}`;

  renderMoves(s);
  if (pendingPromotion) openPromotion();
}

// ------------------------------------------------------------------ material

// Lichess's material diff: for each kind of piece, whichever side has more of it
// shows the difference as the opponent's pieces it has taken, and the side
// ahead on points shows "+N". Counted from the FEN rather than from captures, so
// a promotion or a position loaded from a FEN comes out right too.
const MATERIAL_ROLES = [['q', 'queen', 9], ['r', 'rook', 5], ['b', 'bishop', 3], ['n', 'knight', 3], ['p', 'pawn', 1]];

function renderMaterial(s) {
  const count = {};
  for (const ch of s.fen.split(' ')[0]) if (/[a-z]/i.test(ch)) count[ch] = (count[ch] || 0) + 1;
  const up = { white: [], black: [] };
  let points = 0;
  for (const [code, role, value] of MATERIAL_ROLES) {
    const diff = (count[code.toUpperCase()] || 0) - (count[code] || 0);
    points += diff * value;
    if (diff > 0) up.white.push(...Array(diff).fill([role, 'black']));
    if (diff < 0) up.black.push(...Array(-diff).fill([role, 'white']));
  }
  const top = s.orientation === 'white' ? 'black' : 'white';
  el('strip-top').className = `player-strip ${top}`;
  el('strip-bottom').className = `player-strip ${top === 'white' ? 'black' : 'white'}`;
  drawMaterial(el('material-top'), up[top], top === 'white' ? points : -points);
  drawMaterial(el('material-bottom'), up[top === 'white' ? 'black' : 'white'], top === 'white' ? -points : points);
}

function drawMaterial(box, pieces, points) {
  box.replaceChildren();
  pieces.forEach(([role, color], i) => {
    const piece = document.createElement('piece');
    piece.className = `${role} ${color}` + (i > 0 && pieces[i - 1][0] === role ? ' same' : '');
    box.append(piece);
  });
  if (points > 0) {
    const score = document.createElement('span');
    score.className = 'score-up';
    score.textContent = `+${points}`;
    box.append(score);
  }
}

// -------------------------------------------------------------------- clock

// Two chips above and below the board, in the strips beside the material,
// flipping with orientation like a real chess site's; hidden entirely outside
// a clocked Play or Watch game. Only visibility is decided here — tickClocks()
// draws the numbers on its own timer. The strips keep their height either way,
// so the board does not change size for it.
function renderClockVisibility(s) {
  const on = (s.mode === 'play' && s.play.clockOn) || (s.mode === 'watch' && s.watch.clockOn);
  el('clock-top').hidden = el('clock-bottom').hidden = !on;
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
    playCustomOpen = false;
  }

  const collapsed = settingsLocked(s);
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
    () => {
      playCustomOpen = !playCustomOpen;
      if (playCustomOpen) fillCustomForm('play', state.play.clockWhite, state.play.clockBlack);
      render(state);
    });
  el('play-custom-form').hidden = !playCustomOpen;

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
  // With the DTM tables (#77) a won position has its mate; a cursed win's mate
  // is past the 50-move rule, so that one stays a result.
  if (score.kind === 'tb' && score.mate && Math.abs(score.value) === 2) return '#' + score.mate;
  if (score.kind === 'tb') return score.value > 0 ? '1-0' : score.value < 0 ? '0-1' : '½-½';
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
    watchCustomOpen = false;
  }

  const collapsed = settingsLocked(s);
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
    () => {
      watchCustomOpen = !watchCustomOpen;
      if (watchCustomOpen) fillCustomForm('watch', state.watch.clockWhite, state.watch.clockBlack);
      render(state);
    });
  el('watch-custom-form').hidden = !watchCustomOpen;

  const over = s.outcome.state !== 'ongoing';
  // Start and Step only make sense at the end of a line, which is the only
  // place the engines play from; the server refuses them anywhere else.
  el('watch-run').textContent = w.running ? 'Pause' : 'Start';
  el('watch-run').disabled = !w.running && (over || !w.atTip);
  el('watch-step').disabled = w.running || w.thinking || over || !w.atTip;
  // One move and a pause is a fixed-depth thing: on a clock the time would run
  // on through the pause anyway.
  el('watch-step').hidden = w.clockOn;

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
  const canToggle = analysing || (halted(s) && s.mode !== 'analyse');
  panel.hidden = !canToggle && !showLine;
  renderTbMoves(s);
  if (panel.hidden) { hidePreview(); shownLineKey = shownLinesKey = null; return; }

  panel.classList.toggle('running', (a.running || s.play.thinking || s.watch.thinking) && !a.error);
  // A tablebase position has a line only with the DTM tables: the mating one (#77).
  panel.classList.toggle('has-line', showLine && ((known && (ev.source !== 'tb' || ev.line.length > 0)) || a.running));
  el('analysis-toggle').hidden = !canToggle;
  el('analysis-toggle').textContent = a.on ? 'Turn engine off' : 'Turn engine on';
  el('analysis-score').textContent = s.settings.evalBar && known ? scoreText(ev.score) : '';

  // With the line switched off the stats go too, and so does the height they
  // hold open: an Analyse panel that is only its on/off button should look like
  // one, not like a box with something missing from it.
  el('analysis-stats').hidden = !showLine;
  el('analysis-stats').textContent = showLine ? engineStats(s) : '';
  el('analysis-stats').classList.toggle('bad', showLine && analysing && !!a.error);

  // MultiPV (#69): the K control goes with the on/off switch, since K is what
  // that switch's analysis runs with, in a paused or finished game too. Its
  // rows replace the single line while an analysis with K>1 is what is shown;
  // a Play or Watch search, a move's own line and the tables stay one line.
  const legal = s.legalMoves.length;
  const wanted = Math.min(a.lines, legal);
  el('mpv').hidden = !canToggle;
  el('mpv-count').textContent = a.lines >= legal && legal > 1 ? 'all' : String(wanted);
  el('mpv-less').disabled = wanted <= 1;
  el('mpv-more').disabled = a.lines >= legal;
  el('mpv-all').disabled = a.lines >= legal;
  const multi = showLine && canToggle && wanted > 1 && (ev.source === 'live' || ev.source === 'stored'
    || (a.on && ev.source === 'none'));
  panel.classList.toggle('multi', multi);
  const rows = !multi ? [] : ev.lines.length ? ev.lines
    : ev.line.length ? [{ score: ev.score, line: ev.line }] : [];
  el('analysis-lines').hidden = !multi;
  if (multi) renderLines(s, rows, wanted, analysing);
  else {
    shownLinesKey = null;
    if (preview.on && preview.row >= 0) hidePreview();
  }
  renderLine(s, showLine && !multi ? ev : null, analysing);
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
  if (ev.source === 'tb') return tbSentence(ev.score);
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

// What the tables say, in words. The value is white's view; a cursed win or a
// blessed loss is a win that the 50-move rule turns into a draw.
function tbSentence(score) {
  const v = score.value;
  const who = v > 0 ? 'White' : 'Black';
  const dtz = score.dtz ? `, DTZ ${score.dtz}` : '';
  const mate = score.mate ? Math.abs(score.mate) : 0;
  if (v === 0) return 'Tablebase: draw.';
  if (Math.abs(v) === 1) return `Tablebase: ${who} wins only past the 50-move rule (drawn)${dtz}`
    + (mate ? `; mate in ${mate} without the rule.` : '.');
  if (mate) return `Tablebase: ${who} mates in ${mate}${dtz}.`;
  return `Tablebase: ${who} wins${dtz}.`;
}

const TB_LABEL = { 2: 'Win', 1: 'Cursed win', 0: 'Draw', '-1': 'Blessed loss', '-2': 'Loss' };

// Analyse mode's list of every legal move with its result for the side that
// plays it, best first. Clicking one plays it.
function renderTbMoves(s) {
  const box = el('tb-moves');
  const moves = s.mode === 'analyse' ? s.tbMoves : [];
  box.hidden = !moves.length;
  box.replaceChildren();
  for (const m of moves) {
    const row = document.createElement('button');
    row.type = 'button';
    row.className = `tb-move wdl${m.wdl}`;
    row.dataset.uci = m.uci;
    const san = document.createElement('span');
    san.textContent = m.san;
    const result = document.createElement('span');
    result.className = 'tb-result';
    // m.mate is the mover's (#77): #3 mates in 3, #-3 is mated in 3.
    result.textContent = (m.mate ? `#${m.mate} · ` : '') + TB_LABEL[m.wdl] + (m.dtz ? ` · DTZ ${m.dtz}` : '');
    row.append(san, result);
    box.append(row);
  }
}

// Where the bar sits, as white's share of it. The centipawn score goes through
// a sigmoid: this engine's king-safety terms reach the thousands, so a linear
// scale would sit pinned at one end for most of a game. Both ends stop short
// of full, so a bar that does run out really does mean mate.
function whiteShare(score) {
  if (score.kind === 'mate') return score.value >= 0 ? 0.97 : 0.03;
  // Exact results: a win fills the bar, a cursed win (drawn under the 50-move
  // rule) only leans.
  if (score.kind === 'tb') return { 2: 0.97, 1: 0.58, 0: 0.5, '-1': 0.42, '-2': 0.03 }[score.value];
  return Math.min(0.97, Math.max(0.03, 1 / (1 + Math.exp(-score.value / 400))));
}

// The best line, numbered from the position on the board. In Analyse mode each
// move is a button: clicking it plays the line up to and including that move. In
// Play and Watch it is a line to read, not one to walk into — the engine is in
// the middle of a game there, and clicking a move would open a side line in it.
//
// Every move has the same width (#58), laid out like a score sheet: a full move
// is a number slot and two move slots, so `e4` takes as much room as `exd5+` and
// a line of a given depth is always as long. A move's number belongs to its
// white move's cell, and a line starting with black's move keeps the white slot
// empty inside black's cell, so the cells touch and there is no gap between two
// moves for the pointer to fall through (see the preview below).
let shownLine = [];      // the line the cells were drawn from, ev.line's entries
let shownLineKey = null;
let shownLines = [];     // with several lines (#69): each row's line, ev.lines[i].line
let shownLinesKey = null;

function renderLine(s, ev, clickable) {
  const box = el('analysis-line');
  const line = ev ? ev.line : [];
  // A search pushes a state every iteration, mostly with the same line: leave
  // the cells alone then, so the one under the pointer stays the same element.
  const key = [clickable, s.fullmove, s.turn, s.orientation, ...line.map(m => m.uci)].join(' ');
  if (key === shownLineKey) return;
  shownLineKey = key;
  shownLine = line;
  box.replaceChildren();
  appendLineCells(box, s, line, clickable, -1);
  fitLine();
  // The line changed under a hover: the move under the pointer is the same ply
  // of the new line, since every cell is where it was.
  if (preview.on && preview.row < 0) showPreview(preview.ply, -1);
}

// MultiPV (#69): one row per line, best first, its score (white's view) in
// front and as many of its moves as the row holds. A row whose line is longer
// gets the same arrow as the single line at its end, which opens that row alone
// (fitLines()). The box is as tall as the lines asked for (up to MPV_ROWS, then
// it scrolls) plus what the open rows add, not as the lines that have come in,
// so nothing below it moves while the search fills it.
const MPV_ROWS = 8;
let linesWanted = 1;
const openRows = new Set();   // rows opened by their arrow; by place, not by move

function renderLines(s, lines, wanted, clickable) {
  const box = el('analysis-lines');
  linesWanted = wanted;
  const key = [clickable, s.fullmove, s.turn, s.orientation,
    ...lines.map(l => `${l.score ? scoreText(l.score) : ''}:${l.line.map(m => m.uci).join(',')}`)].join(' ');
  if (key === shownLinesKey) return fitLines();
  shownLinesKey = key;
  shownLines = lines.map(l => l.line);
  box.replaceChildren();
  lines.forEach((l, row) => {
    const div = document.createElement('div');
    div.className = 'mpv-row';
    const score = document.createElement('span');
    score.className = 'mpv-score';
    score.textContent = l.score ? scoreText(l.score) : '';
    const moves = document.createElement('span');
    moves.className = 'mpv-moves';
    appendLineCells(moves, s, l.line, clickable, row);
    const arrow = document.createElement('button');
    arrow.type = 'button';
    arrow.className = 'line-expand mpv-expand';
    arrow.dataset.row = row;
    div.append(score, moves, arrow);
    box.append(div);
  });
  fitLines();
  if (preview.on && preview.row >= 0) showPreview(preview.ply, preview.row);
}

// A line's moves as score-sheet cells (see above) into `box`; `row` is the
// line's row under MultiPV, -1 for the single line.
function appendLineCells(box, s, line, clickable, row) {
  let fullmove = s.fullmove;
  let white = s.turn === 'white';
  let unit = null;
  line.forEach((move, ply) => {
    if (white || ply === 0) {
      unit = document.createElement('span');
      unit.className = 'pv-unit';
      box.append(unit);
    }
    const chip = document.createElement(clickable ? 'button' : 'span');
    if (clickable) {
      chip.type = 'button';
      chip.dataset.line = line.slice(0, ply + 1).map(m => m.uci).join(' ');
    }
    chip.className = 'pv-move';
    chip.dataset.ply = ply;
    chip.dataset.row = row;
    chip.title = move.uci;
    if (white || ply === 0) {
      const number = document.createElement('span');
      number.className = 'move-number';
      number.textContent = white ? `${fullmove}.` : `${fullmove}\u2026`;
      chip.append(number);
      if (!white) {
        const empty = document.createElement('span');
        empty.className = 'pv-san';
        chip.append(empty);
      }
    }
    const san = document.createElement('span');
    san.className = 'pv-san';
    san.textContent = move.san;
    chip.append(san);
    unit.append(chip);
    if (!white) fullmove++;
    white = !white;
  });
}

// The line shows the one row its box holds (#68), so the move list below stays
// where it is however deep the search goes. A line longer than that gets an
// arrow at the row's end, and opened the box is as tall as the line, up to
// LINE_PLIES before it scrolls. Every .pv-unit is a full move of 17.5ch
// (.move-number and two .pv-san), whichever side starts. Open or not is this
// browser's own choice, like a folded section, not the session's.
const LINE_PLIES = 50;
let lineOpen = false;
try { lineOpen = localStorage.getItem('lineOpen') === '1'; } catch (e) {}

function fitLine() {
  const box = el('analysis-line');
  const style = getComputedStyle(box);
  const width = box.clientWidth - parseFloat(style.paddingRight);
  if (width <= 0) return;   // not shown: measured again when it is
  const ch = parseFloat(style.fontSize) * charRatio(box);
  const perRow = Math.max(1, Math.floor(width / (17.5 * ch)));
  const units = box.querySelectorAll('.pv-unit').length;
  const longer = units > perRow;
  const open = lineOpen && longer;
  const rows = open ? Math.min(Math.ceil(units / perRow), Math.ceil((LINE_PLIES / 2 + 1) / perRow)) : 1;
  const height = `${rows * 1.8}em`;
  if (box.style.height !== height) box.style.height = height;
  box.classList.toggle('open', open);
  const arrow = el('line-expand');
  arrow.hidden = !longer;
  arrow.title = open ? 'Show one row of the line' : 'Show the whole line';
  arrow.setAttribute('aria-expanded', open);
}

// fitLine() for each MultiPV row: the arrow where the row's line does not fit,
// and an open row as tall as its line, up to LINE_PLIES. Every row keeps the
// arrow's slot, so no row's cells move when its arrow comes or goes.
function fitLines() {
  const box = el('analysis-lines');
  let extra = 0;
  for (const div of box.querySelectorAll('.mpv-row')) {
    const moves = div.querySelector('.mpv-moves');
    const arrow = div.querySelector('.mpv-expand');
    const width = moves.clientWidth;
    if (width <= 0) return;   // not shown: measured again when it is
    const ch = parseFloat(getComputedStyle(moves).fontSize) * charRatio(box);
    const perRow = Math.max(1, Math.floor(width / (17.5 * ch)));
    const units = moves.querySelectorAll('.pv-unit').length;
    const longer = units > perRow;
    const open = openRows.has(Number(arrow.dataset.row)) && longer;
    const rows = open ? Math.min(Math.ceil(units / perRow), Math.ceil((LINE_PLIES / 2 + 1) / perRow)) : 1;
    const height = `${rows * 1.8}em`;
    if (moves.style.height !== height) moves.style.height = height;
    moves.classList.toggle('open', open);
    arrow.classList.toggle('none', !longer);
    arrow.title = open ? 'Show one row of this line' : 'Show the whole line';
    arrow.setAttribute('aria-expanded', open);
    extra += rows - 1;
  }
  const height = `${(Math.min(linesWanted, MPV_ROWS) + extra) * 1.8}em`;
  if (box.style.height !== height) box.style.height = height;
}

el('analysis-lines').addEventListener('click', event => {
  const arrow = event.target.closest('.mpv-expand');
  if (!arrow) return;
  const row = Number(arrow.dataset.row);
  if (!openRows.delete(row)) openRows.add(row);
  hidePreview();
  fitLines();
});

el('line-expand').addEventListener('click', () => {
  lineOpen = !lineOpen;
  try { localStorage.setItem('lineOpen', lineOpen ? '1' : '0'); } catch (e) {}
  hidePreview();
  fitLine();
});

// The width of the line font's `0` per pixel of font size: what one `ch` is.
let charRatioCache = 0;
function charRatio(box) {
  if (!charRatioCache) {
    const probe = document.createElement('span');
    probe.style.cssText = 'position:absolute;visibility:hidden;width:100ch;font-size:100px';
    box.append(probe);
    charRatioCache = probe.offsetWidth / 100 / 100;
    probe.remove();
  }
  return charRatioCache;
}

// A frame later: fitLine() changes the panel's height, which inside the
// observer's own callback would be a resize loop.
new ResizeObserver(() => requestAnimationFrame(() => { fitLine(); fitLines(); }))
  .observe(el('analysis-panel'));

// ------------------------------------------------------------------- preview

// Hovering a move of the line shows the position after it on a board laid over
// the move list (#58, #68), the way display_board.py does it: the first move shows
// after a short delay, so a pointer just passing over the panel shows nothing,
// and from then on every move switches at once — the board is never cleared
// between two moves, only when the pointer leaves the line altogether. A gap
// between cells (there should be none) keeps whatever is shown.
const HOVER_DELAY_MS = 100;
const preview = { on: false, ply: -1, row: -1, timer: 0, board: null };   // row: see appendLineCells()

// The box the hovered line is drawn in: the single line, or the MultiPV rows.
function lineBox(row) {
  return el(row < 0 ? 'analysis-line' : 'analysis-lines');
}

function previewBoard() {
  if (!preview.board)
    preview.board = Chessground(el('preview-board'), {
      viewOnly: true,
      coordinates: false,
      animation: { enabled: false },   // a jump per move, never a slide that lags the pointer
      highlight: { lastMove: true, check: true },
      drawable: { enabled: false, visible: false },
    });
  return preview.board;
}

function showPreview(ply, row) {
  const move = (row < 0 ? shownLine : shownLines[row] || [])[ply];
  if (!move || !move.fen) return hidePreview();
  preview.on = true;
  preview.ply = ply;
  preview.row = row;
  const box = el('line-preview');
  placePreview(box);
  box.classList.add('shown');
  const fields = move.fen.split(' ');
  previewBoard().set({
    fen: fields[0],
    orientation: state.orientation,
    turnColor: fields[1] === 'w' ? 'white' : 'black',
    lastMove: [move.uci.slice(0, 2), move.uci.slice(2, 4)],
    check: false,
  });
  for (const chip of el('analysis-panel').querySelectorAll('.pv-move'))
    chip.classList.toggle('previewed', Number(chip.dataset.ply) === ply && Number(chip.dataset.row) === row);
}

function hidePreview() {
  clearTimeout(preview.timer);
  preview.on = false;
  preview.ply = -1;
  preview.row = -1;
  el('line-preview').classList.remove('shown');
  for (const chip of el('analysis-panel').querySelectorAll('.pv-move.previewed'))
    chip.classList.remove('previewed');
}

// Over the game's move list, as lichess does it (#68): the line stays in view
// and the moves it would replace are what the board covers. As wide as the list
// (within PREVIEW_MIN..PREVIEW_MAX, whole pixels per square). When the list is
// not on the screen (scrolled away, or the stacked layout), right above the
// line instead, or below it when the window has no room above. Measured on
// every show, since the aside scrolls.
const PREVIEW_MIN = 240, PREVIEW_MAX = 360;

function placePreview(box) {
  const moves = el('moves').getBoundingClientRect();
  const size = Math.floor(Math.min(PREVIEW_MAX, Math.max(PREVIEW_MIN, moves.width)) / 8) * 8;
  if (box.offsetWidth !== size) {
    box.style.width = box.style.height = `${size}px`;
    if (preview.board) preview.board.redrawAll();
  }
  // Moved up as far as the window needs, over the open line box's empty rows,
  // but never over a move of the line that is shown.
  const line = lineBox(preview.row).getBoundingClientRect();
  const cells = lineBox(preview.row).querySelectorAll('.pv-move');
  const lineEnd = cells.length ? Math.min(line.bottom, cells[cells.length - 1].getBoundingClientRect().bottom) : line.top;
  const top = Math.max(lineEnd + 8, Math.min(moves.top, window.innerHeight - size - 4));
  if (moves.top >= 4 && top + size <= window.innerHeight - 4) {
    box.style.top = `${top}px`;
    box.style.left = `${moves.left}px`;
    return;
  }
  const panel = el('analysis-panel').getBoundingClientRect();
  const above = line.top - size - 8;
  box.style.top = `${above >= 4 ? above : line.bottom + 8}px`;
  box.style.left = `${Math.max(4, panel.right - size)}px`;
}

for (const id of ['analysis-line', 'analysis-lines']) {
  el(id).addEventListener('mouseover', event => {
    const chip = event.target.closest('.pv-move');
    if (!chip) return;
    const ply = Number(chip.dataset.ply), row = Number(chip.dataset.row);
    if (preview.on) return showPreview(ply, row);
    clearTimeout(preview.timer);
    preview.ply = ply;
    preview.row = row;
    preview.timer = setTimeout(() => showPreview(preview.ply, preview.row), HOVER_DELAY_MS);
  });
  el(id).addEventListener('mouseleave', hidePreview);
  el(id).addEventListener('scroll', () => { if (preview.on) placePreview(el('line-preview')); });
}

// ---------------------------------------------------------------------- plans

// The plan term (lib/plan_eval.hpp), as the server computed it for the position
// on the board: per piece its best square, db (what standing there is worth to
// its owner, cp), N (moves needed) and term (what that adds to the eval now,
// white's view, cp). The list is by size of term; the board shows every listed
// piece's path, or only the picked one's with its other shortest routes.
const PLAN_ROLE = { K: 'king', Q: 'queen', R: 'rook', B: 'bishop', N: 'knight', P: 'pawn' };

function signed(v) {
  return (v > 0 ? '+' : v < 0 ? '−' : '') + Math.abs(v);
}

function shownPlans(s) {
  if (!s.plans) return [];
  return s.plans.pieces
    .filter(p => planSide === 'both' || p.color === planSide)
    .sort((a, b) => Math.abs(b.term) - Math.abs(a.term) || b.db - a.db);
}

function renderPlans(s) {
  const panel = el('plans-panel');
  panel.hidden = !s.plans;
  if (!s.plans) {
    planPinned = planHover = null;
    board.setAutoShapes([]);
    return;
  }
  const plans = shownPlans(s);
  if (planPinned && !plans.some(p => p.from === planPinned)) planPinned = null;
  el('plans-total').textContent = `${signed(s.plans.total)} cp`;
  el('plans-note').textContent =
    `Plan term ${signed(s.plans.total)} cp beside the static eval ${signed(s.plans.static)} cp ` +
    `(not part of it: the search never sees it). ${s.plans.pieces.length} pieces have a better square` +
    (s.plans.net ? ', judged with the net\'s correction.' : '.');

  const list = el('plans-list');
  list.replaceChildren();
  for (const p of plans) {
    const row = document.createElement('div');
    row.className = `plans-row ${p.color}`;
    row.classList.toggle('picked', p.from === planPinned);
    row.dataset.from = p.from;
    row.title = `${p.path.join(' → ')}` +
      (p.via.length > p.path.length ? ` (${p.via.length - 2} squares on some shortest path)` : '');
    const piece = document.createElement('piece');
    piece.className = `${PLAN_ROLE[p.piece]} ${p.color}`;
    row.append(piece);
    const cells = [
      `${p.from}→${p.to}`,
      String(p.n),
      signed(p.db),
      signed(p.term),
    ];
    for (const text of cells) {
      const cell = document.createElement('span');
      cell.textContent = text;
      row.append(cell);
    }
    list.append(row);
  }
  drawPlanShapes(s);
}

// Arrows along each path, one per move. All pieces: a width that grows with the
// term. One piece (hovered or picked): its path in the accent colour, a label
// with db on the target, and a faint ring on every other square of a shortest path.
function drawPlanShapes(s) {
  if (!s.plans) return;
  const plans = shownPlans(s);
  const focus = planHover || planPinned;
  const picked = focus && plans.find(p => p.from === focus);
  const shapes = [];
  const arrows = (p, brush, lineWidth) => {
    for (let i = 0; i + 1 < p.path.length; i++)
      shapes.push({ orig: p.path[i], dest: p.path[i + 1], brush, modifiers: { lineWidth } });
  };
  if (picked) {
    for (const sq of picked.via)
      if (!picked.path.includes(sq)) shapes.push({ orig: sq, brush: 'planVia' });
    arrows(picked, 'planPick', 10);
    shapes.push({ orig: picked.to, brush: 'planPick', label: { text: signed(picked.db), fill: '#e68f00' } });
  } else {
    const top = Math.max(1, ...plans.map(p => Math.abs(p.term)));
    for (const p of plans)
      arrows(p, p.color === 'white' ? 'planWhite' : 'planBlack', 3 + Math.round(9 * Math.abs(p.term) / top));
  }
  board.setAutoShapes(shapes);
}

// A click on a piece picks its plan, when the plan view is on and it has one.
function pickPlanAt(key) {
  if (!state || !state.plans) return;
  const p = shownPlans(state).find(q => q.from === key);
  if (!p) return;
  planPinned = key;
  renderPlans(state);
}

// ---------------------------------------------------------------- eval terms

// basic_eval() of the position on the board, as the server took it apart
// (Session::write_eval_terms()): per row each side's own share where the engine
// has one, and the total in white's view. The totals must add up to basic_eval;
// when they don't, or a row's white - black is not its total, the panel says so
// in red rather than quietly showing numbers that do not belong together.
//
// The panel has a column of its own right of the side column when the window
// is wide enough for one beside a full-size board (main's padding and gaps, the
// board column's 620px, the side column's 300px and the terms column's 320px
// minimum), and sits in the side column under the plans otherwise.
const roomy = window.matchMedia('(min-width: 1340px)');
roomy.addEventListener('change', placeTerms);

function placeTerms() {
  const panel = el('terms-panel'), column = el('terms-column');
  if (roomy.matches) {
    if (panel.parentNode !== column) column.append(panel);
  } else if (panel.parentNode === column) {
    el('plans-panel').after(panel);
  }
  column.hidden = !roomy.matches || panel.hidden;
}

function pawns(cp) {
  return (cp > 0 ? '+' : cp < 0 ? '−' : '') + (Math.abs(cp) / 100).toFixed(2);
}

function renderTerms(s) {
  const t = s.evalTerms;
  el('terms-panel').hidden = !t;
  placeTerms();
  if (!t) return;

  el('terms-total').textContent = `${pawns(t.basic)} (${signed(t.basic)} cp)`;
  el('terms-net-head').hidden = !(t.net && t.net.ok);
  if (t.net && t.net.ok)
    el('terms-net-total').textContent = `${pawns(t.net.corrected)} (${signed(t.net.corrected)} cp)`;
  // The self-check the server ran when the switch went on (gui/eval_split.hpp):
  // the rebuild against the real eval on the test positions.
  const sc = el('terms-selfcheck');
  sc.replaceChildren();
  sc.classList.toggle('bad', !!t.check && !t.check.ok);
  if (t.check) {
    const head = document.createElement('div');
    head.textContent = t.check.ok
      ? `✓ Self-check: the breakdown matches the real eval on ${t.check.positions} positions (${(t.check.us / 1000).toFixed(1)} ms).`
      : `✗ Self-check failed on ${t.check.positions} positions (${(t.check.us / 1000).toFixed(1)} ms):`;
    sc.append(head);
    for (const m of t.check.messages) {
      const line = document.createElement('div');
      line.textContent = '• ' + m;
      sc.append(line);
    }
  }
  // The bars share one scale, the biggest total on the board (at least a pawn),
  // drawn from the middle: right is good for white, left for black.
  const top = Math.max(100, ...t.rows.map(r => Math.abs(r.total)));
  const list = el('terms-list');
  list.replaceChildren();
  const addRow = (cells, total, title, classes) => {
    const row = document.createElement('div');
    row.className = 'terms-row ' + classes;
    row.title = title;
    for (const text of cells) {
      const cell = document.createElement('span');
      cell.textContent = text;
      row.append(cell);
    }
    const bar = document.createElement('div');
    bar.className = 'terms-bar';
    const fill = document.createElement('i');
    const width = 50 * Math.min(1, Math.abs(total) / top);
    fill.className = total >= 0 ? 'white' : 'black';
    fill.style.width = `${width}%`;
    fill.style.left = total >= 0 ? '50%' : `${50 - width}%`;
    bar.append(fill);
    row.append(bar);
    list.append(row);
    return row;
  };
  for (const r of t.rows) {
    let title = r.info + (r.split === false ? ' — white − black is not the total!' : '');
    if (r.name === 'Material' && r.white !== r.black)
      title += ` (×${(r.total / (r.white - r.black)).toFixed(3)})`;
    const open = r.parts && !termsFolded.has(r.name);
    const name = r.parts ? `${open ? '▾' : '▸'} ${r.name}` : r.name;
    const cells = [
      name,
      r.white === null ? '·' : signed(r.white),
      r.black === null ? '·' : signed(r.black),
      signed(r.total),
    ];
    const bad = r.split === false || r.partsOk === false;
    if (r.partsOk === false) title += ' — the parts below do not add up to this total (gui/eval_split.hpp is stale?)';
    const row = addRow(cells, r.total, title, (bad ? 'bad ' : '') + (r.parts ? 'terms-parent' : ''));
    if (!r.parts) continue;
    row.dataset.name = r.name;
    if (!open) continue;
    // Each part unscaled in the owner's view; the engine divides only the sum by
    // scale, so a part can be a fraction of a cp.
    const cp = raw => {
      const v = raw / r.scale;
      return Number.isInteger(v) ? signed(v) : (v > 0 ? '+' : '−') + Math.abs(v).toFixed(1);
    };
    for (const p of r.parts) {
      if (termsHideZero && p.white === 0 && p.black === 0) continue;
      const hits = list => list.length ? ` (${list.join(', ')})` : '';
      addRow([p.name, cp(p.white), cp(p.black), cp(p.white - p.black)], (p.white - p.black) / r.scale,
             `${p.info}.\nWhite ${p.whiteCount}${hits(p.whiteHits)}\nBlack ${p.blackCount}${hits(p.blackHits)}`,
             'terms-part');
    }
  }

  // The dreamer (lib/plan_eval.hpp), when its switch is on: a term of its own
  // beside the eval, not one of basic_eval's, so it stays out of the sum.
  if (t.dream)
    addRow(['☾ Dreamer (not summed)', signed(t.dream.white), signed(t.dream.black), signed(t.dream.total)],
           t.dream.total,
           `${t.dream.info}\nNot part of basic_eval and never used by the search; ` +
           `basic_eval with it would be ${signed(t.basic + t.dream.total)} cp.`,
           'terms-extra');

  // The net (lib/nne.hpp), when the NNE switch is on: its correction, which
  // the search adds to basic_eval at its quiet leaves; outside the sum too.
  if (t.net)
    addRow(t.net.ok ? ['◆ Net correction (not summed)', '', '', signed(t.net.total)]
                    : ['◆ Net (not loaded)', '', '', '—'],
           t.net.ok ? t.net.total : 0,
           t.net.ok ? `${t.net.info}.\nbasic_eval + net = ${signed(t.net.corrected)} cp, ` +
                      'the score the search gives this position as a quiet leaf.'
                    : `The server could not load the net: ${t.net.error}`,
           'terms-extra');

  const sum = el('terms-sum');
  sum.replaceChildren();
  for (const text of ['Sum', '', '', signed(t.sum)]) {
    const cell = document.createElement('span');
    cell.textContent = text;
    sum.append(cell);
  }
  const ok = t.sum === t.basic && t.rows.every(r => r.split !== false);
  const check = el('terms-check');
  check.classList.toggle('bad', !ok);
  check.textContent = t.sum !== t.basic
    ? `✗ The terms add up to ${signed(t.sum)} cp, but basic_eval is ${signed(t.basic)} cp (off by ${signed(t.basic - t.sum)}).`
    : ok ? `✓ Adds up to basic_eval: ${signed(t.basic)} cp.`
         : `✗ Adds up to basic_eval, but a row's white − black is not its total (red).`;

  // piecetable()'s blend: each side's tables by the enemy's material left.
  const share = n => `${Math.round(100 * n / t.phase.max)}% opening`;
  el('terms-phase').textContent =
    `Phase (piece tables): white's ${share(t.phase.white)} (black's material ${t.phase.white}/${t.phase.max}), ` +
    `black's ${share(t.phase.black)} (${t.phase.black}/${t.phase.max}); the rest endgame.`;
}

// ------------------------------------------------------------------- settings

// The gear's switches. The server holds them, so this only draws them; a second
// tab on the same game sees a switch flipped here as it sees a move.
function renderSettings(s) {
  el('set-evalbar').checked = s.settings.evalBar;
  el('set-engine-line').checked = s.settings.engineLine;
  el('set-plans').checked = s.settings.dreamer;
  el('set-eval-terms').checked = s.settings.advanced;
  // Without tables there is nothing to switch, and the note says why.
  const st = s.settings;
  el('set-tb').checked = st.tb && st.tbAvailable;
  el('set-tb').disabled = !st.tbAvailable;
  el('set-tb-limit').value = String(st.tbLimit);
  el('set-tb-limit').disabled = !st.tbAvailable;
  for (const option of el('set-tb-limit').options)
    option.disabled = Number(option.value) > st.tbMaxPieces;
  el('set-tb-note').textContent = st.tbAvailable
    ? 'Exact results from Syzygy tables' + (st.tbDtm ? ', and the mate in N from Gaviota DTM tables' : '')
      + ', shown at once and given to the engines. Same lifetime as the two above.'
    : st.tbReason;
  el('set-nne').checked = st.nne;
  // Which build the engine is (#65): only a TT_BOUNDS_NEVER_NARROW=0 build can
  // narrow, and "deeper" is a step on top of narrowing.
  el('set-narrow').checked = st.narrow;
  el('set-narrow').disabled = !st.narrowAvailable;
  el('set-narrow-deeper').checked = st.narrowDeeper;
  el('set-narrow-deeper').disabled = !st.narrowAvailable || !st.narrow;
  el('set-narrow-note').textContent = !st.engineKnown
    ? 'Engine build unknown: it did not answer at startup.'
    : st.narrowAvailable
      ? 'Narrowing build (TT_BOUNDS_NEVER_NARROW=0): a stored bound may narrow alpha and beta, from a search of the same depth or, with the second switch, a deeper one. Switching restarts the analysis.'
      : 'Default build (TT_BOUNDS_NEVER_NARROW): a stored bound only cuts, it never narrows the window.';
}

function openShare(open) {
  if (open) openSettings(false);
  el('share').hidden = !open;
  el('share-toggle').setAttribute('aria-expanded', open ? 'true' : 'false');
  el('share-toggle').classList.toggle('active', open);
}

function openSettings(open) {
  if (open) openShare(false);
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
  events.addEventListener('error', () => {
    setLink('reconnecting…', 'down');
    el('audience').hidden = true;   // a count from before the break may be stale
  });
  events.addEventListener('audience', event => showAudience(JSON.parse(event.data)));
}

// The pages open on this server, this one included, in every session (#68).
// The remote ones are those that came through the tunnel's link or the LAN.
function showAudience({ pages, remote }) {
  const box = el('audience');
  box.hidden = false;
  box.classList.toggle('remote', remote > 0);
  box.textContent = remote > 0 ? `${pages} online · ${remote} remote` : `${pages} online`;
  box.title = `${pages} page${pages === 1 ? '' : 's'} open on this server`
    + (remote > 0 ? `, ${remote} through the remote link` : ', all on this machine');
}

function setLink(text, kind) {
  el('link').textContent = text;
  el('link').className = kind;
}

// The narrow stacked layout in style.css; keep this query in step with it.
const stacked = window.matchMedia('(max-width: 860px)');
const BOARD_COLUMN_MAX = 620;   // .board-column's max-width in style.css

function fitBoard() {
  // chessground needs a pixel size, and a whole number of pixels per square
  // keeps the piece SVGs from shimmering. The frame is what gets the size, not
  // the board inside it, so the promotion overlay stays on the squares too.
  const frame = document.querySelector('.board-frame');
  const fit = document.querySelector('.board-fit');
  const height = boardRoom();
  // When the height decides the board's size, the column is cut down to the
  // board (#68), so the room it would leave empty goes to the side column
  // rather than sitting between the board and the engine line.
  const column = document.querySelector('.board-column');
  const width = stacked.matches ? ''
    : `${Math.min(BOARD_COLUMN_MAX, Math.max(256, Math.floor(height / 8) * 8) + column.clientWidth - fit.clientWidth)}px`;
  if (column.style.maxWidth !== width) column.style.maxWidth = width;
  const room = Math.min(fit.clientWidth, height);
  const size = `${Math.max(256, Math.floor(room / 8) * 8)}px`;
  if (frame.style.width !== size) {         // otherwise the observer hearing our own change
    frame.style.width = frame.style.height = size;
    board.redrawAll();
  }
  alignStrips(frame);
}

// The material and clock strips span the board, not the column: when the height
// decides the board's size the column is wider than it, and a clock out at the
// column's edge would float off to the right of the board. The eval bar coming
// or going moves the board too, and the observer on .board-fit hears that.
function alignStrips(frame) {
  const column = document.querySelector('.board-column').getBoundingClientRect();
  const board = frame.getBoundingClientRect();
  for (const strip of [el('strip-top'), el('strip-bottom')]) {
    strip.style.marginLeft = `${board.left - column.left}px`;
    strip.style.width = `${board.width}px`;
  }
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
  return column.clientHeight - outerHeight(el('status')) - outerHeight(el('strip-top')) - outerHeight(el('strip-bottom'));
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
