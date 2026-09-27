// OWNERSHIP=Claude
// The page. It draws whatever state the server sends and asks the server about
// every move; it decides no chess itself, so there is no position here to get
// out of step with the engine's.
//
// State arrives two ways: a GET on load for an immediate first paint, and an SSE
// stream that pushes every later change (so a second tab on the same session
// follows along, and the later Play/Watch modes need no new plumbing).
import { Chessground } from './vendor/chessground.min.js';

const sessionId = new URLSearchParams(location.search).get('id') || 'main';
const el = id => document.getElementById(id);

let state = null;              // last state the server sent
let pendingPromotion = null;   // {orig, dest} while the chooser is open

const board = Chessground(el('board'), {
  orientation: 'white',
  coordinates: true,
  autoCastle: true,            // move the rook along locally; the server confirms it anyway
  highlight: { lastMove: true, check: true },
  animation: { enabled: false, duration: 180 },   // the first state is a jump, not a move
  draggable: { showGhost: true },
  movable: { free: false, color: 'both', showDests: true, events: { after: onUserMove } },
  drawable: { enabled: true },
});

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

// ---------------------------------------------------------------- user actions

function onUserMove(orig, dest) {
  if (state && state.promotions.includes(orig + dest)) {
    pendingPromotion = { orig, dest };
    el('promotion').hidden = false;
    return;
  }
  command('/api/move', { uci: orig + dest });
}

function finishPromotion(piece) {
  const move = pendingPromotion;
  pendingPromotion = null;
  el('promotion').hidden = true;
  if (!move) return;
  if (!piece) return render(state);   // cancelled: put the pawn back
  command('/api/move', { uci: move.orig + move.dest + piece });
}

el('promotion').addEventListener('click', event => {
  const button = event.target.closest('button');
  if (button) finishPromotion(button.dataset.piece);
});

el('new').addEventListener('click', () => command('/api/reset', {}));
el('undo').addEventListener('click', () => command('/api/undo', {}));
el('flip').addEventListener('click', () => command('/api/flip', {}));

el('fen-form').addEventListener('submit', event => {
  event.preventDefault();
  command('/api/fen', { fen: el('fen').value.trim() });
});

el('copy-fen').addEventListener('click', () => {
  el('fen').select();
  navigator.clipboard?.writeText(el('fen').value);
  showMessage('FEN copied.');
});

el('modes').addEventListener('click', event => {
  const button = event.target.closest('button');
  if (button) command('/api/mode', { mode: button.dataset.mode });
});

document.addEventListener('keydown', event => {
  if (event.key === 'Escape' && pendingPromotion) finishPromotion('');
});

// --------------------------------------------------------------------- drawing

const MODE_NOTES = {
  analyse: 'Free play: move for both sides, take moves back, set up any position. ' +
           'Engine analysis arrives in a later issue.',
  play:    'Play against Ascaniusfish is not wired up yet — the board below is still free play.',
  watch:   'Watching Ascaniusfish play itself is not wired up yet — the board below is still free play.',
};

const OUTCOME_TEXT = {
  white_wins: 'White wins',
  black_wins: 'Black wins',
  draw: 'Draw',
};

function apply(next) {
  const first = state === null;
  state = next;
  render(next);
  // Animating from chessground's default start position into the real one on
  // page load looks like a move that never happened; animate from here on.
  if (first) board.set({ animation: { enabled: true } });
}

function render(s) {
  board.set({
    fen: s.fen.split(' ')[0],
    orientation: s.orientation,
    turnColor: s.turn,
    lastMove: s.lastMove || null,
    check: s.check || false,
    movable: { color: 'both', dests: destsMap(s.dests) },
  });

  for (const button of el('modes').children)
    button.classList.toggle('active', button.dataset.mode === s.mode);
  el('mode-note').textContent = MODE_NOTES[s.mode] || '';

  el('status').textContent = statusLine(s);
  el('status').classList.toggle('over', s.outcome.state !== 'ongoing');
  el('undo').disabled = s.ply === 0;

  // Leave a FEN the user is in the middle of typing alone.
  if (document.activeElement !== el('fen')) el('fen').value = s.fen;

  renderHistory(s.history);
  if (pendingPromotion) el('promotion').hidden = false;
}

function destsMap(dests) {
  return new Map(Object.entries(dests));
}

function statusLine(s) {
  if (s.outcome.state !== 'ongoing')
    return `${OUTCOME_TEXT[s.outcome.state]} — ${s.outcome.reason}`;
  const side = s.turn === 'white' ? 'White' : 'Black';
  const check = s.check ? ', in check' : '';
  return `${side} to move (move ${s.fullmove}${check}) — ${s.legalMoves.length} legal moves`;
}

function renderHistory(history) {
  const list = el('history');
  list.replaceChildren();
  for (let i = 0; i < history.length; i += 2) {
    const item = document.createElement('li');
    item.value = 1 + i / 2;
    for (const move of history.slice(i, i + 2)) {
      const span = document.createElement('span');
      span.textContent = move.san;
      item.append(span);
    }
    list.append(item);
  }
  list.scrollTop = list.scrollHeight;
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
  events.addEventListener('open', () => setLink('live', 'live'));
  events.addEventListener('error', () => setLink('reconnecting…', 'down'));
}

function setLink(text, kind) {
  el('link').textContent = text;
  el('link').className = kind;
}

function fitBoard() {
  // chessground needs a pixel size, and a whole number of pixels per square
  // keeps the piece SVGs from shimmering.
  const frame = document.querySelector('.board-frame');
  const size = Math.max(256, Math.floor(frame.clientWidth / 8) * 8);
  el('board').style.width = el('board').style.height = `${size}px`;
  board.redrawAll();
}

new ResizeObserver(fitBoard).observe(document.querySelector('.board-frame'));

setLink('connecting…', 'down');
fetch(`/api/state?id=${encodeURIComponent(sessionId)}`)
  .then(res => res.json())
  .then(apply)
  .catch(() => showMessage('The engine server is not answering. Is ascaniusfish_gui still running?'))
  .finally(connect);
