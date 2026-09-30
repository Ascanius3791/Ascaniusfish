// OWNERSHIP=Claude
#ifndef UCI_CPP
#define UCI_CPP
#include "../lib/uci.hpp"
#include <sstream>
#include <algorithm>
#include <fstream>
#include <unistd.h>

static const char* const UCI_STARTPOS = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

static std::vector<std::string> split_ws(const std::string& s)
{
    std::istringstream in(s);
    std::vector<std::string> out;
    std::string t;
    while(in >> t)
    out.push_back(t);
    return out;
}

static bool all_digits(const std::string& s)
{
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c){ return c>='0' && c<='9'; });
}

bool uci_parse_fen(const std::string& fen, BB& out)
{
    std::vector<std::string> f = split_ws(fen);
    if(f.size()<4 || f.size()>6)
    return false;

    // Placement: 8 ranks of 8 squares, exactly one king per side.
    int ranks=1, squares=0, white_kings=0, black_kings=0;
    for(char c : f[0])
    {
        if(c=='/')
        {
            if(squares!=8) return false;
            ranks++; squares=0;
        }
        else if(c>='1' && c<='8')
        squares += c-'0';
        else if(std::string("PNBRQKpnbrqk").find(c)!=std::string::npos)
        {
            squares++;
            white_kings += c=='K';
            black_kings += c=='k';
        }
        else return false;
        if(squares>8) return false;
    }
    if(ranks!=8 || squares!=8 || white_kings!=1 || black_kings!=1)
    return false;

    if(f[1]!="w" && f[1]!="b")
    return false;

    // FEN_to_BB() only accepts castling flags in KQkq order - normalize.
    std::string castling;
    if(f[2]!="-")
    {
        for(char c : f[2])
        if(std::string("KQkq").find(c)==std::string::npos) return false;
        for(char c : std::string("KQkq"))
        if(f[2].find(c)!=std::string::npos) castling+=c;
    }
    if(castling.empty())
    castling="-";

    const std::string& ep = f[3];
    if(ep!="-" && !(ep.size()==2 && ep[0]>='a' && ep[0]<='h' && (ep[1]=='3' || ep[1]=='6')))
    return false;

    std::string halfmoves = f.size()>4 ? f[4] : "0";
    std::string fullmoves = f.size()>5 ? f[5] : "1";
    if(!all_digits(halfmoves) || !all_digits(fullmoves))
    return false;

    BB b;
    FEN_to_BB(f[0]+" "+f[1]+" "+castling+" "+ep+" "+halfmoves+" "+fullmoves, &b);
    castling_rights(&b);  // drop rights whose king/rook isn't on its home square
    b.number_of_repetitions = 0;
    b.zobrist_hash = Zobrist::compute_Zobrist_Hash(b);
    out = b;
    return true;
}

bool uci_apply_move(const BB& pos, const std::string& uci, BB& out)
{
    BB children[256];
    int n = std::get<0>(all_moves(&pos, children));
    for(int i=0;i<n;i++)
    {
        if(get_UCI(&pos, children+i)==uci)
        {
            out = children[i];
            return true;
        }
    }
    return false;
}

std::string uci_score(int eval, bool white_to_move)
{
    // Mate scores count half moves from the root: INT_MAX-n = white mates in
    // n plies, INT_MIN+n = black mates in n plies (see interpret_eval()).
    int mate_plies = 0;
    bool white_mates = false;
    if(eval >= INT_MAX - max_mating_seq)
    {
        mate_plies = INT_MAX - eval;
        white_mates = true;
    }
    else if(eval <= INT_MIN + max_mating_seq)
    {
        mate_plies = eval - INT_MIN;
    }
    else if(is_tb_score(eval))
    return "cp " + std::to_string(white_to_move == (eval>0) ? TB_WIN_CP : -TB_WIN_CP);  // a table win, not a mate
    else
    return "cp " + std::to_string(white_to_move ? eval : -eval);

    int moves = (mate_plies+1)/2;
    return "mate " + std::to_string(white_mates==white_to_move ? moves : -moves);
}

