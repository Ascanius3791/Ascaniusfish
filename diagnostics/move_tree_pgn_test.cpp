// OWNERSHIP=Claude
// Does gui/move_tree.hpp really hold a tree, and does PGN survive a round trip?
//
// The GUI's move list became a tree (issue #17), which is where a whole class
// of quiet bugs lives: a side line that eats the main line, a cursor that walks
// into a deleted node, a repetition counted across a branch instead of along
// the path, a PGN whose move numbers drift once variations are nested. None of
// those crash — they just show the wrong game — so they need checking here
// rather than by looking at the page.
//
//   g++ -O3 -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable
//       -DNDEBUG -o diagnostics/move_tree_pgn_test diagnostics/move_tree_pgn_test.cpp
//
// Run it from the repo root.
#include "../gui/move_tree.hpp"
#include <cstdio>
#include <string>

static int failures = 0;

static void check(bool ok, const std::string& what, const std::string& detail = "")
{
    std::printf("  %-58s %s%s%s\n", what.c_str(), ok ? "ok" : "FAIL",
                ok || detail.empty() ? "" : "  ", ok ? "" : detail.c_str());
    failures += !ok;
}

static void check_eq(const std::string& got, const std::string& want, const std::string& what)
{
    check(got==want, what, "got \"" + got + "\", wanted \"" + want + "\"");
}

static Move_Tree fresh()
{
    BB start;
    uci_parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", start);
    Move_Tree tree;
    tree.start(start, 0, 1);
    return tree;
}

// The SAN of the line from the root to the cursor, space separated.
static std::string line_of(const Move_Tree& tree)
{
    std::string out;
    for(int id : tree.path_ids())
    if(id>0)
    out += (out.empty() ? "" : " ") + tree.node(id).san;
    return out;
}

static void play_all(Move_Tree& tree, const std::vector<std::string>& moves)
{
    for(const std::string& uci : moves)
    if(!tree.play(uci))
    std::printf("  (internal: %s would not play)\n", uci.c_str());
}

// A move played from an earlier position must branch, not truncate.
static void test_branching()
{
    std::printf("branching\n");
    Move_Tree tree = fresh();
    play_all(tree, {"e2e4", "e7e5", "g1f3", "b8c6"});
    check_eq(line_of(tree), "e4 e5 Nf3 Nc6", "the main line is what was played");

    tree.navigate(Nav::BACK);
    tree.navigate(Nav::BACK);          // back to after 1... e5
    check_eq(line_of(tree), "e4 e5", "two steps back");
    check(tree.play("f1c4"), "a different second move plays");
    check_eq(line_of(tree), "e4 e5 Bc4", "the cursor followed it into the side line");

    tree.navigate(Nav::BACK);
    check(tree.current().children.size()==2, "the position now has two continuations");
    check(tree.node(tree.current().children[0]).san=="Nf3", "the main line is still first");
    tree.navigate(Nav::END);
    check_eq(line_of(tree), "e4 e5 Nf3 Nc6", "End walks the main line, not the side line");

    // Replaying a line already in the tree must step into it, not duplicate it.
    tree.navigate(Nav::START);
    play_all(tree, {"e2e4", "e7e5"});
    check(tree.current().children.size()==2, "replaying a known line adds nothing");
}

