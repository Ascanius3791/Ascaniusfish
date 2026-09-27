// OWNERSHIP=Claude
// One game the browser is looking at, and the JSON the page renders it from.
//
// The server owns the position; the page owns nothing, so a reload is just
// another GET of the state. Sessions are addressed by id so Watch mode can
// later hold two games at once.
//
// The game is a tree with a cursor (gui/move_tree.hpp), not a list of moves:
// "the position" is wherever the cursor is, so stepping back and playing a
// different move opens a side line instead of throwing the game away. Every
// question here — legal moves, FEN, the result, the moves the engine is given —
// is asked of the cursor, which is what makes the engine follow it.
//
// Rules come from tools/game_rules.hpp (SAN, FEN, repetition, Outcome) and
// legality from the engine's own all_moves() — nothing here decides chess.
//
// In Play mode a session also knows which colour the engine has, how it is
// asked to think (gui/engine_link.hpp) and what each of its searches found; in
// Analyse mode it holds the running analysis of the position now on the board;
// in Watch mode it holds the two sides' settings and whether the self-play game
// is running. Starting and reading those searches is the server's job
// (gui_server.cpp); this file only holds the state they produce, so nothing
// here blocks.
#ifndef GUI_SESSION_HPP
#define GUI_SESSION_HPP
#include "engine_link.hpp"
#include "json.hpp"
#include "move_tree.hpp"
#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>

// Which of the GUI's modes a session is in. Analyse is free play plus a live
// analysis, Play is a game against the engine, Watch is Ascaniusfish against
// itself with a setting per side.
enum class Mode { ANALYSE, PLAY, WATCH };

// What a session's engines are busy with. A session runs at most one search at
// a time — in Watch mode the two sides think in turn, never together — so this
// says how to read its answer: a PLAY or WATCH search ends in a move on the
// board, an ANALYSIS search only ever in a line to look at.
enum class Search_Kind { NONE, PLAY, WATCH, ANALYSIS };

inline const char* mode_name(Mode m)
{
    return m==Mode::PLAY ? "play" : m==Mode::WATCH ? "watch" : "analyse";
}

inline bool mode_from_name(const std::string& name, Mode& out)
{
    if(name=="analyse") { out = Mode::ANALYSE; return true; }
    if(name=="play")    { out = Mode::PLAY;    return true; }
    if(name=="watch")   { out = Mode::WATCH;   return true; }
    return false;
}

inline std::string square_name(int square)
{
    return std::string(1, 'a'+square%8) + std::string(1, '1'+square/8);
}

// Which colour the human asked for, before a random choice is made.
enum class Side_Choice { WHITE, BLACK, RANDOM };

inline const char* side_choice_name(Side_Choice s)
{
    return s==Side_Choice::WHITE ? "white" : s==Side_Choice::BLACK ? "black" : "random";
}

inline bool side_choice_from_name(const std::string& name, Side_Choice& out)
{
    if(name=="white")  { out = Side_Choice::WHITE;  return true; }
    if(name=="black")  { out = Side_Choice::BLACK;  return true; }
    if(name=="random") { out = Side_Choice::RANDOM; return true; }
    return false;
}

inline bool coin_flip()
{
    static std::mt19937 rng((unsigned)std::random_device{}());
    return std::uniform_int_distribution<int>(0, 1)(rng)==1;
}

class Session
{
    public:
    std::string id;
    Mode mode = Mode::ANALYSE;
    bool flipped = false;   // board orientation: UI state, but kept here so a reload keeps it

    // Play mode. `human_white` is the resolved colour (a RANDOM choice is
    // decided once, when the game starts), so the engine's colour is its
    // opposite and every later question has one answer.
    Side_Choice side_choice = Side_Choice::WHITE;
    Go_Limits limits;
    bool human_white = true;
    std::string engine_error;
    Search_Info live;        // the running Play search, as far as it has got
    bool live_valid = false;

