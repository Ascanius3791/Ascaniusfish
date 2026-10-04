// OWNERSHIP=Claude
// basic_eval() (src/basic_eval.cpp) rebuilt term by term and part by part, for
// the GUI's "Advanced debugging" panel (#44, brought to main by #66). The
// engine's basic_eval() stays one undivided function; everything here is a
// *copy* of its arithmetic, slower than the original and used by nothing but
// the page.
//
// So: WHEN THE EVAL CHANGES, CHANGE THIS FILE WITH IT (and src/plan_eval.cpp's
// incremental copy of the activity terms, if the dreamer is to stay right).
// eval_self_check() below catches what was forgotten. Every rebuilt term is
// compared with the function basic_eval() calls for it, on the
// EVAL_CHECK_FENS and the position on the board, and the real terms'
// sum with basic_eval() itself. A mismatch names the term. If all terms match
// and the sum still does not, a term was added to or dropped from
// basic_eval().
//
// Every part is in each side's own view (positive = good for that side), like
// the panel's other rows, and kept *unscaled*: the original divides its whole
// sum once (by PHASE_MAX = 24 for material and the tables), so a part's cp
// value is raw/scale and only the sum of all parts truncates to the total.
#ifndef GUI_EVAL_SPLIT_HPP
#define GUI_EVAL_SPLIT_HPP

#include "../lib/plan_eval.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

// The net (lib/nne.hpp) in this process (#67), for the breakdown's net row and
// the dreamer: the engine's default file, nets/nne_d6.bin, looked for in the
// working directory and then in the repo root above gui/ascaniusfish_gui.
// Loaded once, on first use; the engines load their own copy.
struct Gui_Net
{
    bool ok = false;
    std::string path, error;
};

inline const Gui_Net& gui_net()
{
    static const Gui_Net net = []
    {
        Gui_Net n;
        const std::string file = "nets/nne_d6.bin";
        n.path = file;
        char exe[4096];
        const ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe)-1);
        if(!std::ifstream(file).good() && len>0)
        {
            const std::string dir(exe, std::string(exe, len).rfind('/')+1);
            for(const std::string& candidate : { dir + "../" + file, dir + file })
            if(std::ifstream(candidate).good())
            {
                n.path = candidate;
                break;
            }
        }
        n.ok = nne::load(n.path, n.error);
        return n;
    }();
    return net;
}

// The net's correction of `pos`, in white's view (nne::correction() is the mover's).
inline double net_correction_white(const BB& pos)
{
    const double c = nne::correction(pos);
    return pos.white_move ? c : -c;
}

struct Eval_Part
{
    std::string name;
    int raw[2] = {0, 0};        // [1] = white, [0] = black, own view, unscaled
    int count[2] = {0, 0};      // what was counted (squares, pieces), for the tooltip
    std::string info;
    // Which piece counted which square, from*64+to, for the tooltip. Not kept
    // for the mobility parts (every square a piece sees), and capped.
    static const int MAX_HITS = 64;
    short hits[2][MAX_HITS];
    int n_hits[2] = {0, 0};
};

struct Eval_Split
{
    static const int MAX_PARTS = 24;
    Eval_Part parts[MAX_PARTS];
    int n = 0;
    int scale = 1;
    // A term that is not a plain sum of its parts (activity: whole role terms
    // plus the mobility, divided once) sets its total from the parts itself.
    bool has_fixed = false;
    int fixed = 0;

    Eval_Part& add(const std::string& name, const std::string& info)
    {
        Eval_Part& p = parts[n++];
        p.name = name;
        p.info = info;
        return p;
    }
    // Truncated like the original's one division, so it can be compared with it.
    int total() const
    {
        if(has_fixed)
        return fixed;
        int raw = 0;
        for(int k=0;k<n;k++) raw += parts[k].raw[1] - parts[k].raw[0];
        return raw/scale;
    }
};

