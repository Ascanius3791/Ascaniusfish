// OWNERSHIP=Claude
// The live clock behind issue #21: Session::clock_sync()/check_flag() charge
// and freeze it, gui_server.cpp calls them around every clock-relevant
// request. What can go quietly wrong is all of it silent — a review action
// burning the wrong side's time, a flag never falling, a resumed clock
// starting from the wrong instant, an engine allowed to hang for the other
// side's whole remaining time — so it is checked here with synthetic
// timestamps rather than by watching a real clock in the browser.
//
//   g++ -O3 -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable
//       -DNDEBUG -o diagnostics/clock_test diagnostics/clock_test.cpp
//
// Run it from the repo root. No engine process and no real clock: every call
// here is fed an explicit `now`, the same way gui_server.cpp feeds now_ms().
#include "../gui/session.hpp"
#include <cstdio>
#include <string>

static int failures = 0;

static void check(bool ok, const std::string& what, const std::string& detail = "")
{
    std::printf("  %-58s %s%s%s\n", what.c_str(), ok ? "ok" : "FAIL",
                ok || detail.empty() ? "" : "  ", ok ? "" : detail.c_str());
    failures += !ok;
}

static void check_eq(long long got, long long want, const std::string& what)
{
    check(got==want, what, "got " + std::to_string(got) + ", wanted " + std::to_string(want));
}

static void play(Session& session, const std::string& uci)
{
    std::string error;
    if(!session.play(uci, error))
    check(false, "playing " + uci, error);
}

// Mirrors a review request in gui_server.cpp: handle_post() syncs once before
// its handler (nav among them) mutates the tree, and once more moments later
// when the reply's state_json() (via broadcast()) reports the clock — the
// second sync is what actually freezes it once the mutation has left the tip,
// since the first one still has to arm using the *pre*-mutation position (nav
// itself moves the tree before ever calling abort_search()).
static void nav_at(Session& session, long long now, Nav where)
{
    session.clock_sync(now);
    std::string error;
    if(!session.navigate(where, error))
    check(false, "navigating", error);
    session.clock_sync(now);
}

// Mirrors the /api/move handler: check_flag (which syncs) before the move,
// crediting the mover's increment once it lands, then syncing once more —
// mirroring broadcast()'s state_json() call right after — which is what arms
// the new mover's clock from this instant rather than from whenever the next
// unrelated sync happens to land. That only matters the moment a clock starts
// ticking for the first time in a clocked game (issue #31: nobody's clock
// runs before the game's first move), which is exactly the transition a move
// out of the root position can cause.
static bool move_at(Session& session, long long now, const std::string& uci)
{
    if(session.check_flag(now))
    return false;
    bool mover_white = session.white_to_move();
    play(session, uci);
    session.clock_credit_increment(mover_white);
    session.clock_sync(now);
    return true;
}

// A fresh Play game on a clock, both sides the same base+increment, synced
// once at `now` — mirroring the state_json() call broadcast() makes right
// after start_play() — so the clock is actually armed rather than sitting
// unarmed the way set_start() itself deliberately leaves it.
static Session clocked_play(long long base_ms, long long inc_ms, long long now)
{
    Session session("play");
    session.mode = Mode::PLAY;
    session.play_clock_on = true;
    session.play_base_ms[0] = session.play_base_ms[1] = base_ms;
    session.play_inc_ms[0] = session.play_inc_ms[1] = inc_ms;
    session.reset();
    session.clock_sync(now);
    return session;
}

// Only the side to move loses time, and only for as long as it stays their
// move — the other side's clock is untouched. Nobody's clock runs before the
// game's first move lands (issue #31), so White's 5s of deciding it costs
// nothing, and it is Black's clock that starts the instant the move does.
static void test_ticks_only_the_mover()
{
    long long t = 1000000;
    Session session = clocked_play(60000, 0, t);
    check_eq(session.clock_remaining_ms[0], 60000, "White's clock starts at the base");
    check_eq(session.clock_remaining_ms[1], 60000, "so does Black's");

    session.clock_sync(t+5000);
    check_eq(session.clock_remaining_ms[0], 60000, "White's own clock doesn't run before the first move");
    check_eq(session.clock_remaining_ms[1], 60000, "nor does Black's");

    check(move_at(session, t+5000, "e2e4"), "White plays in time");
    session.clock_sync(t+5000+3000);
    check_eq(session.clock_remaining_ms[0], 60000, "White's never ran, so it's still at the base");
    check_eq(session.clock_remaining_ms[1], 57000, "Black's 3s were charged, from the moment White moved");
}