UCI_Engine::UCI_Engine()
{
    table = nullptr;  // ~580MB, built on first isready/go so "uci" answers at once
    wfh = new BB[UCI_WFH_SIZE];
    path_history = new BB[MAX_SEARCH_PLY];
    pv_buf = new BB[256];
    cycle_table = new CuckooCycleTable;
    tm = nullptr;
    BB start;
    uci_parse_fen(UCI_STARTPOS, start);
    game.assign(1, start);
}

UCI_Engine::~UCI_Engine()
{
    stop_search();
    delete table;
    delete[] wfh;
    delete[] path_history;
    delete[] pv_buf;
    delete cycle_table;
    delete tm;
}

void UCI_Engine::send(const std::string& line)
{
    std::lock_guard<std::mutex> lock(out_mutex);
    std::cout << line << std::endl;
}

void UCI_Engine::ensure_table()
{
    if(!table)
    table = new lookup_table;
}

// A relative NNEFile is looked for in the working directory, then next to the
// binary, so the default finds nets/ wherever a GUI or a match starts the engine.
static std::string nne_resolve(const std::string& file)
{
    if(file.empty() || file[0]=='/' || std::ifstream(file).good())
    return file;
    char exe[4096];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe)-1);
    if(n<=0)
    return file;
    const std::string dir(exe, std::string(exe, n).rfind('/')+1);
    return std::ifstream(dir+file).good() ? dir+file : file;
}

// UseNNE / NNEFile (#52): loads the net when it is switched on or its file
// changes. Clears the TT whenever the eval changes, since its quiet leaves
// were scored with the other one.
void UCI_Engine::apply_nne()
{
    const bool was_enabled = nne::enabled;
    const std::string was_loaded = nne::loaded_path;
    nne::enabled = false;
    if(use_nne)
    {
        const std::string path = nne_resolve(nne_file);
        std::string error;
        if(nne::loaded_path==path || nne::load(path, error))
        {
            nne::enabled = true;
            send("info string nne on, " + path);
        }
        else
        send("info string nne off: " + error);
    }
    if(table && (nne::enabled!=was_enabled || (nne::enabled && nne::loaded_path!=was_loaded)))
    table->reset();
}

void UCI_Engine::stop_search()
{
    stop_search_flag.store(true);
    if(search_thread.joinable())
    search_thread.join();
}

void UCI_Engine::handle_position(const std::vector<std::string>& tokens)
{
    size_t i = 1;
    BB root;
    if(i<tokens.size() && tokens[i]=="startpos")
    {
        uci_parse_fen(UCI_STARTPOS, root);
        i++;
    }
    else if(i<tokens.size() && tokens[i]=="fen")
    {
        std::string fen;
        for(i++; i<tokens.size() && tokens[i]!="moves"; i++)
        fen += tokens[i] + " ";
        if(!uci_parse_fen(fen, root))
        {
            send("info string invalid fen: " + fen);
            return;
        }
    }
    else
    {
        send("info string expected 'position startpos' or 'position fen <fen>'");
        return;
    }

    std::vector<BB> positions(1, root);
    if(i<tokens.size() && tokens[i]=="moves")
    {
        for(i++; i<tokens.size(); i++)
        {
            BB next;
            if(!uci_apply_move(positions.back(), tokens[i], next))
            {
                send("info string illegal move: " + tokens[i]);
                return;
            }
            positions.push_back(next);
        }
    }
    game = positions;
}

std::vector<std::string> UCI_Engine::pv_to_uci(const BB& root, const PV_Line& pv)
{
    // Replays the PV move by move so only legal moves are reported; a stale
    // TT tail that no longer matches the position just ends the line.
    std::vector<std::string> out;
    BB cur = root;
    for(int k=0; k<pv.current_lenght && k<MAX_PV_Lenght; k++)
    {
        auto result = all_moves(&cur, pv_buf);
        int n = std::get<0>(result);
        const std::vector<Move>& moves = std::get<1>(result);
        int found = -1;
        for(int i=0;i<n;i++)
        if(moves[i]==pv.at(k)) { found=i; break; }
        if(found<0)
        break;
        out.push_back(get_UCI(&cur, pv_buf+found));
        cur = pv_buf[found];
    }
    return out;
}

