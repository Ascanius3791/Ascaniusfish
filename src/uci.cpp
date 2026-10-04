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
    if(is_tb_score(eval))
    return "cp " + std::to_string(white_to_move == (eval>0) ? TB_WIN_CP : -TB_WIN_CP);  // a table win, not a mate
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

// FNV-1a of a file's bytes, as 16 hex digits; "" if it cannot be read. Names
// the net a stored score was found with (#79).
static std::string file_hash(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if(!in)
    return "";
    uint64_t h = 0xcbf29ce484222325ULL;
    char buf[65536];
    while(in.read(buf, sizeof buf) || in.gcount()>0)
    {
        for(std::streamsize i=0;i<in.gcount();i++)
        h = (h ^ (unsigned char)buf[i]) * 0x100000001b3ULL;
        if(!in)
        break;
    }
    char hex[17];
    std::snprintf(hex, sizeof hex, "%016llx", (unsigned long long)h);
    return hex;
}

// UseNNE / NNEFile (#52): loads the net when it is switched on or its file
// changes. Clears the TT whenever the eval changes, since its quiet leaves
// were scored with the other one.
void UCI_Engine::apply_nne()
{
    nne_applied = true;
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
            if(nne_hash.empty() || nne::loaded_path!=was_loaded)
            nne_hash = file_hash(path);
            send("info string nne on, " + path + ", hash " + nne_hash);
        }
        else
        send("info string nne off: " + error);
    }
    if(table && (nne::enabled!=was_enabled || (nne::enabled && nne::loaded_path!=was_loaded)))
    table->reset();
}