    // Analyse mode. The engine thinks about the position on the board until it
    // changes. Everything here belongs to the position now shown and nothing
    // else: every position change clears it before the page is told, so no
    // frame can ever carry the previous position's eval.
    bool analysis_on = false;
    bool analysis_valid = false;
    bool analysis_finished = false;   // the engine ended the search itself (it ran out of depth)
    Search_Info analysis;
    std::vector<std::string> analysis_uci, analysis_san;   // its PV, legal from here, both ways

    // Watch mode. Ascaniusfish against itself, one engine process per side so
    // the two have their own transposition tables. `running` plays the game on
    // move after move; `step` is one move and then a pause. Pausing clears
    // `running` without touching the search that is out: the move being thought
    // about is still played, which is what "after the current move" means.
    Go_Limits watch_limits[2];   // [0] white, [1] black
    bool watch_running = false;
    bool watch_step = false;

    Search_Kind searching = Search_Kind::NONE;

    bool thinking() const  { return searching==Search_Kind::PLAY; }
    bool watching() const  { return searching==Search_Kind::WATCH; }
    bool analysing() const { return searching==Search_Kind::ANALYSIS; }

    // The colour to move at the cursor: which engine Watch mode asks next, and
    // whose view an arriving score is in.
    bool white_to_move() const { return tree.position().white_move; }

    // Whether the cursor is at the end of its line, which is the only place an
    // engine plays from — stepping back to look around is not a move request.
    bool at_tip() const { return tree.at_tip(); }

    // Which game the position belongs to. It changes whenever the game itself
    // is replaced — a reset, a FEN, a loaded PGN — and never when a move is
    // played, so it is exactly the question "does this engine still know the
    // game it is being asked about, or does it need a ucinewgame?".
    int game_serial() const { return serial; }

    explicit Session(const std::string& id = "main") : id(id)
    {
        reset();
    }

    void reset()
    {
        BB start;
        uci_parse_fen(UCI_STARTPOS, start);
        set_start(start, 0, 1);
    }

    // Replaces the position. Leaves the session untouched and fills `error` if
    // the FEN is malformed or the position is one no game could be in.
    bool set_fen(const std::string& fen, std::string& error)
    {
        BB pos;
        if(!uci_parse_fen(fen, pos))
        {
            error = "not a valid FEN";
            return false;
        }
        if(__builtin_popcountll(pos.Board[5])!=1 || __builtin_popcountll(pos.Board[11])!=1)
        {
            error = "a position needs exactly one king per side";
            return false;
        }
        if(in_check(pos.Board, !pos.white_move))
        {
            error = "the side not to move is in check";
            return false;
        }
        set_start(pos, halfmove_field(fen), fullmove_field(fen));
        return true;
    }

    // Begins a game against the engine from the position the session starts
    // from, resolving a random colour choice. The board orientation follows the
    // human's colour, which is what everyone expects of a chess GUI.
    void start_play()
    {
        mode = Mode::PLAY;
        human_white = side_choice==Side_Choice::RANDOM ? coin_flip() : side_choice==Side_Choice::WHITE;
        flipped = !human_white;
        set_start(start_pos, start_halfmove, start_fullmove);
    }

    // Plays a UCI move ("e2e4", "e7e8q", "e1g1" for castling) from wherever the
    // cursor is. From an earlier position that is a new side line, and from one
    // already in the tree it just follows the move that is there.
    bool play(const std::string& uci, std::string& error)
    {
        std::string reason;
        if(result(reason)!=Outcome::ONGOING)
        {
            error = "the game is over";
            return false;
        }
        if(!tree.play(uci))
        {
            error = "illegal move: " + uci;
            return false;
        }
        clear_analysis();
        return true;
    }

    // Moves the cursor: the arrow keys, and the buttons beside them.
    bool navigate(Nav where, std::string& error)
    {
        if(!tree.navigate(where))
        {
            error = "there is nowhere to go from here";
            return false;
        }
        clear_analysis();
        return true;
    }

