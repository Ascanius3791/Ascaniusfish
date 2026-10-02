// OWNERSHIP=Claude
// Issue #64: can a cutoff line make the engine report a wrong SCORE, not just
// wrong moves?
//
// minimax() let a same-depth TT bound narrow the window until #64 removed it
// (df62ea6, ascaniusfish_2.hpp, "only on exact depth alpha and beta can be updated"). The node then searches
// a smaller window than its parent gave it, so a value it returns as a bound
// (fail-low against the raised alpha, fail-high against the lowered beta, or a
// null-move cutoff against the lowered beta) can lie inside the parent's window,
// where the parent takes it as exact. This probe counts that ("masquerade").
//
// It runs a copy of minimax()/minimax_tactical() that is df62ea6's code plus
// bookkeeping, carrying a
// Taint up the tree: does the score this node hands back rest on a masquerade
// somewhere below, and how far apart were the two claims there (gap: the TT bound
// vs. what the narrowed search returned; 0 = they agree, so the value is right)?
// Exact TT entries remember the taint they were stored with.
//
// Every root iteration is also run, from the same process state (fork(): same
// table, killers, history), with bounds that only cut and never narrow
// (main's rule since #64; CHECK=1 shows main's minimax() matches it node for node). Comparing the two root scores shows
// whether tainted scores are off by more than the window difference alone causes.
//
// Games: each EPD opening is played on for <plies> moves by the copy's own best
// move, one table per game, iterative deepening 1..<depth> per position, the way
// the UCI engine searches a game.
// usage: pv_score_taint_probe <epd> <games> <plies> <depth> [first game index]
// env: CHECK=1 compares main's minimax() with the cut-only copy; NO_REP=1 turns repetition draws off
#include "../lib/uci.hpp"
#include <unordered_map>
#include <sys/wait.h>
#include <unistd.h>

struct Taint
{
    bool on = false;   // the score rests on a bound that was taken as exact
    int gap = 0;       // largest disagreement between a TT bound and the search, along the way
    int kinds = 0;     // 1 narrowed move loop, 2 narrowed null move, 4 inherited from a TT exact entry, 8 at the root itself
};
static Taint merge(Taint a, const Taint& b)
{
    a.on |= b.on; a.gap = std::max(a.gap, b.gap); a.kinds |= b.kinds;
    return a;
}

static bool g_narrow = true;                          // df62ea6's rule; false = bounds only cut (main since #64)
static std::unordered_map<uint64_t, Taint> g_tt_taint;// taint of the exact entry stored for a hash
static long long ev_narrowings = 0, ev_masq[16] = {}, ev_masq_inconsistent[16] = {};
static std::vector<int> ev_gaps;                      // gaps of inconsistent masquerades, all nodes

static bool is_mate(int v) { return v <= INT_MIN + max_mating_seq || v >= INT_MAX - max_mating_seq; }
static int cp_gap(int a, int b)                        // |a-b|, mate scores clipped to 100000
{
    auto clip = [](int v){ return std::max(-100000, std::min(100000, v)); };
    return std::abs(clip(a) - clip(b));
}

static void store(lookup_table* table, const BB* original, const PV_Line& pv, const Taint& t)
{
    TT_entry entry;
    entry.zobrist_hash = original->zobrist_hash;
    entry.pv_line = pv;
    entry.initialized = true;
    table->insert(entry);
    if(pv.bound_type==0 && t.on) g_tt_taint[original->zobrist_hash] = t;
    else g_tt_taint.erase(original->zobrist_hash);
}
static Taint tt_taint(uint64_t h)
{
    auto it = g_tt_taint.find(h);
    if(it==g_tt_taint.end()) return Taint();
    Taint t = it->second; t.kinds |= 4;
    return t;
}

