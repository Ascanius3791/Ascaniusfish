// OWNERSHIP=Claude
// tools/nne_train: trains the eval-correction net (issue #51, see
// docs/NNE_DESIGN.md) on the dataset of tools/nne_data, with libtorch's C++ API
// (the libtorch inside the installed torch wheel; no Python).
//
//   ./tools/nne_train [data=data/nne] [out=nets/nne_d6.bin]
//                     [preds=nets/nne_d6_test.tsv] [frac=1] [seed=51]
//                     [batch=1024] [lr=0.001] [wd=1] [epochs=200] [cpu=0]
//                     [loss=log] [sfac=1] [init=<net.bin>] [train=all]
//
// The net is 780 -> 128 -> 16 -> 1 with a clipped ReLU (clamp to [0, 1]) after
// both hidden layers. Its output is the correction c in centipawns, in the
// mover's view: the dataset's scores are white's view and are negated when the
// FEN has black to move.
//
// The loss (#54) is one of
//   loss=log   log(|static + c - label| + 50 cp)                     (#51)
//   loss=wdl   |E(static + c) - E(label)|^2.5, Stockfish's loss, with
//              E(x) = (1 + σ((x - o)/s') - σ((-x - o)/s')) / 2
//              the expected score of a mover with score x; o = WDL_O and
//              s' = sfac * WDL_S, the curve tools/wdl_fit fitted to our games.
// init= starts from an exported net instead of random weights, and
// train=last then refits only its last layer. epochs=0 only scores `init`.
// nets/nne_d6.bin is two runs (#54, docs/NNE_DESIGN.md "Loss"): the log loss
// (out=nets/nne_log.bin preds=-), then loss=wdl init=nets/nne_log.bin train=last.
//
// frac < 1 trains on that share of the training set (a fixed random subset,
// for the learning curve); validation and test are always whole. Adam (AdamW
// with decoupled weight decay `wd` when wd > 0; wd=1 was best on the
// validation set among 0, 0.01, 0.1, 0.3, 1, 3), the
// learning rate halved after LR_PATIENCE epochs without a better validation
// loss, stopped after STOP_PATIENCE; the best epoch's weights are kept.
//
// At the end the test set is scored: the median |label - prediction|, the
// mean of that error clipped at 1000 cp, and the mean |E(static + c) - E(label)|
// at the fitted curve (whatever the loss), for static + c and for the baseline
// static + (mean correction of the training positions used). `out` gets the
// weights, `preds` every test position with the net's correction, so the
// engine's inference can be checked against it (#52). Either is skipped when
// given as "-".
//
// The weights file, all little-endian:
//   char[4] "NNE1", uint32 version (1), uint32 layer count (3),
//   uint32 sizes[4] (780 128 16 1),
//   then per layer: float32 weights[in][out] (each input's weights to every
//   output, so a sparse first layer adds one contiguous row per active input),
//   float32 bias[out].
// The last layer is already scaled to centipawns: the engine computes
//   h1 = clamp(b1 + sum of W1 rows of the active inputs, 0, 1)
//   h2 = clamp(b2 + h1 W2, 0, 1),   c = b3 + h2 W3
// and gets the correction in the mover's view, in cp.
#include <torch/torch.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

constexpr int N_INPUTS = 780;
constexpr int HIDDEN_1 = 128;
constexpr int HIDDEN_2 = 16;
constexpr float EPS_CP = 50.0f;      // the log loss's epsilon
constexpr double WDL_O = 219.8, WDL_S = 267.7;   // tools/wdl_fit, 400 games at 5+0.05 (#54)
constexpr double WDL_POWER = 2.5;
constexpr double WDL_GAIN = 1e4;     // the wdl loss is optimized times this, so Adam's
                                     // epsilon stays far below its tiny gradients
