// OWNERSHIP=Claude
// The game as a tree of moves, with a cursor, and PGN in both directions.
//
// tools/game_rules.hpp has the linear Game that tools/match.cpp records a
// played game into; that is the right shape for a game being played and the
// wrong one for a game being looked at. Here every position can carry several
// continuations: the first child of a node is its main line and the rest are
// side lines, so playing a move from an earlier position adds a branch instead
// of throwing the rest of the game away. `cursor` says which node the board
// shows, and everything that used to mean "the last position" — legal moves,
// FEN, repetition, the moves the engine is given — now means "the cursor".
//
// All the chess still comes from elsewhere: legality from the engine's
// all_moves(), SAN/FEN/repetition/Outcome from tools/game_rules.hpp. This file
// only decides shape and navigation.
//
// Node ids are indices into one pool and are never reused: a deleted node stays
// in it marked dead, so an id the browser drew before a deletion is refused
// rather than silently meaning a different move.
#ifndef GUI_MOVE_TREE_HPP
#define GUI_MOVE_TREE_HPP
#include "../tools/game_rules.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

// How far a kept line is worth following. A PV longer than this says more about
// the search than about the move, and every node of a game carries one.
constexpr int MOVE_NOTE_PV = 24;

// The engine's answer to one move, kept beside it in the tree. #16 fills it in
// from an analysis search; a PGN comment lands in `comment`.
struct Move_Note
{
    bool from_engine = false;
    int depth = 0;
    std::string score_kind, score_value;   // "cp"/"mate", white's view
    long long nodes = 0, time_ms = 0;
    // The line the search found, in UCI, from the position the move was played
    // in — so `pv[0]` is the move itself and the rest is what the engine
    // expected to follow. It is what the engine-line box shows when you step
    // back onto this move (#25), which is why it belongs to the move rather
    // than to a position: the analysis store is keyed by position and a later
    // analysis of the same one replaces what this search found. Not exported to
    // PGN, so a loaded game has the comment's score and depth but no line.
    std::vector<std::string> pv;
};

// Where the cursor can be asked to go. PREV/NEXT step between the siblings of
// the current move — the other moves tried in the same position.
enum class Nav { BACK, FORWARD, START, END, PREV, NEXT };

inline bool nav_from_name(const std::string& name, Nav& out)
{
    if(name=="back")    { out = Nav::BACK;    return true; }
    if(name=="forward") { out = Nav::FORWARD; return true; }
    if(name=="start")   { out = Nav::START;   return true; }
    if(name=="end")     { out = Nav::END;     return true; }
    if(name=="prev")    { out = Nav::PREV;    return true; }
    if(name=="next")    { out = Nav::NEXT;    return true; }
    return false;
}

// One position, and the move that reached it. The root has no move.
struct Tree_Node
{
    int parent = -1;
    std::vector<int> children;   // children[0] is the main continuation
    std::string uci, san;
    BB pos;                      // the position *after* `uci` (the root: the start position)
    Position_Key key;
    int halfmove_clock = 0;      // for the 50-move rule, counted along this path
    int ply = 0;                 // moves from the root
    Move_Note note;
    std::string comment;         // a PGN {comment} on this move
    bool alive = true;
};

// The tags of an exported game. Play mode fills in the names and the result.
struct Pgn_Tags
{
    std::string event = "Ascaniusfish GUI";
    std::string site = "local";
    std::string date = "????.??.??";
    std::string round = "-";
    std::string white = "?";
    std::string black = "?";
    std::string result = "*";
};

class Move_Tree
{
    public:
    // Starts a one-node tree at `pos`. Every later node grows from this one.
    void start(const BB& pos, int halfmoves, int fullmove)
    {
        nodes.clear();
        start_halfmove = halfmoves;
        start_fullmove = fullmove;
        Tree_Node root;
        root.pos = pos;
        root.halfmove_clock = halfmoves;
        nodes.push_back(root);
        cursor = 0;
        refresh();
        nodes[0].key = position_key(pos, effective_en_passant(pos, legal, n_legal));
    }