// minimax_tactical() of main, plus taint. Its only TT read is the quiet-leaf exact probe.
PV_Line q_copy(const BB* const original, BB* const wfh, const WEIGHTS& W, int alpha, int beta, lookup_table* const table, Taint& out, int forced_moves_left = max_non_king_pieces)
{
    out = Taint();
    const int a_in = alpha, b_in = beta;
    poll_search_abort();
    Move_List moves;
    const int number_of_captures = generate_legal_moves<GEN_CAPTURES>(original, moves);
    const int number_of_new_moves = number_of_captures>=2 ? 2 : count_legal_moves(original, 2);
    const bool in_check = original->get_in_check();
    const bool is_forced_move = number_of_new_moves==1 && forced_moves_left>0;
    const bool spends_forced_move = is_forced_move || (in_check && forced_moves_left>0);
    if((is_forced_move && number_of_captures==0) || in_check)
    generate_legal_moves<GEN_QUIETS>(original, moves);
    int tactical_order[MOVE_LIST_CAP];
    int number_of_tactical_moves = 0;
    for(int idx=0; idx<moves.size; idx++)
        if(is_forced_move || in_check || moves[idx].promotion_piece_type==QUEEN_PROMOTION ||
           (moves[idx].promotion_piece_type==-1 && is_good_capture(original, moves[idx].from, moves[idx].to, moves[idx].is_en_passant)))
        tactical_order[number_of_tactical_moves++] = idx;

    if(number_of_tactical_moves==0)
    {
        if(number_of_new_moves==0)
        {
            int best_eval = original->get_in_check() ? (original->white_move ? INT_MIN : INT_MAX) : 0;
            PV_Line e = PV_Line(best_eval); e.current_lenght = 0; e.bound_type = 0;
            return e;
        }
        if(table)
        {
            TT_readout readout = table->is_retrivable_eval(original, 0);
            if(readout.is_found && readout.pv_line.bound_type==0)
            {
                if(readout.pv_line.eval>a_in && readout.pv_line.eval<b_in) out = tt_taint(original->zobrist_hash);
                return readout.pv_line;
            }
        }
        int evaluation = eval(original, W, 0);
        if(nne::enabled) evaluation = nne::corrected_eval(original, evaluation);
        PV_Line leaf = PV_Line(evaluation); leaf.current_lenght = 0; leaf.bound_type = 0;
        return leaf;
    }

    PV_Line pv_line = PV_Line(original->white_move ? INT_MIN : INT_MAX);
    Taint best;
    if(!original->get_in_check())
    {
        int stand_pat = eval(original, W, 0);
        pv_line = PV_Line(stand_pat); pv_line.current_lenght = 0; pv_line.bound_type = 0;
        if(original->white_move)
        {
            if(stand_pat>=beta) { pv_line.bound_type = -1; return pv_line; }
            alpha=max(alpha,stand_pat);
        }
        else
        {
            if(stand_pat<=alpha) { pv_line.bound_type = 1; return pv_line; }
            beta=min(beta,stand_pat);
        }
    }
    order_tactical_moves(original, moves.moves, tactical_order, number_of_tactical_moves);
    bool searched_a_move = false;
    for(int k=0; k<number_of_tactical_moves; k++)
    {
        const int idx = tactical_order[k];
        make_move(original, moves[idx], wfh);
        if(in_check && forced_moves_left==0 && wfh->get_in_check() && is_quiet_move(original, moves[idx]))
        continue;
        searched_a_move = true;
        Taint ct;
        PV_Line candidate = q_copy(wfh, wfh+1, W, alpha, beta, table, ct, forced_moves_left - spends_forced_move);
        int child_eval = candidate.eval;
        if(child_eval<INT_MIN+max_mating_seq) child_eval++;
        if(child_eval>INT_MAX-max_mating_seq) child_eval--;
        candidate.eval = child_eval;
        bool improves_pv;
        if(original->white_move) { improves_pv = child_eval>pv_line.eval; pv_line.eval=max(pv_line.eval,child_eval); alpha=max(alpha,child_eval); }
        else { improves_pv = child_eval<pv_line.eval; pv_line.eval=min(pv_line.eval,child_eval); beta=min(beta,child_eval); }
        if(improves_pv) { pv_line = PV_Line(moves[idx], 0, &candidate); best = ct; }
        if(beta<=alpha) break;
    }
    if(!searched_a_move)
    {
        PV_Line p = PV_Line(0); p.current_lenght = 0; p.bound_type = 0;
        return p;
    }
    if(pv_line.eval>a_in && pv_line.eval<b_in) out = best;
    return pv_line;
}