constexpr float OUT_SCALE = 100.0f;  // the last layer computes pawns; cp = pawns * 100
constexpr float CLIP_CP = 1000.0f;   // the clipped mean error
constexpr int LR_PATIENCE = 3;
constexpr int STOP_PATIENCE = 8;
constexpr int EVAL_BATCH = 8192;

// One split of the dataset. Features are kept as lists of active inputs and
// expanded to a dense batch per step.
struct Data
{
    std::vector<uint16_t> feat;
    std::vector<uint32_t> off{0};   // row i's features are feat[off[i] .. off[i+1])
    std::vector<float> corr;        // label - static, mover's view
    std::vector<float> stat_m;      // static, mover's view
    std::vector<std::string> game, fen;     // kept only for the test set
    std::vector<int> stat, label;           // white's view, as in the file
    size_t size() const { return corr.size(); }
};

static std::vector<std::string> split(const std::string& s, char sep)
{
    std::vector<std::string> out;
    size_t start = 0;
    for(;;)
    {
        size_t end = s.find(sep, start);
        out.push_back(s.substr(start, end - start));
        if(end == std::string::npos) return out;
        start = end + 1;
    }
}

static bool load(const std::string& path, Data& d, bool keep_positions)
{
    std::ifstream in(path);
    if(!in) { std::fprintf(stderr, "cannot read %s\n", path.c_str()); return false; }
    std::string line;
    while(std::getline(in, line))
    {
        if(line.empty() || line[0] == '#') continue;
        std::vector<std::string> col = split(line, '\t');
        if(col.size() != 8) { std::fprintf(stderr, "%s: bad line: %s\n", path.c_str(), line.c_str()); return false; }
        const size_t sp = col[3].find(' ');
        const bool white = sp != std::string::npos && col[3][sp+1] == 'w';
        const int st = std::stoi(col[4]), lb = std::stoi(col[6]);
        d.corr.push_back(float(white ? lb - st : st - lb));
        d.stat_m.push_back(float(white ? st : -st));
        for(const std::string& f : split(col[7], ','))
        {
            const int i = std::stoi(f);
            if(i < 0 || i >= N_INPUTS) { std::fprintf(stderr, "%s: input %d out of range\n", path.c_str(), i); return false; }
            d.feat.push_back(uint16_t(i));
        }
        d.off.push_back(uint32_t(d.feat.size()));
        if(keep_positions)
        {
            d.game.push_back(col[0]);
            d.fen.push_back(col[3]);
            d.stat.push_back(st);
            d.label.push_back(lb);
        }
    }
    return true;
}

struct NetImpl : torch::nn::Module
{
    torch::nn::Linear l1{nullptr}, l2{nullptr}, l3{nullptr};
    NetImpl()
    {
        l1 = register_module("l1", torch::nn::Linear(N_INPUTS, HIDDEN_1));
        l2 = register_module("l2", torch::nn::Linear(HIDDEN_1, HIDDEN_2));
        l3 = register_module("l3", torch::nn::Linear(HIDDEN_2, 1));
    }
    torch::Tensor forward(torch::Tensor x)
    {
        x = torch::clamp(l1(x), 0, 1);
        x = torch::clamp(l2(x), 0, 1);
        return l3(x).squeeze(1) * OUT_SCALE;
    }
};
TORCH_MODULE(Net);

// Rows rows[0..n) of d as a dense 0/1 batch on `dev`, and their corrections.
// Also their static evals (mover's view) in `st`.
static void make_batch(const Data& d, const uint32_t* rows, int n, torch::Device dev,
                       torch::Tensor& x, torch::Tensor& y, torch::Tensor& st)
{
    std::vector<int64_t> idx;
    std::vector<float> target(n), stat(n);
    idx.reserve(size_t(n) * 40);
    for(int b = 0; b < n; b++)
    {
        const uint32_t r = rows[b];
        for(uint32_t k = d.off[r]; k < d.off[r+1]; k++)
        idx.push_back(int64_t(b) * N_INPUTS + d.feat[k]);
        target[b] = d.corr[r];
        stat[b] = d.stat_m[r];
    }
    torch::Tensor ti = torch::from_blob(idx.data(), {int64_t(idx.size())}, torch::kInt64).to(dev);
    x = torch::zeros({n, N_INPUTS}, torch::TensorOptions().device(dev));
    x.view({-1}).index_fill_(0, ti, 1.0);
    y = torch::from_blob(target.data(), {n}, torch::kFloat32).to(dev);
    st = torch::from_blob(stat.data(), {n}, torch::kFloat32).to(dev);
}

