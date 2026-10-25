// OWNERSHIP=Claude
// #114's net shapes, before any is trained:
// - the named features of lib/nne.hpp against basic_eval()'s own definitions
//   (passed, isolated, backward pawns, outposts, rooks on open files, hanging
//   pieces, the unstoppable passer), and a position against its mirror;
// - random nets of the new shapes (3072 inputs; the middle 128 layer): the
//   engine's integer inference against a float forward pass in double, the
//   updated first layer against one from scratch, AVX2 against the generic
//   code, and the time of one correction.
//   make nne-named-test    (or: ./diagnostics/nne_named_test [tsv with FENs in column 2])
#include "../lib/uci.hpp"
#include "../lib/nne.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool ok, const std::string& what)
{
    std::printf("  %-70s %s\n", what.c_str(), ok ? "ok" : "FAIL");
    failures += !ok;
}

static uint64_t mover_view(const BB& pos, uint64_t b) { return pos.white_move ? b : __builtin_bswap64(b); }

static uint64_t neighbour_files(int column)
{
    uint64_t n = 0;
    if(column>0) n |= mask_column[column-1];
    if(column<7) n |= mask_column[column+1];
    return n;
}

// basic_eval()'s definitions, square by square, in white's view for `white`'s pieces.
struct Reference { uint64_t passed, isolated, backward, knight_outpost, bishop_outpost, rook_open, rook_half_open, hanging; bool unstoppable; };

static Reference reference(const BB& pos, bool white)
{
    const uint64_t* B = pos.Board;
    const int own = 6*!white, enemy = 6*white;
    const uint64_t pawns = B[own], enemy_pawns = B[enemy];
    Reference r = {};
    r.passed = passed_pawns_of_colour(&pos, white);
    // pawn_struckture_eval_of_colour(): isolated
    for(uint64_t p = pawns; p; p &= p-1)
    {
        const int i = __builtin_ctzll(p);
        if(!(pawns & neighbour_files(i%8))) r.isolated |= 1ULL<<i;
    }
    // pawn_shape_eval(): backward
    const uint64_t enemy_attacks = pawn_attacks(enemy_pawns, !white);
    for(uint64_t p = pawns; p; p &= p-1)
    {
        const int i = __builtin_ctzll(p), rank = i/8;
        const uint64_t level_or_behind = white ? (~0ULL >> (8*(7-rank))) : (~0ULL << (8*rank));
        const int stop = white ? i+8 : i-8;
        const uint64_t n = neighbour_files(i%8);
        if((pawns & n) && !(pawns & n & level_or_behind) && (enemy_attacks >> stop & 1)) r.backward |= 1ULL<<i;
    }
    // placement_eval(): outposts, rooks on open and half-open files
    uint64_t minors = (B[2+own] | B[3+own]) & pawn_attacks(pawns, white) & (white ? 0x0000FFFFFF000000ULL : 0x000000FFFFFF0000ULL);
    for(; minors; minors &= minors-1)
    {
        const int i = __builtin_ctzll(minors), rank = i/8;
        const uint64_t ahead = white ? (~0ULL << (8*(rank+1))) : ((1ULL << (8*rank))-1);
        if(enemy_pawns & neighbour_files(i%8) & ahead) continue;
        (B[2+own] >> i & 1 ? r.knight_outpost : r.bishop_outpost) |= 1ULL<<i;
    }
    for(uint64_t rooks = B[1+own]; rooks; rooks &= rooks-1)
    {
        const int i = __builtin_ctzll(rooks);
        const uint64_t file = mask_column[i%8];
        if(!(file & (pawns|enemy_pawns))) r.rook_open |= 1ULL<<i;
        else if(!(file & pawns)) r.rook_half_open |= 1ULL<<i;
    }
    // threat_eval(): attacked and not defended
    const uint64_t occupancy = pos.get_occupancy();
    uint64_t attacks[2];
    for(int w=0;w<2;w++)
    {
        const int o = 6*!w;
        uint64_t a = pawn_attacks(B[o], w), bb;
        for(bb = B[2+o]; bb; bb &= bb-1) a |= Kn_template[__builtin_ctzll(bb)];
        for(bb = B[3+o]|B[4+o]; bb; bb &= bb-1) a |= get_bishop_attacks(__builtin_ctzll(bb), occupancy);
        for(bb = B[1+o]|B[4+o]; bb; bb &= bb-1) a |= get_rook_attacks(__builtin_ctzll(bb), occupancy);
        if(B[5+o]) a |= K_template[__builtin_ctzll(B[5+o])];
        attacks[w] = a;
    }
    r.hanging = (B[own]|B[1+own]|B[2+own]|B[3+own]|B[4+own]) & attacks[!white] & ~attacks[white];
    // passed_pawn_parts(): the square rule
    for(uint64_t p = r.passed; p; p &= p-1)
    {
        int part[PASSER_PARTS];
        passed_pawn_parts(&pos, white, __builtin_ctzll(p), game_phase(&pos), WEIGHTS_OG, part);
        r.unstoppable |= part[PASSER_SQUARE] != 0;
    }
    return r;
}

