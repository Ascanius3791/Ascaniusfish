// OWNERSHIP=Claude
#include "../ascaniusfish.hpp"
#include "../ascaniusfish_2.hpp"

#include <chrono>
#include <iostream>
#include <vector>

// Measures how much of minimax()'s search cost is attributable to imperfect
// move ordering, by comparing two searches of the SAME real game positions:
//
//   baseline: fresh, empty TT - ordinary sorting_eval() heuristic ordering only.
//   oracle:   fresh, empty TT, pre-seeded with the baseline's OWN previously
//             computed PV, one entry per node along that PV, so sorting_moves()
//             tries the correct move first at every ply - the best case an
//             ordering heuristic could ever achieve, since the "hint" is the
//             actual answer.
//
// The seeded entries must never let the TT do the search's job for it (a
// literal exact-value cache hit would trivially skip whole subtrees, which
// measures caching, not ordering - the reason plain "TT warming" was rejected
// as a test methodology). Two independent safety nets keep them inert on the
// alpha/beta side (see minimax(), ascaniusfish_2.hpp ~line 155):
//   1. eval=INT_MIN paired with bound_type=-1 (lower bound): the update is
//      `alpha=max(alpha,INT_MIN)`, always a no-op regardless of the window.
//   2. depth=HARMLESS_HINT_DEPTH (far above any real search depth used here):
//      the alpha/beta-tightening branch is gated on `depth==stored.depth`
//      exactly, so with a stored depth no real search depth can ever equal,
//      that branch is never even entered.
// What DOES always fire is `tt_hint=readout.pv_line` right after, unconditional
// on the branch above - that's the only channel these entries have: the first
// move sorting_moves() tries. Because of this, baseline and oracle searches of
// the same position are mathematically guaranteed to reach the same root eval
// (alpha-beta's node count depends on move order; its root value does not) -
// checked live below as a correctness guard on the whole methodology.

static const int HARMLESS_HINT_DEPTH = 1000;

// Replays a single move from `pos` using ordinary move generation, to walk a
// recorded PV_Line one node at a time without re-running search.
static bool apply_move(const BB& pos, const Move& mv, BB& out)
{
    static BB scratch[300];
    auto result = all_moves(&pos, scratch);
    int n = std::get<0>(result);
    std::vector<Move> moves = std::get<1>(result);
    for(int i=0;i<n;i++)
    {
        if(moves[i]==mv)
        {
            out = scratch[i];
            return true;
        }
    }
    return false;
}

// Inserts one inert move-ordering hint per node along `pv`, starting at `root`.
static void seed_table_with_pv(lookup_table& table, const BB& root, const PV_Line& pv)
{
    BB pos = root;
    int len = std::min(pv.current_lenght, MAX_PV_Lenght);
    for(int i=0;i<len;i++)
    {
        Move mv = pv.moves[i];

        PV_Line hint;
        hint.moves[0] = mv;
        hint.current_lenght = 1;
        hint.depth = HARMLESS_HINT_DEPTH;
        hint.eval = INT_MIN;
        hint.bound_type = -1;

        TT_entry entry;
        entry.board = pos;
        entry.zobrist_hash = pos.zobrist_hash;
        entry.pv_line = hint;
        entry.initialized = true;
        table.insert(entry);

        if(i+1<len)
        {
            BB next;
            if(!apply_move(pos, mv, next))
            break; // a PV move must be legal from its own node - shouldn't happen
            pos = next;
        }
    }
}

// Re-probes the table after a search to check how many of the oracle hints we
// planted are still there, still carrying our sentinel depth/bound_type - i.e.
// verifies the eviction-immunity argument (HARMLESS_HINT_DEPTH dominates every
// real entry's value_for_victim_index and always loses the "is this a deeper
// entry for the same board" replacement check in lookup_table::insert()),
// rather than just trusting it.
static std::pair<int,int> count_surviving_hints(lookup_table& table, const BB& root, const PV_Line& pv)
{
    BB pos = root;
    int len = std::min(pv.current_lenght, MAX_PV_Lenght);
    int survived=0;
    for(int i=0;i<len;i++)
    {
        TT_readout readout = table.is_retrivable_eval(&pos, 0);
        if(readout.is_found && readout.pv_line.depth==HARMLESS_HINT_DEPTH && readout.pv_line.bound_type==-1
           && readout.pv_line.current_lenght>0 && readout.pv_line.moves[0]==pv.moves[i])
        survived++;

        if(i+1<len)
        {
            BB next;
            if(!apply_move(pos, pv.moves[i], next))
            break;
            pos = next;
        }
    }
    return {survived, len};
}