// piecetable(): per piece type, each side's (opening*phase + endgame*(24-phase))
// in PHASE_MAXths. count = the number of such pieces; the info carries the
// opening and endgame halves. Integer arithmetic throughout, so grouping by
// piece type is exact.
inline Eval_Split split_piecetable(const BB* const original, const WEIGHTS& W)
{
    static const char* names[6] = {"Pawns", "Rooks", "Knights", "Bishops", "Queens", "King"};
    Eval_Split s;
    s.scale = PHASE_MAX;
    const int phase = game_phase(original);
    for(int type=0;type<6;type++)
    {
        int opening[2] = {0, 0}, endgame[2] = {0, 0};
        Eval_Part& p = s.add(names[type], "");
        for(int white=0;white<2;white++)
        {
            const int flip = white ? 0 : 56;// black reads the table rank-flipped
            uint64_t bb = original->Board[type + 6*!white];
            while(bb)
            {
                int i = find_and_delete_trailling_1(bb);
                opening[white] += W.piece_table_value_opening[type][i^flip]*phase;
                endgame[white] += W.piece_table_value_endgame[type][i^flip]*(PHASE_MAX-phase);
                p.count[white]++;
            }
            p.raw[white] = opening[white] + endgame[white];
        }
        auto cp = [](int raw) { return std::to_string((raw + (raw<0 ? -PHASE_MAX/2 : PHASE_MAX/2))/PHASE_MAX); };
        p.info = "opening + endgame table, cp: white " + cp(opening[1]) + " + " + cp(endgame[1])
               + " (" + std::to_string(p.count[1]) + " pieces), black " + cp(opening[0]) + " + " + cp(endgame[0])
               + " (" + std::to_string(p.count[0]) + ")";
    }
    return s;
}

