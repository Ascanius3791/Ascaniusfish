// OWNERSHIP=Claude
// make gui-match A=<engine> B=<engine>: one game between two UCI engines at a
// fixed time per move, shown live in display_board.py, with the depth every
// search reached.
//
//   ./tools/gui_match <white> <black> [key=value ...]     (run from the repo root)
//
// <white>/<black> are UCI engine binaries (or wrapper scripts, e.g. one that
// runs the engine under perf record). Each move is "go movetime <ms>". The
// GUI gets every move and the mover's current PV; the terminal gets one line
// per move, and a per-engine depth summary at the end.
//
// Options:
//   movetime=10000     ms per move
//   fen=<fen>          start position (default: the standard start position)
//   pgn=<file>         game output (default gui_match.pgn), "{+0.35/7 10.00s}" per move
//   gui=0              no GUI, terminal only
//   quit_wait=60000    ms to wait for an engine to exit (perf record needs a while)
#include "game_rules.hpp"
#include "uci_engine.hpp"

#include <cmath>
#include <map>
#include <memory>

constexpr long long MOVE_GRACE_MS = 5000;  // past movetime before an engine counts as hung
constexpr int MAX_GAME_PLIES = 600;

[[noreturn]] static void die(const std::string& msg)
{
    std::fprintf(stderr, "gui_match: %s\n", msg.c_str());
    std::exit(2);
}

// Engine eval in the GUI's encoding: centipawns from white's view, mates near INT_MIN/INT_MAX.
static long long gui_eval(const Search_Info& s, bool white_to_move)
{
    long long v = std::atoll(s.score_value.c_str());
    long long white_view;
    if(s.score_kind=="mate")
    {
        bool mover_mates = v>0;
        long long plies = std::llabs(v)*2 - (mover_mates ? 1 : 0);
        bool white_mates = mover_mates==white_to_move;
        white_view = white_mates ? (long long)INT_MAX-plies : (long long)INT_MIN+plies;
    }
    else
    white_view = white_to_move ? v : -v;
    return white_view;
}

// "12. Nf3 Nc6 13. ..." for a PV starting at `pos`; stops at the first move it can't play.
static std::string pv_to_san(BB pos, int fullmove, const std::vector<std::string>& pv)
{
    std::unique_ptr<BB[]> children(new BB[MAX_LEGAL_MOVES]);
    std::string out;
    for(size_t i=0;i<pv.size();i++)
    {
        int n = std::get<0>(all_moves(&pos, children.get()));
        int k = 0;
        while(k<n && get_UCI(&pos, children.get()+k)!=pv[i]) k++;
        if(k==n)
        break;
        if(pos.white_move) out += std::to_string(fullmove) + ". ";
        else if(i==0) out += std::to_string(fullmove) + "... ";
        out += san(pos, children.get(), n, k) + " ";
        if(!pos.white_move) fullmove++;
        pos = children[k];
    }
    return out;
}

struct Side_Stats
{
    std::string label;
    std::vector<int> depths;
    long long nodes = 0, time_ms = 0;  // up to the last completed iteration of each search
};

static std::string today()
{
    std::time_t t = std::time(nullptr);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y.%m.%d", std::localtime(&t));
    return buf;
}

static std::string basename_of(const std::string& path)
{
    size_t slash = path.find_last_of('/');
    return slash==std::string::npos ? path : path.substr(slash+1);
}

