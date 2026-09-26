// OWNERSHIP=Claude
// make tt-stats: how often does the TT throw away positions the search asks
// for again later, by depth of the lost entry? (issue #11, lib/tt_stats.hpp)
//
// Plays self-play games from the match openings at a fixed depth per move
// (iterative deepening 1..depth, like UCI "go depth N"). The TT is kept
// across the moves of a game, as in UCI play, and reset between games.
//
//   ./tools/tt_stats [depth=5] [games=4] [plies=160] [openings=tools/openings.epd]
// Built with -DTT_STATS; the TT size is the engine's (-DTT_EXPONENT=n to vary).
#ifndef TT_STATS
#error "tools/tt_stats must be built with -DTT_STATS (use make tt-stats)"
#endif
#include "../lib/uci.hpp"
#include "game_rules.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// The FEN of every opening in an EPD suite (hmvc/fmvn opcodes become the clocks).
static std::vector<std::string> load_opening_fens(const std::string& path)
{
    std::ifstream f(path);
    if(!f)
    {
        std::fprintf(stderr, "cannot open %s\n", path.c_str());
        std::exit(1);
    }
    std::vector<std::string> out;
    std::string line;
    while(std::getline(f, line))
    {
        if(line.empty() || line[0]=='#')
        continue;
        std::istringstream in(line);
        std::string field, fen;
        for(int i=0;i<4 && in>>field;i++) fen += (i ? " " : "") + field;
        std::string ops;
        std::getline(in, ops);
        auto opcode = [&](const std::string& name) -> std::string
        {
            size_t at = ops.find(" " + name + " ");
            if(at==std::string::npos) return "";
            at += name.size()+2;
            return ops.substr(at, ops.find(';', at)-at);
        };
        std::string hmvc = opcode("hmvc"), fmvn = opcode("fmvn");
        out.push_back(fen + " " + (hmvc.empty() ? "0" : hmvc) + " " + (fmvn.empty() ? "1" : fmvn));
    }
    return out;
}

int main(int argc, char** argv)
{
    int depth = 5, games = 4, max_plies = 160;
    std::string openings_path = "tools/openings.epd";
    for(int i=1;i<argc;i++)
    {
        std::string arg = argv[i];
        size_t eq = arg.find('=');
        std::string key = arg.substr(0, eq), value = eq==std::string::npos ? "" : arg.substr(eq+1);
        if(value.empty()) continue;  // unset make variables arrive as "key="
        if(key=="depth") depth = std::atoi(value.c_str());
        else if(key=="games") games = std::atoi(value.c_str());
        else if(key=="plies") max_plies = std::atoi(value.c_str());
        else if(key=="openings") openings_path = value;
        else
        {
            std::fprintf(stderr, "usage: tools/tt_stats [depth=5] [games=4] [plies=160] [openings=tools/openings.epd]\n");
            return 2;
        }
    }

    Zobrist zobrist_keys;
    init_magics();
    init_sliders_attacks(1);//bishop
    init_sliders_attacks(0);//rook

    lookup_table* table = new lookup_table;
    BB* wfh = new BB[UCI_WFH_SIZE];
    BB* path_history = new BB[MAX_SEARCH_PLY];
    CuckooCycleTable* cycle_table = new CuckooCycleTable;
    BB root_children[MAX_LEGAL_MOVES];

    std::vector<std::string> fens = load_opening_fens(openings_path);
    std::printf("tt-stats: depth %d, %d games, max %d plies, TT %d x %d entries\n",
        depth, games, max_plies, 1<<TT_EXPONENT_FOR_SIZE, TT_BUCKET_SIZE);

    long long start_ns = steady_now_ns();
    for(int g=0; g<games; g++)
    {
        const std::string& fen = fens[g % fens.size()];
        BB start;
        if(!uci_parse_fen(fen, start))
        {
            std::fprintf(stderr, "invalid opening FEN: %s\n", fen.c_str());
            return 1;
        }
        std::istringstream clocks(fen.substr(fen.rfind(' ', fen.rfind(' ')-1)+1));
        int halfmoves = 0, fullmove = 1;
        clocks >> halfmoves >> fullmove;
        Game game;
        game.start(start, halfmoves, fullmove);
        table->reset();

        long long nodes_before = search_nodes;
        std::string reason = "ply limit";
        while((int)game.uci_moves.size()<max_plies)
        {
            if(game.outcome(reason)!=Outcome::ONGOING)
            break;
            const BB root = game.positions.back();
            clear_killer_moves();
            // Repetition context, seeded from the game like UCI_Engine::search().
            int seed_count = std::min({(int)game.positions.size(), game.halfmove_clock+1, MAX_SEARCH_PLY});
            for(int i=0;i<seed_count;i++)
            path_history[i] = game.positions[game.positions.size()-seed_count+i];

            PV_Line pv;
            for(int d=1; d<=depth; d++)
            pv = minimax(&root, wfh, d, WEIGHTS_OG, INT_MIN, INT_MAX, table, path_history, seed_count-1, cycle_table);

            auto result = all_moves(&root, root_children);
            int n = std::get<0>(result);
            const std::vector<Move>& moves = std::get<1>(result);
            int best = 0;  // a PV that doesn't start with a legal move falls back to the first one
            for(int i=0;i<n;i++)
            if(pv.current_lenght>0 && moves[i]==pv.moves[0]) { best = i; break; }
            game.play(get_UCI(&root, root_children+best));
        }
        std::printf("game %d: %3zu plies, %10lld nodes, %s\n", g+1, game.uci_moves.size(), search_nodes-nodes_before, reason.c_str());
        std::fflush(stdout);
    }
    std::printf("total %lld nodes in %lld s\n", search_nodes, (steady_now_ns()-start_ns)/1000000000);
    tt_stats::print_report((long long)(1<<TT_EXPONENT_FOR_SIZE)*TT_BUCKET_SIZE);

    delete table;
    delete[] wfh;
    delete[] path_history;
    delete cycle_table;
    return 0;
}