// The same position seen from the other side: ranks flipped, colours and the side to move swapped.
static BB mirror(const BB& pos)
{
    BB m = pos;
    for(int i=0;i<12;i++) m.Board[i] = __builtin_bswap64(pos.Board[(i+6)%12]);
    m.white_move = !pos.white_move;
    for(int side=0;side<2;side++) { m.castle[0][side] = pos.castle[1][side]; m.castle[1][side] = pos.castle[0][side]; }
    m.en_passant = __builtin_bswap64(pos.en_passant);
    return m;
}

// A random net of the given shape, written in the trainer's format.
static void write_net(const std::string& path, int n_inputs, bool middle, std::mt19937& rng, std::vector<std::vector<float>>& layers)
{
    std::normal_distribution<float> g(0, 1);
    const int sizes[5] = {n_inputs, nne::HIDDEN_1, middle ? nne::HIDDEN_1 : nne::HIDDEN_2, middle ? nne::HIDDEN_2 : 1, 1};
    const int n_layers = middle ? 4 : 3;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    const uint32_t header[3] = {0, 2, uint32_t(n_layers)};
    std::fwrite("NNE1", 1, 4, f);
    std::fwrite(header + 1, 4, 2, f);
    for(int i=0;i<=n_layers;i++) { uint32_t s = sizes[i]; std::fwrite(&s, 4, 1, f); }
    layers.clear();
    for(int l=0;l<n_layers;l++)
    {
        const int in = sizes[l], out = sizes[l+1];
        // scaled so that about half the units are inside (0, 1); the last layer in cp
        const float spread = l==0 ? 0.08f : l==n_layers-1 ? 60.0f : 2.0f/std::sqrt(float(in));
        std::vector<float> w(size_t(in)*out + out);
        for(float& x : w) x = g(rng)*spread;
        for(int k=0;k<out && l<n_layers-1;k++) w[size_t(in)*out + k] += 0.3f;
        std::fwrite(w.data(), 4, w.size(), f);
        layers.push_back(w);
    }
    std::fclose(f);
}

static double forward(const std::vector<std::vector<float>>& layers, const std::vector<int>& active)
{
    std::vector<double> h(nne::HIDDEN_1);
    const std::vector<float>& l1 = layers[0];
    const size_t n_in = (l1.size() - nne::HIDDEN_1)/nne::HIDDEN_1;
    for(int j=0;j<nne::HIDDEN_1;j++)
    {
        double s = l1[n_in*nne::HIDDEN_1 + j];
        for(int i : active) s += l1[size_t(i)*nne::HIDDEN_1 + j];
        h[j] = std::min(std::max(s, 0.0), 1.0);
    }
    for(size_t l=1;l<layers.size();l++)
    {
        const std::vector<float>& w = layers[l];
        const size_t in = h.size(), out = w.size()/(in+1);
        std::vector<double> next(out);
        for(size_t k=0;k<out;k++)
        {
            double s = w[in*out + k];
            for(size_t j=0;j<in;j++) s += h[j]*w[j*out + k];
            next[k] = l+1 < layers.size() ? std::min(std::max(s, 0.0), 1.0) : s;
        }
        h = next;
    }
    return h[0];
}

// The loaded net's time per correction, the children of each position in a
// row as at the leaves of a search; the children are made before the clock runs.
static double time_siblings(const std::vector<BB>& positions)
{
    std::vector<BB> kids;
    for(size_t p=0;p<positions.size() && p<3000;p++)
    {
        BB k[256];
        const int n = std::get<0>(all_moves(&positions[p], k));
        kids.insert(kids.end(), k, k+n);
    }
    volatile float sink = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for(const BB& k : kids) sink = sink + nne::correction(k);
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count()/kids.size();
}