    int cursor_id() const  { return cursor; }
    int size() const       { return (int)nodes.size(); }
    const Tree_Node& node(int id) const { return nodes[id]; }
    const Tree_Node& current() const    { return nodes[cursor]; }
    const BB& position() const          { return nodes[cursor].pos; }
    const BB* legal_moves() const       { return legal; }
    int n_legal_moves() const           { return n_legal; }
    bool at_root() const                { return cursor==0; }
    bool at_tip() const                 { return nodes[cursor].children.empty(); }
    int start_fullmove_number() const   { return start_fullmove; }
    int start_halfmove_clock() const    { return start_halfmove; }
    const BB& root_position() const     { return nodes[0].pos; }

    // The move number of the position at `id`, as a printed game counts them.
    int fullmove_of(int id) const
    {
        return start_fullmove + (nodes[id].ply + !nodes[0].pos.white_move)/2;
    }

    int fullmove() const { return fullmove_of(cursor); }
    int halfmove_clock() const { return nodes[cursor].halfmove_clock; }

    // The moves from the root down to the cursor: what the engine is given, so
    // its search follows the cursor rather than the end of the game.
    std::vector<std::string> path_moves() const
    {
        std::vector<std::string> line;
        for(int id=cursor; id>0; id=nodes[id].parent)
        line.push_back(nodes[id].uci);
        std::reverse(line.begin(), line.end());
        return line;
    }

    // The node ids from the root down to the cursor, the cursor last.
    std::vector<int> path_ids() const
    {
        std::vector<int> line;
        for(int id=cursor; id>=0; id=nodes[id].parent)
        line.push_back(id);
        std::reverse(line.begin(), line.end());
        return line;
    }

    std::string fen() const
    {
        return Game::fen_of(nodes[cursor].pos, nodes[cursor].key[13],
                            nodes[cursor].halfmove_clock, fullmove());
    }

    // Plays a UCI move from the cursor and follows it. A move already there is
    // stepped into rather than added twice, so replaying a line you have seen
    // before does not litter the tree with duplicates.
    bool play(const std::string& uci)
    {
        for(int child : nodes[cursor].children)
        if(nodes[child].uci==uci)
        {
            cursor = child;
            refresh();
            return true;
        }
        for(int k=0;k<n_legal;k++)
        {
            if(get_UCI(&nodes[cursor].pos, legal+k)!=uci)
            continue;
            int id = add_child(k);
            cursor = id;
            refresh();
            return true;
        }
        return false;
    }

    // Moves the cursor. False when there is nowhere to go, which is how the
    // page's arrow keys stop at the ends of a line.
    bool navigate(Nav where)
    {
        int target = cursor;
        switch(where)
        {
            case Nav::BACK:    target = nodes[cursor].parent; break;
            case Nav::FORWARD: target = nodes[cursor].children.empty() ? -1 : nodes[cursor].children[0]; break;
            case Nav::START:   target = 0; break;
            case Nav::END:
            while(!nodes[target].children.empty())
            target = nodes[target].children[0];
            break;
            case Nav::PREV:
            case Nav::NEXT:
            target = sibling(cursor, where==Nav::NEXT);
            break;
        }
        if(target<0 || target==cursor)
        return false;
        cursor = target;
        refresh();
        return true;
    }

    // Jumps straight to a node, which is what clicking a move in the tree does.
    bool go_to(int id)
    {
        if(id<0 || id>=(int)nodes.size() || !nodes[id].alive)
        return false;
        cursor = id;
        refresh();
        return true;
    }

    // Makes the line through the cursor the main line: the cursor and every
    // ancestor of it becomes the first child of its parent. One call turns a
    // side line you like into the game, which is what "promote" is for.
    bool promote_to_main()
    {
        bool changed = false;
        for(int id=cursor; nodes[id].parent>=0; id=nodes[id].parent)
        {
            std::vector<int>& siblings = nodes[nodes[id].parent].children;
            auto it = std::find(siblings.begin(), siblings.end(), id);
            if(it==siblings.begin())
            continue;
            siblings.erase(it);
            siblings.insert(siblings.begin(), id);
            changed = true;
        }
        return changed;
    }

