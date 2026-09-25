// OWNERSHIP=Claude
// make speed-compare A=<ref> B=<ref>: is B faster or slower than A?
//
//   ./tools/speed_compare <refA> <refB> [rounds=10] [depth=bench default]
//
// A ref is anything git understands (main, HEAD~3, a hash); "." is the
// current working tree including uncommitted changes. Each ref is checked out
// in a git worktree under /tmp (the working tree is left untouched) and built
// with *this* tree's tools/bench.cpp, so both sides run the same workload.
// Runs alternate AB, BA, AB, ... to cancel load/thermal drift; the verdict
// uses the paired per-round nps ratio B/A with a 95% t-interval.
#include "git_build.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static const char* const CXXFLAGS = "-O3 -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG -pthread";

static void die(const std::string& msg)
{
    std::fprintf(stderr, "speed_compare: %s\n", msg.c_str());
    std::exit(2);
}

// 95% two-sided Student t critical values, df = 1..30.
static double t95(int df)
{
    static const double T[] = {12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228,
                               2.201, 2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093, 2.086,
                               2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045, 2.042};
    return df>=1 && df<=30 ? T[df-1] : 1.960;
}

struct Mean_CI { double mean, half; };

static Mean_CI mean_ci(const std::vector<double>& x)
{
    double n = x.size(), m = 0, v = 0;
    for(double a : x) m += a;
    m /= n;
    for(double a : x) v += (a-m)*(a-m);
    double sd = n>1 ? std::sqrt(v/(n-1)) : 0;
    return {m, t95((int)n-1)*sd/std::sqrt(n)};
}

struct Side : Checkout
{
    std::string binary;
    long long nodes = -1;
    std::vector<double> nps;
};

static void prepare(Side& s, const std::string& root, const std::string& tag)
{
    std::string error = checkout(s, root, "/tmp/ascaniusfish_speed_" + tag);
    if(!error.empty())
    die(error);
    if(s.worktree && sh("mkdir -p '" + s.dir + "/tools' && cp '" + root + "/tools/bench.cpp' '" + s.dir + "/tools/bench.cpp'")!=0)
    die("could not copy tools/bench.cpp into " + s.dir);
    s.binary = "/tmp/ascaniusfish_speed_bench_" + tag;
    std::printf("building %s ...\n", s.label.c_str());
    std::fflush(stdout);
    std::string cmd = std::string("cd '") + s.dir + "' && g++ " + CXXFLAGS + " -o '" + s.binary + "' tools/bench.cpp 2>&1 | grep -E 'error' | head -5";
    std::string errors = run_capture(cmd);
    if(!errors.empty() || sh("test -x '" + s.binary + "'")!=0)
    die("build failed for " + s.label + " (refs older than the UCI front end can't build tools/bench.cpp):\n" + errors);
}

static void run_once(Side& s, const std::string& depth_arg)
{
    std::string out = run_capture("'" + s.binary + "' " + depth_arg + " -q");
    long long nodes = 0, ms = 0, nps = 0;
    size_t at = out.rfind("bench:");
    if(at==std::string::npos || std::sscanf(out.c_str()+at, "bench: nodes %lld time_ms %lld nps %lld", &nodes, &ms, &nps)!=3)
    die("unexpected bench output from " + s.label + ":\n" + out);
    if(s.nodes>=0 && s.nodes!=nodes)
    std::printf("  warning: %s gave %lld nodes, earlier %lld (search not deterministic?)\n", s.label.c_str(), nodes, s.nodes);
    s.nodes = nodes;
    s.nps.push_back((double)nps);
}

static void cleanup(Side& s, const std::string& root)
{
    remove_checkout(s, root);
    std::remove(s.binary.c_str());
}

int main(int argc, char** argv)
{
    if(argc<3)
    {
        std::fprintf(stderr, "usage: %s <refA> <refB> [rounds=10] [depth]\n  ref \".\" = working tree incl. uncommitted changes\n", argv[0]);
        return 2;
    }
    int rounds = argc>3 ? std::atoi(argv[3]) : 10;
    std::string depth_arg = argc>4 ? std::to_string(std::atoi(argv[4])) : "";
    if(rounds<2)
    die("need at least 2 rounds");

    std::string root = repo_root();
    if(root.empty())
    die("not inside the git repository");

    Side a, b;
    a.ref = argv[1];
    b.ref = argv[2];
    prepare(a, root, "A");
    prepare(b, root, "B");

    std::printf("%d rounds, interleaved\n", rounds);
    for(int r=0; r<rounds; r++)
    {
        if(r%2==0) { run_once(a, depth_arg); run_once(b, depth_arg); }
        else       { run_once(b, depth_arg); run_once(a, depth_arg); }
        std::printf("  round %2d: A %8.0f nps   B %8.0f nps\n", r+1, a.nps.back(), b.nps.back());
        std::fflush(stdout);
    }

    std::vector<double> ratio;
    for(int r=0; r<rounds; r++)
    ratio.push_back(b.nps[r]/a.nps[r]);
    Mean_CI ma = mean_ci(a.nps), mb = mean_ci(b.nps), mr = mean_ci(ratio);

    std::printf("\nA  %-40s nps %8.0f ± %6.0f   nodes %lld\n", a.label.c_str(), ma.mean, ma.half, a.nodes);
    std::printf("B  %-40s nps %8.0f ± %6.0f   nodes %lld\n", b.label.c_str(), mb.mean, mb.half, b.nodes);
    double pct = (mr.mean-1)*100, half = mr.half*100;
    std::printf("B vs A: %+.2f%% ± %.2f%% nps (95%% CI, paired) -> %s\n", pct, half,
                std::fabs(pct)<=half ? "no significant difference"
                                     : pct>0 ? "B is significantly faster" : "B is significantly slower");
    if(a.nodes!=b.nodes)
    std::printf("note: node signatures differ, so the search itself changed; nps compares cost per node, not time to depth.\n");

    cleanup(a, root);
    cleanup(b, root);
    return 0;
}
