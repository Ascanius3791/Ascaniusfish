// OWNERSHIP=Claude
#ifndef WEIGHT_SET_CPP
#define WEIGHT_SET_CPP
#include "../lib/weight_set.hpp"
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <climits>
#include <fstream>
#include <sstream>
#include <vector>

// One field of a weight set: its key, where it lives in a WEIGHTS and how many
// numbers it has (64 = a piece-square table). `section` is written as a
// comment before it.
struct Weight_Field
{
    std::string key;
    int* values;
    int count;
    const char* section;
};

// Every field, in file order. The pointers go into W.
static int weight_fields(WEIGHTS& W, Weight_Field* out)
{
    static const char* PIECES[6] = {"pawn", "rook", "knight", "bishop", "queen", "king"};
    int n = 0;
    auto add = [&](const std::string& key, int* values, int count, const char* section = nullptr)
    {
        out[n++] = {key, values, count, section};
    };
    add("piece_value", W.piece_value, 6, "material_eval(): pawn rook knight bishop queen king");
    for(int p=0;p<6;p++)
    add(std::string("piece_table_value_opening ") + PIECES[p], W.piece_table_value_opening[p], 64,
        p ? nullptr : "piecetable(): rank 8 first, files a..h, white's view; black reads it rank-flipped");
    for(int p=0;p<6;p++)
    add(std::string("piece_table_value_endgame ") + PIECES[p], W.piece_table_value_endgame[p], 64);
    add("punishment_for_double_pawn", &W.punishment_for_double_pawn, 1, "positional_eval()");
    add("punishment_for_trippled_pawn", &W.punishment_for_trippled_pawn, 1);
    add("punishment_for_isolated_pawn", &W.punishment_for_isolated_pawn, 1);
    add("pawn_supporting_value", &W.pawn_supporting_value, 1);
    add("passed_pawn_value", W.passed_pawn_value, 8, "passed_pawn_eval(): by relative rank, full with bare kings, half with all material");
    add("passed_free_path", W.passed_free_path, 8,
        "passed_pawn_eval()'s endgame modifiers (#90), by relative rank where 8 numbers, scaled to 0 with all material:\n"
        "# nothing ahead; per square (max 5) of the enemy king's / our king's distance to the stop square;\n"
        "# an own pawn beside or protecting it; our rook behind it (+), theirs (-); unstoppable by the\n"
        "# rule of the square (not scaled, the side's best passer only)");
    add("passed_king_enemy", W.passed_king_enemy, 8);
    add("passed_king_own", W.passed_king_own, 8);
    add("passed_supported", W.passed_supported, 8);
    add("passed_rook_behind", &W.passed_rook_behind, 1);
    add("passed_unstoppable", &W.passed_unstoppable, 1);
    add("mobility_value", &W.mobility_value, 1, "basic_eval(): per attacked square");
    add("activity_pawn_attack", &W.activity_pawn_attack, 1,
        "piece_activity_eval(): added for the owner per piece or square hit, halved at the end;\n"
        "# bishop = every diagonal, queens included, rook = every line");
    add("activity_pawn_defend", &W.activity_pawn_defend, 1);
    add("activity_pawn_blocked", &W.activity_pawn_blocked, 1);
    add("activity_pawn_push_attack", &W.activity_pawn_push_attack, 1);
    add("activity_pawn_push_defend", &W.activity_pawn_push_defend, 1);
    add("activity_bishop_defend", &W.activity_bishop_defend, 1);
    add("activity_bishop_attack", &W.activity_bishop_attack, 1);
    add("activity_bishop_square", &W.activity_bishop_square, 1);
    add("activity_rook_defend", &W.activity_rook_defend, 1);
    add("activity_rook_attack", &W.activity_rook_attack, 1);
    add("activity_rook_square", &W.activity_rook_square, 1);
    add("activity_knight_defend", &W.activity_knight_defend, 1);
    add("activity_knight_attack", &W.activity_knight_attack, 1);
    add("activity_knight_square", &W.activity_knight_square, 1);
    add("activity_king_defend", &W.activity_king_defend, 1);
    add("activity_king_attack", &W.activity_king_attack, 1);
    add("tempo_opening", &W.tempo_opening, 1, "tempo_eval(): the side to move's, with all and with no material");
    add("tempo_endgame", &W.tempo_endgame, 1);
    add("ks_attacker_weight", W.ks_attacker_weight, 6,
        "king_safety_eval() (src/king_safety.cpp); by piece: pawn rook knight bishop queen king");
    add("ks_min_attackers", &W.ks_min_attackers, 1);
    add("ks_hit", &W.ks_hit, 1);
    add("ks_weak_ring_square", &W.ks_weak_ring_square, 1);
    add("ks_safe_check", W.ks_safe_check, 6);
    add("ks_unsafe_check", &W.ks_unsafe_check, 1);
    add("ks_no_queen", &W.ks_no_queen, 1);
    add("ks_danger_div", &W.ks_danger_div, 1);
    add("ks_shelter", W.ks_shelter, 8);
    add("ks_open_file", &W.ks_open_file, 1);
    add("ks_storm", W.ks_storm, 8);
    add("ks_storm_blocked_div", &W.ks_storm_blocked_div, 1);
    add("ks_full_piece_material", &W.ks_full_piece_material, 1);
    return n;
}

static const int MAX_WEIGHT_FIELDS = 64;

// A table's k-th number in the file (rank 8 first) is this square.
static inline int weight_table_square(int k)
{
    return (7 - k/8)*8 + k%8;
}