    // Removes the move at the cursor and everything after it, leaving the
    // cursor on the position that move was played from. The root cannot go.
    bool delete_at_cursor()
    {
        int victim = cursor, parent = nodes[victim].parent;
        if(parent<0)
        return false;
        std::vector<int>& siblings = nodes[parent].children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), victim), siblings.end());
        kill(victim);
        cursor = parent;
        refresh();
        return true;
    }

    // Records what an engine search found for the move at the cursor.
    void annotate(const Move_Note& note)
    {
        if(cursor>0)
        nodes[cursor].note = note;
    }

    // The rules, along the current path: mate/stalemate, 50 moves, repetition
    // (counted over the ancestors of the cursor, never across a branch),
    // material.
    Outcome outcome(std::string& reason) const
    {
        const Tree_Node& here = nodes[cursor];
        if(n_legal==0)
        {
            if(here.pos.get_in_check())
            {
                reason = here.pos.white_move ? "black mates" : "white mates";
                return here.pos.white_move ? Outcome::BLACK_WINS : Outcome::WHITE_WINS;
            }
            reason = "stalemate";
            return Outcome::DRAW;
        }
        if(here.halfmove_clock>=100)
        {
            reason = "fifty move rule";
            return Outcome::DRAW;
        }
        int repeats = 0, back = 0;
        for(int id=cursor; id>=0 && back<=here.halfmove_clock; id=nodes[id].parent, back++)
        {
            if(back%2)
            continue;                        // only the same side to move can repeat
            repeats += nodes[id].key==here.key;
            if(nodes[id].parent<0)
            break;
        }
        if(repeats>=3)
        {
            reason = "3-fold repetition";
            return Outcome::DRAW;
        }
        if(insufficient_material(here.pos))
        {
            reason = "insufficient material";
            return Outcome::DRAW;
        }
        return Outcome::ONGOING;
    }

    // ------------------------------------------------------------------- PGN

    // The whole tree as PGN: the main line with its side lines in nested
    // parentheses, wrapped like a PGN file's movetext.
    std::string pgn(const Pgn_Tags& tags) const
    {
        std::string out;
        out += tag("Event", tags.event) + tag("Site", tags.site) + tag("Date", tags.date)
             + tag("Round", tags.round) + tag("White", tags.white) + tag("Black", tags.black)
             + tag("Result", tags.result);
        std::string start = Game::fen_of(nodes[0].pos, nodes[0].key[13], start_halfmove, start_fullmove);
        if(start!=std::string(UCI_STARTPOS))
        out += tag("SetUp", "1") + tag("FEN", start);
        out += "\n";

        std::string text;
        write_moves(0, true, text);
        if(!text.empty())
        text += " ";
        text += tags.result;
        return out + wrap(text) + "\n";
    }

    // Rebuilds the tree from PGN, variations and all. The tree is only replaced
    // once the whole game has been read, so a broken file leaves what you had.
    bool load_pgn(const std::string& text, std::string& error)
    {
        BB start;
        uci_parse_fen(UCI_STARTPOS, start);
        int halfmoves = 0, fullmove = 1;
        size_t i = 0;
        if(!read_tags(text, i, start, halfmoves, fullmove, error))
        return false;

        Move_Tree built;
        built.start(start, halfmoves, fullmove);
        if(!built.read_movetext(text, i, error))
        return false;
        if(built.nodes.size()<2)
        {
            error = "no moves in that PGN";
            return false;
        }
        *this = built;
        cursor = 0;
        refresh();
        navigate(Nav::END);
        return true;
    }

    private:
    std::vector<Tree_Node> nodes;
    int cursor = 0;
    int start_halfmove = 0, start_fullmove = 1;
    BB legal[MAX_LEGAL_MOVES];   // the legal moves of the cursor's position
    int n_legal = 0;

    void refresh()
    {
        n_legal = std::get<0>(all_moves(&nodes[cursor].pos, legal));
    }

    // A new child of the cursor for its k-th legal move.
    int add_child(int k)
    {
        const Tree_Node& parent = nodes[cursor];
        const BB& before = parent.pos;
        Tree_Node child;
        child.parent = cursor;
        child.ply = parent.ply+1;
        child.uci = get_UCI(&before, legal+k);
        child.san = san(before, legal, n_legal, k);
        child.pos = legal[k];

        bool pawn_move = before.Board[before.white_move ? 0 : 6] != child.pos.Board[before.white_move ? 0 : 6];
        uint64_t all_before = 0, all_after = 0;
        for(int i=0;i<12;i++) { all_before |= before.Board[i]; all_after |= child.pos.Board[i]; }
        bool capture = __builtin_popcountll(all_after) < __builtin_popcountll(all_before);
        child.halfmove_clock = pawn_move || capture ? 0 : parent.halfmove_clock+1;

        // The key needs to know whether an en passant capture is really legal
        // here, which takes the new position's own moves; they are wanted once
        // and thrown away, since only the cursor keeps its move list.
        BB grandchildren[MAX_LEGAL_MOVES];
        int n = std::get<0>(all_moves(&child.pos, grandchildren));
        child.key = position_key(child.pos, effective_en_passant(child.pos, grandchildren, n));

        int id = (int)nodes.size();
        nodes.push_back(child);
        nodes[cursor].children.push_back(id);
        return id;
    }

    // The sibling before or after `id` among the children of its parent.
    int sibling(int id, bool forward) const
    {
        if(nodes[id].parent<0)
        return -1;
        const std::vector<int>& siblings = nodes[nodes[id].parent].children;
        auto it = std::find(siblings.begin(), siblings.end(), id);
        if(it==siblings.end())
        return -1;
        size_t at = (size_t)(it-siblings.begin());
        if(forward)
        return at+1<siblings.size() ? siblings[at+1] : -1;
        return at>0 ? siblings[at-1] : -1;
    }

    // Marks a subtree dead. The nodes stay in the pool so their ids can never
    // come to mean a different move.
    void kill(int id)
    {
        nodes[id].alive = false;
        for(int child : nodes[id].children)
        kill(child);
        nodes[id].children.clear();
    }

    static std::string tag(const std::string& name, const std::string& value)
    {
        return "[" + name + " \"" + value + "\"]\n";
    }

    // "12." before a white move, "12..." before a black one when the reader
    // needs reminding — at the start of a line and after a variation closed.
    std::string number_before(int id, bool force) const
    {
        if(nodes[id].pos.white_move)
        return std::to_string(fullmove_of(id)) + ". ";
        return force ? std::to_string(fullmove_of(id)) + "... " : std::string();
    }

    // The moves after `id`: the main line, with each alternative to a main-line
    // move written as a parenthesised variation right after it.
    void write_moves(int id, bool number_black, std::string& out) const
    {
        bool force = number_black;
        for(int at=id; !nodes[at].children.empty(); )
        {
            const std::vector<int>& children = nodes[at].children;
            append(out, number_before(at, force) + move_text(children[0]));
            force = false;
            for(size_t v=1; v<children.size(); v++)
            {
                append(out, "(" + number_before(at, true) + move_text(children[v]));
                write_moves(children[v], false, out);
                out += ")";
                force = true;   // the main line resumes, so a black move needs its number again
            }
            at = children[0];
        }
    }

    std::string move_text(int id) const
    {
        const Tree_Node& node = nodes[id];
        std::string text = node.san, note = note_text(node.note);
        if(!node.comment.empty())
        note += (note.empty() ? "" : " ") + node.comment;
        if(!note.empty())
        text += " {" + note + "}";
        return text;
    }

    // What an engine's search found, written the way tools/gui_match.cpp writes
    // it: "{+0.35/7 10.00s}", the score in white's view, the depth reached and
    // the time taken. Without this a downloaded game would keep the moves and
    // lose everything the engines thought about them. It reads back as an
    // ordinary PGN comment, so a round trip through a file keeps the text.
    static std::string note_text(const Move_Note& note)
    {
        if(!note.from_engine || note.score_kind.empty())
        return "";
        long long value = std::atoll(note.score_value.c_str());
        char buf[64];
        if(note.score_kind=="mate")
        std::snprintf(buf, sizeof buf, "%sM%lld/%d %.2fs", value<0 ? "-" : "+",
                      value<0 ? -value : value, note.depth, note.time_ms/1000.0);
        else
        std::snprintf(buf, sizeof buf, "%+.2f/%d %.2fs", value/100.0, note.depth, note.time_ms/1000.0);
        return buf;
    }

    // Movetext words are separated by a space, except that nothing follows an
    // opening parenthesis directly.
    static void append(std::string& out, const std::string& word)
    {
        if(!out.empty() && out.back()!='(')
        out += ' ';
        out += word;
    }

    // Movetext at 80 columns, never breaking a word.
    static std::string wrap(const std::string& text)
    {
        std::string out, line;
        size_t i = 0;
        while(i<text.size())
        {
            size_t end = text.find(' ', i);
            std::string word = text.substr(i, end==std::string::npos ? end : end-i);
            i = end==std::string::npos ? text.size() : end+1;
            if(word.empty())
            continue;
            if(!line.empty() && line.size()+1+word.size()>80)
            {
                out += line + "\n";
                line.clear();
            }
            line += line.empty() ? word : " " + word;
        }
        return out + line;
    }

    // ------------------------------------------------------------ PGN reading

    // The tag pairs before the movetext. Only SetUp/FEN change anything here;
    // the rest are read past. `i` ends at the first byte of the movetext.
    static bool read_tags(const std::string& text, size_t& i, BB& start,
                          int& halfmoves, int& fullmove, std::string& error)
    {
        for(;;)
        {
            while(i<text.size() && isspace((unsigned char)text[i]))
            i++;
            if(i>=text.size() || text[i]!='[')
            return true;
            size_t end = text.find(']', i);
            if(end==std::string::npos)
            {
                error = "a PGN tag is never closed";
                return false;
            }
            std::string pair = text.substr(i+1, end-i-1);
            i = end+1;
            size_t quote = pair.find('"');
            if(quote==std::string::npos)
            continue;
            std::string name = pair.substr(0, pair.find(' '));
            size_t close = pair.rfind('"');
            if(close<=quote)
            continue;
            std::string value = pair.substr(quote+1, close-quote-1);
            if(name!="FEN")
            continue;
            if(!uci_parse_fen(value, start))
            {
                error = "the PGN's FEN tag is not a valid position: " + value;
                return false;
            }
            halfmoves = clock_field(value, 5, 0);
            fullmove = clock_field(value, 6, 1);
        }
    }

    // The movetext of the first game in the file, into this (already started)
    // tree. `(` opens a variation on the move just played, so the cursor steps
    // back to the position that move was played from and the stack remembers
    // where to return to.
    bool read_movetext(const std::string& text, size_t i, std::string& error)
    {
        std::vector<int> stack;
        for(;;)
        {
            while(i<text.size() && isspace((unsigned char)text[i]))
            i++;
            if(i>=text.size())
            break;
            char c = text[i];
            if(c=='{')
            {
                size_t end = text.find('}', i);
                if(end==std::string::npos)
                {
                    error = "a PGN comment is never closed";
                    return false;
                }
                if(cursor>0)
                nodes[cursor].comment = one_line(text.substr(i+1, end-i-1));
                i = end+1;
                continue;
            }
            if(c==';')                                  // a comment to the end of the line
            {
                size_t end = text.find('\n', i);
                i = end==std::string::npos ? text.size() : end+1;
                continue;
            }
            if(c=='(')
            {
                if(cursor==0)
                {
                    error = "a PGN variation starts before any move";
                    return false;
                }
                stack.push_back(cursor);
                cursor = nodes[cursor].parent;
                refresh();
                i++;
                continue;
            }
            if(c==')')
            {
                if(stack.empty())
                {
                    error = "a PGN variation is closed that was never opened";
                    return false;
                }
                cursor = stack.back();
                stack.pop_back();
                refresh();
                i++;
                continue;
            }
            if(c=='[')                                  // the next game's tags: this one is done
            break;
            size_t start = i;
            while(i<text.size() && !isspace((unsigned char)text[i])
                  && text[i]!='(' && text[i]!=')' && text[i]!='{' && text[i]!=';')
            i++;
            std::string token = text.substr(start, i-start);
            if(token.empty())
            {
                error = "unreadable PGN near byte " + std::to_string(start);
                return false;
            }
            if(is_result(token))
            {
                if(stack.empty())
                break;                                  // the end of the game
                continue;                               // a result inside a variation: ignore it
            }
            if(token[0]=='$' || token=="*")             // a NAG, or a game with no result yet
            continue;
            // A move number, possibly with the move stuck to it ("1.e4",
            // "1...e5"). The dot is what makes it a number: "0-0" also starts
            // with a digit and is a move.
            if(isdigit((unsigned char)token[0]) && token.find('.')!=std::string::npos)
            {
                size_t at = 0;
                while(at<token.size() && (isdigit((unsigned char)token[at]) || token[at]=='.'))
                at++;
                if(at>=token.size())
                continue;
                token = token.substr(at);
            }
            if(!play_san(token, error))
            return false;
        }
        if(!stack.empty())
        {
            error = "a PGN variation is never closed";
            return false;
        }
        return true;
    }

    // A SAN move from the cursor. Rather than parsing SAN, this names every
    // legal move with the same san() the exporter uses and compares: one
    // spelling of the rules, so import and export cannot disagree.
    bool play_san(const std::string& token, std::string& error)
    {
        std::string want = plain_san(token);
        for(int k=0;k<n_legal;k++)
        if(plain_san(san(nodes[cursor].pos, legal, n_legal, k))==want)
        return play(get_UCI(&nodes[cursor].pos, legal+k));
        error = "\"" + token + "\" is not a legal move in " + fen();
        return false;
    }

    // SAN stripped to what identifies the move: no check or mate mark, no
    // annotation glyphs, castling however it was spelled, and a promotion
    // whether or not it was written with "=".
    static std::string plain_san(const std::string& san)
    {
        std::string out;
        for(char c : san)
        if(c!='+' && c!='#' && c!='!' && c!='?' && c!='=')
        out += c=='0' ? 'O' : c=='o' ? 'O' : c;
        if(!out.empty() && (out.back()=='q' || out.back()=='r' || out.back()=='b' || out.back()=='n'))
        out.back() = (char)toupper(out.back());
        return out;
    }

    static bool is_result(const std::string& token)
    {
        return token=="1-0" || token=="0-1" || token=="1/2-1/2";
    }

    static std::string trim(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t\r\n");
        if(a==std::string::npos)
        return std::string();
        return s.substr(a, s.find_last_not_of(" \t\r\n")-a+1);
    }

    // A comment as one line: every run of whitespace becomes a single space.
    // A comment long enough to be wrapped by the exporter comes back with a
    // newline inside it, and writing that newline out again would wrap the
    // next export differently — so a game would not survive two round trips.
    static std::string one_line(const std::string& s)
    {
        std::string out;
        for(char c : trim(s))
        if(!isspace((unsigned char)c))
        out += c;
        else if(!out.empty() && out.back()!=' ')
        out += ' ';
        return out;
    }

    // A FEN's clock fields (5 and 6); the same reading Session does of a FEN
    // the user typed, kept here so PGN loading does not need it passed in.
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

#endif // GUI_MOVE_TREE_HPP