// Mirrors Play::engine_move()'s iterative-deepening loop (ascaniusfish_2.hpp
// ~line 949) closely enough to reuse its repetition-context wiring, but
// returns the final PV_Line instead of consuming it - engine_move() itself
// only returns an eval int, which isn't enough to seed the oracle table or to
// know the chosen move for arbitrary callers.
static PV_Line search_position_get_pv(const BB* original, BB* wfh, int depth, lookup_table* table, const CuckooCycleTable& cycle_table, BB* path_history_buf)
{
    auto result = all_moves(original, wfh);
    int number_of_new_moves = std::get<0>(result);

    int seed_count = std::min({(int)history.size(), original->halfmoves_since_last_capture_or_pawn_move+1, MAX_SEARCH_PLY});
    for(int i=0;i<seed_count;i++)
    path_history_buf[i] = history[history.size()-seed_count+i];
    int ply = (seed_count>0) ? seed_count-1 : 0;

    PV_Line pv_line;
    for(int d=1; d<=depth; d++)
    pv_line = minimax(original, wfh+number_of_new_moves, d, WEIGHTS_OG, INT_MIN, INT_MAX, table, path_history_buf, ply, &cycle_table);
    return pv_line;
}

// Same as search_position_get_pv() but WITHOUT the d=1..depth-1 iterative-
// deepening warm-up: a single minimax() call straight at the target depth,
// against whatever table state the caller already set up (fresh+empty for
// "cold baseline", fresh+seeded for "cold oracle"). engine_move()'s real ID
// loop reuses the SAME table across its own d=1..depth iterations, so even
// "baseline" there already benefits from real TT-derived move-ordering hints
// built up by its own cheaper shallow passes before the deepest iteration
// runs - that self-bootstrapping shrinks the oracle seed's apparent edge to
// "on top of already-decent ordering" rather than "vs none at all". This cold
// variant removes that confound to show the raw ordering-quality ceiling.
static PV_Line search_single_depth_get_pv(const BB* original, BB* wfh, int depth, lookup_table* table, const CuckooCycleTable& cycle_table, BB* path_history_buf)
{
    auto result = all_moves(original, wfh);
    int number_of_new_moves = std::get<0>(result);

    int seed_count = std::min({(int)history.size(), original->halfmoves_since_last_capture_or_pawn_move+1, MAX_SEARCH_PLY});
    for(int i=0;i<seed_count;i++)
    path_history_buf[i] = history[history.size()-seed_count+i];
    int ply = (seed_count>0) ? seed_count-1 : 0;

    return minimax(original, wfh+number_of_new_moves, depth, WEIGHTS_OG, INT_MIN, INT_MAX, table, path_history_buf, ply, &cycle_table);
}

