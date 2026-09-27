// OWNERSHIP=Claude
// One game the browser is looking at, and the JSON the page renders it from.
//
// The server owns the position; the page owns nothing, so a reload is just
// another GET of the state. Sessions are addressed by id so Watch mode can
// later hold two games at once.
//
// Rules come from tools/game_rules.hpp (SAN, FEN, repetition, Outcome) and
// legality from the engine's own all_moves() — nothing here decides chess.
//
// In Play mode a session also knows which colour the engine has, how it is
// asked to think (gui/engine_link.hpp) and what each of its searches found; in
// Analyse mode it holds the running analysis of the position now on the board.
// Starting and reading those searches is the server's job (gui_server.cpp);
// this file only holds the state they produce, so nothing here blocks.
#ifndef GUI_SESSION_HPP
#define GUI_SESSION_HPP
#include "../tools/game_rules.hpp"
#include "engine_link.hpp"
#include "json.hpp"
#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>

// Which of the GUI's modes a session is in. Watch — Ascaniusfish against
// itself — is still selector-only; Analyse is free play plus a live analysis,
// Play is a game against the engine.
enum class Mode { ANALYSE, PLAY, WATCH };

// What the one engine process of a session is busy with. A session runs at
// most one search, so this says how to read its answer: a PLAY search ends in
// a move on the board, an ANALYSIS search only ever in a line to look at.
enum class Search_Kind { NONE, PLAY, ANALYSIS };

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

// The engine's answer to one move, kept beside it in the move list.
struct Move_Note
{
    bool from_engine = false;
    int depth = 0;
    std::string score_kind, score_value;   // "cp"/"mate", white's view
    long long nodes = 0, time_ms = 0;
};

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

    Search_Kind searching = Search_Kind::NONE;

    bool thinking() const  { return searching==Search_Kind::PLAY; }
    bool analysing() const { return searching==Search_Kind::ANALYSIS; }

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

    // Plays a UCI move ("e2e4", "e7e8q", "e1g1" for castling).
    bool play(const std::string& uci, std::string& error)
    {
        std::string reason;
        if(result(reason)!=Outcome::ONGOING)
        {
            error = "the game is over";
            return false;
        }
        if(!game.play(uci))
        {
            error = "illegal move: " + uci;
            return false;
        }
        played.push_back(uci);
        notes.push_back(Move_Note());
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
        walk.start(game.positions.back(), game.halfmove_clock, game.fullmove());
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
    // iteration replaced the line in between. Either all of it is played or
    // none of it.
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
        size_t before = played.size();
        for(const std::string& move : line)
        if(!play(move, error))
        {
            replay(before);
            error = "that line does not fit the position any more: " + error;
            return false;
        }
        return true;
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
        if(notes.empty())
        return;
        Move_Note& note = notes.back();
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
    }

    // Takes back one move; in Play mode the engine's reply and your own move
    // both go, so it is your turn again on a position you chose.
    bool undo(std::string& error)
    {
        if(resigned)
        {
            resigned = false;   // taking back a resignation plays the game on
            return true;
        }
        if(played.empty())
        {
            error = "nothing to take back";
            return false;
        }
        size_t keep = played.size()-1;
        if(mode==Mode::PLAY)
        while(keep>0 && game.positions[keep].white_move!=human_white)
        keep--;
        replay(keep);
        return true;
    }

    // Resigning ends the game for the side to move (in Play mode, the human's
    // colour whether or not it is their turn).
    void resign()
    {
        resigned = true;
        resigned_white = mode==Mode::PLAY ? human_white : game.positions.back().white_move;
    }

    bool engine_to_move() const
    {
        std::string reason;
        return mode==Mode::PLAY && result(reason)==Outcome::ONGOING
            && game.positions.back().white_move!=human_white;
    }

    // The game's result, resignation included.
    Outcome result(std::string& reason) const
    {
        if(resigned)
        {
            reason = resigned_white ? "white resigns" : "black resigns";
            return resigned_white ? Outcome::BLACK_WINS : Outcome::WHITE_WINS;
        }
        return game.outcome(reason);
    }

    std::string fen() const { return game.fen(); }

    // What the engine is told to think about: the root and the moves from it.
    const std::string& root_fen() const { return start_fen; }
    const std::vector<std::string>& moves() const { return played; }

    std::string state_json() const
    {
        const BB& pos = game.positions.back();
        std::string reason;
        Outcome outcome = result(reason);

        json::Out o;
        o.obj();
        o.key("id").str(id);
        o.key("mode").str(mode_name(mode));
        o.key("orientation").str(flipped ? "black" : "white");
        o.key("fen").str(game.fen());
        o.key("turn").str(pos.white_move ? "white" : "black");
        o.key("ply").num((long long)played.size());
        o.key("fullmove").num(game.fullmove());
        o.key("halfmoveClock").num(game.halfmove_clock);

        o.key("lastMove");
        if(played.empty())
        o.null();
        else
        o.arr().str(played.back().substr(0, 2)).str(played.back().substr(2, 2)).end_arr();

        o.key("check");
        if(in_check(pos.Board, pos.white_move))
        o.str(pos.white_move ? "white" : "black");
        else
        o.null();

        // Where each piece may go, and which of those destinations promote.
        // chessground wants every destination once, so the four promotion moves
        // to one square collapse into one entry plus a "promotions" hint.
        std::map<std::string, std::vector<std::string>> dests;
        std::set<std::string> promotions;
        for(int k=0;k<game.n_children;k++)
        {
            std::string uci = get_UCI(&pos, game.children+k);
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
        for(int k=0;k<game.n_children;k++)
        o.obj()
            .key("uci").str(get_UCI(&pos, game.children+k))
            .key("san").str(san(pos, game.children, game.n_children, k))
         .end_obj();
        o.end_arr();

        o.key("history").arr();
        for(size_t i=0;i<game.san_moves.size();i++)
        {
            o.obj().key("uci").str(game.uci_moves[i]).key("san").str(game.san_moves[i]);
            o.key("note");
            if(i<notes.size() && notes[i].from_engine)
            write_note(o, notes[i]);
            else
            o.null();
            o.end_obj();
        }
        o.end_arr();

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
    Game game;
    BB start_pos;
    int start_halfmove = 0, start_fullmove = 1;
    std::string start_fen;                 // FEN of the position the game starts from
    std::vector<std::string> played;
    std::vector<Move_Note> notes;          // one per played move
    bool resigned = false, resigned_white = false;

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
                   .key("value").num(game.positions.back().white_move ? value : -value)
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
        start_pos = pos;
        start_halfmove = halfmove;
        start_fullmove = fullmove;
        played.clear();
        notes.clear();
        resigned = false;
        engine_error.clear();
        live_valid = false;
        clear_analysis();
        game.start(start_pos, start_halfmove, start_fullmove);
        start_fen = game.fen();
    }

    // Game only grows forwards, so taking back a move means replaying the rest.
    // Games are short and all_moves() is cheap next to a search.
    void replay(size_t keep)
    {
        std::vector<std::string> line(played.begin(), played.begin()+std::min(keep, played.size()));
        std::vector<Move_Note> kept(notes.begin(), notes.begin()+std::min(keep, notes.size()));
        game.start(start_pos, start_halfmove, start_fullmove);
        played.clear();
        notes.clear();
        clear_analysis();
        for(size_t i=0;i<line.size();i++)
        if(game.play(line[i]))
        {
            played.push_back(line[i]);
            notes.push_back(i<kept.size() ? kept[i] : Move_Note());
        }
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