int main(int argc, char** argv)
{
    const std::string tsv_path = argc > 1 ? argv[1] : "nets/nne_d6_test.tsv";
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    std::vector<BB> positions;
    std::ifstream in(tsv_path);
    for(std::string line; std::getline(in, line);)
    {
        if(line.empty() || line[0]=='#') continue;
        std::vector<std::string> col;
        std::stringstream s(line);
        for(std::string c; std::getline(s, c, '\t');) col.push_back(c);
        BB pos;
        if(col.size() > 1 && uci_parse_fen(col[1], pos)) positions.push_back(pos);
    }
    if(positions.empty()) { std::printf("no positions in %s\n", tsv_path.c_str()); return 1; }
    std::printf("%zu positions from %s\n", positions.size(), tsv_path.c_str());

    using namespace nne;
    const char* names[] = {"passed", "isolated", "backward", "knight outpost", "bishop outpost", "rook on open file", "rook on half-open file", "hanging", "unstoppable passer"};
    long long differ[9] = {}, mirror_differ = 0, set[9] = {};
    for(const BB& pos : positions)
    {
        uint64_t w[FEATURE_WORDS], m[FEATURE_WORDS];
        feature_set(pos, w, FEATURE_WORDS);
        BB mpos = mirror(pos);
        feature_set(mpos, m, FEATURE_WORDS);
        for(int i=0;i<FEATURE_WORDS;i++) mirror_differ += w[i] != m[i];
        for(int s=0;s<2;s++)
        {
            const Reference r = reference(pos, s ? !pos.white_move : pos.white_move);
            const uint64_t got[8] = {w[named_word(PASSED, s)], w[named_word(ISOLATED, s)], w[named_word(BACKWARD, s)],
                                     w[named_word(KNIGHT_OUTPOST, s)], w[named_word(BISHOP_OUTPOST, s)], w[named_word(ROOK_OPEN, s)],
                                     w[named_word(ROOK_HALF_OPEN, s)], w[named_word(HANGING, s)]};
            const uint64_t want[8] = {r.passed, r.isolated, r.backward, r.knight_outpost, r.bishop_outpost, r.rook_open, r.rook_half_open, r.hanging};
            for(int k=0;k<8;k++) { differ[k] += got[k] != mover_view(pos, want[k]); set[k] += want[k] != 0; }
            differ[8] += bool(w[GLOBAL_WORD] >> (OWN_UNSTOPPABLE + s) & 1) != r.unstoppable;
            set[8] += r.unstoppable;
        }
    }
    for(int k=0;k<9;k++)
    check(differ[k]==0, std::string(names[k]) + " as basic_eval() (" + std::to_string(differ[k]) + " sides differ, "
                        + std::to_string(set[k]) + " have one)");
    check(mirror_differ==0, "a position and its mirror give the same words (" + std::to_string(mirror_differ) + " differ)");

    std::mt19937 rng(114);
    for(int shape=0;shape<2;shape++)
    {
        const int n_inputs = shape==0 ? N_INPUTS_NAMED : N_INPUTS_PIECES;
        const bool middle = shape==1;
        const std::string name = shape==0 ? "3072-128-16-1" : "780-128-128-16-1";
        const std::string path = "diagnostics/nne_named_test_net.bin";  // removed once loaded
        std::vector<std::vector<float>> layers;
        write_net(path, n_inputs, middle, rng, layers);
        std::string error;
        if(!load(path, error)) { check(false, name + ": loads (" + error + ")"); continue; }
        std::remove(path.c_str());
        double worst = 0, spread = 0;
        long long incremental = 0, paths = 0, children = 0;
        const bool avx2 = use_avx2;
        for(size_t p=0;p<positions.size();p++)
        {
            const BB& pos = positions[p];
            int active[N_INPUTS];
            const int n = active_features(pos, active, n_words);
            const double want = forward(layers, std::vector<int>(active, active+n));
            const double got = correction_from_scratch(pos);
            worst = std::max(worst, std::fabs(got - want));
            spread += std::fabs(want);
            if(p % 10) continue;
            incremental += correction(pos) != got;
            use_avx2 = false;
            paths += correction_from_scratch(pos) != got;
            use_avx2 = avx2;
            BB kids[256];
            const int k = std::get<0>(all_moves(&pos, kids));
            for(int c=0;c<k;c++) { incremental += correction(kids[c]) != correction_from_scratch(kids[c]); children++; }
        }
        char buf[200];
        std::snprintf(buf, sizeof buf, "%s: engine within 1 cp of a float pass (largest %.3f, mean |c| %.0f)", name.c_str(), worst, spread/positions.size());
        check(worst < 1.0, buf);
        check(incremental==0, name + ": updated first layer == from scratch (" + std::to_string(children) + " children)");
        check(paths==0, name + ": AVX2 == generic code");

        std::printf("  %s: one correction, siblings in a row: %.0f ns\n", name.c_str(), time_siblings(positions));
    }
    std::string error;
    if(load("nets/nne_d6.bin", error))
    std::printf("  780-128-16-1 (nets/nne_d6.bin): one correction, siblings in a row: %.0f ns\n", time_siblings(positions));

    // the named features alone
    {
        volatile uint64_t sink = 0;
        uint64_t w[FEATURE_WORDS];
        const auto t0 = std::chrono::steady_clock::now();
        for(int rep=0;rep<4;rep++)
        for(const BB& pos : positions) { feature_set(pos, w, FEATURE_WORDS); sink = sink + w[GLOBAL_WORD]; }
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count()/(4.0*positions.size());
        std::printf("  feature_set() with the named features: %.0f ns\n", ns);
    }
    std::printf(failures ? "%d FAILED\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
