<!-- OWNERSHIP=Claude -->
# Browser GUI

How `./gui/ascaniusfish_gui` (`make gui`, `gui/`) is built. Read before working in `gui/`. Keep it in sync with the code there.

## Files
`./gui/ascaniusfish_gui` (`make gui`, entry `gui/gui_server.cpp`) serves a chess board on
`http://localhost:8173` and is a **separate executable** — it does not use `./ascaniusfish`,
`nicely_written_play()` or `display_board.py`. Options are `key=value`: `port=`, `root=`, `fen=`.

- `gui/http_server.hpp` — the slice of HTTP this needs on plain sockets (no third-party library):
  GET static files, POST JSON, and SSE streams held open across one `poll()` loop, so the server
  can push a new position to every page watching a session. Binds `127.0.0.1` only.
- `gui/move_tree.hpp` — the game as a tree with a cursor, and PGN both ways. The linear `Game`
  in `tools/game_rules.hpp` stays as it is (`tools/match.cpp` and `tools/gui_match.cpp` use it);
  this is the shape a game being *looked at* needs. A `Tree_Node` holds the move, its SAN, the
  position after it, the repetition key, the halfmove clock along *this path*, a `Move_Note`
  eval slot and a PGN comment; `children[0]` is the main line and the rest are side lines.
  Node ids index one pool and are **never reused** — a deleted node stays in it marked dead, so
  an id the browser drew before a deletion is refused rather than meaning another move. `Nav`
  is the cursor's six moves (back/forward/start/end, and prev/next between siblings).
  `load_pgn()` reads a move by naming every legal move with the same `san()` the exporter uses
  and comparing, so import and export cannot drift apart; it handles nested variations, NAGs,
  `{}`/`;` comments and a `FEN`/`SetUp` tag. An engine move's `Move_Note` is exported as the
  comment `{+0.35/7 10.00s}`, the same shape `tools/gui_match.cpp` writes, so a downloaded game
  keeps what the searches found; the note also keeps that search's PV (`Move_Note::pv`, capped
  at `MOVE_NOTE_PV`, in UCI **from the position the move was played in**, so `pv[0]` is the move
  itself) — that is what the engine-line box shows when you step back onto the move, and it
  belongs to the move rather than to a position because the `Analysis_Store` is keyed by
  position and a later analysis of the same one replaces it. It is not exported to PGN; a comment read back is collapsed to one line, or the newline
  the exporter's wrapping put inside it would re-wrap the next export differently. `diagnostics/move_tree_pgn_test.cpp` covers all of
  it, including a Lichess-shaped study PGN.