// piece_activity_eval(): one part per weight, and one per mobility table. "Own"/
// "enemy" are pieces of that colour on a square the piece hits (occupancy-aware
// for sliders). A mobility part is each piece's table at the squares it attacks
// that no own piece stands on, opening and endgame blended by the phase. Kept in
// PHASE_MAXths; the total is the role parts plus the mobility divided once, as
// in the original.
inline Eval_Split split_piece_activity(const BB* const original, const WEIGHTS& W)
{
    Eval_Split s;
    s.scale = PHASE_MAX;
    const int phase = game_phase(original);
    enum { P_HIT, P_DEF, P_BLOCKED, P_PUSH_HIT, P_PUSH_DEF, B_OWN, B_ENEMY, R_OWN, R_ENEMY, Q_OWN, Q_ENEMY, N_OWN, N_ENEMY, K_OWN, K_ENEMY, N_ROLES };
    struct { const char* name; int weight; const char* info; } spec[] = {
        {"P attacks",            W.activity_pawn_attack, "enemy pieces on the pawn's capture squares"},
        {"P defends",            W.activity_pawn_defend, "own pieces on the pawn's capture squares"},
        {"P blocked",            W.activity_pawn_blocked, "a piece right in front of the pawn"},
        {"P attacks after push", W.activity_pawn_push_attack, "enemy pieces on the squares the pawn would capture on after one push (or two, from the starting rank)"},
        {"P defends after push", W.activity_pawn_push_defend, "own pieces on the squares the pawn would capture on after one push (or two, from the starting rank)"},
        {"B defends",            W.activity_bishop_defend, "own pieces a bishop sees"},
        {"B attacks",            W.activity_bishop_attack, "enemy pieces a bishop sees"},
        {"R defends",            W.activity_rook_defend, "own pieces a rook sees"},
        {"R attacks",            W.activity_rook_attack, "enemy pieces a rook sees"},
        {"Q defends",            W.activity_queen_defend, "own pieces a queen sees"},
        {"Q attacks",            W.activity_queen_attack, "enemy pieces a queen sees"},
        {"N defends",            W.activity_knight_defend, "own pieces a knight covers"},
        {"N attacks",            W.activity_knight_attack, "enemy pieces a knight covers"},
        {"K defends",            W.activity_king_defend, "own pieces next to the king"},
        {"K attacks",            W.activity_king_attack, "enemy pieces next to the king"},
    };
    for(const auto& sp : spec)// "P attacks ×30", "P blocked −20"
    s.add(std::string(sp.name) + (sp.weight<0 ? " −" : " ×") + std::to_string(std::abs(sp.weight)), sp.info);
    struct { const char* name; int type; const int* op; const int* eg; } mob[] = {
        {"B mobility", 3, W.mobility_bishop_opening, W.mobility_bishop_endgame},
        {"R mobility", 1, W.mobility_rook_opening, W.mobility_rook_endgame},
        {"Q mobility", 4, W.mobility_queen_opening, W.mobility_queen_endgame},
        {"N mobility", 2, W.mobility_knight_opening, W.mobility_knight_endgame},
    };
    for(const auto& m : mob)
    s.add(m.name, "its table at the squares the piece attacks that no own piece stands on (count: those squares, all its pieces)");

    uint64_t all_black_pieces = original->Board[6]|original->Board[7]|original->Board[8]|original->Board[9]|original->Board[10]|original->Board[11];
    uint64_t all_white_pieces = original->Board[0]|original->Board[1]|original->Board[2]|original->Board[3]|original->Board[4]|original->Board[5];
    uint64_t all_pieces = all_black_pieces|all_white_pieces;
    for(int col=0;col<2;col++)
    {
        uint64_t own_pieces = col ? all_white_pieces : all_black_pieces;
        uint64_t enemy_pieces = col ? all_black_pieces : all_white_pieces;
        auto hit = [&](int part, int from, uint64_t targets, bool record = true)
        {
            Eval_Part& p = s.parts[part];
            p.count[col] += count(targets);
            while(record && targets && p.n_hits[col] < Eval_Part::MAX_HITS)
            p.hits[col][p.n_hits[col]++] = (short)(from*64 + find_and_delete_trailling_1(targets));
        };
        uint64_t own_pawns = original->Board[0+6*!col];
        while(own_pawns)
        {
            int i = find_and_delete_trailling_1(own_pawns);
            hit(P_HIT, i, enemy_pieces & (col ? BP_template[i] : WP_template[i]));
            hit(P_DEF, i, own_pieces & (col ? BP_template[i] : WP_template[i]));
            hit(P_BLOCKED, i, all_pieces & (col ? 1ULL << i+8 : 1ULL << i >> 8));
            const uint64_t push_attacks = col ? (BP_template[i]<<8 | (i/8==1 ? BP_template[i]<<16 : 0)) : (WP_template[i]>>8 | (i/8==6 ? WP_template[i]>>16 : 0));
            hit(P_PUSH_HIT, i, enemy_pieces & push_attacks);
            hit(P_PUSH_DEF, i, own_pieces & push_attacks);
        }
        for(int k=0;k<4;k++)
        {
            const int type = mob[k].type, own_part = type==3 ? B_OWN : type==1 ? R_OWN : type==4 ? Q_OWN : N_OWN;
            uint64_t pieces = original->Board[type+6*!col];
            while(pieces)
            {
                int i = find_and_delete_trailling_1(pieces);
                const uint64_t attacks = type==3 ? get_bishop_attacks(i, all_pieces) : type==1 ? get_rook_attacks(i, all_pieces)
                                       : type==4 ? get_bishop_attacks(i, all_pieces) | get_rook_attacks(i, all_pieces) : Kn_template[i];
                hit(own_part, i, own_pieces & attacks);
                hit(own_part+1, i, enemy_pieces & attacks);
                const int m = count(attacks & ~own_pieces);
                Eval_Part& p = s.parts[N_ROLES+k];
                p.count[col] += m;
                p.raw[col] += mob[k].op[m]*phase + mob[k].eg[m]*(PHASE_MAX-phase);
            }
        }
        uint64_t own_king = original->Board[5+6*!col];
        while(own_king)
        {
            int i = find_and_delete_trailling_1(own_king);
            hit(K_OWN, i, own_pieces & K_template[i]);
            hit(K_ENEMY, i, enemy_pieces & K_template[i]);
        }
    }
    int roles = 0, mobility = 0;
    for(int k=0;k<N_ROLES;k++)
    for(int col=0;col<2;col++)
    {
        s.parts[k].raw[col] = s.parts[k].count[col]*spec[k].weight*PHASE_MAX;
        roles += (col ? 1 : -1)*s.parts[k].count[col]*spec[k].weight;
    }
    for(int k=N_ROLES;k<s.n;k++)
    mobility += s.parts[k].raw[1] - s.parts[k].raw[0];
    s.has_fixed = true;
    s.fixed = roles + mobility/PHASE_MAX;
    return s;
}