long long perft(const BB* pos, int depth, BB* buf)
{
    int n = std::get<0>(all_moves(pos, buf));
    if(depth<=1)
    return depth==1 ? n : 1;
    long long nodes = 0;
    for(int i=0;i<n;i++)
    nodes += perft(buf+i, depth-1, buf+n);
    return nodes;
}

void UCI_Engine::perft_divide(int depth)
{
    const BB& root = game.back();
    auto start = std::chrono::steady_clock::now();
    int n = std::get<0>(all_moves(&root, wfh));
    long long total = 0;
    for(int i=0;i<n;i++)
    {
        long long nodes = perft(wfh+i, depth-1, wfh+n);
        total += nodes;
        send(get_UCI(&root, wfh+i) + ": " + std::to_string(nodes));
    }
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    send("");
    send("Nodes searched: " + std::to_string(total) + " (" + std::to_string(ms) + " ms)");
}

void UCI_Engine::handle_go(const std::vector<std::string>& tokens)
{
    UCI_Limits limits;
    auto next_ll = [&](size_t& i) -> long long
    {
        if(i+1<tokens.size() && (all_digits(tokens[i+1]) || (tokens[i+1][0]=='-' && all_digits(tokens[i+1].substr(1)))))
        return std::stoll(tokens[++i]);
        return 0;
    };
    for(size_t i=1;i<tokens.size();i++)
    {
        const std::string& t = tokens[i];
        if(t=="depth")          limits.depth = (int)next_ll(i);
        else if(t=="movetime")  limits.movetime_ms = next_ll(i);
        else if(t=="wtime")     limits.wtime = next_ll(i);
        else if(t=="btime")     limits.btime = next_ll(i);
        else if(t=="winc")      limits.winc = next_ll(i);
        else if(t=="binc")      limits.binc = next_ll(i);
        else if(t=="movestogo") limits.movestogo = (int)next_ll(i);
        else if(t=="infinite")  limits.infinite = true;
        else if(t=="perft")     limits.perft = (int)next_ll(i);
    }

    stop_search();
    if(limits.perft>0)
    {
        perft_divide(limits.perft);
        return;
    }
    ensure_table();
    long long start_ns = steady_now_ns();

    bool clocked = limits.wtime>=0 || limits.btime>=0;
    if(limits.depth==0 && limits.movetime_ms==0 && !clocked)
    limits.infinite = true;  // bare "go" searches until "stop"

    // Deadline: movetime, or the time manager's hard limit on a clock.
    long long budget_ms = limits.movetime_ms;
    delete tm;
    tm = nullptr;
    if(clocked && budget_ms==0)
    {
        bool white = game.back().white_move;
        tm = new TimeManager(game.back(), white ? limits.wtime : limits.btime, white ? limits.winc : limits.binc, limits.movestogo, lambda_history);
        budget_ms = tm->hard_ms();
    }
    search_deadline_ns.store(budget_ms>0 && !limits.infinite ? start_ns + budget_ms*1000000LL : 0);
    stop_search_flag.store(false);
    search_thread = std::thread(&UCI_Engine::search, this, limits, start_ns);
}

// A capture or a pawn move. The BB clock is not usable for this (it counts
// every ply, see tools/game_rules.hpp), so it is judged from the two boards.
static bool is_zeroing_step(const BB& before, const BB& after)
{
    const int pawns = before.white_move ? 0 : 6;
    uint64_t b = 0, a = 0;
    for(int i=0;i<12;i++)
    {
        b |= before.Board[i];
        a |= after.Board[i];
    }
    return before.Board[pawns]!=after.Board[pawns] || __builtin_popcountll(a)<__builtin_popcountll(b);
}