    // Jumps to a node, which is what clicking a move in the tree does. An id
    // from before a deletion is refused rather than landing somewhere else.
    bool go_to(int node, std::string& error)
    {
        if(!tree.go_to(node))
        {
            error = "that move is not in the game any more";
            return false;
        }
        clear_analysis();
        return true;
    }

    // Makes the line through the cursor the main line. The position does not
    // change, so the analysis running on it is still about the right board.
    bool promote(std::string& error)
    {
        if(!tree.promote_to_main())
        {
            error = "this is already the main line";
            return false;
        }
        return true;
    }

    // Throws away the move at the cursor and everything after it.
    bool delete_variation(std::string& error)
    {
        if(!tree.delete_at_cursor())
        {
            error = "there is no move here to delete";
            return false;
        }
        resigned = false;   // the game it ended is gone
        clear_analysis();
        return true;
    }

    // Forgets the analysis. Called by every position change here and by the
    // server whenever it stops a search, so the line the page draws is always
    // the line for the position the page draws.
    void clear_analysis()
    {
        analysis_valid = false;
        analysis_finished = false;
        analysis_uci.clear();
        analysis_san.clear();
    }

    // Records one analysis iteration. The engine gives its PV in UCI; it is
    // replayed from the current position to get SAN, and a stale TT tail that
    // is not legal here simply ends the line rather than being shown.
    void set_analysis(const Search_Info& info)
    {
        analysis = info;
        analysis_valid = true;
        analysis_uci.clear();
        analysis_san.clear();
        Game walk;
        walk.start(tree.position(), tree.halfmove_clock(), tree.fullmove());
        for(const std::string& uci : info.pv)
        {
            if(!walk.play(uci))
            break;
            analysis_uci.push_back(uci);
            analysis_san.push_back(walk.san_moves.back());
        }
    }

    // Steps the board along a line of UCI moves, which is what clicking a move
    // in the analysis line does. The page sends the moves it drew rather than
    // an index into the line, so you get the move you clicked even if a deeper
    // iteration replaced the line in between. The line is checked through before
    // any of it is played, so a line that does not fit leaves the tree alone.
    bool enter_line(const std::string& moves, std::string& error)
    {
        std::istringstream in(moves);
        std::vector<std::string> line;
        std::string uci;
        while(in >> uci)
        line.push_back(uci);
        if(line.empty())
        {
            error = "no moves given";
            return false;
        }
        Game walk;
        walk.start(tree.position(), tree.halfmove_clock(), tree.fullmove());
        for(const std::string& move : line)
        if(!walk.play(move))
        {
            error = "that line does not fit the position any more: illegal move " + move;
            return false;
        }
        for(const std::string& move : line)
        tree.play(move);
        clear_analysis();
        return true;
    }

    // Whether Watch mode wants a move thought about: the game is running (or
    // one step was asked for), the cursor is at the end of the line — stepping
    // back to look at an earlier position is not a request to play there — and
    // the game is not over.
    bool watch_to_move() const
    {
        std::string reason;
        return mode==Mode::WATCH && (watch_running || watch_step) && tree.at_tip()
            && engine_error.empty() && result(reason)==Outcome::ONGOING;
    }

    // The limits the side to move plays under.
    const Go_Limits& watch_limits_now() const { return watch_limits[white_to_move() ? 0 : 1]; }

    // Stops the self-play game where it is. Everything that moves the position
    // out from under the engines calls this, so a game never plays on into a
    // position nobody asked for.
    void watch_pause()
    {
        watch_running = false;
        watch_step = false;
    }

    // Whether Analyse mode wants a search running: the toggle is on, the
    // engine is alive, and the position is one that still has moves.
    bool analysis_wanted() const
    {
        std::string reason;
        return mode==Mode::ANALYSE && analysis_on && !analysis_finished
            && engine_error.empty() && result(reason)==Outcome::ONGOING;
    }