// Stepping back to look around must not burn the mover's clock, and time
// spent reviewing must not be charged to whoever the cursor lands on either.
static void test_freezes_while_reviewing()
{
    long long t = 1000000;
    Session session = clocked_play(60000, 0, t);
    check(move_at(session, t, "e2e4"), "White's opening move, instantly");   // Black now on the clock

    // Black thinks for 4s, then the review begins.
    nav_at(session, t+4000, Nav::START);
    check_eq(session.clock_remaining_ms[1], 56000, "Black's 4s before stepping away was charged");
    check_eq(session.clock_remaining_ms[0], 60000, "White's is untouched — it was never White's move");

    // A long look around: neither side's clock may move while off the tip.
    session.clock_sync(t+4000+50000);
    check_eq(session.clock_remaining_ms[1], 56000, "frozen through the whole review, however long");
    check_eq(session.clock_remaining_ms[0], 60000, "and White's stays exactly as it was");

    // Back to the tip, then 6s more: that time belongs to Black again — the
    // review didn't hand it to whoever the cursor passed through on the way.
    nav_at(session, t+4000+50000, Nav::FORWARD);
    session.clock_sync(t+4000+50000+6000);
    check_eq(session.clock_remaining_ms[1], 50000, "only the 6s back at the tip was charged to Black");
    check_eq(session.clock_remaining_ms[0], 60000, "White's still hasn't moved");
}

// An increment is credited to whoever just moved, once, right after their
// move lands — not before, and not to the side about to move next. Taking
// 10s over the opening move costs nothing (issue #31), so the increment is
// all that changes White's clock.
static void test_increment_credited_after_move()
{
    long long t = 1000000;
    Session session = clocked_play(60000, 2000, t);
    check(move_at(session, t+10000, "e2e4"), "White moves after 10s");
    check_eq(session.clock_remaining_ms[0], 62000, "base + increment, none of the 10s charged");
    check_eq(session.clock_remaining_ms[1], 60000, "Black's increment isn't credited until Black moves");
}

// A clock reaching zero ends the game exactly like a resignation: result()
// says who lost and why, and there is no move left to take back. White's own
// opening move is free (issue #31), so this plays it first and lets Black's
// clock — the one actually running — hit zero instead.
static void test_flag_ends_the_game()
{
    long long t = 1000000;
    Session session = clocked_play(10000, 0, t);
    check(move_at(session, t, "e2e4"), "White's opening move, instantly");   // starts Black's clock

    check(!session.check_flag(t+9999), "not flagged with 1ms still on Black's clock");
    check(session.check_flag(t+10000), "flagged the instant the clock reaches zero");
    check(!session.check_flag(t+20000), "check_flag reports it only once, not on every later call");
    check(session.flagged, "but the game itself stays flagged");

    std::string reason;
    Outcome outcome = session.result(reason);
    check(outcome==Outcome::WHITE_WINS, "Black was on the clock, so White wins");
    check(reason=="black loses on time", "the reason names the side and the cause");

    std::string error;
    check(!session.undo(error), "a flag fall can't be taken back");
    check(!error.empty(), "and says why");
}