// Tablebase root filter (issue #38). Ranks the root's legal moves by what the
// tables say about the position each leads to and keeps the best class only:
// a win that still finishes inside the 50-move rule (fewest plies to the next
// zeroing move first), else every move that holds the draw, else the slowest
// loss. `keep` gets indices into `children`. False = no usable answer (too
// many pieces, castling rights, a table missing): search normally.
// DTZ is not DTM, so winning moves that tie are left to the search.
bool UCI_Engine::tb_root_filter(const BB& root, const std::vector<BB>& children, std::vector<int>& keep, int& tb_class)
{
    const int n = (int)children.size();
    int hc = game[0].halfmoves_since_last_capture_or_pawn_move;  // the real clock: the FEN's, then the game's steps
    for(size_t i=1;i<game.size();i++)
    hc = is_zeroing_step(game[i-1], game[i]) ? 0 : hc+1;
    std::vector<int> cls(n), key(n);  // class 3 win, 2 draw, 1 loss; lower key is better
    for(int i=0;i<n;i++)
    {
        int dz;
        if(!syzygy::probe_dtz(&children[i], dz))
        return false;
        // dz is the opponent's: negative means they lose, i.e. we win
        const bool zeroing = is_zeroing_step(root, children[i]);
        if(dz<0 && dz>=-100 && (zeroing || hc+1-dz<=100))
        {
            cls[i]=3;
            key[i]=zeroing ? 0 : -dz;
        }
        else if(dz>0 && dz<=100)
        {
            cls[i]=1;
            key[i]=-dz;  // the longest resistance
        }
        else
        {
            cls[i]=2;
            key[i]=0;
        }
    }
    int best_class=0, best_key=INT_MAX;
    for(int i=0;i<n;i++)
    if(cls[i]>best_class || (cls[i]==best_class && key[i]<best_key))
    {
        best_class=cls[i];
        best_key=key[i];
    }
    keep.clear();
    for(int i=0;i<n;i++)
    if(cls[i]==best_class && key[i]==best_key)
    keep.push_back(i);
    tb_class = best_class;
    return true;
}

