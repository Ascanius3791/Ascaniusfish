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

function onUserMove(orig, dest) {
  if (state && state.promotions.includes(orig + dest)) {
    pendingPromotion = { orig, dest };
    openPromotion();
    return;
  }
  command('/api/move', { uci: orig + dest });
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
    piece.className = `${role} ${state.turn}`;
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
  command('/api/move', { uci: move.orig + move.dest + piece });
}

el('promotion').addEventListener('click', event => {
  const piece = event.target.closest('piece');
  finishPromotion(piece ? piece.dataset.piece : '');
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

el('nav').addEventListener('click', event => {
  const button = event.target.closest('button');
  if (button) nudge('/api/nav', { where: button.dataset.nav });
});

el('moves').addEventListener('click', event => {
  const button = event.target.closest('.move');
  if (button) command('/api/goto', { node: Number(button.dataset.node) });
});

el('promote').addEventListener('click', () => command('/api/promote', {}));
el('delete-move').addEventListener('click', () => command('/api/delete', {}));

el('pgn-form').addEventListener('submit', event => {
  event.preventDefault();
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
  // A shortcut must never fire into a FEN or a PGN being typed: there Home,
  // End and the arrows are the text field's own.
  if (typingSomewhere() || event.ctrlKey || event.metaKey || event.altKey) return;
  const where = NAV_KEYS[event.key];
  if (!where) return;
  event.preventDefault();
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
  el('undo').disabled = (!s.tree.canBack && !s.play.resigned) || s.play.thinking;
  el('resign').hidden = !playing;
  el('resign').disabled = over;

  renderPlayPanel(s);
  renderAnalysis(s);

  // Leave a FEN or a PGN the user is in the middle of typing alone.
  if (document.activeElement !== el('fen')) el('fen').value = s.fen;
  if (document.activeElement !== el('pgn')) el('pgn').value = s.pgn;
  el('download-pgn').href = `/api/pgn?id=${encodeURIComponent(sessionId)}`;

  renderMoves(s);
  if (pendingPromotion) openPromotion();
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
  box.querySelector('.move.current')?.scrollIntoView({ block: 'nearest' });
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