bool read_weight_set(const std::string& text, WEIGHTS& W, Weight_Set_Info& info, std::string& error)
{
    // tokens with their line, comments dropped
    std::vector<std::pair<std::string, int>> tokens;
    {
        std::istringstream in(text);
        std::string line;
        for(int no=1; std::getline(in, line); no++)
        {
            const size_t hash = line.find('#');
            if(hash!=std::string::npos)
            line.resize(hash);
            std::istringstream words(line);
            std::string w;
            while(words >> w)
            tokens.push_back({w, no});
        }
    }

    WEIGHTS out = W;
    Weight_Set_Info meta;
    Weight_Field fields[MAX_WEIGHT_FIELDS];
    const int n_fields = weight_fields(out, fields);
    bool seen[MAX_WEIGHT_FIELDS] = {};
    bool have_version = false;
    auto fail = [&](int line, const std::string& what)
    {
        error = "line " + std::to_string(line) + ": " + what;
        return false;
    };
    auto number = [&](size_t i, int& v)
    {
        const std::string& s = tokens[i].first;
        char* end = nullptr;
        errno = 0;
        const long x = std::strtol(s.c_str(), &end, 10);
        if(s.empty() || *end || errno || x<INT_MIN || x>INT_MAX)
        return false;
        v = (int)x;
        return true;
    };

    for(size_t i=0; i<tokens.size(); )
    {
        const std::string& key = tokens[i].first;
        const int line = tokens[i].second;
        if(key=="version" || key=="parent" || key=="data" || key=="tuner" || key=="loss")
        {
            if(i+1>=tokens.size())
            return fail(line, key + " without a value");
            if(key=="version" || key=="parent")
            {
                int v;
                if(!number(i+1, v) || v<(key=="version" ? 1 : 0))
                return fail(line, key + " is not a number" + (key=="version" ? " >= 1" : " >= 0"));
                if(key=="version")
                {
                    if(have_version)
                    return fail(line, "version given twice");
                    have_version = true;
                    out.version = v;
                }
                else
                meta.parent = v;
            }
            else
            (key=="data" ? meta.data : key=="tuner" ? meta.tuner : meta.loss) = tokens[i+1].first;
            i += 2;
            continue;
        }
        int f = -1;
        size_t at = i+1;
        for(int k=0;k<n_fields && f<0;k++)
        if(fields[k].key==key)
        f = k;
        if(f<0 && i+1<tokens.size())// a table: two words
        for(int k=0;k<n_fields && f<0;k++)
        if(fields[k].key==key + " " + tokens[i+1].first)
        f = k, at = i+2;
        if(f<0)
        return fail(line, "unknown key '" + key + "'");
        if(seen[f])
        return fail(line, "'" + fields[f].key + "' given twice");
        seen[f] = true;
        for(int k=0;k<fields[f].count;k++, at++)
        {
            int v;
            if(at>=tokens.size() || !number(at, v))
            return fail(at<tokens.size() ? tokens[at].second : line,
                        "'" + fields[f].key + "' needs " + std::to_string(fields[f].count) + " numbers, number " + std::to_string(k+1) + " is "
                        + (at<tokens.size() ? "'" + tokens[at].first + "'" : "missing"));
            fields[f].values[fields[f].count==64 ? weight_table_square(k) : k] = v;
        }
        i = at;
    }
    if(!have_version)
    return fail(tokens.empty() ? 0 : tokens.back().second, "no version");
    for(int k=0;k<n_fields;k++)
    if(!seen[k])
    {
        error = "missing '" + fields[k].key + "'";
        return false;
    }
    if(out.ks_danger_div<=0 || out.ks_storm_blocked_div<=0 || out.ks_full_piece_material<=0)
    {
        error = "ks_danger_div, ks_storm_blocked_div and ks_full_piece_material must be > 0";
        return false;
    }
    W = out;
    info = meta;
    return true;
}

bool load_weight_set(const std::string& path, WEIGHTS& W, Weight_Set_Info& info, std::string& error)
{
    std::ifstream in(path);
    if(!in)
    {
        error = "cannot read " + path;
        return false;
    }
    std::ostringstream text;
    text << in.rdbuf();
    if(!read_weight_set(text.str(), W, info, error))
    {
        error = path + ": " + error;
        return false;
    }
    return true;
}

std::string write_weight_set(const WEIGHTS& W, const Weight_Set_Info& info)
{
    WEIGHTS copy = W;
    Weight_Field fields[MAX_WEIGHT_FIELDS];
    const int n_fields = weight_fields(copy, fields);
    std::string s = "# OWNERSHIP=Claude\n"
                    "# A weight set of Ascaniusfish's eval: every number basic_eval() uses (#85).\n"
                    "# Format: lib/weight_set.hpp.\n";
    s += "version " + std::to_string(W.version) + "\n";
    s += "parent " + std::to_string(info.parent) + "\n";
    s += "data " + info.data + "\ntuner " + info.tuner + "\nloss " + info.loss + "\n";
    for(int f=0;f<n_fields;f++)
    {
        const Weight_Field& w = fields[f];
        if(w.section)
        s += std::string("\n# ") + w.section + "\n";
        s += w.key;
        if(w.count==64)
        {
            char cell[16];
            for(int k=0;k<64;k++)
            {
                std::snprintf(cell, sizeof cell, "%5d", w.values[weight_table_square(k)]);
                s += (k%8 ? "" : "\n") + std::string(cell);
            }
        }
        else
        for(int k=0;k<w.count;k++)
        s += " " + std::to_string(w.values[k]);
        s += "\n";
    }
    return s;
}

#endif // WEIGHT_SET_CPP