// A running search is not aborted by Pause — "after the current move" means
// the clock keeps counting until that move actually lands, only freezing
// once it does (or Pause wouldn't be honouring the move already in flight).
// Starts from White's *second* move: its opening move is free (issue #31),
// which this test isn't about, so two instant plies get it out of the way.
static void test_watch_clock_survives_pause()
{
    Session session("watch");
    session.mode = Mode::WATCH;
    session.watch_clock_on = true;
    session.watch_base_ms[0] = session.watch_base_ms[1] = 30000;
    session.reset();
    play(session, "e2e4");
    play(session, "e7e5");

    long long t = 1000000;
    session.watch_running = true;
    session.searching = Search_Kind::WATCH;      // White's search is out
    session.clock_sync(t);
    check(session.clock_ticking(), "ticking while a Watch search is running");

    session.watch_pause();                       // Pause pressed mid-search
    check(session.clock_ticking(), "still ticking — the move being thought about isn't aborted");
    session.clock_sync(t+7000);
    check_eq(session.clock_remaining_ms[0], 23000, "White's think time counts right through the pause");

    // The move lands (mirrors collect_search(): check_flag while `searching`
    // still reflects the finished search, then it is cleared and the reply's
    // state_json() — via broadcast() — syncs again, which is what actually
    // freezes the clock once nothing is ticking any more).
    check(!session.check_flag(t+7000), "not flagged");
    session.searching = Search_Kind::NONE;
    session.clock_sync(t+7000);
    check(!session.clock_ticking(), "paused and no search in flight: now it really is frozen");
    session.clock_sync(t+7000+9000);
    check_eq(session.clock_remaining_ms[0], 23000, "and stays frozen once the search is done");
}

// Issue #31: neither side's clock runs before the game's first move, in
// either mode — a fresh Play or Watch game shows full time and stays there
// however long the position sits at the root, and the clock only starts the
// instant a move actually leaves it. Covered piecemeal by the moves inside
// the tests above; this checks the rule directly, including the case those
// tests can't reach — a Watch search already out for the very first move.
static void test_no_clock_before_first_move()
{
    long long t = 1000000;
    Session play_session = clocked_play(60000, 0, t);
    check(!play_session.clock_ticking(), "Play: not ticking at the root");
    play_session.clock_sync(t+30000);
    check_eq(play_session.clock_remaining_ms[0], 60000, "Play: White's clock ignores time spent at the root");
    check(move_at(play_session, t+30000, "e2e4"), "Play: White's move lands");
    check(play_session.clock_ticking(), "Play: Black's clock starts the instant it does");

    Session watch_session("watch");
    watch_session.mode = Mode::WATCH;
    watch_session.watch_clock_on = true;
    watch_session.watch_base_ms[0] = watch_session.watch_base_ms[1] = 60000;
    watch_session.reset();
    watch_session.watch_running = true;
    watch_session.searching = Search_Kind::WATCH;   // a search for the very first move is already out
    check(!watch_session.clock_ticking(), "Watch: not ticking at the root, even mid-search");
    watch_session.clock_sync(t+30000);
    check_eq(watch_session.clock_remaining_ms[0], 60000, "Watch: White's clock ignores that search's time so far");
    play(watch_session, "e2e4");
    watch_session.clock_sync(t+30000);
    check(watch_session.clock_ticking(), "Watch: Black's clock starts the instant White's move lands");
}

// The hang-detection deadline for a clocked search is the mover's own
// remaining time, not the max of both sides' — otherwise the side with the
// shorter clock could hang for as long as its opponent's whole clock.
static void test_deadline_is_mover_aware()
{
    Go_Limits limits;
    limits.wtime_ms = 5000;
    limits.btime_ms = 60000;
    check_eq(limits.deadline_ms(true)-ENGINE_GRACE_MS, 5000, "White's deadline is White's own remaining time");
    check_eq(limits.deadline_ms(false)-ENGINE_GRACE_MS, 60000, "Black's is Black's, not White's shorter one");
}

int main()
{
    // Without these, sliding attacks are garbage and in_check() quietly
    // misses checks — every tool that touches movegen starts here.
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    std::printf("gui/session.hpp: the live clock (issue #21)\n\n");
    test_ticks_only_the_mover();
    test_freezes_while_reviewing();
    test_increment_credited_after_move();
    test_flag_ends_the_game();
    test_watch_clock_survives_pause();
    test_no_clock_before_first_move();
    test_deadline_is_mover_aware();

    std::printf("\n%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