    // Records what the engine's search found for the move just played.
    void annotate_last(const Search_Info& info, bool white_moved)
    {
        Move_Note note;
        note.from_engine = true;
        note.depth = info.depth;
        note.nodes = info.nodes;
        note.time_ms = info.time_ms;
        note.score_kind = info.score_kind;
        // UCI scores are the mover's view; the move list shows white's, as a
        // printed game does, so a score after a black move flips sign.
        if(!info.score_kind.empty())
        {
            long long value = std::atoll(info.score_value.c_str());
            note.score_value = std::to_string(white_moved ? value : -value);
        }
        tree.annotate(note);
    }

    // Takes the move at the cursor back: it and its continuations go, and the
    // cursor is left on the position it was played from. This is the one
    // destructive step — the arrows only move the cursor. In Play mode the
    // engine's reply and your own move both go, so it is your turn again on a
    // position you chose.
    bool undo(std::string& error)
    {
        if(resigned)
        {
            resigned = false;   // taking back a resignation plays the game on
            return true;
        }
        if(tree.at_root())
        {
            error = "nothing to take back";
            return false;
        }
        tree.delete_at_cursor();
        if(mode==Mode::PLAY)
        while(!tree.at_root() && tree.position().white_move!=human_white)
        tree.delete_at_cursor();
        clear_analysis();
        return true;
    }

    // Resigning ends the game for the side to move (in Play mode, the human's
    // colour whether or not it is their turn).
    void resign()
    {
        resigned = true;
        resigned_white = mode==Mode::PLAY ? human_white : tree.position().white_move;
    }

    // The engine only answers at the end of a line: stepping back into the game
    // to look around is not a request for it to play there. Playing a move from
    // an earlier position makes a new end, and then it does answer.
    bool engine_to_move() const
    {
        std::string reason;
        return mode==Mode::PLAY && tree.at_tip() && result(reason)==Outcome::ONGOING
            && tree.position().white_move!=human_white;
    }

    // The game's result, resignation included. A resignation belongs to the
    // game that was played, so it does not follow the cursor into a side line.
    Outcome result(std::string& reason) const
    {
        if(resigned)
        {
            reason = resigned_white ? "white resigns" : "black resigns";
            return resigned_white ? Outcome::BLACK_WINS : Outcome::WHITE_WINS;
        }
        return tree.outcome(reason);
    }

    std::string fen() const { return tree.fen(); }

    // What the engine is told to think about: the root and the moves down to
    // the cursor, so its search is always about the position on the board.
    const std::string& root_fen() const { return start_fen; }
    std::vector<std::string> moves() const { return tree.path_moves(); }

    // The whole tree as PGN, and back. A load replaces the game, so the caller
    // stops whatever the engine was doing first.
    std::string pgn() const { return tree.pgn(tags()); }

    bool load_pgn(const std::string& text, std::string& error)
    {
        Move_Tree loaded;
        if(!loaded.load_pgn(text, error))
        return false;
        tree = loaded;
        serial++;
        start_pos = tree.root_position();
        start_halfmove = tree.start_halfmove_clock();
        start_fullmove = tree.start_fullmove_number();
        start_fen = Game::fen_of(start_pos, tree.node(0).key[13], start_halfmove, start_fullmove);
        resigned = false;
        engine_error.clear();
        live_valid = false;
        watch_pause();
        clear_analysis();
        return true;
    }