// Iterative deepening over the kept root moves only. Each is searched as a
// child of the root, the way minimax()'s own move loop does it (that loop is
// in Ascanius's file and stays untouched).
void UCI_Engine::search_tb_root(const BB& root, const std::vector<Move>& moves, const std::vector<BB>& children, const std::vector<int>& keep, int tb_class, int ply, const UCI_Limits& limits, long long start_ns)
{
    std::string best = get_UCI(&root, &children[keep[0]]);
    const int tb_cp = tb_class==3 ? 10000 : tb_class==2 ? 0 : -10000;  // side to move's view
    std::vector<int> order = keep;
    if(keep.size()==1)
    {
        send("info depth 1 score cp " + std::to_string(tb_cp) + " nodes 1 tbhits 1 pv " + best);
        while(limits.infinite && !stop_search_flag.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        send("bestmove " + best);
        return;
    }
    path_history[ply] = root;
    int max_depth = limits.depth>0 ? std::min(limits.depth, UCI_MAX_DEPTH) : UCI_MAX_DEPTH;
    long long nodes_before = search_nodes;
    for(int d=1; d<=max_depth; d++)
    {
        if(search_stop_requested())
        break;
        int best_eval = root.white_move ? INT_MIN : INT_MAX;
        int best_idx = -1;
        PV_Line best_pv;
        std::vector<int> evals(order.size());
        try
        {
            for(size_t k=0;k<order.size();k++)
            {
                BB child = children[order[k]];
                PV_Line cpv;
                bool repeat = false;
                const int window_start = std::max(0, (ply+1) - child.halfmoves_since_last_capture_or_pawn_move);
                for(int j=ply-1; j>=window_start; j-=2)
                if(are_equal(&path_history[j], &child)) { repeat = true; break; }
                if(repeat)
                cpv = make_repetition_draw_pv_line(&child, wfh, d-1, ply+1, ply, WEIGHTS_OG);
                else
                cpv = minimax(&child, wfh, d-1, WEIGHTS_OG, INT_MIN, INT_MAX, table, path_history, ply+1, cycle_table, ply);
                if(cpv.eval<INT_MIN+max_mating_seq)
                cpv.eval++;
                if(cpv.eval>INT_MAX-max_mating_seq)
                cpv.eval--;
                evals[k] = cpv.eval;
                if(best_idx<0 || (root.white_move ? cpv.eval>best_eval : cpv.eval<best_eval))
                {
                    best_eval = cpv.eval;
                    best_idx = (int)k;
                    best_pv = PV_Line(moves[order[k]], d, &cpv);
                }
            }
        }
        catch(const search_aborted&)
        {
            break;
        }
        // best first at the next depth
        std::vector<int> next;
        next.push_back(order[best_idx]);
        for(size_t k=0;k<order.size();k++)
        if((int)k!=best_idx)
        next.push_back(order[k]);
        order = next;

        long long elapsed_ms = (steady_now_ns()-start_ns)/1000000;
        long long nodes = search_nodes-nodes_before;
        std::vector<std::string> line = pv_to_uci(root, best_pv);
        if(!line.empty())
        best = line[0];
        std::string info = "info depth " + std::to_string(d)
                         + " score " + uci_score(best_pv.eval, root.white_move)
                         + " nodes " + std::to_string(nodes)
                         + " nps " + std::to_string(nodes*1000/std::max(1LL, elapsed_ms))
                         + " time " + std::to_string(elapsed_ms)
                         + " tbhits 1 pv";
        for(const std::string& m : line)
        info += " " + m;
        send(info);
        bool proven_mate = best_pv.eval <= INT_MIN + max_mating_seq || best_pv.eval >= INT_MAX - max_mating_seq;
        if(proven_mate && !limits.infinite)
        break;
        if(tm)
        {
            tm->iteration_done(d, best_pv, elapsed_ms);
            if(!tm->start_next_iteration((steady_now_ns()-start_ns)/1000000))
            break;
        }
    }
    if(tm)
    tm->commit_lambda();
    // `best` is what the last completed iteration chose (or the first kept move)
    while(limits.infinite && !stop_search_flag.load())
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    send("bestmove " + best);
}

void UCI_Engine::search(UCI_Limits limits, long long start_ns)
{
    if(table) table->new_search();
    const BB root = game.back();
    auto result = all_moves(&root, wfh);
    int n = std::get<0>(result);
    if(n==0)
    {
        while(limits.infinite && !stop_search_flag.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        send("bestmove 0000");
        return;
    }
    clear_killer_moves();
    if(syzygy::max_pieces()>0)
    {
        std::vector<BB> children(wfh, wfh+n);
        std::vector<int> keep;
        int tb_class;
        if(tb_root_filter(root, children, keep, tb_class) && !keep.empty())
        {
            int seed = std::min({(int)game.size(), root.halfmoves_since_last_capture_or_pawn_move+1, MAX_SEARCH_PLY});
            for(int i=0;i<seed;i++)
            path_history[i] = game[game.size()-seed+i];
            search_tb_root(root, std::get<1>(result), children, keep, tb_class, seed-1, limits, start_ns);
            return;
        }
    }
    // Fallback if even depth 1 gets aborted: the move ordering's favourite.
    std::vector<int> order = sorting_moves(wfh, std::get<1>(result), n, root.white_move, nullptr, 0, WEIGHTS_OG);
    std::string best = get_UCI(&root, wfh+order[0]);

    // Repetition context, seeded from the game like engine_move() does.
    int seed_count = std::min({(int)game.size(), root.halfmoves_since_last_capture_or_pawn_move+1, MAX_SEARCH_PLY});
    for(int i=0;i<seed_count;i++)
    path_history[i] = game[game.size()-seed_count+i];
    int ply = seed_count-1;

    int max_depth = limits.depth>0 ? std::min(limits.depth, UCI_MAX_DEPTH) : UCI_MAX_DEPTH;
    if(n==1 && tm)
    max_depth = 1;  // forced move: keep the clock, depth 1 only for the score
    long long nodes_before = search_nodes;
    const long long tb_hits_before = tb_hits;
    for(int d=1; d<=max_depth; d++)
    {
        if(search_stop_requested())
        break;
        PV_Line pv;
        try
        {
            pv = minimax(&root, wfh, d, WEIGHTS_OG, INT_MIN, INT_MAX, table, path_history, ply, cycle_table);
        }
        catch(const search_aborted&)
        {
            break;
        }

        long long elapsed_ms = (steady_now_ns()-start_ns)/1000000;
        long long nodes = search_nodes-nodes_before;
        std::vector<std::string> line = pv_to_uci(root, pv);
        if(!line.empty())
        best = line[0];
        std::string info = "info depth " + std::to_string(d)
                         + " score " + uci_score(pv.eval, root.white_move)
                         + " nodes " + std::to_string(nodes)
                         + " nps " + std::to_string(nodes*1000/std::max(1LL, elapsed_ms))
                         + " time " + std::to_string(elapsed_ms)
                         + " tbhits " + std::to_string(tb_hits-tb_hits_before)
                         + " pv";
        for(const std::string& m : line)
        info += " " + m;
        send(info);

        bool proven_mate = pv.eval <= INT_MIN + max_mating_seq || pv.eval >= INT_MAX - max_mating_seq;
        if(proven_mate && !limits.infinite)
        break;
        // On a clock, λ is updated with every new PV; the next iteration
        // starts only if it is expected to finish within the soft limit.
        if(tm)
        {
            tm->iteration_done(d, pv, elapsed_ms);
            char buf[96];
            std::snprintf(buf, sizeof buf, "info string tm lambda %.3f raw %.3f soft %lld hard %lld", tm->lambda(), tm->raw_lambda(), tm->soft_ms(), tm->hard_ms());
            send(buf);
            if(!tm->start_next_iteration((steady_now_ns()-start_ns)/1000000))
            break;
        }
    }

    if(tm)
    tm->commit_lambda();
    while(limits.infinite && !stop_search_flag.load())
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    send("bestmove " + best);
}

int UCI_Engine::loop()
{
    std::string line;
    while(std::getline(std::cin, line))
    {
        std::vector<std::string> tokens = split_ws(line);
        if(tokens.empty())
        continue;
        const std::string& cmd = tokens[0];
        if(cmd=="uci")
        {
            send("id name Ascaniusfish");
            send("id author Ascanius");
            send("option name SyzygyPath type string default <empty>");
            send("option name SyzygyProbeLimit type spin default 5 min 0 max 5");
            send("option name NNEFile type string default nets/nne_d6.bin");
            send("option name UseNNE type check default false");
            send("uciok");
        }
        else if(cmd=="isready")
        {
            ensure_table();
            send("readyok");
        }
        else if(cmd=="ucinewgame")
        {
            stop_search();
            if(table)
            table->reset();
            lambda_history.reset();
            BB start;
            uci_parse_fen(UCI_STARTPOS, start);
            game.assign(1, start);
        }
        else if(cmd=="position")
        {
            stop_search();
            handle_position(tokens);
        }
        else if(cmd=="go")
        handle_go(tokens);
        else if(cmd=="stop")
        stop_search();
        else if(cmd=="quit")
        break;
        else if(cmd=="setoption")
        {
            // setoption name <name> [value <v>]; names and values may hold spaces
            size_t vpos = 0;
            while(vpos<tokens.size() && tokens[vpos]!="value")
            vpos++;
            std::string name, value;
            for(size_t k=2;k<vpos && k<tokens.size();k++)
            name += (name.empty() ? "" : " ") + tokens[k];
            for(size_t k=vpos+1;k<tokens.size();k++)
            value += (value.empty() ? "" : " ") + tokens[k];
            if(name=="SyzygyPath")
            {
                stop_search();
                syzygy_dir = value=="<empty>" ? "" : value;
                syzygy::release();
                if(syzygy_dir.empty())
                send("info string syzygy off");
                else
                {
                    int loaded = syzygy::init(syzygy_dir);
                    send("info string syzygy " + std::to_string(loaded) + " tables from " + syzygy_dir);
                }
            }
            else if(name=="SyzygyProbeLimit")
            {
                stop_search();
                tb_probe_limit = std::max(0, std::min(5, std::atoi(value.c_str())));
            }
            else if(name=="NNEFile")
            {
                stop_search();
                nne_file = value;
                apply_nne();
            }
            else if(name=="UseNNE")
            {
                stop_search();
                use_nne = value=="true";
                apply_nne();
            }
        }
        else if(cmd=="debug" || cmd=="register" || cmd=="ponderhit")
        ;  // no options / pondering yet
        else
        send("info string unknown command: " + cmd);
    }
    stop_search();
    return 0;
}

int uci_loop()
{
    UCI_Engine engine;
    return engine.loop();
}

#endif // UCI_CPP
