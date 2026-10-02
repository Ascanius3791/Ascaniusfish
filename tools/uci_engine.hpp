// OWNERSHIP=Claude
// One UCI engine process, driven over pipes (tools/match.cpp, tools/gui_match.cpp).
#ifndef TOOLS_UCI_ENGINE_HPP
#define TOOLS_UCI_ENGINE_HPP
#include "../lib/search_control.hpp"  // steady_now_ns
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

constexpr long long READY_TIMEOUT_MS = 60000;

// One more line of a MultiPV set (#69): its score and moves.
struct Search_Line
{
    std::string score_kind, score_value;  // "cp"/"mate", mover's view
    std::vector<std::string> pv;
};

// One "info depth ..." line of an engine: what the last completed iteration of
// its search reached. The score is the mover's view, as UCI has it. Under
// MultiPV this is line 1 and `more` holds lines 2..K of the same depth, best
// first (gui/engine_link.hpp collects them).
struct Search_Info
{
    int depth = 0;
    long long nodes = 0, nps = 0, time_ms = 0;
    std::string score_kind, score_value;  // "cp"/"mate", mover's view
    std::vector<std::string> pv;
    int multipv = 0;                      // the line's "multipv i", 0 when the engine gave none
    std::vector<Search_Line> more;
};

inline bool parse_info(const std::string& line, Search_Info& info)
{
    if(line.compare(0, 11, "info depth ")!=0)
    return false;
    std::istringstream in(line);
    std::string tok;
    Search_Info s;
    while(in >> tok)
    {
        if(tok=="depth") in >> s.depth;
        else if(tok=="multipv") in >> s.multipv;
        else if(tok=="nodes") in >> s.nodes;
        else if(tok=="nps") in >> s.nps;
        else if(tok=="time") in >> s.time_ms;
        else if(tok=="score") in >> s.score_kind >> s.score_value;
        else if(tok=="pv") { while(in >> tok) s.pv.push_back(tok); }
    }
    info = s;
    return true;
}

// "+0.35" or "+M3"; "" for a search that never reported a score.
inline std::string score_text(const Search_Info& s)
{
    if(s.score_kind.empty())
    return "";
    if(s.score_kind=="mate")
    return std::string(s.score_value[0]=='-' ? "-M" : "+M") + (s.score_value.c_str()+(s.score_value[0]=='-'));
    char buf[32];
    std::snprintf(buf, sizeof buf, "%+.2f", std::atoi(s.score_value.c_str())/100.0);
    return buf;
}

static long long now_ms()
{
    return steady_now_ns()/1000000;
}

// One UCI engine process.
class Engine
{
    public:
    std::string path;
    std::vector<std::pair<std::string, std::string>> options;  // sent as setoption on every start
    pid_t pid = -1;

    bool start()
    {
        int to_child[2], from_child[2];
        if(pipe2(to_child, O_CLOEXEC)!=0 || pipe2(from_child, O_CLOEXEC)!=0)
        return false;
        pid = fork();
        if(pid==0)
        {
            dup2(to_child[0], 0);
            dup2(from_child[1], 1);
            int null = open("/dev/null", O_WRONLY);
            dup2(null, 2);
            execl(path.c_str(), path.c_str(), (char*)nullptr);
            _exit(127);
        }
        close(to_child[0]);
        close(from_child[1]);
        in = to_child[1];
        out = from_child[0];
        buffer.clear();
        advertised.clear();
        send("uci");
        if(!wait_for("uciok", 10000))
        return false;
        for(const auto& [name, value] : options)
        send("setoption name " + name + " value " + value);
        return ready();
    }

    bool ready()
    {
        send("isready");
        return wait_for("readyok", READY_TIMEOUT_MS);
    }

    void send(const std::string& line)
    {
        std::string s = line + "\n";
        if(write(in, s.data(), s.size())<0) {}  // a dead engine shows up as a read failure
    }

    // Next line before the deadline (steady ms); false on EOF or timeout.
    bool read_line(std::string& line, long long deadline_ms)
    {
        for(;;)
        {
            size_t nl = buffer.find('\n');
            if(nl!=std::string::npos)
            {
                line = buffer.substr(0, nl);
                if(!line.empty() && line.back()=='\r') line.pop_back();
                buffer.erase(0, nl+1);
                return true;
            }
            long long left = deadline_ms-now_ms();
            if(left<=0)
            return false;
            pollfd p = {out, POLLIN, 0};
            if(poll(&p, 1, (int)std::min(left, 1000000LL))<=0)
            continue;
            char chunk[4096];
            ssize_t n = read(out, chunk, sizeof chunk);
            if(n<=0)
            return false;
            buffer.append(chunk, n);
        }
    }

    bool wait_for(const std::string& token, long long timeout_ms, std::string* line_out = nullptr)
    {
        long long deadline = now_ms()+timeout_ms;
        std::string line;
        while(read_line(line, deadline))
        {
            if(line.compare(0, token.size(), token)==0)
            {
                if(line_out) *line_out = line;
                return true;
            }
            last_info = line.compare(0, 5, "info ")==0 && line.find(" score ")!=std::string::npos ? line : last_info;
            size_t type = line.find(" type ");
            if(line.compare(0, 12, "option name ")==0 && type!=std::string::npos)
            advertised.push_back(line.substr(12, type-12));
        }
        return false;
    }

    // Whether the engine named this option in its answer to "uci".
    bool has_option(const std::string& name) const
    {
        return std::find(advertised.begin(), advertised.end(), name)!=advertised.end();
    }

    // Asks the engine to quit and waits up to wait_ms before killing it.
    void stop(long long wait_ms = 1000)
    {
        if(pid<0)
        return;
        send("quit");
        close(in);
        close(out);
        for(long long i=0;i<wait_ms/10 && waitpid(pid, nullptr, WNOHANG)==0;i++)
        usleep(10000);
        if(waitpid(pid, nullptr, WNOHANG)==0)
        {
            ::kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
        }
        pid = -1;
    }

    bool restart()
    {
        stop();
        return start();
    }

    long rss_kb() const
    {
        std::ifstream f("/proc/" + std::to_string(pid) + "/status");
        std::string key;
        long value = 0;
        while(f >> key)
        if(key=="VmRSS:" && f >> value) return value;
        return 0;
    }

    std::string last_info;  // last "info ... score ..." line of the current search
    std::vector<std::string> advertised;  // option names from the answer to "uci"

    private:
    int in = -1, out = -1;
    std::string buffer;
};

#endif // TOOLS_UCI_ENGINE_HPP