// minimax() of df62ea6, plus taint, plus g_narrow.
PV_Line mm_copy(const BB*const original ,BB* const wfh ,int depth, const WEIGHTS& W,int alpha, int beta,lookup_table* const table, BB* const path_history, int ply, const CuckooCycleTable* const cycle_table, int root_ply, bool null_move_allowed, Taint& out)
{
    out = Taint();
    const int a_in = alpha, b_in = beta;
    auto in_caller_window = [&](int v){ return v>a_in && v<b_in; };
    number_of_mimimax_calls++;
    poll_search_abort();
    int effective_root_ply = (root_ply==INT_MIN) ? ply : root_ply;
    const bool at_root = ply==effective_root_ply;
    if(path_history && ply<MAX_SEARCH_PLY) path_history[ply] = *original;

    Move cycle_avoiding_move;
    bool cycle_move_found = false;
    if(path_history && cycle_table && ply<MAX_SEARCH_PLY)
    {
        int window_start = max(0, ply - original->halfmoves_since_last_capture_or_pawn_move);
        for(int i=ply-3; i>=window_start; i-=2)
        {
            int found_piece_type; Move found_move;
            if(cycle_table->detect_upcoming_cycle(*original, path_history[i], ply-i, found_piece_type, found_move))
            { cycle_avoiding_move = found_move; cycle_move_found = true; break; }
        }
    }

    PV_Line tt_hint;
    bool is_tt_hint_found=0;
    int narrow_lower = INT_MIN, narrow_upper = INT_MAX;   // the TT bound that narrowed the window, if any
    if(table)
    {
        TT_readout readout = table->is_retrivable_eval(original, depth);
        if(readout.is_found)
        {
            bool is_proven_mate = readout.pv_line.bound_type==0
                && (readout.pv_line.eval <= INT_MIN + max_mating_seq || readout.pv_line.eval >= INT_MAX - max_mating_seq);
            if((depth<=readout.pv_line.depth || is_proven_mate) && readout.pv_line.current_lenght>0)
            {
                if(readout.pv_line.bound_type==0)
                {
                    if(in_caller_window(readout.pv_line.eval)) out = tt_taint(original->zobrist_hash);
                    return readout.pv_line;
                }
                else if(depth==readout.pv_line.depth)
                {
                    const int e = readout.pv_line.eval;
                    if(g_narrow)
                    {
                        if(readout.pv_line.bound_type == -1 && e>alpha) { alpha = e; narrow_lower = e; }
                        else if(readout.pv_line.bound_type == 1 && e<beta) { beta = e; narrow_upper = e; }
                        if(alpha >= beta) return readout.pv_line;
                        if(narrow_lower!=INT_MIN || narrow_upper!=INT_MAX) ev_narrowings++;
                    }
                    else if((readout.pv_line.bound_type == -1 && e>=beta) || (readout.pv_line.bound_type == 1 && e<=alpha))
                    return readout.pv_line;
                }
            }
            tt_hint=readout.pv_line;
            is_tt_hint_found=1;
        }
    }
    // A value v this node returns is a masquerade if the parent reads it as exact
    // (inside a_in..b_in) while the narrowed search only proved a bound.
    auto masquerade = [&](int v, int kind) -> Taint
    {
        Taint t;
        if(!in_caller_window(v)) return t;
        int bound;
        if(narrow_lower!=INT_MIN && v<=narrow_lower) bound = narrow_lower;      // failed low against the raised alpha
        else if(narrow_upper!=INT_MAX && v>=narrow_upper) bound = narrow_upper; // failed high against the lowered beta
        else return t;
        if(at_root) kind |= 8;
        t.on = true; t.kinds = kind; t.gap = cp_gap(v, bound);
        ev_masq[kind]++;
        if(t.gap) { ev_masq_inconsistent[kind]++; ev_gaps.push_back(t.gap); }
        return t;
    };

    int tb_score;
    if(ply!=effective_root_ply && null_move_allowed && tb_probe_score(original, tb_score))
    {
        PV_Line p = PV_Line(tb_score); p.depth = depth; p.current_lenght = 0; p.bound_type = 0;
        return p;
    }

    if(depth==0)
    {
        Taint qt;
        PV_Line q = q_copy(original, wfh, W, alpha, beta, table, qt);
        out = merge(in_caller_window(q.eval) ? qt : Taint(), masquerade(q.eval, 1));
        return q;
    }

    int exception_state=exception_eval(original);
    if(exception_state==1||exception_state==2)
    {
        int best_eval = exception_state==1 ? 0 : (original->white_move ? INT_MIN : INT_MAX);
        PV_Line e = PV_Line(best_eval); e.depth=depth; e.current_lenght=0; e.bound_type = 0;
        if(table) store(table, original, e, Taint());
        return e;
    }

    if(ENABLE_NULL_MOVE_PRUNING && depth >= NULL_MOVE_MIN_DEPTH && ply != effective_root_ply && null_move_allowed
       && !side_to_move_lacks_non_pawn_material(original) && !original->get_in_check())
    {
        BB null_child(original, "base");
        Zobrist::update_zobrist_hash_null_move(*original, null_child);
        int null_depth = depth - 1 - NULL_MOVE_REDUCTION;
        Taint nt;
        if(original->white_move)
        {
            PV_Line null_pv = mm_copy(&null_child, wfh, null_depth, W, beta-1, beta, table, nullptr, ply+1, nullptr, effective_root_ply, false, nt);
            int null_eval = null_pv.eval;
            if(null_eval<INT_MIN+max_mating_seq) null_eval++;
            if(null_eval>INT_MAX-max_mating_seq) null_eval--;
            if(null_eval >= beta)
            {
                PV_Line c = PV_Line(null_eval); c.depth = depth; c.current_lenght = 0; c.bound_type = -1;
                if(table) store(table, original, c, Taint());
                out = masquerade(null_eval, 2);
                return c;
            }
        }
        else
        {
            PV_Line null_pv = mm_copy(&null_child, wfh, null_depth, W, alpha, alpha+1, table, nullptr, ply+1, nullptr, effective_root_ply, false, nt);
            int null_eval = null_pv.eval;
            if(null_eval<INT_MIN+max_mating_seq) null_eval++;
            if(null_eval>INT_MAX-max_mating_seq) null_eval--;
            if(null_eval <= alpha)
            {
                PV_Line c = PV_Line(null_eval); c.depth = depth; c.current_lenght = 0; c.bound_type = 1;
                if(table) store(table, original, c, Taint());
                out = masquerade(null_eval, 2);
                return c;
            }
        }
    }

    Move_List moves;
    const int number_of_new_moves = generate_legal_moves<GEN_ALL>(original, moves);
    int tt_move_index = -1;
    if(is_tt_hint_found && tt_hint.current_lenght>0)
    for(int i=0;i<number_of_new_moves;i++)
    if(moves[i]==tt_hint.moves[0]) { tt_move_index=i; break; }
    Staged_Move_Order order;
    prunable_moves_total+=number_of_new_moves-1;
    const int alpha_0 = alpha, beta_0 = beta;
    PV_Line pv_line =PV_Line(original->white_move ? INT_MIN : INT_MAX);
    pv_line.depth=depth;
    Taint best;
    const int ply_from_root = ply-effective_root_ply;
    const bool in_check = original->get_in_check();
    Taint ct;
    auto search_child = [&](BB* child, int child_depth, int a, int b)
    {
        PV_Line line = mm_copy(child,wfh+1,child_depth,W,a,b,table,path_history,ply+1,cycle_table,effective_root_ply,true,ct);
        if(line.eval<INT_MIN+max_mating_seq) line.eval++;
        if(line.eval>INT_MAX-max_mating_seq) line.eval--;
        return line;
    };
    auto improves_bound = [&](int eval) { return original->white_move ? eval>alpha : eval<beta; };
    auto inside_window  = [&](int eval) { return original->white_move ? eval<beta : eval>alpha; };
    for(int i=0;i<number_of_new_moves;i++)
    {
        if(i==(tt_move_index>=0))
        order.init(original,moves.moves,number_of_new_moves,tt_move_index,ply-effective_root_ply);
        int move_index = (i==0 && tt_move_index>=0) ? tt_move_index : order.next();
        int depth_to_use=depth-1;
        Move move =moves[move_index];
        BB* child = wfh;
        make_move(original, move, child);
        bool forced_draw = false;
        if(path_history && ply+1<MAX_SEARCH_PLY)
        {
            int child_window_start = max(0, (ply+1) - child->halfmoves_since_last_capture_or_pawn_move);
            for(int j=ply-1; j>=child_window_start; j-=2)
            if(are_equal(&path_history[j], child)) { forced_draw = true; break; }
            if(!forced_draw && cycle_move_found && move==cycle_avoiding_move) forced_draw = true;
        }
        ct = Taint();
        PV_Line candidate_pv_line;
        if(forced_draw)
        candidate_pv_line = make_repetition_draw_pv_line(child, wfh+1, depth_to_use, ply+1, effective_root_ply, W);
        else if(i==0 || !ENABLE_PVS)
        candidate_pv_line = search_child(child,depth_to_use,alpha,beta);
        else
        {
            const int null_alpha = original->white_move ? alpha : beta-1;
            const int null_beta  = original->white_move ? alpha+1 : beta;
            const bool reduce = ENABLE_LMR && depth>=LMR_MIN_DEPTH && i>=LMR_FULL_DEPTH_MOVES && !in_check
                && is_quiet_move(original,move)
                && !(ply_from_root<MAX_SEARCH_PLY && (move==killer_moves[ply_from_root][0] || move==killer_moves[ply_from_root][1]))
                && !child->get_in_check();
            if(reduce)
            candidate_pv_line = search_child(child,depth_to_use-lmr_reduction(depth,i),null_alpha,null_beta);
            if(!reduce || improves_bound(candidate_pv_line.eval))
            candidate_pv_line = search_child(child,depth_to_use,null_alpha,null_beta);
            if(improves_bound(candidate_pv_line.eval) && inside_window(candidate_pv_line.eval))
            candidate_pv_line = search_child(child,depth_to_use,alpha,beta);
        }
        int eval = candidate_pv_line.eval;
        bool improves_pv;
        if(original->white_move) { improves_pv = eval>pv_line.eval; pv_line.eval=max(pv_line.eval,eval); alpha=max(alpha,eval); }
        else { improves_pv = eval<pv_line.eval; pv_line.eval=min(pv_line.eval,eval); beta=min(beta,eval); }
        if(improves_pv) { pv_line = PV_Line(move,depth,&candidate_pv_line); best = ct; }
        if(beta<=alpha)
        {
            if(is_quiet_move(original,move)) store_quiet_cutoff(original,ply-effective_root_ply,depth,move,order);
            pruned_moves+=number_of_new_moves-i-1;
            break;
        }
    }
    if(pv_line.eval <= alpha_0) pv_line.bound_type = 1;
    else if(pv_line.eval >= beta_0) pv_line.bound_type = -1;
    else pv_line.bound_type = 0;
    // The best child's taint matters only where its value became this node's
    // exact value; a masquerade is added where this node's own bound leaks out.
    Taint stored = pv_line.bound_type==0 ? best : Taint();
    if(table) store(table, original, pv_line, stored);
    out = merge(in_caller_window(pv_line.eval) ? best : Taint(), masquerade(pv_line.eval, 1));
    return pv_line;
}

