// OWNERSHIP=Claude
// Shell and git-worktree helpers for tools that build other git refs
// (tools/speed_compare.cpp, tools/match.cpp).
#ifndef TOOLS_GIT_BUILD_HPP
#define TOOLS_GIT_BUILD_HPP
#include <cstdio>
#include <cstdlib>
#include <string>

inline std::string run_capture(const std::string& cmd, int* status = nullptr)
{
    std::string out;
    FILE* p = popen(cmd.c_str(), "r");
    if(!p)
    return out;
    char buf[4096];
    while(fgets(buf, sizeof buf, p))
    out += buf;
    int s = pclose(p);
    if(status) *status = s;
    while(!out.empty() && (out.back()=='\n' || out.back()=='\r'))
    out.pop_back();
    return out;
}

inline int sh(const std::string& cmd)
{
    return std::system(cmd.c_str());
}

inline std::string repo_root()
{
    return run_capture("git rev-parse --show-toplevel");
}

// A git ref checked out for building. "." is the working tree itself
// (including uncommitted changes); anything else gets a detached worktree
// under /tmp.
struct Checkout
{
    std::string ref, label, dir;
    bool worktree = false;
};

// Returns an error message, or "" on success.
inline std::string checkout(Checkout& c, const std::string& root, const std::string& dir_prefix)
{
    if(c.ref==".")
    {
        c.label = ". (working tree)";
        c.dir = root;
        return "";
    }
    if(c.ref.find_first_of("'\\ \t\n")!=std::string::npos)
    return "bad ref: " + c.ref;
    int st = 0;
    std::string sha = run_capture("git -C '" + root + "' rev-parse --verify --quiet '" + c.ref + "^{commit}'", &st);
    if(st!=0 || sha.empty())
    return "unknown ref: " + c.ref;
    c.label = c.ref + " (" + sha.substr(0, 9) + ")";
    c.dir = dir_prefix + "_" + sha.substr(0, 12);
    (void)sh("git -C '" + root + "' worktree remove --force '" + c.dir + "' >/dev/null 2>&1; rm -rf '" + c.dir + "'");
    if(sh("git -C '" + root + "' worktree add --detach --quiet '" + c.dir + "' " + sha)!=0)
    return "git worktree add failed for " + c.ref;
    c.worktree = true;
    return "";
}

inline void remove_checkout(const Checkout& c, const std::string& root)
{
    if(c.worktree)
    (void)sh("git -C '" + root + "' worktree remove --force '" + c.dir + "' >/dev/null 2>&1");
}

#endif // TOOLS_GIT_BUILD_HPP
