// OWNERSHIP=Claude
#ifndef UCI_CPP
#define UCI_CPP
#include "../lib/uci.hpp"
#include <sstream>
#include <algorithm>

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
        if(moves[i]==pv.moves[k]) { found=i; break; }
        if(found<0)
        break;
        out.push_back(get_UCI(&cur, pv_buf+found));
        cur = pv_buf[found];
    }
    return out;
}

static long long perft(const BB* pos, int depth, BB* buf)
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

    // Deadline: movetime, or a simple share of the clock until M4 brings
    // real time management.
    long long budget_ms = limits.movetime_ms;
    if(clocked && budget_ms==0)
    {
        bool white = game.back().white_move;
        long long time = std::max(0LL, white ? limits.wtime : limits.btime);
        long long inc = white ? limits.winc : limits.binc;
        long long moves_left = limits.movestogo>0 ? limits.movestogo+1 : 30;
        budget_ms = time/moves_left + inc*3/4;
        budget_ms = std::max(10LL, std::min(budget_ms, time-50));
    }
    search_deadline_ns.store(budget_ms>0 && !limits.infinite ? start_ns + budget_ms*1000000LL : 0);
    stop_search_flag.store(false);
    search_thread = std::thread(&UCI_Engine::search, this, limits, start_ns);
}

void UCI_Engine::search(UCI_Limits limits, long long start_ns)
{
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
    // Fallback if even depth 1 gets aborted: the move ordering's favourite.
    std::vector<int> order = sorting_moves(wfh, std::get<1>(result), n, root.white_move, nullptr, 0, WEIGHTS_OG);
    std::string best = get_UCI(&root, wfh+order[0]);

    // Repetition context, seeded from the game like engine_move() does.
    int seed_count = std::min({(int)game.size(), root.halfmoves_since_last_capture_or_pawn_move+1, MAX_SEARCH_PLY});
    for(int i=0;i<seed_count;i++)
    path_history[i] = game[game.size()-seed_count+i];
    int ply = seed_count-1;

    int max_depth = limits.depth>0 ? std::min(limits.depth, UCI_MAX_DEPTH) : UCI_MAX_DEPTH;
    long long nodes_before = search_nodes;
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
                         + " pv";
        for(const std::string& m : line)
        info += " " + m;
        send(info);

        bool proven_mate = pv.eval <= INT_MIN + max_mating_seq || pv.eval >= INT_MAX - max_mating_seq;
        if(proven_mate && !limits.infinite)
        break;
        // On a clock, don't start an iteration that likely can't finish.
        long long deadline = search_deadline_ns.load();
        if(deadline && !limits.movetime_ms && steady_now_ns()-start_ns > (deadline-start_ns)/2)
        break;
    }

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
        else if(cmd=="setoption" || cmd=="debug" || cmd=="register" || cmd=="ponderhit")
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