// material_eval(): each side's piece values by type (the kings cancel), opening
// and endgame blended by the phase, in PHASE_MAXths.
inline Eval_Split split_material(const BB* const original, const WEIGHTS& W)
{
    static const char* names[5] = {"Pawns", "Rooks", "Knights", "Bishops", "Queens"};
    Eval_Split s;
    s.scale = PHASE_MAX;
    const int phase = game_phase(original);
    for(int type=0;type<5;type++)
    {
        const int blended = W.piece_value[type]*phase + W.piece_value_endgame[type]*(PHASE_MAX-phase);
        Eval_Part& p = s.add(names[type], "piece value " + std::to_string(W.piece_value[type]) + " (opening) to "
                             + std::to_string(W.piece_value_endgame[type]) + " (endgame), now " + std::to_string(blended/PHASE_MAX) + " each");
        for(int white=0;white<2;white++)
        {
            p.count[white] = count(original->Board[type + 6*!white]);
            p.raw[white] = blended*p.count[white];
        }
    }
    return s;
}

// king_safety_eval() (src/king_safety.cpp): each king's attack and shelter
// penalty, as king_safety_detail() gives them, own view (<= 0).
inline Eval_Split split_king_safety(const BB* const original, const WEIGHTS& W)
{
    Eval_Split s;
    const King_Safety_Detail d[2] = { king_safety_detail(original, false, W), king_safety_detail(original, true, W) };
    Eval_Part& attack = s.add("Attack on the king", "enemy pieces reaching the king ring: white's king "
        + std::to_string(d[1].attackers) + " attackers / " + std::to_string(d[1].danger_units) + " danger units, black's king "
        + std::to_string(d[0].attackers) + " / " + std::to_string(d[0].danger_units));
    Eval_Part& shelter = s.add("Pawn shelter", "pawn cover in front of each king, scaled by the enemy's pieces (knight and bishop 1, rook 2, queen 4)");
    for(int white=0;white<2;white++)
    {
        attack.raw[white] = -d[white].attack_penalty;
        attack.count[white] = d[white].attackers;
        shelter.raw[white] = -d[white].shelter_penalty;
    }
    return s;
}

// positional_eval() = pawn_struckture_eval_of_colour(white) - (black).
inline Eval_Split split_pawn_structure(const BB* const original, const WEIGHTS& W)
{
    Eval_Split s;
    enum { DOUBLED, TRIPLED, ISOLATED };
    Eval_Part* part[3];
    part[DOUBLED] = &s.add("Doubled", "−" + std::to_string(W.punishment_for_double_pawn) + " per file with two or more own pawns");
    part[TRIPLED] = &s.add("Tripled", "another −" + std::to_string(W.punishment_for_trippled_pawn) + " per file with three or more");
    part[ISOLATED] = &s.add("Isolated", "−" + std::to_string(W.punishment_for_isolated_pawn) + " per pawn with no own pawn on a neighbouring file");
    auto record = [](Eval_Part* p, int white, int from, int to)
    {
        if(p->n_hits[white] < Eval_Part::MAX_HITS)
        p->hits[white][p->n_hits[white]++] = (short)(from*64 + to);
    };
    for(int white=0;white<2;white++)
    {
        const uint64_t pawns = original->Board[0+6*!white];
        for(int j=0;j<8;j++)
        {
            const int n = count(pawns & mask_column[j]);
            part[DOUBLED]->count[white] += n>1;
            part[TRIPLED]->count[white] += n>2;
        }
        uint64_t virtual_pawns = pawns;
        while(virtual_pawns)
        {
            const int i = find_and_delete_trailling_1(virtual_pawns);
            const int column = i%8;
            const bool left = column>0 && (mask_column[column-1] & pawns);
            const bool right = column<7 && (mask_column[column+1] & pawns);
            if(!left && !right)
            {
                part[ISOLATED]->count[white]++;
                record(part[ISOLATED], white, i, i);
            }
        }
        part[DOUBLED]->raw[white] = -W.punishment_for_double_pawn*part[DOUBLED]->count[white];
        part[TRIPLED]->raw[white] = -W.punishment_for_trippled_pawn*part[TRIPLED]->count[white];
        part[ISOLATED]->raw[white] = -W.punishment_for_isolated_pawn*part[ISOLATED]->count[white];
    }
    return s;
}

