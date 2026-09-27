// OWNERSHIP=Claude
// The page. It draws whatever state the server sends and asks the server about
// every move; it decides no chess itself, so there is no position here to get
// out of step with the engine's.
//
// State arrives two ways: a GET on load for an immediate first paint, and an SSE
// stream that pushes every later change. In Play mode that stream is also how
// the engine's thinking and its move arrive, and in Analyse mode every
// iteration of the running search: the page never waits for a search, it is
// simply sent a new state whenever there is one.
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
el('resign').addEventListener('click', () => command('/api/resign', {}));

el('sides').addEventListener('click', event => {
  const button = event.target.closest('button');
  if (button) command('/api/play', { side: button.dataset.side });
});

el('limit-form').addEventListener('submit', event => {
  event.preventDefault();
  command('/api/play', { kind: el('limit-kind').value, value: Number(el('limit-value').value) });
});

el('fen-form').addEventListener('submit', event => {
  event.preventDefault();
  command('/api/fen', { fen: el('fen').value.trim() });
});

el('copy-fen').addEventListener('click', () => {
  el('fen').select();
  navigator.clipboard?.writeText(el('fen').value);
  showMessage('FEN copied.');
});

el('analysis-toggle').addEventListener('click', () => {
  if (state) command('/api/analyse', { on: !state.analysis.on });
});

el('analysis-line').addEventListener('click', event => {
  const button = event.target.closest('.pv-move');
  if (button) command('/api/line', { moves: button.dataset.line });
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
           'Turn the engine on to see its eval and its best line for whatever is on the board.',
  play:    'You against Ascaniusfish. Load a FEN to start from a position of your own.',
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
  const playing = s.mode === 'play';
  const over = s.outcome.state !== 'ongoing';
  // In Play mode only your own pieces may move, and only while the engine is
  // not thinking — a move made mid-search would be answered for the wrong
  // position. Elsewhere both colours are free.
  const movableColor = over ? undefined
                      : playing ? (s.play.thinking ? undefined : s.play.humanColor)
                      : 'both';

  board.set({
    fen: s.fen.split(' ')[0],
    orientation: s.orientation,
    turnColor: s.turn,
    lastMove: s.lastMove || null,
    check: s.check || false,
    movable: { color: movableColor, dests: movableColor ? destsMap(s.dests) : new Map() },
  });

  for (const button of el('modes').children)
    button.classList.toggle('active', button.dataset.mode === s.mode);
  el('mode-note').textContent = MODE_NOTES[s.mode] || '';

  el('status').textContent = statusLine(s);
  el('status').classList.toggle('over', over);
  el('undo').disabled = (s.ply === 0 && !s.play.resigned) || s.play.thinking;
  el('resign').hidden = !playing;
  el('resign').disabled = over;

  renderPlayPanel(s);
  renderAnalysis(s);

  // Leave a FEN the user is in the middle of typing alone.
  if (document.activeElement !== el('fen')) el('fen').value = s.fen;

  renderHistory(s.history);
  if (pendingPromotion) el('promotion').hidden = false;
}

// ------------------------------------------------------------------ play panel

function renderPlayPanel(s) {
  const play = s.play;
  el('play-panel').hidden = s.mode !== 'play';

  for (const button of el('sides').children)
    button.classList.toggle('active', button.dataset.side === play.side);

  // Don't fight a value being typed or a select being opened.
  if (document.activeElement !== el('limit-kind')) el('limit-kind').value = play.limit.kind;
  if (document.activeElement !== el('limit-value')) el('limit-value').value = play.limit.value;

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

// ------------------------------------------------------------------- analysis

function renderAnalysis(s) {
  const analysing = s.mode === 'analyse';
  const a = s.analysis;
  const search = a.search;
  const panel = el('analysis-panel');

  panel.hidden = !analysing;
  el('evalbar').hidden = !analysing;
  if (!analysing) return;

  panel.classList.toggle('running', a.running && !a.error);
  el('analysis-toggle').textContent = a.on ? 'Turn engine off' : 'Turn engine on';
  el('analysis-score').textContent = search ? scoreText(search.score) : '';

  el('analysis-stats').textContent = analysisStats(s);
  el('analysis-stats').classList.toggle('bad', !!a.error);

  // The bar turns over with the board, and says nothing at all until there is
  // a score for this very position — a stale eval would be worse than none.
  const bar = el('evalbar');
  bar.classList.toggle('flipped', s.orientation === 'black');
  bar.classList.toggle('idle', !search || !search.score);
  bar.querySelector('.evalbar-fill').style.height =
    `${(search && search.score ? whiteShare(search.score) : 0.5) * 100}%`;

  renderLine(s, search);
}

function analysisStats(s) {
  const a = s.analysis;
  if (a.error) return `Engine: ${a.error}`;
  if (!a.on) return 'The engine is off.';
  if (s.outcome.state !== 'ongoing') return 'The game is over — nothing to search.';
  const search = a.search;
  if (!search) return 'Starting the search\u2026';
  const nps = search.nps ? `${Math.round(search.nps / 1000).toLocaleString()} knps` : '';
  return [`depth ${search.depth}`, `${search.nodes.toLocaleString()} nodes`, nps]
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

// The best line, numbered from the position on the board. Each move is a
// button: clicking it plays the line up to and including that move.
function renderLine(s, search) {
  const box = el('analysis-line');
  box.replaceChildren();
  if (!search) return;
  let fullmove = s.fullmove;
  let white = s.turn === 'white';
  search.line.forEach((move, ply) => {
    if (white || ply === 0) {
      const number = document.createElement('span');
      number.className = 'move-number';
      number.textContent = white ? `${fullmove}.` : `${fullmove}\u2026`;
      box.append(number, ' ');
    }
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'pv-move';
    button.dataset.line = search.line.slice(0, ply + 1).map(m => m.uci).join(' ');
    button.textContent = move.san;
    button.title = move.uci;
    box.append(button, ' ');
    if (!white) fullmove++;
    white = !white;
  });
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

function renderHistory(history) {
  const list = el('history');
  list.replaceChildren();
  for (let i = 0; i < history.length; i += 2) {
    const item = document.createElement('li');
    item.value = 1 + i / 2;
    for (const move of history.slice(i, i + 2)) {
      const span = document.createElement('span');
      span.textContent = move.san;
      // An engine move carries what its search found: eval and depth reached.
      if (move.note) {
        const note = document.createElement('em');
        const score = scoreText(move.note.score);
        note.textContent = `${score}/${move.note.depth}`;
        note.title = `${move.note.nodes} nodes in ${move.note.time} ms`;
        span.append(' ', note);
      }
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