static void test_navigation()
{
    std::printf("navigation\n");
    Move_Tree tree = fresh();
    play_all(tree, {"e2e4", "e7e5", "g1f3"});
    tree.navigate(Nav::BACK);
    play_all(tree, {"f1c4"});           // a side line: 2. Bc4
    tree.navigate(Nav::BACK);
    play_all(tree, {"d2d4"});           // and another: 2. d4

    tree.navigate(Nav::START);
    check(tree.at_root() && !tree.navigate(Nav::BACK), "Back stops at the root");
    tree.navigate(Nav::END);
    check(tree.at_tip() && !tree.navigate(Nav::FORWARD), "Forward stops at the end of a line");
    check_eq(line_of(tree), "e4 e5 Nf3", "End is the main line's end");

    check(tree.navigate(Nav::NEXT), "Down moves to the next sibling");
    check_eq(line_of(tree), "e4 e5 Bc4", "the second continuation");
    check(tree.navigate(Nav::NEXT), "Down again");
    check_eq(line_of(tree), "e4 e5 d4", "the third continuation");
    check(!tree.navigate(Nav::NEXT), "Down stops at the last sibling");
    check(tree.navigate(Nav::PREV) && tree.navigate(Nav::PREV), "Up walks back to the first");
    check_eq(line_of(tree), "e4 e5 Nf3", "back at the main line");
    check(!tree.navigate(Nav::PREV), "Up stops at the first sibling");

    int nf3 = tree.cursor_id();
    tree.navigate(Nav::START);
    check(tree.go_to(nf3) && line_of(tree)=="e4 e5 Nf3", "clicking a move jumps to it");
    check(!tree.go_to(9999), "an id that does not exist is refused");
}

static void test_promote_and_delete()
{
    std::printf("promote and delete\n");
    Move_Tree tree = fresh();
    play_all(tree, {"e2e4", "e7e5", "g1f3", "b8c6"});
    tree.navigate(Nav::START);
    play_all(tree, {"d2d4", "d7d5"});   // a side line from the very first move
    check_eq(line_of(tree), "d4 d5", "the side line");

    check(tree.promote_to_main(), "promoting reports a change");
    tree.navigate(Nav::START);
    tree.navigate(Nav::END);
    check_eq(line_of(tree), "d4 d5", "it is the main line now");
    check(!tree.promote_to_main(), "promoting the main line changes nothing");

    tree.navigate(Nav::START);
    tree.navigate(Nav::FORWARD);
    check(tree.navigate(Nav::NEXT) && tree.current().san=="e4",
          "the old main line is now the side line");
    int e4 = tree.cursor_id();
    tree.navigate(Nav::END);
    int deep = tree.cursor_id();
    check(tree.go_to(e4) && tree.delete_at_cursor(), "deleting a variation at its first move");
    check(tree.at_root(), "the cursor is left on the position it was played from");
    check(tree.current().children.size()==1, "only the main line is left");
    check(!tree.go_to(e4) && !tree.go_to(deep), "every id under it is refused afterwards");
    check(!tree.delete_at_cursor(), "the root cannot be deleted");
}

// The 50-move clock and repetition belong to the path, not to the whole tree:
// a repetition on one branch must not be seen from its sibling.
static void test_path_rules()
{
    std::printf("clocks and repetition along the path\n");
    Move_Tree tree = fresh();
    play_all(tree, {"e2e4", "e7e5", "g1f3", "b8c6"});
    check(tree.halfmove_clock()==2, "the halfmove clock counts the quiet moves");
    play_all(tree, {"f3e5"});
    check(tree.halfmove_clock()==0, "a capture resets it");

    // Shuffle the knights back and forth until the start position of the
    // shuffle has been seen three times.
    Move_Tree rep = fresh();
    play_all(rep, {"g1f3", "b8c6", "f3g1", "c6b8", "g1f3", "b8c6", "f3g1", "c6b8"});
    std::string reason;
    check(rep.outcome(reason)==Outcome::DRAW && reason=="3-fold repetition",
          "a repetition along the path is a draw", reason);

    // Step back to the second occurrence and branch off: that path has seen
    // the position twice, so it is not a draw.
    rep.navigate(Nav::BACK);
    rep.navigate(Nav::BACK);
    rep.navigate(Nav::BACK);
    rep.navigate(Nav::BACK);
    check(rep.play("d2d4"), "a side line from the second occurrence");
    check(rep.outcome(reason)==Outcome::ONGOING, "the sibling branch sees no repetition", reason);
    rep.navigate(Nav::START);
    rep.navigate(Nav::END);
    check(rep.outcome(reason)==Outcome::DRAW, "while the main line still is one", reason);
}