    std::string state_json() const
    {
        const BB& pos = tree.position();
        const Tree_Node& here = tree.current();
        std::string reason;
        Outcome outcome = result(reason);

        json::Out o;
        o.obj();
        o.key("id").str(id);
        o.key("mode").str(mode_name(mode));
        o.key("orientation").str(flipped ? "black" : "white");
        o.key("fen").str(tree.fen());
        o.key("turn").str(pos.white_move ? "white" : "black");
        o.key("ply").num(here.ply);
        o.key("fullmove").num(tree.fullmove());
        o.key("halfmoveClock").num(tree.halfmove_clock());

        o.key("lastMove");
        if(tree.at_root())
        o.null();
        else
        o.arr().str(here.uci.substr(0, 2)).str(here.uci.substr(2, 2)).end_arr();

        o.key("check");
        if(in_check(pos.Board, pos.white_move))
        o.str(pos.white_move ? "white" : "black");
        else
        o.null();

        // Where each piece may go, and which of those destinations promote.
        // chessground wants every destination once, so the four promotion moves
        // to one square collapse into one entry plus a "promotions" hint.
        const BB* legal = tree.legal_moves();
        int n_legal = tree.n_legal_moves();
        std::map<std::string, std::vector<std::string>> dests;
        std::set<std::string> promotions;
        for(int k=0;k<n_legal;k++)
        {
            std::string uci = get_UCI(&pos, legal+k);
            std::string from = uci.substr(0, 2), to = uci.substr(2, 2);
            std::vector<std::string>& targets = dests[from];
            if(std::find(targets.begin(), targets.end(), to)==targets.end())
            targets.push_back(to);
            if(uci.size()==5)
            promotions.insert(from+to);
        }

        o.key("dests").obj();
        for(const auto& entry : dests)
        {
            o.key(entry.first).arr();
            for(const std::string& to : entry.second)
            o.str(to);
            o.end_arr();
        }
        o.end_obj();

        o.key("promotions").arr();
        for(const std::string& p : promotions)
        o.str(p);
        o.end_arr();

        o.key("legalMoves").arr();
        for(int k=0;k<n_legal;k++)
        o.obj()
            .key("uci").str(get_UCI(&pos, legal+k))
            .key("san").str(san(pos, legal, n_legal, k))
         .end_obj();
        o.end_arr();

        write_tree(o);
        o.key("pgn").str(pgn());

        o.key("outcome").obj();
        o.key("state").str(outcome==Outcome::ONGOING ? "ongoing" :
                           outcome==Outcome::WHITE_WINS ? "white_wins" :
                           outcome==Outcome::BLACK_WINS ? "black_wins" : "draw");
        o.key("reason").str(outcome==Outcome::ONGOING ? "" : reason);
        o.end_obj();

        // Everything the Play panel shows. It is sent in every mode so the page
        // can draw the settings form before a game has started.
        o.key("play").obj();
        o.key("side").str(side_choice_name(side_choice));
        o.key("humanColor").str(human_white ? "white" : "black");
        o.key("engineColor").str(human_white ? "black" : "white");
        o.key("limit").obj().key("kind").str(limits.kind()).key("value").num(limits.value()).end_obj();
        o.key("thinking").boolean(thinking());
        o.key("engineTurn").boolean(engine_to_move());
        o.key("resigned").boolean(resigned);
        o.key("error").str(engine_error);
        o.key("search");
        if(thinking() && live_valid)
        write_search(o, live);
        else
        o.null();
        o.end_obj();

        // Watch mode: the two sides' settings, whether the game is running, and
        // the search the side to move has got so far. Like the Play panel it is
        // sent in every mode, so the settings can be set up before starting.
        o.key("watch").obj();
        o.key("running").boolean(watch_running);
        o.key("stepping").boolean(watch_step);
        o.key("thinking").boolean(watching());
        o.key("mover").str(white_to_move() ? "white" : "black");
        o.key("atTip").boolean(tree.at_tip());
        o.key("error").str(engine_error);
        for(int side=0;side<2;side++)
        o.key(side==0 ? "white" : "black").obj()
            .key("kind").str(watch_limits[side].kind())
            .key("value").num(watch_limits[side].value())
         .end_obj();
        o.key("search");
        if(watching() && live_valid)
        write_search(o, live);
        else
        o.null();
        o.end_obj();

        // Analyse mode's live search. "search" is null unless there is a line
        // for exactly this position, so the page has nothing stale to draw.
        o.key("analysis").obj();
        o.key("on").boolean(analysis_on);
        o.key("running").boolean(analysing());
        o.key("error").str(engine_error);
        o.key("search");
        if(analysis_valid)
        write_analysis(o);
        else
        o.null();
        o.end_obj();

        o.end_obj();
        return o.s;
    }

