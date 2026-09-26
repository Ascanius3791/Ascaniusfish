// OWNERSHIP=Claude
// One UCI engine process, driven over pipes (tools/match.cpp, tools/gui_match.cpp).
#ifndef TOOLS_UCI_ENGINE_HPP
#define TOOLS_UCI_ENGINE_HPP
#include "../lib/search_control.hpp"  // steady_now_ns
#include <algorithm>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

constexpr long long READY_TIMEOUT_MS = 60000;

static long long now_ms()
{
    return steady_now_ns()/1000000;
}

// One UCI engine process.
class Engine
{
    public:
    std::string path;
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
        send("uci");
        return wait_for("uciok", 10000) && ready();
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
        }
        return false;
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

    private:
    int in = -1, out = -1;
    std::string buffer;
};

#endif // TOOLS_UCI_ENGINE_HPP