- `gui/session.hpp` — a `Session` is one game (a `Move_Tree`), a `Mode` and a board orientation;
  `Sessions` maps ids to them, so two browser tabs can hold two independent games.
  **"The position" is the cursor, not the end of the game**: legal moves, FEN, the result, the
  repetition count and the moves handed to the engine all follow it, which is what makes the
  analysis search follow it too. `state_json()` is the one thing the page renders from: FEN,
  `dests` per square, `promotions`, legal moves with SAN, the whole tree flat (every live node
  with its parent, children, SAN, move number and engine note) plus `cursor` and the
  `can{Back,Forward,Prev,Next,Promote}` flags, the game as PGN, `Outcome`, and the Play, Watch
  and Analysis panels' state. Play adds the resolved human colour, the `Go_Limits`,
  a resignation and whether a search is running; Watch adds a `Go_Limits` per side, whether the
  self-play game is running or stepping, and which side is thinking; Analyse adds only the
  toggle, whether a search is running and the engine's error. What a search *found* is not in
  any of the three: `eval` is the one score and line the page draws, in every mode, and
  `eval_view()` picks it (#25). A search running now wins, since it is about the position on the
  board; otherwise the mode says what is being looked at — in Analyse the position, so the best
  result kept for it wins, and in Play and Watch the move you are on, so the search that chose it
  does. `source` is `live`/`stored`/`move`, or `none` for a position nothing has searched, which
  is what makes the bar show nothing rather than 0.00. The score is **in white's view** whichever
  side moved (a bar that flipped with the mover would swing a board width every move), and so is
  every other score on the page, the Play and Watch panels' thinking indicator included. `settings` carries the
  gear's two switches (the eval gauge, the engine-line box); they live in the `Session` like the
  board orientation, so a reload and a second tab agree, and they are never written to a file.
  `diagnostics/eval_view_test.cpp` covers the choosing. A session
  runs at most one search — in Watch mode the two sides think in turn, never together — and
  `Search_Kind` says how to read its answer: a `PLAY` or `WATCH` search ends in a move on the
  board, an `ANALYSIS` search only in a line to look at. Every position change
  calls `clear_analysis()` *before* the page is told, so no frame can carry the previous
  position's eval; that call also files what the analysis reached in the
  `Analysis_Store` and puts the new position's kept result up, so `analysis` is either the
  live search's or a kept one and never nothing that has been looked at. `analysis_stored`
  says which, and `analysis_live_depth` how far the live search has got (both go to the page
  as `stored`/`liveDepth`). The live search's first iteration replaces a kept result however
  deep, so the page shows the engine rebuilding after a path refresh (#100); only a kept mate
  stays up until the search has one too (`set_analysis()`). `game_serial()` counts the games the session has held — it changes on a
  reset, a FEN or a loaded PGN and never on a move — which is what decides whether an engine
  needs a `ucinewgame` before it is asked anything.
- `gui/analysis_store.hpp` — what the analysis has already found in this game, so coming back
  to a position shows its eval, depth and line at once instead of an empty bar (#24). Fixed
  1024 slots, direct-mapped and **keyed by the position** (`Position_Key`, not a node id), so a
  transposition or the same position down a side line is the same entry and an entry can only
  ever be handed back for the position it was made in. A mate beats a score for one position, otherwise deeper wins and a fresh result of
  equal depth replaces the kept one (`covers()`, #100), and any
  other position simply takes the slot; a new game, a FEN or a loaded PGN clears the lot
  (`Session::forget_analysis()`). Memory only. `diagnostics/analysis_store_test.cpp` covers it.
- `gui/ptt.hpp` — the **PTT** (persistent TT, #79), its sibling on disk, like Lichess's cloud eval:
  every search of ≥5 s (`PTT_MIN_MS`) at MultiPV 1 of a position on the board, in any mode
  (`Session::ptt_offer()`), as one tab-separated `key=value` line of `~/.ascaniusfish/ptt.txt`
  (`ptt=<file>`, `ptt=none` off; outside the repo so every worktree's GUI shares it): the FEN with
  clocks, depth, score, nodes, time, line, and the provenance the engine reports before each
  search (`info string provenance evalversion N weights W nne <net hash|off> syzygy N gaviota N commit C`).
  Append-only under `flock`, re-read when another GUI appends, compacted on open. Keyed by the
  position **and its halfmove clock** (`PTT_Key`): an entry counts only at the clock it was found
  at. Per key a newer eval version beats an older one; within one a mate beats a score, then deeper wins. "Current" = the
  engine's `EVAL_VERSION` (`lib/engine_version.hpp`, read at startup by `engine_build()`), its
  weight set (an entry without one is set 1) and the session's net; anything else is shown with
  `older` and never seeds or plays. Since the engine scores a return to any earlier game position as a draw, the history since the last zeroing move
  is cut out brutally, both ways: nothing is kept, and no entry is read (`Session::ptt_entry()`),
  where a position since then has been on the board twice or the line runs back into one of them;
  nor is a search within reach of the 50-move rule kept. In Analyse a kept entry shows at once
  (`recall_analysis()`, also at a new game's start), with a `+` that turns the analysis on; Play
  and Watch play a current entry found in at least the move's soft time (or as deep as a fixed
  depth) at once, and every other current entry seeds the search as UCI `hint depth D pv …`
  (`PTT_SEED`, measured by `tools/ptt_seed`). The Engine panel's `PTT` checkbox
  (`Session::ptt_use`, `pttUse` in `/api/settings`) turns all reading off for the session;
  storing goes on. `diagnostics/ptt_test.cpp` covers it. Beside it, the `Transp.` checkbox
  (`Session::routes_on`, `routes` in `/api/settings`, off by default, #100) sends an analysis
  the tree's other routes to the positions on the cursor's path (`transposition_routes()`, the
  engine's `route` lines), so their old entries are refreshed too.
- `gui/engine_link.hpp` — the UCI client of Play, Watch and Analyse mode: a `Go_Limits` (depth,
  movetime, or `Go_Limits::analysis()` = `go infinite` for Analyse;
  `wtime`/`btime`/`winc`/`binc` fields already there for M4), a `Search_Request`, and an
  `Engine_Link` that drives one `./ascaniusfish_uci` (`tools/uci_engine.hpp`) on a worker
  thread. The worker touches no `Session` and no socket: it calls `on_update()`, which only
  does `Http_Server::wake()` (a self-pipe), and the poll loop's `on_tick` picks the answer up.
  So a search never blocks a request — page loads, `flip`, `undo` and mode switches are all
  answered while the engine thinks, and every iteration's depth/score is pushed over SSE.
  `gui_server.cpp` keeps up to **four** links per session (`Engine_Set`): one for Play and
  Analyse, one per side for Watch, the root engine of Correspondence mode (`SLOT_ROOT`, #101),
  each started on the first search it is asked for. **Pure
  watch** (#91, the Watch panel's checkbox, `Session::watch_pure`, `pure` in `/api/watch`, on
  by default) has the white link play both sides, so each search starts from the TT the other
  side just filled (safe: engine settings are per session, only `Go_Limits` differ per side);
  off is tournament mode, a process per side. `tools/match`/`tools/gui_match` always keep one
  process per side. `tools/watch_tt` measures the difference
  (`docs/measurements/watch_shared_tt_2026-10-04.md`). A link the
  session's mode cannot use is let go on the next tick, and so is every link of a session no
  page has been watching for 5 s — the self-play game is paused and the analysis toggle goes
  off first. Each process holds ~140 MB of tables, so leaving an idle pair around would eat the
  room this repo keeps for running several engines at once.
- `gui/tablebase_view.hpp` — the server links `lib/syzygy.hpp` itself (#40), so a position with ≤ limit
  pieces shows its exact result at once, with the analysis off. `./gui/ascaniusfish_gui syzygy=<dir>`
  (`make gui SYZYGY=<dir>`) opens the tables once for every session (`tablebase_setup()`); without it,
  or with no tables in it, the gear's switch is disabled and `settings.tbReason` says why. Per session
  `tb_on`/`tb_limit` (3–5) live in the `Session` and go to every engine as `setoption SyzygyPath`
  (`<empty>` when off) and `SyzygyProbeLimit`, sent by `Engine_Link` (`Search_Request::options`, only
  what that process does not hold yet) before the next `position`/`go`. `eval_view()` puts the tables
  first: `source` is `tb`, the score `{kind:"tb", value: white's WDL -2..2, dtz}`. In Analyse the state
  also carries `tbMoves` (every legal move, best first, result for the mover; clicking one plays it).
  `diagnostics/eval_view_test.cpp` covers it (needs `~/syzygy-nr` or argv[1]).
  With `gaviota=<dir>` (`make gui`, on by default when `~/gaviota` exists, `GAVIOTA=none` off) the
  server also links the Gaviota prober (#77) and adds to the Syzygy result, under the same switch and
  limit: `dtm_view()` gives the mate distance, every legal move's, and the mating line (quickest mate,
  longest resistance, 50-move rule ignored), worked out once per position (`Session::dtm()`, a cache:
  the line is ~2000 probes). The score gets `mate` (white's, in moves; the page shows `#N` for a
  plain win, a cursed win stays a result), `eval.line` is the mating line, `tbMoves` carry the mover's
  `mate` and are ranked by it within a result, and the engines get `GaviotaTbPath`.
- `gui/eval_split.hpp` — `basic_eval()` rebuilt for the gear's two debugging switches, both off
  by default and per session (#66). **Advanced debugging** (`advanced`) puts the static eval of
  the position on the board in a column of its own (≥1340px window), each term per side with its
  parts (hover: which piece hit which square), and runs `eval_self_check()` when switched on: the
  result sits at the top of the column, and a mismatch names the term (or says a term was added to
  or dropped from `basic_eval()` when every term matches but the sum does not); it is also printed
  on the server's stderr. **Dreamer** (`dreamer`) shows the plan term: each piece's dream square
  and path on the board, a list, and a row of its own in the breakdown, outside the sum.
  With the gear's NNE switch on (#67) the server loads `nets/nne_d6.bin` itself (`gui_net()`):
  the breakdown gets a net-correction row (also outside the sum) and basic_eval + net, the
  search's quiet-leaf score, under its header; the dreamer's db then counts the change of the
  net's correction too (`plan_eval_detail(..., with_nne)`, one net evaluation per target), and
  the self-check runs with it, plus the net's incremental first layer against a from-scratch one.
  The state carries them as `evalTerms` and `plans`; `Session::write_eval_terms()`/`write_plans()`.
- `gui/json.hpp` — a JSON writer that inserts the commas, plus a flat-object parser for request bodies.
- `gui/web/` — `index.html`/`app.js`/`style.css`/`sound.js` and the `logo.png`/`favicon.png` (scaled
  down from the root's `Ascaniusfish.png`) are ours; `gui/web/vendor/` holds chessground
  (GPL-3, see its `README.md`), upstream's own prebuilt ESM bundle plus CSS with the board and
  all piece images as `data:` URIs. Nothing is fetched from the network and no Node is involved.
  **The page is exactly the window and never scrolls**: the board must not move when a move is
  played, so `body` is `overflow: hidden`, the side column and the move list are each their own
  scroll box, and everything whose text grows while the engine thinks (the status line, the
  engine line, the analysis stats, the best line) is held to a fixed height. `fitBoard()` sizes
  the board from the width the row gives it *and* the height the column has left — nothing can
  be scrolled to, so anything that does not fit is simply gone. Don't use `scrollIntoView()`
  here: it scrolls every scrollable ancestor, the document among them (#22). Above and below the
  board sit two fixed-height `.player-strip`s, one per side: lichess's material diff (counted from
  the FEN, drawn with chessground's own piece images at a small size) and the clock chip.
  `fitBoard()` lines them up with the board rather than the column, and when the height decides
  the board's size it cuts the column down to the board, so the side column (300–800px) gets
  the rest (#68). Queued premoves are marked by
  chessground's `current-premove` square colour (`highlight.custom`), not by arrows. The header
  is the shield's pewter, the shield lit behind; left of `live` it counts the pages open on the server, every session, and
  how many came through the tunnel or the LAN (`Http_Server::audience()`, an `audience` SSE
  event sent to every stream on a change, #68).

## Requests and server state
**A Cloudflare quick tunnel does not carry SSE** reliably: it holds the start of a stream back, the
first ~256 KB in bursts seconds apart (a lone state can wait until the stream ends), so a remote
player saw the engine's move only on a reload until enough traffic had passed; padding sent up
front does not release it. A page on a `*.trycloudflare.com` host,
or one whose stream sends nothing in its first 3 s (the server sends the state the moment a stream
opens), long-polls instead: `GET /api/wait?id=&after=n&page=` is held in `Http_Server` until the
session publishes a state past `n` (at once if it already has, after 20 s with the last one) and
answered as `{"n":…, "state":…}`, an ordinary response the tunnel passes on at once. `page` is the
page's own id, so it counts in `subscribers()` (its engines are not let go) and in `audience()`
between one answer and its next wait (`POLLER_GRACE_MS`).

**The server owns the position and the page owns nothing** — reloading the browser is just another
`GET /api/state`. All chess logic stays in C++; the JS is presentation only. Routes:
`GET /api/state`, `GET /api/events` (SSE), `GET /api/wait` (the same states as a long poll),
`POST /api/{move,fen,reset,undo,resign,play,watch,mode,flip,analyse,line,settings,corr}`, all taking
`id` (default `main`). Both Play and Watch play under a Clock or a fixed depth (#21), starting on a
1+1 clock (a clock is seeded with base + increment, `Session::clock_start_ms()`, #58) (`Session::play_base_ms`/`watch_base_ms`), which is also what Custom opens with; a `kind` of
`depth` carries `value` (Watch: per side), and a `kind` of `clock` carries `baseMs`/`incMs` in
ms — presets and Custom base+increment are entirely a page-side concept (`gui/web/app.js`'s
`CLOCK_PRESETS`/`HYPERBULLET_PRESETS`), resolved to a plain base+increment before the request is
sent, so the server only ever sees one shape either way a clock was chosen. `POST /api/play`
carries the Play settings (`side` = white/black/random, plus `kind`/`value`, or `kind=clock` with
`baseMs`/`incMs` and, for an asymmetric Custom clock, `blackBaseMs`/`blackIncMs`); before the
first move that starts a new game (Random's colour is drawn afresh), but once a move is on the
board it is refused with 409 unless the game is **paused**, and then it applies in place and the
game goes on: a side change turns the board, a depth is just the new limit, and a clock gives the
side(s) named that much time again (`Session::reseed_clock()`). `POST /api/watch` carries both a
side's setting (`side` = white/black plus the same `kind` shape) and a run control (`action` =
start/pause/step); unlike Play's one call, a clock's two colours are set with two requests, one
per side, under the same paused-only rule. So both panels' settings picker is hidden once a game
is on and until it is paused (`gui/web/app.js`'s `settingsLocked()`). `POST /api/pause` (`on` =
true/false) is **Play's** pause: the engine's search is dropped (asked for again on Resume), your
moves are refused and the clock stops (`Session::paused`); Watch keeps Start/Pause. The state's top-level
`paused` is `Session::paused_now()`: Play's flag, or a Watch game not running with nothing in flight.
While paused **or over** the Engine panel's toggle works in Play and Watch too
(`Session::analysis_wanted()`, judged on the position, so a resigned or adjudicated game can still
be looked at); Resume/Start turn it off again. `POST /api/adjudicate` (`result` = white/black/draw)
ends a Play or Watch game like a resignation does, with the reason "adjudicated" (`Session::adjudicate()`);
taking a move back undoes it. Pause is deliberately **not** an
abort: the move being thought about is finished and played, which is what "after the current
move" means — and, since a clock counts through Pause for exactly that reason, `GET /api/state`'s
top-level `clock` object (`whiteMs`, `blackMs`, `running`) is live only while something is
actually ticking; the page interpolates it locally between pushes rather than being sent one every
tick (`Session::clock_sync()`/`clock_ticking()`, `gui/session.hpp`). The server itself notices a
flag falling — not just a clocked engine's own time management — on every poll iteration
(`Session::check_flag()`; `gui/http_server.hpp`'s `poll_timeout` shortens while any clock ticks),
ending the game the way a resignation does, with a `TimeControl` PGN tag alongside it.
`POST /api/settings` carries the gear's switches (`evalBar`, `engineLine`), each applied only
when the body names it, so one can be flipped without saying anything about the other.
It also takes `lines` (1–256, 256 = every legal move): the analysis's K (#69), set by the
Engine panel's Lines control and kept in `Session::analysis_lines`. Only an `ANALYSIS` search
gets `MultiPV` K (the toggle in a paused or finished game included); Play and Watch are always
sent `MultiPV 1`. `Engine_Link` gathers a depth's `multipv 1..K` lines into one `Search_Info`
(`more` = lines 2..K) and publishes it only when complete; the `Analysis_Store` keeps the lines,
and a kept result stands in for the live search only with as many lines. `eval.lines` is every
line with its score (white's view) and moves with `fen`, empty at K=1, where `eval.line` alone is drawn.
`POST /api/analyse` is the engine on/off toggle; `POST /api/line` walks the board along a
space-separated list of UCI `moves` (all of them or none), which is what clicking a move in the
analysis line does — the page sends the moves it drew rather than an index, so a deeper iteration
arriving between the draw and the click cannot play a different move. Anything that changes the
position under a running search aborts it and drops its `bestmove` by token.

## Moving around the tree
Moving around the tree: `POST /api/nav` (`where` = back/forward/start/end/prev/next, what the
arrow keys and Home/End send), `POST /api/goto` (`node` = a node id, what clicking a move sends),
`POST /api/promote` (the line through the cursor becomes the main line — the position does not
change, so a running analysis is deliberately left alone), `POST /api/delete` (the move at the
cursor and everything after it), `POST /api/pgn` (`pgn` = a game to load) and `GET /api/pgn`
(the game as a file). "Take back" (`/api/undo`) is the one destructive step among these: it
removes the move at the cursor, and in Play mode keeps going until it is your turn again. In
Play mode the engine only answers at the **end of a line**, so stepping back into the game to
look around does not set it thinking; playing a different move there makes a new end, and then
it does answer.

## Startup
Like every tool that touches movegen, `main()` must run `Zobrist`/`initialize_rand()`/`init_magics()`/
`init_sliders_attacks()` first — without them sliding attacks are garbage and `in_check()` silently
misses checks instead of crashing.

## Modes
The mode selector shows Analyse / Play / Watch / Correspondence. Analyse is free play, FEN setup and a live
`go infinite` analysis of whatever is on the board; Play is a full game against
`./ascaniusfish_uci` (`engine=` picks a different binary); Watch is Ascaniusfish against itself,
one shared process (Pure watch) or two, with a depth or movetime each, started/paused/stepped from the panel and reviewable
in the move tree afterwards with every move's eval and reached depth. The eval bar beside the
board and the Engine panel (the score, depth/nodes/nps and the best line in SAN) are the same two
things in **all three** modes (#25), drawn from `eval`; the gear in the header opens the settings
panel, whose two switches turn either of them off everywhere at once, and with both off Play and
Watch look exactly as they did before. The Engine panel also holds Analyse's on/off button, so
there it stays whatever the switches say. The line's moves are buttons only in Analyse, where
clicking one walks the board into it — in a game being played that would open a side line. Every move of the line is a fixed-width cell (a score sheet, so a line of fixed depth has a fixed length), and hovering one shows the position after it on a `viewOnly` chessground laid over the move list, as lichess does (#58, #68); each `eval.line` entry carries that `fen`, so the page still plays no chess. The line shows one row; a longer one gets an arrow at that row's end that opens all of it, up to 50 plies before it scrolls (`fitLine()`; open or not is the browser's `localStorage`, not the session's). Under MultiPV (#69) every row has the same arrow and opens on its own (`fitLines()`, kept in memory only). It plays only from the **end of a line**,
like Play, so stepping back into the game pauses it. The analysis toggle starts
**off**: a `go infinite` search holds a core for as long as it runs, and this repo deliberately
leaves room for several engine processes at once. `main()` ignores `SIGPIPE` — an engine that died must be a message on the page,
not the end of the server — and takes `SIGINT`/`SIGTERM` as "leave through `main()`", which
aborts every search and quits the engines rather than orphaning them.

**Correspondence mode** (#101) is Analyse on the board plus a root engine (`SLOT_ROOT`) that runs
`go infinite` on one node of the tree, the root (`Session::corr_root`, a dead node falls back to the
start), with the game's path to it. The Engine panel's "Analyse position in correspondence mode"
makes the position on the board the root and keeps the game; picking the mode directly starts a
fresh board whose start is the root, and a FEN, PGN or reset moves it there. The Analyse engine is
the **Searcher**. `POST /api/corr` takes `action`: `enter`; `save` puts the position on the board
into the root's TT, marked with the **generation** (`corr_generation`: the Returns that brought
saves, plus one, #100), with the analysis result on show (depth, score, first move; not an older
eval's) as the engine's `ttmark` (a mate only if the engine confirmed it,
`Search_Info::mate_confirmed`, since an exact mate there is a proof; an unconfirmed one goes in as
±9999 cp), after a `ttdemote`, since the root engine's unmarked entries were found without it. The
GUI keeps the list (`corr_saves`, one per position) by the engine's rule (`corr_tt_result()`,
`tt_improves_marked()`): a second save replaces the first if it is a shorter mate, a deeper exact
result, or of a newer generation over no mate, and the mark is the higher either way. The panel
warns when a save replaces a deeper one, when it is kept out, and when an older save that is no
mate lies above it on its path (`corr_older_above()`): that one was found without it and cuts
before the search gets there. `return` sends the cursor to the root; after new saves the next
generation starts (`corr_next_generation()`): the Searcher, which keeps its TT, gets `ttdemote` and
every save before its next search (`corr_searcher_prelude`; a fresh process gets every save,
`Search_Request::prelude_new`), so every search in generation g holds every mark below g. A Return
without new saves changes nothing in either engine. A new root starts the Searcher over
(`corr_searcher_fresh`), as does leaving the mode with marks in it. `update` is the **full update**:
the next generation, then every save that is no mate and still on the board, deepest first, is
searched again to its depth (`corr_update_limit()`, a `go depth`) and saved, the Searcher getting
each one, and a Return at the end; moving the board, Return or `stop-update` stops it. Every
analysis is sent `UCI_AnalyseMode`, so the engine sets a marked root's own result aside at any
limit (`docs/UCI.md`). The root is restarted for each save, since the TT is the search's
(`maybe_start_root()`/`collect_root()` in `gui_server.cpp`); the engine's `info string
ttmark…`/`ttdemote…` answers show at the panel's foot. The root search stops when the mode is left
or no page watches, and its results are not offered to the PTT.