    private:
    Move_Tree tree;
    BB start_pos;
    int start_halfmove = 0, start_fullmove = 1;
    std::string start_fen;                 // FEN of the position the game starts from
    bool resigned = false, resigned_white = false;
    int serial = 0;                        // bumped whenever the game itself is replaced

    // The tags an exported game carries. In Play mode the names say who had
    // which colour, which is the only thing this session knows about them.
    Pgn_Tags tags() const
    {
        Pgn_Tags t;
        if(mode==Mode::PLAY)
        {
            t.white = human_white ? "Human" : "Ascaniusfish";
            t.black = human_white ? "Ascaniusfish" : "Human";
        }
        else if(mode==Mode::WATCH)
        t.white = t.black = "Ascaniusfish";
        std::string reason;
        Outcome outcome = result(reason);
        t.result = outcome==Outcome::WHITE_WINS ? "1-0" :
                   outcome==Outcome::BLACK_WINS ? "0-1" :
                   outcome==Outcome::DRAW ? "1/2-1/2" : "*";
        return t;
    }

    // The tree, flat: every live node with its parent and its children, so the
    // page can draw the nesting itself from one array. Ids are the tree's own,
    // which is what /api/goto takes — the page never has to count plies.
    void write_tree(json::Out& o) const
    {
        o.key("tree").obj();
        o.key("cursor").num(tree.cursor_id());
        o.key("canBack").boolean(!tree.at_root());
        o.key("canForward").boolean(!tree.at_tip());
        o.key("canPrev").boolean(sibling_count(-1)>0);
        o.key("canNext").boolean(sibling_count(1)>0);
        o.key("canPromote").boolean(can_promote());
        o.key("nodes").arr();
        for(int id=0; id<tree.size(); id++)
        {
            const Tree_Node& n = tree.node(id);
            if(!n.alive)
            continue;
            o.obj();
            o.key("id").num(id);
            o.key("parent").num(n.parent);
            o.key("children").arr();
            for(int child : n.children)
            o.num(child);
            o.end_arr();
            o.key("san").str(n.san);
            o.key("uci").str(n.uci);
            o.key("comment").str(n.comment);
            // The number this move is printed with, and whose move it was: both
            // belong to the position it was played from.
            o.key("moveNumber").num(n.parent<0 ? tree.start_fullmove_number() : tree.fullmove_of(n.parent));
            o.key("white").boolean(n.parent>=0 && tree.node(n.parent).pos.white_move);
            o.key("note");
            if(n.note.from_engine)
            write_note(o, n.note);
            else
            o.null();
            o.end_obj();
        }
        o.end_arr();
        o.end_obj();
    }

    // How many siblings the cursor has before it (-1) or after it (1), which is
    // what greys the two variation buttons out.
    int sibling_count(int direction) const
    {
        int id = tree.cursor_id(), parent = tree.node(id).parent;
        if(parent<0)
        return 0;
        const std::vector<int>& siblings = tree.node(parent).children;
        int at = (int)(std::find(siblings.begin(), siblings.end(), id)-siblings.begin());
        return direction<0 ? at : (int)siblings.size()-1-at;
    }

    // Whether "promote" would change anything: the cursor, or an ancestor of
    // it, is not the first child of its parent.
    bool can_promote() const
    {
        for(int id=tree.cursor_id(); tree.node(id).parent>=0; id=tree.node(id).parent)
        if(tree.node(tree.node(id).parent).children[0]!=id)
        return true;
        return false;
    }