struct Loss
{
    bool wdl = false;
    double s = WDL_S;   // the curve's scale, sfac * WDL_S

    template<class T> static T expected(const T& x, double s)
    {
        if constexpr(std::is_same_v<T, double>)
        return 0.5 * (1 + 1/(1 + std::exp(-(x - WDL_O)/s)) - 1/(1 + std::exp(-(-x - WDL_O)/s)));
        else
        return 0.5 * (1 + torch::sigmoid((x - WDL_O)/s) - torch::sigmoid((-x - WDL_O)/s));
    }
    // One position's loss; `pred` and `corr` are corrections, `st` the static eval.
    double of(double pred, double corr, double st) const
    {
        if(!wdl) return std::log(std::fabs(pred - corr) + EPS_CP);
        return std::pow(std::fabs(expected(st + pred, s) - expected(st + corr, s)), WDL_POWER);
    }
    // The batch's mean loss, times WDL_GAIN for wdl (only what is optimized).
    torch::Tensor of(const torch::Tensor& pred, const torch::Tensor& corr, const torch::Tensor& st) const
    {
        if(!wdl) return torch::log((pred - corr).abs() + EPS_CP).mean();
        return torch::pow((expected(st + pred, s) - expected(st + corr, s)).abs(), WDL_POWER).mean() * WDL_GAIN;
    }
};

// The net's corrections for every row of d, in cp.
static std::vector<float> predict(Net& net, const Data& d, torch::Device dev)
{
    torch::NoGradGuard no_grad;
    net->eval();
    std::vector<uint32_t> rows(d.size());
    std::iota(rows.begin(), rows.end(), 0u);
    std::vector<float> out;
    out.reserve(d.size());
    for(size_t s = 0; s < d.size(); s += EVAL_BATCH)
    {
        const int n = int(std::min<size_t>(EVAL_BATCH, d.size() - s));
        torch::Tensor x, y, st;
        make_batch(d, rows.data() + s, n, dev, x, y, st);
        torch::Tensor p = net->forward(x).to(torch::kCPU).contiguous();
        out.insert(out.end(), p.data_ptr<float>(), p.data_ptr<float>() + n);
    }
    net->train();
    return out;
}

// e_err: the mean |E(static + c) - E(label)| at the fitted curve.
struct Metrics { double loss, median, clipped_mean, e_err; };

static Metrics score(const Data& d, const std::vector<float>& pred, const Loss& lf)
{
    std::vector<double> err(d.size());
    double loss = 0, clipped = 0, e_err = 0;
    for(size_t i = 0; i < d.size(); i++)
    {
        err[i] = std::fabs(double(d.corr[i]) - pred[i]);
        loss += lf.of(pred[i], d.corr[i], d.stat_m[i]);
        clipped += std::min<double>(err[i], CLIP_CP);
        e_err += std::fabs(Loss::expected(double(d.stat_m[i]) + pred[i], WDL_S)
                         - Loss::expected(double(d.stat_m[i]) + d.corr[i], WDL_S));
    }
    const size_t mid = err.size() / 2;
    std::nth_element(err.begin(), err.begin() + mid, err.end());
    double median = err[mid];
    if(err.size() % 2 == 0)
    median = (median + *std::max_element(err.begin(), err.begin() + mid)) / 2;
    return {loss / d.size(), median, clipped / d.size(), e_err / d.size()};
}