int main()
{
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
    Zobrist zobrist_init = Zobrist();

    CuckooCycleTable cycle_table;

    const int NUM_MOVES = 10;
    const int MIN_DEPTH = 1, MAX_DEPTH = 5;
    const int WFH_SIZE = 3000;

    BB* wfh = new BB[WFH_SIZE];
    BB* path_history_buf = new BB[MAX_SEARCH_PLY];
    lookup_table* baseline_table = new lookup_table();
    lookup_table* test_table = new lookup_table();

    std::cout << "Oracle move-ordering pruning experiment\n";
    std::cout << "========================================\n";

    for(int depth=MIN_DEPTH; depth<=MAX_DEPTH; depth++)
    {
        history.clear();
        BB position;
        initialize_FEN_to::Standartboard(position.Board);
        castling_rights(&position);
        position.zobrist_hash = Zobrist::compute_Zobrist_Hash(position);
        history.push_back(position);

        double baseline_total_ms=0, test_total_ms=0;
        long long baseline_total_nodes=0, test_total_nodes=0;
        double cold_baseline_total_ms=0, cold_oracle_total_ms=0;
        long long cold_baseline_total_nodes=0, cold_oracle_total_nodes=0;

        std::cout << "\n--- Depth " << depth << " ---\n";

        int move_no=1;
        for(; move_no<=NUM_MOVES; move_no++)
        {
            if(result(&position)!=0)
            {
                std::cout << "  Game ended early before move " << move_no << ", stopping this depth.\n";
                break;
            }

            auto probe = all_moves(&position, wfh);
            int n_moves = std::get<0>(probe);
            if(n_moves==0)
            {
                std::cout << "  No legal moves before move " << move_no << ", stopping this depth.\n";
                break;
            }
            if(n_moves==1)
            {
                BB next;
                apply_move(position, std::get<1>(probe)[0], next);
                position = next;
                history.push_back(position);
                std::cout << "  Move " << move_no << ": forced move, search skipped.\n";
                continue;
            }

            baseline_table->reset();
            int nodes_before = number_of_mimimax_calls;
            auto t0 = std::chrono::high_resolution_clock::now();
            PV_Line baseline_pv = search_position_get_pv(&position, wfh, depth, baseline_table, cycle_table, path_history_buf);
            auto t1 = std::chrono::high_resolution_clock::now();
            long long baseline_nodes = number_of_mimimax_calls - nodes_before;
            double baseline_ms = std::chrono::duration<double, std::milli>(t1-t0).count();

            test_table->reset();
            seed_table_with_pv(*test_table, position, baseline_pv);
            nodes_before = number_of_mimimax_calls;
            auto t2 = std::chrono::high_resolution_clock::now();
            PV_Line oracle_pv = search_position_get_pv(&position, wfh, depth, test_table, cycle_table, path_history_buf);
            auto t3 = std::chrono::high_resolution_clock::now();
            long long oracle_nodes = number_of_mimimax_calls - nodes_before;
            double oracle_ms = std::chrono::duration<double, std::milli>(t3-t2).count();

            auto [hints_survived, hints_planted] = count_surviving_hints(*test_table, position, baseline_pv);
            if(hints_survived != hints_planted)
            {
                std::cout << "  WARNING: only " << hints_survived << "/" << hints_planted
                          << " oracle hints survived the search - some got evicted or overwritten!\n";
            }

            if(baseline_pv.eval != oracle_pv.eval)
            {
                std::cout << "  WARNING: eval mismatch at move " << move_no
                          << " (baseline=" << baseline_pv.eval << ", oracle=" << oracle_pv.eval
                          << ") - the seeded hints should never change the root value, only node count!\n";
            }

            double speedup = oracle_ms>0 ? baseline_ms/oracle_ms : 0.0;
            double node_reduction = baseline_nodes>0 ? 100.0*(1.0 - (double)oracle_nodes/baseline_nodes) : 0.0;

            std::cout << "  Move " << move_no
                      << ": baseline=" << baseline_ms << "ms (" << baseline_nodes << " nodes)"
                      << "  oracle=" << oracle_ms << "ms (" << oracle_nodes << " nodes)"
                      << "  speedup=" << speedup << "x"
                      << "  node_reduction=" << node_reduction << "%\n";

            baseline_total_ms += baseline_ms;
            test_total_ms += oracle_ms;
            baseline_total_nodes += baseline_nodes;
            test_total_nodes += oracle_nodes;

            // Cold variant: same position, same oracle PV (still ground truth
            // for this position/depth - it doesn't depend on how it's re-used),
            // but neither search gets ID's own d=1..depth-1 warm-up. Reuses
            // baseline_table/test_table since their ID-based contents from
            // above are no longer needed.
            baseline_table->reset();
            nodes_before = number_of_mimimax_calls;
            auto tc0 = std::chrono::high_resolution_clock::now();
            PV_Line cold_baseline_pv = search_single_depth_get_pv(&position, wfh, depth, baseline_table, cycle_table, path_history_buf);
            auto tc1 = std::chrono::high_resolution_clock::now();
            long long cold_baseline_nodes = number_of_mimimax_calls - nodes_before;
            double cold_baseline_ms = std::chrono::duration<double, std::milli>(tc1-tc0).count();

            test_table->reset();
            seed_table_with_pv(*test_table, position, baseline_pv);
            nodes_before = number_of_mimimax_calls;
            auto tc2 = std::chrono::high_resolution_clock::now();
            PV_Line cold_oracle_pv = search_single_depth_get_pv(&position, wfh, depth, test_table, cycle_table, path_history_buf);
            auto tc3 = std::chrono::high_resolution_clock::now();
            long long cold_oracle_nodes = number_of_mimimax_calls - nodes_before;
            double cold_oracle_ms = std::chrono::duration<double, std::milli>(tc3-tc2).count();

            if(cold_baseline_pv.eval != cold_oracle_pv.eval)
            {
                std::cout << "  WARNING: cold eval mismatch at move " << move_no
                          << " (cold_baseline=" << cold_baseline_pv.eval << ", cold_oracle=" << cold_oracle_pv.eval << ")\n";
            }

            double cold_speedup = cold_oracle_ms>0 ? cold_baseline_ms/cold_oracle_ms : 0.0;
            double cold_node_reduction = cold_baseline_nodes>0 ? 100.0*(1.0 - (double)cold_oracle_nodes/cold_baseline_nodes) : 0.0;

            std::cout << "    [cold, no ID warm-up] baseline=" << cold_baseline_ms << "ms (" << cold_baseline_nodes << " nodes)"
                      << "  oracle=" << cold_oracle_ms << "ms (" << cold_oracle_nodes << " nodes)"
                      << "  speedup=" << cold_speedup << "x"
                      << "  node_reduction=" << cold_node_reduction << "%\n";

            cold_baseline_total_ms += cold_baseline_ms;
            cold_oracle_total_ms += cold_oracle_ms;
            cold_baseline_total_nodes += cold_baseline_nodes;
            cold_oracle_total_nodes += cold_oracle_nodes;

            // advance the real game using the move baseline actually chose -
            // this defines "the move that actually appeared on the board".
            // current_lenght==0 means minimax()'s root call hit its own
            // (deliberately aggressive, stricter-than-FIDE) search-time
            // repetition heuristic before generating any moves at all - see
            // ascaniusfish_2.hpp's repetition/cycle detection block. moves[0]
            // is then a default/garbage Move that matches nothing, and the
            // real engine_move() has the same latent quirk: its move-match
            // loop silently falls through to best_move_index's 0-initialized
            // default, i.e. "play whatever move generation listed first".
            // Mirrored here rather than treated as an error, since it's
            // genuine (if idiosyncratic) engine behavior, not a bug in this
            // test.
            Move chosen_move = (baseline_pv.current_lenght>0) ? baseline_pv.moves[0] : std::get<1>(probe)[0];
            if(baseline_pv.current_lenght==0)
            {
                std::cout << "  Note: root hit minimax()'s search-time repetition heuristic - "
                          << "falling back to move-generation order, matching engine_move()'s own quirk here.\n";
            }
            BB next;
            if(!apply_move(position, chosen_move, next))
            {
                std::cout << "  ERROR: could not replay baseline's chosen move, stopping this depth.\n";
                break;
            }
            position = next;
            history.push_back(position);
        }

        std::cout << "Depth " << depth << " totals [ID]:   baseline=" << baseline_total_ms << "ms (" << baseline_total_nodes << " nodes), "
                  << "oracle=" << test_total_ms << "ms (" << test_total_nodes << " nodes), "
                  << "overall speedup=" << (test_total_ms>0 ? baseline_total_ms/test_total_ms : 0.0) << "x, "
                  << "node_reduction=" << (baseline_total_nodes>0 ? 100.0*(1.0-(double)test_total_nodes/baseline_total_nodes) : 0.0) << "%\n";
        std::cout << "Depth " << depth << " totals [cold]: baseline=" << cold_baseline_total_ms << "ms (" << cold_baseline_total_nodes << " nodes), "
                  << "oracle=" << cold_oracle_total_ms << "ms (" << cold_oracle_total_nodes << " nodes), "
                  << "overall speedup=" << (cold_oracle_total_ms>0 ? cold_baseline_total_ms/cold_oracle_total_ms : 0.0) << "x, "
                  << "node_reduction=" << (cold_baseline_total_nodes>0 ? 100.0*(1.0-(double)cold_oracle_total_nodes/cold_baseline_total_nodes) : 0.0) << "%\n";
    }

    delete[] wfh;
    delete[] path_history_buf;
    delete baseline_table;
    delete test_table;
    return 0;
}