// passed_pawn_eval() (#84, #90): each passed pawn's passed_pawn_parts(), one
// part per modifier. The unstoppable bonus counts once per side, for its best
// passer, as there.
inline Eval_Split split_passed(const BB* const original, const WEIGHTS& W)
{
    Eval_Split s;
    const int phase = game_phase(original);
    std::string by_rank;
    for(int r=1;r<7;r++)
    by_rank += (r>1 ? " " : "") + std::to_string(passed_pawn_bonus(r, phase, W));
    const std::string scaled = "; endgame value, scaled to 0 with all pieces on (now "
                             + std::to_string(100*(PHASE_MAX-phase)/PHASE_MAX) + "%)";
    Eval_Part* part[PASSER_PARTS];
    part[PASSER_BASE] = &s.add("Passed pawns", "no enemy pawn ahead on its own or a neighbouring file; ranks 2-7 give "
                               + by_rank + " cp at phase " + std::to_string(phase) + "/" + std::to_string(PHASE_MAX));
    part[PASSER_PATH] = &s.add("Free path", "no piece on any square ahead of the passer" + scaled);
    part[PASSER_KING] = &s.add("King distance", "the enemy king's distance to the stop square (up to 5) against ours, by rank" + scaled);
    part[PASSER_SUPPORT] = &s.add("Supported", "an own pawn beside the passer or protecting it" + scaled);
    part[PASSER_ROOK] = &s.add("Rook behind", "the first piece behind the passer on its file is a rook: ours +, theirs -" + scaled);
    part[PASSER_SQUARE] = &s.add("Unstoppable", "rule of the square: path free, the enemy has king and pawns only and its king "
                                 "cannot reach the promotion square in time; the side's best passer only");
    for(int white=0;white<2;white++)
    {
        uint64_t passed = passed_pawns_of_colour(original, white);
        while(passed)
        {
            const int i = find_and_delete_trailling_1(passed);
            int value[PASSER_PARTS];
            passed_pawn_parts(original, white, i, phase, W, value);
            for(int k=0;k<PASSER_PARTS;k++)
            {
                Eval_Part& p = *part[k];
                if(value[k]==0 && k!=PASSER_BASE)
                continue;
                p.count[white]++;
                p.raw[white] = k==PASSER_SQUARE ? std::max(p.raw[white], value[k]) : p.raw[white] + value[k];
                if(p.n_hits[white] < Eval_Part::MAX_HITS)
                p.hits[white][p.n_hits[white]++] = (short)(i*64 + i);
            }
        }
    }
    return s;
}

// tempo_eval() (#70): the side to move's tempo, from W.tempo_endgame with no
// pieces to W.tempo_opening with all of them, blended by the phase.
inline Eval_Split split_tempo(const BB* const original, const WEIGHTS& W)
{
    Eval_Split s;
    const int phase = game_phase(original);
    Eval_Part& p = s.add("Side to move", "the mover's tempo: " + std::to_string(W.tempo_endgame) + " cp with no pieces to "
                         + std::to_string(W.tempo_opening) + " with all of them; phase here " + std::to_string(phase) + "/" + std::to_string(PHASE_MAX));
    const int mover = original->white_move;
    p.count[mover] = 1;
    p.raw[mover] = (W.tempo_opening*phase + W.tempo_endgame*(PHASE_MAX-phase))/PHASE_MAX;
    return s;
}

// One row of the breakdown: a term of basic_eval(), rebuilt from its parts
// (`rebuilt`) and as basic_eval() computes it (`real`, from the function it
// calls; a term with no function of its own has real == rebuilt). Both white's
// view.
struct Eval_Row
{
    const char* name = "";
    const char* function = "";   // what basic_eval() calls for it, for the messages
    bool has_real = true;
    int real = 0;
    int rebuilt = 0;
    Eval_Split split;
};

static const int EVAL_ROWS = 7;

// Every term in basic_eval()'s order. `basic` is basic_eval() itself and
// `real_sum` the real terms added up, so real_sum != basic means basic_eval()
// has a term this file does not know (or lost one it does).
struct Eval_Breakdown
{
    Eval_Row rows[EVAL_ROWS];
    int real_sum = 0;
    int rebuilt_sum = 0;
    int basic = 0;
};