struct Iter { int eval; char move[8]; long long nodes; Taint t; };

static Iter run_iteration(bool narrow, BB& root, BB* wfh, int d, lookup_table* table, BB* path_history, int ply, CuckooCycleTable* cycle_table, bool real_main)
{
    g_narrow = narrow;
    Iter r{};
    long long n0 = search_nodes;
    PV_Line pv;
    if(real_main) pv = minimax(&root, wfh, d, WEIGHTS_OG, INT_MIN, INT_MAX, table, path_history, ply, cycle_table);
    else pv = mm_copy(&root, wfh, d, WEIGHTS_OG, INT_MIN, INT_MAX, table, path_history, ply, cycle_table, INT_MIN, true, r.t);
    r.eval = pv.eval;
    r.nodes = search_nodes-n0;
    std::string m = "none";
    if(pv.current_lenght>0)
    {
        BB c; make_move(&root, pv.moves[0], &c);
        m = get_UCI(&root, &c);
    }
    snprintf(r.move, sizeof r.move, "%s", m.c_str());
    return r;
}

// The same iteration in a fork, so the parent's table and move ordering are untouched.
static Iter forked(bool narrow, bool real_main, BB& root, BB* wfh, int d, lookup_table* table, BB* path_history, int ply, CuckooCycleTable* cycle_table)
{
    int fd[2];
    if(pipe(fd)) { perror("pipe"); exit(1); }
    fflush(stdout); std::cout.flush();
    pid_t pid = fork();
    if(pid==0)
    {
        close(fd[0]);
        Iter r = run_iteration(narrow, root, wfh, d, table, path_history, ply, cycle_table, real_main);
        if(write(fd[1], &r, sizeof r)!=(ssize_t)sizeof r) _exit(2);
        _exit(0);
    }
    close(fd[1]);
    Iter r{};
    if(read(fd[0], &r, sizeof r)!=(ssize_t)sizeof r) { std::cerr << "child failed\n"; exit(1); }
    close(fd[0]);
    waitpid(pid, nullptr, 0);
    return r;
}