static void test_pgn_round_trip()
{
    std::printf("PGN round trip\n");
    Move_Tree tree = fresh();
    play_all(tree, {"e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "g8f6"});
    tree.navigate(Nav::BACK);
    play_all(tree, {"f8c5"});                  // 3... Bc5, a side line
    tree.navigate(Nav::BACK);
    tree.navigate(Nav::BACK);
    play_all(tree, {"f1c4", "f8c5", "c2c3"});  // 3. Bc4 Bc5 4. c3, a deeper one
    tree.navigate(Nav::START);
    play_all(tree, {"d2d4"});                  // 1. d4, a branch at the root

    Pgn_Tags tags;
    tags.white = "Ascanius";
    tags.black = "Ascaniusfish";
    tags.result = "*";
    std::string first = tree.pgn(tags);

    Move_Tree back;
    std::string error;
    check(back.load_pgn(first, error), "the export loads again", error);
    check_eq(back.pgn(tags), first, "and exports byte for byte the same PGN");
    if(failures)
    std::printf("---- exported ----\n%s---- reloaded ----\n%s------------------\n",
                first.c_str(), back.pgn(tags).c_str());

    back.navigate(Nav::END);
    check_eq(line_of(back), "e4 e5 Nf3 Nc6 Bb5 Nf6", "the main line came back");
    back.navigate(Nav::START);
    check(back.current().children.size()==2, "so did the branch at move 1");
}

// A position of its own must travel with the game.
static void test_pgn_from_a_fen()
{
    std::printf("PGN from a set-up position\n");
    BB start;
    uci_parse_fen("4k3/8/8/8/8/8/4P3/4K3 w - - 3 28", start);
    Move_Tree tree;
    tree.start(start, 3, 28);
    play_all(tree, {"e2e4", "e8d7"});
    tree.navigate(Nav::BACK);
    play_all(tree, {"e8f7"});

    Pgn_Tags tags;
    std::string text = tree.pgn(tags);
    check(text.find("[SetUp \"1\"]")!=std::string::npos, "it carries a SetUp tag");
    check(text.find("[FEN \"4k3/8/8/8/8/8/4P3/4K3 w - - 3 28\"]")!=std::string::npos,
          "and the position it started from");
    check(text.find("28. e4")!=std::string::npos, "numbered from move 28, not from 1");

    Move_Tree back;
    std::string error;
    check(back.load_pgn(text, error), "and it loads again", error);
    check(back.start_fullmove_number()==28 && back.node(0).halfmove_clock==3,
          "with both clocks");
    check_eq(back.pgn(tags), text, "round trip");
}

// Written to match what Lichess exports from a study: its tag order, its
// {[%eval ...]} and {[%clk ...]} comments, NAGs as "$1", "?!"-style suffixes on
// the moves themselves, and variations nested several deep. (Offline, so this
// is a hand-copy of that format rather than a downloaded file — the point is
// that nothing here is shaped for our own exporter's convenience.)
static const char* const LICHESS_STUDY_PGN =
"[Event \"Sicilian: Najdorf\"]\n"
"[Site \"https://lichess.org/study/abcd1234/wxyz5678\"]\n"
"[Result \"*\"]\n"
"[UTCDate \"2024.11.03\"]\n"
"[UTCTime \"19:41:07\"]\n"
"[Variant \"Standard\"]\n"
"[ECO \"B90\"]\n"
"[Opening \"Sicilian Defense: Najdorf Variation\"]\n"
"[Annotator \"https://lichess.org/@/someone\"]\n"
"\n"
"1. e4 { [%eval 0.24] [%clk 0:03:00] } c5 { [%eval 0.31] } 2. Nf3 d6 3. d4 cxd4\n"
"4. Nxd4 Nf6 5. Nc3 a6 { The Najdorf. } 6. Be3 (6. Bg5 e6 7. f4 Be7 (7... Qb6 $5\n"
"8. Qd2 Qxb2 { the Poisoned Pawn }) 8. Qf3 Qc7) (6. Be2 e5 7. Nb3 Be7) 6... e5 $1\n"
"7. Nb3 Be6 8. f3 Be7 9. Qd2 { [%eval 0.15] } 0-0 10. 0-0-0 Nbd7 11. g4 b5\n"
"12. g5?! Nh5 (12... b4 $2 13. Nd5) 13. Nd5 Bxd5 14. exd5 f5 *\n";

static void test_lichess_pgn()
{
    std::printf("a Lichess-shaped PGN with side lines\n");
    Move_Tree tree;
    std::string error;
    check(tree.load_pgn(LICHESS_STUDY_PGN, error), "it loads", error);
    if(!error.empty())
    return;

    tree.navigate(Nav::END);
    check_eq(line_of(tree),
             "e4 c5 Nf3 d6 d4 cxd4 Nxd4 Nf6 Nc3 a6 Be3 e5 Nb3 Be6 f3 Be7 Qd2 O-O "
             "O-O-O Nbd7 g4 b5 g5 Nh5 Nd5 Bxd5 exd5 f5",
             "the main line, castling included");

    // 6. Be3 has two alternatives, and the first of them has one of its own.
    tree.navigate(Nav::START);
    for(int i=0;i<10;i++)
    tree.navigate(Nav::FORWARD);               // to the position before move 6
    check(tree.current().children.size()==3, "move 6 has three continuations");
    // Up/down switch the move you are on, so stepping into the main line first
    // is what puts the cursor among the three.
    tree.navigate(Nav::FORWARD);
    check(tree.current().san=="Be3", "the main line plays 6. Be3");
    check(tree.navigate(Nav::NEXT) && tree.current().san=="Bg5", "the first side line is 6. Bg5");
    tree.navigate(Nav::FORWARD);               // 6... e6
    tree.navigate(Nav::FORWARD);               // 7. f4
    check(tree.current().children.size()==2, "7. f4 has a side line of its own");
    check(tree.navigate(Nav::FORWARD) && tree.current().san=="Be7", "its main reply");
    check(tree.navigate(Nav::NEXT) && tree.current().san=="Qb6", "and the nested variation");
    tree.navigate(Nav::END);
    check_eq(line_of(tree), "e4 c5 Nf3 d6 d4 cxd4 Nxd4 Nf6 Nc3 a6 Bg5 e6 f4 Qb6 Qd2 Qxb2",
             "down to the end of the nested line");
    check_eq(tree.current().comment, "the Poisoned Pawn", "the comment came with it");

    // What we read must be what we would have written.
    Pgn_Tags tags;
    std::string ours = tree.pgn(tags);
    Move_Tree again;
    check(again.load_pgn(ours, error), "our own export of it loads", error);
    check_eq(again.pgn(tags), ours, "and round trips");
}

static void test_broken_pgn()
{
    std::printf("PGN that should be refused\n");
    Move_Tree tree = fresh();
    play_all(tree, {"e2e4"});
    std::string error;

    check(!tree.load_pgn("1. e4 e5 2. Nf6", error), "an illegal move is refused", error);
    check(!tree.load_pgn("1. e4 (1. d4", error), "an unclosed variation is refused", error);
    check(!tree.load_pgn("(1. e4)", error), "a variation before any move is refused", error);
    check(!tree.load_pgn("[Event \"x\"]\n*\n", error), "a game with no moves is refused", error);
    check(!tree.load_pgn("[FEN \"not a position\"]\n1. e4\n", error), "a bad FEN tag is refused", error);
    check_eq(line_of(tree), "e4", "and the tree that was there is untouched");
}

int main()
{
    // Without these, sliding attacks are garbage and in_check() quietly misses
    // checks — every tool that touches movegen starts here.
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    std::printf("gui/move_tree.hpp: a tree of moves, and PGN both ways\n\n");
    test_branching();
    test_navigation();
    test_promote_and_delete();
    test_path_rules();
    test_pgn_round_trip();
    test_pgn_from_a_fen();
    test_lichess_pgn();
    test_broken_pgn();

    std::printf("\n%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