// WeightsFile (#85): loads a weight set (lib/weight_set.hpp), or the compiled-in
// default for an empty value; a relative path is looked for like NNEFile's. A
// file that does not read leaves the set in use. Clears the TT when the set
// changes, since its scores were found with the other one.
void UCI_Engine::apply_weights(const std::string& file)
{
    WEIGHTS next = WEIGHTS_OG;
    Weight_Set_Info info;
    std::string error;
    if(!file.empty() && !load_weight_set(nne_resolve(file), next, info, error))
    {
        send("info string weights " + std::to_string(weights.version) + " kept: " + error);
        return;
    }
    const bool changed = std::memcmp(&next, &weights, sizeof(WEIGHTS))!=0;
    weights = next;
    send("info string weights " + std::to_string(weights.version) + (file.empty() ? ", the default set" : ", " + file));
    if(table && changed)
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
    hint_depth = 0;   // a hint belongs to the position it was given for
    hint_pv.clear();
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

// What the scores of the coming search are found with (#79), sent as
// "info string provenance ..." before its first "info depth": the eval version,
// the weight set's version (#85), the net (its file hash, or off), how many
// pieces each tablebase answers for (0 = not loaded) and the commit the binary
// was built from.
std::string UCI_Engine::provenance() const
{
    const int syzygy_pieces = std::min(tb_probe_limit, syzygy::max_pieces());
    const int gaviota_pieces = std::min(tb_probe_limit, gaviota::max_pieces());
    return "provenance evalversion " + std::to_string(EVAL_VERSION)
         + " weights " + std::to_string(weights.version)
         + " nne " + (nne::enabled ? nne_hash : std::string("off"))
         + " syzygy " + std::to_string(std::max(0, syzygy_pieces))
         + " gaviota " + std::to_string(std::max(0, gaviota_pieces))
         + " commit " + ENGINE_COMMIT;
}

// Seeds the TT with a line found earlier for `root` (#79): the position i plies
// down the line gets its move at depth D-i as a vacuous lower bound (eval
// INT_MIN, bound_type -1). minimax() reads such an entry as the TT move only —
// a lower bound of INT_MIN never cuts and never narrows — and insert() keeps
// it until the search stores something at least as deep there, so it orders
// the moves through iteration D and gives way to the engine's own entries at
// D+1. A wrong or stale line can cost time, never change a score. Called after
// new_search(), so the entries are this search's and do not age. Returns the
// plies seeded; the line ends at its first illegal move.
int UCI_Engine::seed_hint(const BB& root)
{
    if(!table || hint_depth<=0)
    return 0;
    BB pos = root;
    int seeded = 0;
    for(size_t i=0; i<hint_pv.size() && hint_depth-(int)i>=1; i++)
    {
        auto result = all_moves(&pos, pv_buf);
        const int n = std::get<0>(result);
        int found = -1;
        for(int k=0;k<n;k++)
        if(get_UCI(&pos, pv_buf+k)==hint_pv[i]) { found = k; break; }
        if(found<0)
        break;
        TT_entry entry;
        entry.initialized = true;
        entry.zobrist_hash = pos.zobrist_hash;
        entry.pv_line = PV_Line(std::get<1>(result)[found], hint_depth-(int)i);
        entry.pv_line.eval = INT_MIN;
        entry.pv_line.bound_type = -1;
        table->insert(entry);
        pos = pv_buf[found];
        seeded++;
    }
    return seeded;
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

void UCI_Engine::extend_pv_from_tt(const BB& root, PV_Line& pv, int ply)
{
    // Stops at a position without a TT move, an illegal move, a position
    // already on the line or in the game (path_history[0..ply]), or MAX_PV_Lenght.
    std::vector<uint64_t> seen;
    for(int i=0;i<=ply;i++)
    seen.push_back(path_history[i].zobrist_hash);
    BB cur = root;
    for(int k=0; ; k++)
    {
        Move next;
        if(k<pv.current_lenght)
        next = pv.at(k);
        else
        {
            if(k>=MAX_PV_Lenght)
            return;
            TT_readout readout = table->is_retrivable_eval(&cur, 0);
            if(!readout.is_found || readout.pv_line.current_lenght==0)
            return;
            next = readout.pv_line.moves[0];
        }
        auto result = all_moves(&cur, pv_buf);
        const int n = std::get<0>(result);
        const std::vector<Move>& moves = std::get<1>(result);
        int found = -1;
        for(int i=0;i<n;i++)
        if(moves[i]==next) { found=i; break; }
        if(found<0)
        return;
        if(k>=pv.current_lenght)
        {
            if(std::find(seen.begin(), seen.end(), pv_buf[found].zobrist_hash)!=seen.end())
            return;
            if(!pv.set_move(k, moves[found]))
            return;
            pv.current_lenght = k+1;
        }
        cur = pv_buf[found];
        seen.push_back(cur.zobrist_hash);
    }
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
        tm = new TimeManager(game.back(), white ? limits.wtime : limits.btime, white ? limits.winc : limits.binc, limits.movestogo, lambda_history, weights);
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
    std::vector<int> cls, key;
    if(!tb_classes(root, children, cls, key))
    return false;
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

// Each root move's class under the 50-move rule, from the DTZ tables and the
// game's real halfmove clock: 3 win, 2 draw, 1 loss; within a class a lower
// key is better (fewest plies to the next zeroing move, the longest resistance).
bool UCI_Engine::tb_classes(const BB& root, const std::vector<BB>& children, std::vector<int>& cls, std::vector<int>& key)
{
    const int n = (int)children.size();
    int hc = game[0].halfmoves_since_last_capture_or_pawn_move;  // the real clock: the FEN's, then the game's steps
    for(size_t i=1;i<game.size();i++)
    hc = is_zeroing_step(game[i-1], game[i]) ? 0 : hc+1;
    cls.assign(n, 0);
    key.assign(n, 0);
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
    return true;
}

// A won or lost root inside the Gaviota tables (#77): the move with the
// quickest mate, or the longest resistance, played at once and reported as
// `mate N`, with the whole mating line as the PV. The DTM tables ignore the
// 50-move rule; with Syzygy loaded a win is only played along moves that still
// win under it, and a root the rule makes a draw (a cursed win, a blessed
// loss) is left to the DTZ path. SyzygyProbeLimit caps these tables too, so
// one setting says up to how many pieces any table is used. False = not
// answered here.
bool UCI_Engine::dtm_root(const BB& root, const std::vector<BB>& children, const UCI_Limits& limits, long long start_ns)
{
    int pieces = 0;
    for(int i=0;i<12;i++)
    pieces += __builtin_popcountll(root.Board[i]);
    if(pieces>tb_probe_limit)
    return false;
    int root_res, root_plies;
    if(!gaviota::probe_dtm(&root, root_res, root_plies) || root_res==0)
    return false;
    const int n = (int)children.size();
    std::vector<int> res(n), plies(n);
    for(int i=0;i<n;i++)
    if(!gaviota::probe_dtm(&children[i], res[i], plies[i]))
    return false;
    long long probes = n+1;
    std::vector<int> cls, key;
    const bool fifty = syzygy::max_pieces()>0 && tb_classes(root, children, cls, key);
    if(fifty && *std::max_element(cls.begin(), cls.end()) != (root_res==1 ? 3 : 1))
    return false;
    // children[i] is the opponent's: their loss is our win
    int pick = -1;
    for(int i=0;i<n;i++)
    {
        if(root_res==1 ? (res[i]!=-1 || (fifty && cls[i]!=3)) : res[i]!=1)
        continue;
        if(pick<0 || (root_res==1 ? plies[i]<plies[pick] : plies[i]>plies[pick]))
        pick = i;
    }
    if(pick<0)
    return false;
    const int mate_plies = plies[pick]+1;

    // The line: the same choice at every ply, the loser's the longest resistance.
    std::vector<std::string> line{get_UCI(&root, &children[pick])};
    BB pos = children[pick];
    BB buf[MAX_ORDERED_MOVES];
    while(line.size()<64)
    {
        const int m = std::get<0>(all_moves(&pos, buf, MAX_ORDERED_MOVES));
        // the winner's quickest mate (a child the opponent loses), else the
        // loser's slowest one (a child the opponent wins)
        int win = -1, win_plies = 0, lose = -1, lose_plies = 0;
        bool known = true;
        for(int i=0;i<m && known;i++)
        {
            int r, p;
            known = gaviota::probe_dtm(&buf[i], r, p);
            probes++;
            if(known && r==-1 && (win<0 || p<win_plies)) { win = i; win_plies = p; }
            if(known && r==1 && (lose<0 || p>lose_plies)) { lose = i; lose_plies = p; }
        }
        const int next = win>=0 ? win : lose;
        if(m==0 || !known || next<0)
        break;
        line.push_back(get_UCI(&pos, &buf[next]));
        pos = buf[next];
    }

    const bool white_mates = root.white_move==(root_res==1);
    const long long elapsed_ms = (steady_now_ns()-start_ns)/1000000;
    std::string info = "info depth 1 score " + uci_score(white_mates ? INT_MAX-mate_plies : INT_MIN+mate_plies, root.white_move)
                     + " nodes " + std::to_string(probes) + " time " + std::to_string(elapsed_ms)
                     + " tbhits " + std::to_string(probes) + " pv";
    for(const std::string& mv : line)
    info += " " + mv;
    send(info);
    while(limits.infinite && !stop_search_flag.load())
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    send("bestmove " + line[0]);
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
    // A search below a kept move only says something where the tables don't
    // answer. minimax() probes every node within SyzygyProbeLimit, so once the
    // kept moves' positions are all within it, each one is its table score at
    // once and there is nothing to search: the eval picks among them, a mate first.
    bool all_probed = true;
    for(int i : keep)
    {
        int unused;
        if(!tb_probe_score(&children[i], unused))
        {
            all_probed = false;
            break;
        }
    }
    if(keep.size()==1 || all_probed)
    {
        int pick = keep[0];
        int pick_eval = eval(&children[pick], weights, -1);
        for(int i : keep)
        {
            const int e = eval(&children[i], weights, -1);
            if(root.white_move ? e>pick_eval : e<pick_eval)
            {
                pick = i;
                pick_eval = e;
            }
        }
        best = get_UCI(&root, &children[pick]);
        const std::string score = exception_eval(&children[pick])==2
            ? uci_score(root.white_move ? INT_MAX-1 : INT_MIN+1, root.white_move)  // mate in 1
            : "cp " + std::to_string(tb_cp);
        const std::string count = std::to_string(keep.size());
        send("info depth 1 score " + score + " nodes " + count + " tbhits " + count + " pv " + best);
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
                cpv = make_repetition_draw_pv_line(&child, wfh, d-1, ply+1, ply, weights);
                else
                cpv = minimax(&child, wfh, d-1, weights, INT_MIN, INT_MAX, table, path_history, ply+1, cycle_table, ply);
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
        bool proven_mate = best_pv.eval <= INT_MIN + max_mating_seq || best_pv.eval >= INT_MAX - max_mating_seq;
        // The tables' result, not the eval's guess below a kept move; only a
        // mate the search has seen says more than they do.
        std::string info = "info depth " + std::to_string(d)
                         + " score " + (proven_mate ? uci_score(best_pv.eval, root.white_move) : "cp " + std::to_string(tb_cp))
                         + " nodes " + std::to_string(nodes)
                         + " nps " + std::to_string(nodes*1000/std::max(1LL, elapsed_ms))
                         + " time " + std::to_string(elapsed_ms)
                         + " tbhits 1 pv";
        for(const std::string& m : line)
        info += " " + m;
        send(info);
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

int UCI_Engine::verify_mate(const BB& root, PV_Line& pv, long long budget, bool may_change_line)
{
    const bool white_mates = pv.eval > 0;
    const int claimed = white_mates ? INT_MAX - pv.eval : pv.eval - INT_MIN;
    const bool mater_to_move = root.white_move == white_mates;
    int verdict = 0, found = -1;
    bool quickest = false;
    std::vector<Move> line;
    long long nodes = 0;
    try
    {
        // Checks first: cheap, and most mates are a run of checks. A shorter
        // mate than the one they find needs a quiet move: full width below it.
        Mate_Result c = find_mate(root, wfh, white_mates, claimed, Mate_Mode::CHECKS_ONLY, budget);
        nodes += c.nodes;
        Mate_Result f;
        if(c.status==Mate_Result::FOUND)
        {
            verdict = 1;
            found = c.plies;
            line = c.line;
            f = find_mate(root, wfh, white_mates, found-2, Mate_Mode::FULL_WIDTH, std::max(0LL, budget-nodes));
            quickest = f.status!=Mate_Result::UNKNOWN;
        }
        else
        {
            f = find_mate(root, wfh, white_mates, claimed, Mate_Mode::FULL_WIDTH, std::max(0LL, budget-nodes));
            verdict = f.status==Mate_Result::FOUND ? 1 : f.status==Mate_Result::NONE ? -1 : 0;
        }
        nodes += f.nodes;
        if(f.status==Mate_Result::FOUND)
        {
            found = f.plies;
            line = f.line;
            quickest = true;
        }
    }
    catch(const search_aborted&)
    {
        verdict = 0;  // out of time: the claim stands unchecked
    }
    std::string info = "info string mate claim " + std::to_string(claimed) + " plies ";
    if(verdict==1)
    {
        info += "confirmed: " + std::to_string(found) + " plies" + (quickest ? ", the quickest" : ", checks");
        // The side that mates plays the mate found; the side mated keeps its
        // own defence and only gets the score.
        const bool same_move = !line.empty() && pv.current_lenght>0 && line[0]==pv.at(0);
        if(mater_to_move && !line.empty() && (may_change_line || same_move))
        {
            PV_Line mate_pv(0);
            for(const Move& m : line)
            mate_pv.append(m, 0);
            mate_pv.depth = pv.depth;
            mate_pv.bound_type = 0;
            pv = std::move(mate_pv);
        }
        if(!mater_to_move || may_change_line || same_move)
        pv.eval = white_mates ? INT_MAX - found : INT_MIN + found;
    }
    else
    info += verdict<0 ? "refuted" : "unchecked";
    send(info + " nodes " + std::to_string(nodes));
    return verdict;
}

std::vector<PV_Line> multipv_search(const BB& root, BB* wfh, int depth, lookup_table* table, BB* path_history, int ply,
                                    const CuckooCycleTable* cycle_table, int lines_wanted, const WEIGHTS& W)
{
    // Pass k searches the root without the first moves of lines 1..k-1;
    // pass 1 is the plain search.
    std::vector<PV_Line> lines;
    std::vector<Move> excluded;
    for(int k=0; k<lines_wanted; k++)
    {
        PV_Line line = minimax(&root, wfh, depth, W, INT_MIN, INT_MAX, table, path_history, ply, cycle_table,
                               INT_MIN, true, excluded.data(), (int)excluded.size());
        if(k>0 && line.current_lenght==0)
        break;
        if(line.current_lenght>0)
        excluded.push_back(line.at(0));
        lines.push_back(std::move(line));
    }
    // A later pass can find more than an earlier one (it sees the TT entries
    // the earlier ones left): best first for the side to move.
    std::stable_sort(lines.begin(), lines.end(), [&](const PV_Line& a, const PV_Line& b)
    { return root.white_move ? a.eval>b.eval : a.eval<b.eval; });
    return lines;
}

void UCI_Engine::search(UCI_Limits limits, long long start_ns)
{
    if(table) table->new_search();
    const BB root = game.back();
    send("info string " + provenance());
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
    if(gaviota::max_pieces()>0 && dtm_root(root, std::vector<BB>(wfh, wfh+n), limits, start_ns))
    return;
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
    if(hint_depth>0)
    send("info string hint seeded " + std::to_string(seed_hint(root)) + " plies at depth " + std::to_string(hint_depth));
    // Fallback if even depth 1 gets aborted: the move ordering's favourite.
    std::vector<int> order = sorting_moves(wfh, std::get<1>(result), n, root.white_move, nullptr, 0, weights);
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
    const int lines_wanted = std::min(multipv, n);
    for(int d=1; d<=max_depth; d++)
    {
        if(search_stop_requested())
        break;
        // A depth aborted part way keeps the previous set.
        std::vector<PV_Line> lines;
        try
        {
            lines = multipv_search(root, wfh, d, table, path_history, ply, cycle_table, lines_wanted, weights);
        }
        catch(const search_aborted&)
        {
            break;
        }
        if(tt_walk)
        for(PV_Line& line : lines)
        extend_pv_from_tt(root, line, ply);
        PV_Line& pv = lines[0];

        // A mate the search claims is checked by the mate search before it is
        // reported as such or ends the deepening (#78); a table score is not a mate.
        const bool mate_claimed = (pv.eval <= INT_MIN + max_mating_seq || pv.eval >= INT_MAX - max_mating_seq) && !is_tb_score(pv.eval);
        int mate_check = 0;
        if(mate_claimed)
        mate_check = verify_mate(root, pv, std::max(MATE_VERIFY_MIN_NODES, search_nodes-nodes_before), lines_wanted==1);

        long long elapsed_ms = (steady_now_ns()-start_ns)/1000000;
        long long nodes = search_nodes-nodes_before;
        for(size_t k=0; k<lines.size(); k++)
        {
            std::vector<std::string> line = pv_to_uci(root, lines[k]);
            if(k==0 && !line.empty())
            best = line[0];
            std::string info = "info depth " + std::to_string(d)
                             + (lines_wanted>1 ? " multipv " + std::to_string(k+1) : "")
                             + " score " + uci_score(lines[k].eval, root.white_move)
                             + " nodes " + std::to_string(nodes)
                             + " nps " + std::to_string(nodes*1000/std::max(1LL, elapsed_ms))
                             + " time " + std::to_string(elapsed_ms)
                             + " tbhits " + std::to_string(tb_hits-tb_hits_before)
                             + " pv";
            for(const std::string& m : line)
            info += " " + m;
            send(info);
        }

        if((is_tb_score(pv.eval) || mate_check==1) && !limits.infinite)
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
            send("info string evalversion " + std::to_string(EVAL_VERSION) + " weights " + std::to_string(weights.version) + " commit " + ENGINE_COMMIT);
            send("option name SyzygyPath type string default <empty>");
            send("option name SyzygyProbeLimit type spin default 5 min 0 max 5");
            if(gaviota::compiled_in())
            {
                send("option name GaviotaTbPath type string default <empty>");
                send("option name GaviotaTbCache type spin default 32 min 1 max 1024");
            }
            send("option name NNEFile type string default nets/nne_d6.bin");
            send("option name UseNNE type check default true");
            send("option name WeightsFile type string default <empty>");
            send("option name MultiPV type spin default 1 min 1 max " + std::to_string(MAX_ORDERED_MOVES));
            send("option name TTWalk type check default true");
            send("option name TTDeeperCuts type check default true");
            if(!tt_bounds_never_narrow)//only a -DTT_BOUNDS_NEVER_NARROW=0 build can narrow (#65)
            {
                send("option name TTNarrowing type check default false");
                send("option name TTNarrowingDeeper type check default false");
            }
            send("uciok");
        }
        else if(cmd=="isready")
        {
            ensure_table();
            if(!nne_applied)
            apply_nne();//the default UseNNE=true, when no setoption has applied it yet
            send("readyok");
        }
        else if(cmd=="ucinewgame")
        {
            stop_search();
            if(table)
            table->reset();
            mate_tt_clear();
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
        {
            if(!nne_applied)
            apply_nne();//see isready
            handle_go(tokens);
        }
        else if(cmd=="hint")
        {
            // hint depth D pv <moves>: after "position", before "go" (#79)
            stop_search();
            hint_depth = 0;
            hint_pv.clear();
            for(size_t k=1;k<tokens.size();k++)
            {
                if(tokens[k]=="depth" && k+1<tokens.size() && all_digits(tokens[k+1]))
                hint_depth = std::min(UCI_MAX_DEPTH, std::atoi(tokens[++k].c_str()));
                else if(tokens[k]=="pv")
                {
                    hint_pv.assign(tokens.begin()+k+1, tokens.end());
                    break;
                }
            }
            if(hint_pv.empty())
            hint_depth = 0;
        }
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
            else if(name=="GaviotaTbPath" || name=="GaviotaTbCache")
            {
                stop_search();
                if(name=="GaviotaTbPath")
                gaviota_dir = value=="<empty>" ? "" : value;
                else
                gaviota_cache_mb = std::max(1, std::min(1024, std::atoi(value.c_str())));
                if(gaviota_dir.empty())
                {
                    gaviota::release();
                    send("info string gaviota off");
                }
                else
                {
                    const int pieces = gaviota::init(gaviota_dir, gaviota_cache_mb);
                    send("info string gaviota " + (pieces ? "up to " + std::to_string(pieces) + " pieces" : std::string("no tables"))
                         + " from " + gaviota_dir);
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
            else if(name=="WeightsFile")
            {
                stop_search();
                apply_weights(value=="<empty>" ? "" : value);
            }
            else if(name=="MultiPV")
            {
                stop_search();
                multipv = std::max(1, std::min(MAX_ORDERED_MOVES, std::atoi(value.c_str())));
            }
            else if(name=="TTWalk")
            {
                stop_search();
                tt_walk = value=="true";
            }
            else if(name=="TTDeeperCuts")
            {
                stop_search();
                tt_deeper_cuts = value=="true";
            }
            else if(name=="UseNNE")
            {
                stop_search();
                use_nne = value=="true";
                apply_nne();
            }
            else if(!tt_bounds_never_narrow && (name=="TTNarrowing" || name=="TTNarrowingDeeper"))
            {
                stop_search();
                (name=="TTNarrowing" ? narrow : narrow_deeper) = value=="true";
                tt_narrowing = narrow ? (narrow_deeper ? 2 : 1) : 0;
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
