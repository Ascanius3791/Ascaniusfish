// OWNERSHIP=Claude
// piecetable() and piece_activity_eval() (src/basic_eval.cpp) taken apart term
// by term, for the GUI's eval terms panel (#44). A debugging aid only: these are
// copies, slower than the originals and used by nothing but the page. If an
// original changes, the copy here goes stale — the page says so, since the
// parts are checked against the original's total every time.
//
// Every part is in each side's own view (positive = good for that side), like
// the panel's other rows, and kept *unscaled*: the original divides its whole
// sum once (by MATERIAL_MAX = 39 for the tables, by 2 for activity), so a part's
// cp value is raw/scale and only the sum of all parts truncates to the total.
#ifndef GUI_EVAL_SPLIT_HPP
#define GUI_EVAL_SPLIT_HPP

#include <string>

struct Eval_Part
{
    const char* name;
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
    static const int MAX_PARTS = 16;
    Eval_Part parts[MAX_PARTS];
    int n = 0;
    int scale = 1;

    Eval_Part& add(const char* name, const std::string& info)
    {
        Eval_Part& p = parts[n++];
        p.name = name;
        p.info = info;
        return p;
    }
    // Truncated like the original's one division, so it can be compared with it.
    int total() const
    {
        int raw = 0;
        for(int k=0;k<n;k++) raw += parts[k].raw[1] - parts[k].raw[0];
        return raw/scale;
    }
};

// piecetable(): per piece type, each side's (opening*OW + endgame*EW) in 39ths.
// count = the number of such pieces; the info carries the opening and endgame
// halves. Integer arithmetic throughout, so grouping by piece type is exact.
inline Eval_Split split_piecetable(const BB* const original, const WEIGHTS& W)
{
    static const char* names[6] = {"Pawns", "Rooks", "Knights", "Bishops", "Queens", "King"};
    Eval_Split s;
    s.scale = MATERIAL_MAX;
    int OW[2], EW[2];
    OW[1] = enemy_material_left_39ths(original, 1);
    OW[0] = enemy_material_left_39ths(original, 0);
    EW[0] = MATERIAL_MAX - OW[0];
    EW[1] = MATERIAL_MAX - OW[1];
    for(int type=0;type<6;type++)
    {
        int opening[2] = {0, 0}, endgame[2] = {0, 0};
        Eval_Part& p = s.add(names[type], "");
        for(int white=0;white<2;white++)
        {
            const int table = white ? type : (type==0 ? 6 : type);
            uint64_t bb = original->Board[type + 6*!white];
            while(bb)
            {
                int i = find_and_delete_trailling_1(bb);
                opening[white] += W.piece_table_value_opening[table][i]*OW[white];
                endgame[white] += W.piece_table_value_endgame[table][i]*EW[white];
                p.count[white]++;
            }
            p.raw[white] = opening[white] + endgame[white];
        }
        auto cp = [](int raw) { return std::to_string((raw + (raw<0 ? -MATERIAL_MAX/2 : MATERIAL_MAX/2))/MATERIAL_MAX); };
        p.info = "opening + endgame table, cp: white " + cp(opening[1]) + " + " + cp(endgame[1])
               + " (" + std::to_string(p.count[1]) + " pieces), black " + cp(opening[0]) + " + " + cp(endgame[0])
               + " (" + std::to_string(p.count[0]) + ")";
    }
    return s;
}

// piece_activity_eval(): one part per weight. "Own"/"enemy" are pieces of that
// colour on a square the piece hits (occupancy-aware for sliders); queens count
// both as a bishop and as a rook, as in the original. The names carry the "/2"
// the original divides its whole sum by, so count × weight / 2 is the cp shown.
inline Eval_Split split_piece_activity(const BB* const original)
{
    Eval_Split s;
    s.scale = 2;
    enum { P_HIT, P_BLOCKED, P_PUSH_HIT, D_OWN, D_ENEMY, D_MOB, S_OWN, S_ENEMY, S_MOB, N_OWN, N_ENEMY, N_MOB, K_OWN, K_ENEMY };
    struct { const char* name; int weight; const char* info; } spec[] = {
        {"P hits a piece ×30/2",     30, "pieces (either colour) on the pawn's capture squares"},
        {"P blocked −20/2",         -20, "a piece right in front of the pawn"},
        {"P hits after push ×20/2",  20, "pieces (either colour) on the squares the pawn would capture on after one push"},
        {"B/Q defends ×10/2",        10, "own pieces a bishop or queen sees diagonally"},
        {"B/Q attacks ×40/2",        40, "enemy pieces a bishop or queen sees diagonally"},
        {"B/Q diagonals ×5/2",        5, "squares a bishop or queen sees diagonally"},
        {"R/Q defends −10/2",       -10, "own pieces a rook or queen sees on lines (a penalty)"},
        {"R/Q attacks ×40/2",        40, "enemy pieces a rook or queen sees on lines"},
        {"R/Q lines ×7/2",            7, "squares a rook or queen sees on lines"},
        {"N defends −10/2",         -10, "own pieces a knight covers (a penalty)"},
        {"N attacks ×10/2",          10, "enemy pieces a knight covers"},
        {"N squares ×5/2",            5, "squares a knight covers, empty board"},
        {"K defends ×15/2",          15, "own pieces next to the king"},
        {"K attacks ×20/2",          20, "enemy pieces next to the king"},
    };
    for(const auto& sp : spec) s.add(sp.name, sp.info);

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
            hit(P_HIT, i, all_pieces & (col ? BP_template[i] : WP_template[i]));
            hit(P_BLOCKED, i, all_pieces & (col ? 1ULL << i+8 : 1ULL << i >> 8));
            hit(P_PUSH_HIT, i, all_pieces & (col ? BP_template[i]<<8 : WP_template[i]>>8));
        }
        uint64_t own_bishops = original->Board[3+6*!col]|original->Board[4+6*!col];
        while(own_bishops)
        {
            int i = find_and_delete_trailling_1(own_bishops);
            uint64_t attacks = get_bishop_attacks(i, all_pieces);
            hit(D_OWN, i, own_pieces & attacks);
            hit(D_ENEMY, i, enemy_pieces & attacks);
            hit(D_MOB, i, attacks, false);
        }
        uint64_t own_rooks = original->Board[1+6*!col]|original->Board[4+6*!col];
        while(own_rooks)
        {
            int i = find_and_delete_trailling_1(own_rooks);
            uint64_t attacks = get_rook_attacks(i, all_pieces);
            hit(S_OWN, i, own_pieces & attacks);
            hit(S_ENEMY, i, enemy_pieces & attacks);
            hit(S_MOB, i, attacks, false);
        }
        uint64_t own_knights = original->Board[2+6*!col];
        while(own_knights)
        {
            int i = find_and_delete_trailling_1(own_knights);
            hit(N_OWN, i, own_pieces & Kn_template[i]);
            hit(N_ENEMY, i, enemy_pieces & Kn_template[i]);
            hit(N_MOB, i, Kn_template[i], false);
        }
        uint64_t own_king = original->Board[5+6*!col];
        while(own_king)
        {
            int i = find_and_delete_trailling_1(own_king);
            hit(K_OWN, i, own_pieces & K_template[i]);
            hit(K_ENEMY, i, enemy_pieces & K_template[i]);
        }
    }
    for(int k=0;k<s.n;k++)
    for(int col=0;col<2;col++)
    s.parts[k].raw[col] = s.parts[k].count[col]*spec[k].weight;
    return s;
}

#endif