inline Eval_Breakdown eval_breakdown(const BB* const pos, const WEIGHTS& W)
{
    Eval_Breakdown b;
    auto set = [&](int k, const char* name, const char* function, bool has_real, int real, const Eval_Split& split)
    {
        Eval_Row& r = b.rows[k];
        r.name = name;
        r.function = function;
        r.has_real = has_real;
        r.split = split;
        r.rebuilt = split.total();
        r.real = has_real ? real : r.rebuilt;
        b.real_sum += r.real;
        b.rebuilt_sum += r.rebuilt;
    };
    set(0, "Material",         "material_eval()",       true,  material_eval(pos, W),       split_material(pos, W));
    set(1, "Piece tables",     "piecetable()",          true,  piecetable(pos, W),          split_piecetable(pos, W));
    set(2, "King safety",      "king_safety_eval()",    true,  king_safety_eval(pos, W),    split_king_safety(pos, W));
    set(3, "Pawn structure",   "positional_eval()",     true,  positional_eval(pos, W),     split_pawn_structure(pos, W));
    set(4, "Passed pawns",     "passed_pawn_eval()",    true,  passed_pawn_eval(pos, W),    split_passed(pos, W));
    set(5, "Piece activity",   "piece_activity_eval()", true,  piece_activity_eval(pos, W), split_piece_activity(pos, W));
    set(6, "Tempo",            "tempo_eval()",          true,  tempo_eval(pos, W),          split_tempo(pos, W));
    b.basic = basic_eval(pos, W);
    return b;
}

// The positions eval_self_check() runs on: tools/bench.cpp's, opening to
// pawn endings.
static const char* const EVAL_CHECK_FENS[] =
{
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 10",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 11",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
    "rq3rk1/ppp2ppp/1bnpb3/3N2B1/3NP3/7P/PPPQ1PP1/2KR3R w - - 7 14",
    "r1bq1r1k/1pp1n1pp/1p1p4/4p2Q/4Pp2/1BNP4/PPP2PPP/3R1RK1 w - - 2 14",
    "r3r1k1/2p2ppp/p1p1bn2/8/1q2P3/2NPQN2/PPP3PP/R4RK1 b - - 2 15",
    "r1bbk1nr/pp3p1p/2n5/1N4p1/2Np1B2/8/PPP2PPP/2KR1B1R w kq - 0 13",
    "r1bq1rk1/ppp1nppp/4n3/3p3Q/3P4/1BP1B3/PP1N2PP/R4RK1 w - - 1 16",
    "4r1k1/r1q2ppp/ppp2n2/4P3/5Rb1/1N1BQ3/PPP3PP/R5K1 w - - 1 17",
    "2rqkb1r/ppp2p2/2npb1p1/1N1Nn2p/2P1PP2/8/PP2B1PP/R1BQK2R b KQ - 0 11",
    "r1bq1r1k/b1p1npp1/p2p3p/1p6/3PP3/1B2NN2/PP3PPP/R2Q1RK1 w - - 1 16",
    "3r1rk1/p5pp/bpp1pp2/8/q1PP1P2/b3P3/P2NQRPP/1R2B1K1 b - - 6 22",
    "r1q2rk1/2p1bppp/2Pp4/p6b/Q1PNp3/4B3/PP1R1PPP/2K4R w - - 2 18",
    "4k2r/1pb2ppp/1p2p3/1R1p4/3P4/2r1PN2/P4PPP/1R4K1 b - - 3 22",
    "3q2k1/pb3p1p/4pbp1/2r5/PpN2N2/1P2P2P/5PP1/Q2R2K1 b - - 4 26",
    "6k1/3b3r/1p1p4/p1n2p2/1PPNpP1q/P3Q1p1/1R1RB1P1/5K2 b - - 0 1",
    "r2r1n2/pp2bk2/2p1p2p/3q4/3PN1QP/2P3R1/P4PP1/5RK1 w - - 0 1",
    "6k1/6p1/6Pp/ppp5/3pn2P/1P3K2/1PP2P2/8 b - - 0 1",
    "8/8/8/8/5kp1/P7/8/1K1N4 w - - 0 1",
    "8/8/8/5N2/8/p7/8/2NK3k w - - 0 1",
    "8/3k4/8/8/8/4B3/4KB2/2B5 w - - 0 1",
    "8/8/1P6/5pr1/8/4R3/7k/2K5 w - - 0 1",
    "8/2p4P/8/kr6/6R1/8/8/1K6 w - - 0 1",
    "8/8/3P3k/8/1p6/8/1P6/1K3n2 b - - 0 1",
    "8/R7/2q5/8/6k1/8/1P5p/K6R w - - 0 124",
    "8/8/8/8/8/6k1/6p1/6K1 w - - 0 1",
};