static void print_metrics(const char* name, const Metrics& m)
{
    std::printf("  %-28s median %7.2f cp   clipped mean %7.2f cp   E error %.5f   loss %.6g\n",
                name, m.median, m.clipped_mean, m.e_err, m.loss);
}

static void put_u32(std::FILE* f, uint32_t v) { std::fwrite(&v, 4, 1, f); }

// W as [in][out] (torch keeps [out][in]), then the bias, both times `scale`.
static void put_layer(std::FILE* f, const torch::nn::Linear& l, float scale)
{
    torch::Tensor w = (l->weight.detach().to(torch::kCPU).t() * scale).contiguous();
    torch::Tensor b = (l->bias.detach().to(torch::kCPU) * scale).contiguous();
    std::fwrite(w.data_ptr<float>(), sizeof(float), w.numel(), f);
    std::fwrite(b.data_ptr<float>(), sizeof(float), b.numel(), f);
}

static bool export_weights(const Net& net, std::FILE* f)
{
    std::fwrite("NNE1", 1, 4, f);
    put_u32(f, 1);
    put_u32(f, 3);
    for(uint32_t s : {uint32_t(N_INPUTS), uint32_t(HIDDEN_1), uint32_t(HIDDEN_2), 1u})
    put_u32(f, s);
    put_layer(f, net->l1, 1);
    put_layer(f, net->l2, 1);
    put_layer(f, net->l3, OUT_SCALE);
    return std::fclose(f) == 0;
}

static bool get_u32(std::FILE* f, uint32_t& v) { return std::fread(&v, 4, 1, f) == 1; }

// The inverse of put_layer: [in][out] / scale into torch's [out][in].
static bool get_layer(std::FILE* f, torch::nn::Linear& l, float scale)
{
    const int64_t out = l->weight.size(0), in = l->weight.size(1);
    std::vector<float> w(in * out), b(out);
    if(std::fread(w.data(), sizeof(float), w.size(), f) != w.size()
    || std::fread(b.data(), sizeof(float), b.size(), f) != b.size()) return false;
    torch::NoGradGuard no_grad;
    l->weight.copy_(torch::from_blob(w.data(), {in, out}, torch::kFloat32).t() / scale);
    l->bias.copy_(torch::from_blob(b.data(), {out}, torch::kFloat32) / scale);
    return true;
}

// A net written by export_weights, for init=.
static bool import_weights(Net& net, const std::string& path)
{
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if(!f) { std::fprintf(stderr, "cannot read %s\n", path.c_str()); return false; }
    char magic[4];
    uint32_t version, layers, sizes[4];
    bool ok = std::fread(magic, 1, 4, f) == 4 && !std::memcmp(magic, "NNE1", 4)
           && get_u32(f, version) && version == 1 && get_u32(f, layers) && layers == 3;
    for(int i = 0; ok && i < 4; i++) ok = get_u32(f, sizes[i]);
    ok = ok && sizes[0] == N_INPUTS && sizes[1] == HIDDEN_1 && sizes[2] == HIDDEN_2 && sizes[3] == 1
       && get_layer(f, net->l1, 1) && get_layer(f, net->l2, 1) && get_layer(f, net->l3, OUT_SCALE)
       && std::fgetc(f) == EOF;
    std::fclose(f);
    if(!ok) std::fprintf(stderr, "%s is not a 780-128-16-1 NNE1 net\n", path.c_str());
    return ok;
}

static bool export_predictions(const Data& d, const std::vector<float>& pred, std::FILE* f)
{
    std::fprintf(f, "#game\tfen\tstatic\tlabel\tcorrection\n");
    std::fprintf(f, "# static and label in white's view; correction = the net's output, "
                    "cp, in the mover's view\n");
    for(size_t i = 0; i < d.size(); i++)
    std::fprintf(f, "%s\t%s\t%d\t%d\t%.4f\n", d.game[i].c_str(), d.fen[i].c_str(),
                 d.stat[i], d.label[i], pred[i]);
    return std::fclose(f) == 0;
}