int main(int argc, char** argv)
{
    if(argc<3)
    die("usage: gui_match <white> <black> [movetime=10000] [fen=...] [pgn=gui_match.pgn] [gui=0] [quit_wait=60000]");
    std::map<std::string, std::string> opt = {{"movetime", "10000"}, {"fen", Standart_FEN}, {"pgn", "gui_match.pgn"},
                                              {"gui", "1"}, {"quit_wait", "60000"}};
    for(int i=3;i<argc;i++)
    {
        std::string a = argv[i];
        size_t eq = a.find('=');
        if(eq==std::string::npos || !opt.count(a.substr(0, eq)))
        die("unknown option " + a);
        opt[a.substr(0, eq)] = a.substr(eq+1);
    }
    const long long movetime = std::atoll(opt["movetime"].c_str());
    const std::string fen = opt["fen"];

    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    Engine engines[2];  // [0] = white, [1] = black
    Side_Stats stats[2];
    for(int c=0;c<2;c++)
    {
        engines[c].path = argv[1+c];
        stats[c].label = basename_of(argv[1+c]);
        if(!engines[c].start())
        die("engine does not start: " + engines[c].path);
        engines[c].send("ucinewgame");
    }
    if(stats[0].label==stats[1].label)
    { stats[0].label += " (white)"; stats[1].label += " (black)"; }

    std::unique_ptr<Game> game(new Game);
    BB start;
    if(!uci_parse_fen(fen, start))
    die("bad fen: " + fen);
    int halfmoves = 0, fullmove = 1;
    {
        std::istringstream fields(fen);
        std::string skip;
        for(int i=0;i<4;i++) fields >> skip;
        fields >> halfmoves >> fullmove;
    }
    game->start(start, halfmoves, fullmove);
    const std::string start_fen = game->fen();

    FILE* gui = nullptr;
    if(opt["gui"]!="0")
    {
        gui = popen(("python3 display_board.py \"" + start_fen + "\"").c_str(), "w");
        if(!gui) die("cannot start display_board.py (run from the repo root)");
    }
    auto to_gui = [&](const std::string& line)
    {
        if(!gui) return;
        std::fprintf(gui, "%s\n", line.c_str());
        std::fflush(gui);
    };

    std::printf("White: %s\nBlack: %s\n%lld ms per move\n\n", stats[0].label.c_str(), stats[1].label.c_str(), movetime);
    std::printf("%5s  %-5s %-8s %5s %12s %9s %8s %8s\n", "ply", "side", "move", "depth", "nodes", "nps", "time", "score");

    std::string result, reason;
    std::vector<std::string> comments;
    for(;;)
    {
        Outcome o = game->outcome(reason);
        if(o!=Outcome::ONGOING)
        {
            result = o==Outcome::WHITE_WINS ? "1-0" : o==Outcome::BLACK_WINS ? "0-1" : "1/2-1/2";
            break;
        }
        if((int)game->uci_moves.size()>=MAX_GAME_PLIES)
        {
            reason = "adjudicated draw: game too long";
            result = "1/2-1/2";
            break;
        }
        const BB pos = game->positions.back();
        const bool wtm = pos.white_move;
        Engine& e = engines[wtm ? 0 : 1];

        std::string position = "position fen " + start_fen;
        if(!game->uci_moves.empty()) position += " moves";
        for(const std::string& m : game->uci_moves) position += " " + m;
        e.send(position);
        e.send("go movetime " + std::to_string(movetime));

        long long t0 = now_ms(), deadline = t0+movetime+MOVE_GRACE_MS;
        Search_Info last;
        std::string line, bestmove;
        while(e.read_line(line, deadline))
        {
            Search_Info s;
            if(parse_info(line, s))
            {
                last = s;
                if(!s.pv.empty())
                to_gui("PV depth=" + std::to_string(s.depth) + " eval=" + std::to_string(gui_eval(s, wtm))
                       + " " + pv_to_san(pos, game->fullmove(), s.pv));
            }
            else if(line.compare(0, 9, "bestmove ")==0)
            {
                std::istringstream in(line.substr(9));
                in >> bestmove;
                break;
            }
        }
        long long used = now_ms()-t0;
        if(bestmove.empty())
        {
            reason = std::string(wtm ? "white" : "black") + " engine hung or crashed";
            result = wtm ? "0-1" : "1-0";
            break;
        }
        int ply = (int)game->uci_moves.size()+1;
        if(!game->play(bestmove))
        {
            reason = std::string(wtm ? "white" : "black") + " plays an illegal move " + bestmove;
            result = wtm ? "0-1" : "1-0";
            break;
        }
        to_gui(bestmove);

        Side_Stats& st = stats[wtm ? 0 : 1];
        st.depths.push_back(last.depth);
        st.nodes += last.nodes;
        st.time_ms += last.time_ms;
        std::string score = score_text(last);
        std::printf("%5d  %-5s %-8s %5d %12lld %9lld %7.2fs %8s\n", ply, wtm ? "white" : "black",
                    game->san_moves.back().c_str(), last.depth, last.nodes, last.nps, used/1000.0, score.c_str());
        std::fflush(stdout);
        char comment[64];
        std::snprintf(comment, sizeof comment, "{%s/%d %.2fs}", score.c_str(), last.depth, used/1000.0);
        comments.push_back(comment);
    }

    std::printf("\nResult: %s (%s)\n\n", result.c_str(), reason.c_str());
    std::printf("%-28s %6s %10s %8s %6s %6s %12s\n", "engine", "moves", "mean depth", "median", "min", "max", "nodes/s");
    for(const Side_Stats& st : stats)
    {
        if(st.depths.empty()) continue;
        std::vector<int> d = st.depths;
        std::sort(d.begin(), d.end());
        double mean = 0;
        for(int x : d) mean += x;
        mean /= d.size();
        std::printf("%-28s %6zu %10.2f %8d %6d %6d %12lld\n", st.label.c_str(), d.size(), mean, d[d.size()/2], d.front(), d.back(),
                    st.nodes*1000/std::max(1LL, st.time_ms));
    }
    for(const Side_Stats& st : stats)
    {
        std::map<int, int> hist;
        for(int x : st.depths) hist[x]++;
        std::printf("%s depth histogram:", st.label.c_str());
        for(auto& [depth, n] : hist) std::printf("  d%d:%d", depth, n);
        std::printf("\n");
    }

    std::ofstream pgn(opt["pgn"]);
    pgn << "[Event \"gui_match\"]\n[Date \"" << today() << "\"]\n[White \"" << stats[0].label << "\"]\n[Black \""
        << stats[1].label << "\"]\n[Result \"" << result << "\"]\n[FEN \"" << start_fen << "\"]\n[SetUp \"1\"]\n[TimeControl \"movetime "
        << movetime << "ms\"]\n\n";
    std::string text, row;
    bool white_first = game->positions[0].white_move;
    for(size_t i=0;i<game->san_moves.size();i++)
    {
        bool white = (i%2==0)==white_first;
        int number = game->start_fullmove + ((int)i+!white_first)/2;
        std::string token = white ? std::to_string(number) + ". " : i==0 ? std::to_string(number) + "... " : "";
        token += game->san_moves[i] + " " + comments[i];
        if(row.size()+token.size()+1>80) { text += row + "\n"; row.clear(); }
        row += (row.empty() ? "" : " ") + token;
    }
    pgn << text << row << (row.empty() ? "" : " ") << "{" << reason << "} " << result << "\n";
    std::printf("PGN in %s\n", opt["pgn"].c_str());

    long long quit_wait = std::atoll(opt["quit_wait"].c_str());
    for(Engine& e : engines) e.stop(quit_wait);
    if(gui)
    {
        std::printf("Close the board window to exit.\n");
        pclose(gui);
    }
}