// What eval_self_check() found. `messages` is empty when everything matched;
// otherwise one line per term that differs, and one for a sum that does not.
struct Eval_Check
{
    int positions = 0;
    double ms = 0;
    std::vector<std::string> messages;
    bool ok() const { return messages.empty(); }
};

// The rebuild against the real eval on every EVAL_CHECK_FENS position and on
// `current`: each term, then the sum. With `dreamer`, also the plan term's fast
// db against the reference db (src/plan_eval.cpp keeps its own copy of the
// activity terms). With `net` (#67; gui_net() must be loaded), the dreamer is
// checked as the page shows it, with the net in db, and the net's
// incremental first layer, which the dreamer leans on, against one summed
// from scratch. Well under 0.1 s.
inline Eval_Check eval_self_check(const BB& current, bool dreamer, bool net = false, const WEIGHTS& W = WEIGHTS_OG)
{
    const auto start = std::chrono::steady_clock::now();
    Eval_Check c;
    struct Miss { int n = 0, worst = 0; std::string fen; };
    Miss rows[EVAL_ROWS], sum, plan, incremental;
    auto note = [](Miss& m, int diff, const std::string& fen)
    {
        if(diff==0)
        return;
        if(m.n++==0)
        m.fen = fen;
        m.worst = std::max(m.worst, std::abs(diff));
    };
    auto check = [&](const BB& pos, const std::string& fen)
    {
        const Eval_Breakdown b = eval_breakdown(&pos, W);
        for(int k=0;k<EVAL_ROWS;k++)
        note(rows[k], b.rows[k].rebuilt - b.rows[k].real, fen);
        note(sum, b.basic - b.real_sum, fen);
        if(dreamer)
        note(plan, plan_eval_detail(&pos, W, nullptr, nullptr, false, PLAN_Q2_MODE, net)
                 - plan_eval_detail(&pos, W, nullptr, nullptr, true, PLAN_Q2_MODE, net), fen);
        if(net)// bit for bit: rounding to cp would hide a drifting accumulator
        note(incremental, nne::correction(pos) != nne::correction_from_scratch(pos), fen);
        c.positions++;
    };
    check(current, "the position on the board");
    for(const char* fen : EVAL_CHECK_FENS)
    {
        BB pos;
        if(uci_parse_fen(fen, pos))
        check(pos, fen);
    }
    const Eval_Breakdown names = eval_breakdown(&current, W);
    auto where = [](const Miss& m) { return " on " + std::to_string(m.n) + " position(s), by up to " + std::to_string(m.worst) + " cp; first: " + m.fen; };
    bool terms_ok = true;
    for(int k=0;k<EVAL_ROWS;k++)
    if(rows[k].n)
    {
        terms_ok = false;
        c.messages.push_back(std::string(names.rows[k].name) + ": the rebuild (gui/eval_split.hpp) differs from "
                             + names.rows[k].function + where(rows[k]));
    }
    if(sum.n)
    c.messages.push_back(std::string(terms_ok ? "Every term matches, but " : "Also, ")
                         + "basic_eval() is not the sum of the terms gui/eval_split.hpp knows" + where(sum)
                         + ": a term was added to or dropped from basic_eval() (or its inline attacked-squares line changed)");
    if(plan.n)
    c.messages.push_back("Dreamer: the fast plan term differs from the reference" + where(plan)
                         + ": src/plan_eval.cpp's copy of the activity terms is out of date");
    if(incremental.n)
    c.messages.push_back("Net: nne::correction() differs from nne::correction_from_scratch() on "
                         + std::to_string(incremental.n) + " position(s); first: " + incremental.fen
                         + ": the kept first layer (src/nne.cpp) drifted");
    c.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return c;
}

#endif