    // The score of a finished engine search, from white's view.
    static void write_note(json::Out& o, const Move_Note& note)
    {
        o.obj();
        o.key("depth").num(note.depth);
        o.key("nodes").num(note.nodes);
        o.key("time").num(note.time_ms);
        o.key("score");
        if(note.score_kind.empty())
        o.null();
        else
        o.obj().key("kind").str(note.score_kind).key("value").num(std::atoll(note.score_value.c_str())).end_obj();
        o.end_obj();
    }

    // The running search. Its score stays the mover's view, which is what a
    // thinking indicator wants: "+0.40" means the engine likes its position.
    static void write_search(json::Out& o, const Search_Info& info)
    {
        o.obj();
        o.key("depth").num(info.depth);
        o.key("nodes").num(info.nodes);
        o.key("nps").num(info.nps);
        o.key("time").num(info.time_ms);
        o.key("score");
        if(info.score_kind.empty())
        o.null();
        else
        o.obj().key("kind").str(info.score_kind).key("value").num(std::atoll(info.score_value.c_str())).end_obj();
        o.end_obj();
    }

    // The analysis, as the page draws it. Unlike every other score here this
    // one is white's view whichever side is to move: an eval bar that flipped
    // with the mover would swing a full board width on every move.
    void write_analysis(json::Out& o) const
    {
        o.obj();
        o.key("depth").num(analysis.depth);
        o.key("nodes").num(analysis.nodes);
        o.key("nps").num(analysis.nps);
        o.key("time").num(analysis.time_ms);
        o.key("score");
        if(analysis.score_kind.empty())
        o.null();
        else
        {
            long long value = std::atoll(analysis.score_value.c_str());
            o.obj().key("kind").str(analysis.score_kind)
                   .key("value").num(tree.position().white_move ? value : -value)
             .end_obj();
        }
        o.key("line").arr();
        for(size_t i=0;i<analysis_san.size();i++)
        o.obj().key("uci").str(analysis_uci[i]).key("san").str(analysis_san[i]).end_obj();
        o.end_arr();
        o.end_obj();
    }

    void set_start(const BB& pos, int halfmove, int fullmove)
    {
        serial++;
        start_pos = pos;
        start_halfmove = halfmove;
        start_fullmove = fullmove;
        resigned = false;
        engine_error.clear();
        live_valid = false;
        watch_pause();
        clear_analysis();
        tree.start(start_pos, start_halfmove, start_fullmove);
        start_fen = tree.fen();
    }

    // The clock fields of an already-validated FEN (fields 5 and 6); a FEN that
    // leaves them out means halfmove 0, fullmove 1, as uci_parse_fen also has it.
    static int halfmove_field(const std::string& fen) { return clock_field(fen, 5, 0); }
    static int fullmove_field(const std::string& fen) { return clock_field(fen, 6, 1); }

    static int clock_field(const std::string& fen, int which, int fallback)
    {
        std::istringstream in(fen);
        std::string field;
        for(int i=0;i<which;i++)
        if(!(in >> field))
        return fallback;
        if(field.empty() || field.find_first_not_of("0123456789")!=std::string::npos || field.size()>9)
        return fallback;
        return std::stoi(field);
    }
};

// The set of games the server holds, created on first mention of an id.
class Sessions
{
    public:
    Session& get(const std::string& raw_id)
    {
        std::string id = clean(raw_id);
        auto it = games.find(id);
        if(it==games.end())
        it = games.emplace(id, Session(id)).first;
        return it->second;
    }

    private:
    std::map<std::string, Session> games;

    // Ids come from a URL, so keep them to something short and printable.
    static std::string clean(const std::string& raw)
    {
        std::string id;
        for(char c : raw)
        if(isalnum((unsigned char)c) || c=='-' || c=='_')
        id += c;
        return id.empty() ? "main" : id.substr(0, 32);
    }
};

#endif // GUI_SESSION_HPP