int main(int argc, char** argv)
{
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
    std::string error;
    if(!getenv("NO_NNE") && nne::load("nets/nne_d6.bin", error)) nne::enabled = true;
    if(argc<5) { std::cerr << "usage: " << argv[0] << " <epd> <games> <plies> <depth> [first]\n"; return 2; }
    const int games = atoi(argv[2]), plies = atoi(argv[3]), depth = atoi(argv[4]);
    const int first = argc>5 ? atoi(argv[5]) : 0;
    const bool check = getenv("CHECK")!=nullptr;
    std::vector<std::string> fens;
    {
        std::ifstream in(argv[1]);
        std::string line;
        while(std::getline(in, line))
        {
            if(line.empty() || line[0]=='#') continue;
            std::istringstream ls(line);
            std::string f[4]; ls >> f[0] >> f[1] >> f[2] >> f[3];
            fens.push_back(f[0]+" "+f[1]+" "+f[2]+" "+f[3]+" 0 1");
        }
    }
    BB* wfh = new BB[UCI_WFH_SIZE];
    BB* path_history = new BB[MAX_SEARCH_PLY];
    CuckooCycleTable* cycle_table = new CuckooCycleTable;
    lookup_table* table = new lookup_table;

    long long root_hits = 0, root_kind = 0, null_kind = 0, inherited_kind = 0;
    long long iters = 0, tainted = 0, tainted_incons = 0, mismatch = 0;
    long long diff_move[2] = {}, n_cls[2] = {}, big[2] = {};
    double sum_abs[2] = {};
    std::vector<int> diffs[2];
    for(int g=first; g<first+games && g<(int)fens.size(); g++)
    {
        table->full_reset();
        g_tt_taint.clear();
        BB cur{fens[g]};
        std::vector<BB> game{cur};
        std::cout << "game " << g << " " << fens[g] << std::endl;
        for(int p=0; p<plies; p++)
        {
            table->new_search();
            clear_killer_moves();
            int seed = std::min({(int)game.size(), cur.halfmoves_since_last_capture_or_pawn_move+1, MAX_SEARCH_PLY});
            for(int i=0;i<seed;i++) path_history[i] = game[game.size()-seed+i];
            if(count_legal_moves(&cur, 1)==0) break;
            Iter a{};
            // NO_REP=1: no repetition/cycle draws, so they cannot be what makes a TT bound disagree
            BB* ph = getenv("NO_REP") ? nullptr : path_history;
            CuckooCycleTable* ct = getenv("NO_REP") ? nullptr : cycle_table;
            for(int d=1; d<=depth; d++)
            {
                Iter b = forked(false, false, cur, wfh, d, table, ph, seed-1, ct);
                if(check)
                {
                    Iter m = forked(true, true, cur, wfh, d, table, ph, seed-1, ct);
                    if(m.eval!=b.eval || m.nodes!=b.nodes || strcmp(m.move,b.move)) { mismatch++;
                        std::cout << "  MISMATCH main " << m.eval << " " << m.move << " " << m.nodes << " cut-only " << b.eval << " " << b.move << " " << b.nodes << std::endl; }
                }
                a = run_iteration(true, cur, wfh, d, table, ph, seed-1, ct, false);
                std::cout << "  ply " << p << " d " << d << " narrow " << a.eval << " " << a.move
                          << " | cut-only " << b.eval << " " << b.move << " nodes " << a.nodes;
                if(a.t.on) std::cout << "  TAINT kinds " << a.t.kinds << " gap " << a.t.gap;
                std::cout << std::endl;
                if(a.nodes<=1) { root_hits++; continue; }  // the root's own exact entry handed back, nothing searched
                iters++;
                int cls = a.t.on ? 1 : 0;
                if(a.t.on) { if(a.t.kinds & 8) root_kind++; if(a.t.kinds & 2) null_kind++; if(a.t.kinds & 4) inherited_kind++; }
                tainted += cls;
                if(a.t.on && a.t.gap) tainted_incons++;
                int diff = (is_mate(a.eval) || is_mate(b.eval)) ? (a.eval==b.eval ? 0 : -1) : std::abs(a.eval-b.eval);
                if(diff>=0) { n_cls[cls]++; sum_abs[cls]+=diff; diffs[cls].push_back(diff); if(diff>=50) big[cls]++; }
                if(strcmp(a.move,b.move)) diff_move[cls]++;
            }
            BB next;
            bool found = false;
            Move_List ml;
            int n = generate_legal_moves<GEN_ALL>(&cur, ml);
            for(int i=0;i<n && !found;i++)
            {
                make_move(&cur, ml[i], &next);
                if(get_UCI(&cur, &next)==a.move) found = true;
            }
            if(!found) break;
            cur = next;
            game.push_back(cur);
        }
    }
    auto med = [](std::vector<int> v){ if(v.empty()) return -1; std::sort(v.begin(), v.end()); return v[v.size()/2]; };
    auto p90 = [](std::vector<int> v){ if(v.empty()) return -1; std::sort(v.begin(), v.end()); return v[v.size()*9/10]; };
    std::cout << "\n== summary ==\n";
    std::cout << "root iterations answered by the root's own exact entry (not counted) " << root_hits << "\n";
    std::cout << "tainted root scores with a masquerade at the root " << root_kind << ", a null-move one " << null_kind << ", one from a stored exact entry " << inherited_kind << "\n";
    std::cout << "root iterations searched " << iters << ", root score tainted " << tainted << " (gap>0: " << tainted_incons << ")";
    if(check) std::cout << ", main/cut-only mismatches " << mismatch;
    std::cout << "\n";
    const char* nm[2] = {"untainted", "tainted  "};
    for(int c=0;c<2;c++)
    std::cout << nm[c] << ": n " << n_cls[c] << "  |narrow - cut-only| mean " << (n_cls[c]? sum_abs[c]/n_cls[c] : 0)
              << " median " << med(diffs[c]) << " p90 " << p90(diffs[c]) << " >=50cp " << big[c]
              << "  other root move " << diff_move[c] << "\n";
    std::cout << "nodes narrowed by a TT bound " << ev_narrowings << "\n";
    const char* kn[] = {"", "move loop", "null move", "", "", "", "", "", "", "root move loop", "root null"};
    for(int k=0;k<16;k++) if(ev_masq[k])
    std::cout << "masquerades (" << (k<11? kn[k] : "?") << "): " << ev_masq[k] << ", inconsistent " << ev_masq_inconsistent[k] << "\n";
    std::cout << "inconsistent gap median " << med(ev_gaps) << " p90 " << p90(ev_gaps) << " count " << ev_gaps.size() << "\n";
}