int main(int argc, char** argv)
{
    std::string data = "data/nne", out = "nets/nne_d6.bin", preds = "nets/nne_d6_test.tsv", init;
    double frac = 1, lr = 1e-3, wd = 1, sfac = 1;
    int batch = 1024, max_epochs = 200, seed = 51;
    bool cpu = false, last_only = false;
    Loss lf;
    for(int i = 1; i < argc; i++)
    {
        const std::string a = argv[i];
        const size_t eq = a.find('=');
        const std::string k = a.substr(0, eq), v = eq == std::string::npos ? "" : a.substr(eq + 1);
        if(k == "data") data = v;
        else if(k == "out") out = v;
        else if(k == "preds") preds = v;
        else if(k == "frac") frac = std::stod(v);
        else if(k == "lr") lr = std::stod(v);
        else if(k == "wd") wd = std::stod(v);
        else if(k == "batch") batch = std::stoi(v);
        else if(k == "epochs") max_epochs = std::stoi(v);
        else if(k == "seed") seed = std::stoi(v);
        else if(k == "cpu") cpu = v != "0";
        else if(k == "loss" && (v == "log" || v == "wdl")) lf.wdl = v == "wdl";
        else if(k == "sfac") sfac = std::stod(v);
        else if(k == "init") init = v;
        else if(k == "train" && (v == "all" || v == "last")) last_only = v == "last";
        else { std::fprintf(stderr, "unknown argument %s\n", a.c_str()); return 1; }
    }
    lf.s = sfac * WDL_S;
    if(last_only && init.empty()) { std::fprintf(stderr, "train=last needs init=\n"); return 1; }

    torch::manual_seed(seed);
    torch::set_num_threads(2);
    const bool cuda = !cpu && torch::cuda::is_available();
    const torch::Device dev = cuda ? torch::Device(torch::kCUDA) : torch::Device(torch::kCPU);

    // Opened now, so a path that cannot be written fails before the training.
    std::FILE* out_file = nullptr;
    std::FILE* preds_file = nullptr;
    for(auto [path, file, mode] : {std::tuple{&out, &out_file, "wb"}, std::tuple{&preds, &preds_file, "w"}})
    if(*path != "-" && !(*file = std::fopen(path->c_str(), mode)))
    {
        std::fprintf(stderr, "cannot write %s\n", path->c_str());
        return 1;
    }

    Data train, valid, test;
    if(!load(data + "/train.tsv", train, false) || !load(data + "/valid.tsv", valid, false)
    || !load(data + "/test.tsv", test, true))
    return 1;

    std::mt19937_64 rng(seed);
    std::vector<uint32_t> order(train.size());
    std::iota(order.begin(), order.end(), 0u);
    std::shuffle(order.begin(), order.end(), rng);
    order.resize(size_t(std::llround(frac * order.size())));
    double mean_corr = 0;
    for(uint32_t r : order) mean_corr += train.corr[r];
    mean_corr /= order.size();

    std::printf("nne_train: %zu of %zu training positions (frac %.2f), valid %zu, test %zu, on %s\n",
                order.size(), train.size(), frac, valid.size(), test.size(), cuda ? "CUDA" : "CPU");
    std::printf("  batch %d, lr %g, weight decay %g, seed %d, mean training correction %+.2f cp\n",
                batch, lr, wd, seed, mean_corr);
    std::fflush(stdout);

    Net net;
    if(!init.empty() && !import_weights(net, init)) return 1;
    net->to(dev);
    std::vector<torch::Tensor> trained = net->parameters();
    if(last_only)
    {
        for(torch::Tensor& p : trained) p.set_requires_grad(false);
        trained = net->l3->parameters();
        for(torch::Tensor& p : trained) p.set_requires_grad(true);
    }
    std::printf("  loss %s", lf.wdl ? "wdl" : "log");
    if(lf.wdl) std::printf(" (o %g, s %g = %g x %g)", WDL_O, lf.s, sfac, WDL_S);
    std::printf(", %s%s\n", init.empty() ? "random weights" : ("from " + init).c_str(), last_only ? ", last layer only" : "");
    std::fflush(stdout);
    std::unique_ptr<torch::optim::Optimizer> opt;
    if(wd > 0) opt = std::make_unique<torch::optim::AdamW>(trained, torch::optim::AdamWOptions(lr).weight_decay(wd));
    else       opt = std::make_unique<torch::optim::Adam>(trained, torch::optim::AdamOptions(lr));

    // Epoch 0 is the starting point, so a net given by init= is kept unless training beats it.
    std::vector<torch::Tensor> best;
    double best_loss = score(valid, predict(net, valid, dev), lf).loss;
    for(const torch::Tensor& p : net->parameters()) best.push_back(p.detach().clone());
    int best_epoch = 0, bad = 0;
    const double gain = lf.wdl ? WDL_GAIN : 1;
    const auto t0 = std::chrono::steady_clock::now();
    for(int epoch = 1; epoch <= max_epochs; epoch++)
    {
        std::shuffle(order.begin(), order.end(), rng);
        double sum = 0;
        int steps = 0;
        for(size_t s = 0; s + batch <= order.size(); s += batch)
        {
            torch::Tensor x, y, st;
            make_batch(train, order.data() + s, batch, dev, x, y, st);
            opt->zero_grad();
            torch::Tensor loss = lf.of(net->forward(x), y, st);
            loss.backward();
            opt->step();
            sum += loss.item<double>() / gain;
            steps++;
        }
        const Metrics v = score(valid, predict(net, valid, dev), lf);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const bool better = v.loss < best_loss;
        std::printf("epoch %3d  train loss %.6g  valid loss %.6g  median %6.2f  clipped %6.2f  E err %.5f  lr %.2g  %5.0fs%s\n",
                    epoch, sum / steps, v.loss, v.median, v.clipped_mean, v.e_err, lr, secs, better ? "  *" : "");
        std::fflush(stdout);
        if(better)
        {
            best_loss = v.loss;
            best_epoch = epoch;
            bad = 0;
            best.clear();
            for(const torch::Tensor& p : net->parameters()) best.push_back(p.detach().clone());
            continue;
        }
        if(++bad >= STOP_PATIENCE) break;
        if(bad % LR_PATIENCE == 0)
        {
            lr /= 2;
            for(auto& g : opt->param_groups())
            g.options().set_lr(lr);
        }
    }
    {
        torch::NoGradGuard no_grad;
        std::vector<torch::Tensor> params = net->parameters();
        for(size_t i = 0; i < params.size(); i++) params[i].copy_(best[i]);
    }

    const std::vector<float> pred = predict(net, test, dev);
    std::printf("test set (%zu positions), best epoch %d:\n", test.size(), best_epoch);
    print_metrics("static + net", score(test, pred, lf));
    print_metrics("static + mean correction", score(test, std::vector<float>(test.size(), float(mean_corr)), lf));
    print_metrics("static", score(test, std::vector<float>(test.size(), 0.0f), lf));

    if(out_file)
    {
        if(!export_weights(net, out_file)) { std::fprintf(stderr, "cannot write %s\n", out.c_str()); return 1; }
        std::printf("weights: %s\n", out.c_str());
    }
    if(preds_file)
    {
        if(!export_predictions(test, pred, preds_file)) { std::fprintf(stderr, "cannot write %s\n", preds.c_str()); return 1; }
        std::printf("test predictions: %s\n", preds.c_str());
    }
    return 0;
}
