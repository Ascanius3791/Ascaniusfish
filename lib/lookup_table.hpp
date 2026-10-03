// OWNERSHIP=Ascanius
#ifndef LOOKUP_TABLE_HPP
#define LOOKUP_TABLE_HPP
#include<vector>
#include<string>
#include"../src/Bitboards.cpp"
#include"../src/Settings.cpp"
#include"../src/move_generation.cpp"
#include"../src/zobrist.cpp"
#include"tt_result.hpp"

#include<climits>
#include<map>


struct TT_entry
{
    uint64_t zobrist_hash;
    TT_Result pv_line;
    bool initialized=0;
    bool is_from_opening_book=0;
    int search_id=0;// which search (lookup_table_base::new_search) last wrote or refreshed this entry
};

// A TT_entry as the table keeps it: everything but the key, which its bucket
// keeps apart with the other keys (#81).
struct TT_slot
{
    TT_Result pv_line;
    bool is_from_opening_book=0;
    int search_id=0;
    TT_slot() = default;
    TT_slot(const TT_entry& entry) : pv_line(entry.pv_line), is_from_opening_book(entry.is_from_opening_book), search_id(entry.search_id) {}
};

struct TT_readout
{
    PV_Line pv_line;
    bool is_found=0;
    bool is_from_opening_book=0;
};

// EXPONENT_FOR_SIZE/BUCKET_SIZE are template parameters (not runtime ones) so that
// table stays a plain fixed-size C array for every table size we need,
// instead of a runtime-sized/heap-backed container.
template<int EXPONENT_FOR_SIZE, int BUCKET_SIZE>
class lookup_table_base
{
    // def zobrist hash is the full hash
    // def hash is the first exponent_for_size bits of the zobrist hash
    protected:
    static const int bucket_size=BUCKET_SIZE;
    static const int exponent_for_size = EXPONENT_FOR_SIZE;
    static const int size = 1 << exponent_for_size;
    static const uint64_t mask = -1ULL >> (64 - exponent_for_size);
    // A bucket's keys share one cache line, ahead of its entries (#81): a miss
    // reads only that line, a hit one more. The low byte of key[0] is the fill
    // count; those bits are the bucket index, the same for every key in it.
    static_assert(EXPONENT_FOR_SIZE>=8, "the fill count lives in the key bits the bucket index fixes");
    static constexpr uint64_t key_bits = ~0xFFULL;
    struct alignas(64) TT_bucket
    {
        uint64_t key[BUCKET_SIZE];
        TT_slot slot[BUCKET_SIZE];
        short fill_count() const { return key[0] & 0xFF; }
        bool holds(int i, uint64_t zobrist_hash) const { return ((key[i]^zobrist_hash) & key_bits)==0; }
    };
    TT_bucket table[size];

    size_t get_hash(uint64_t zobrist_hash) const;
    public:
    lookup_table_base();
    virtual ~lookup_table_base() = default;
    int number_of_inserions=0, number_of_succ_readouts=0, number_of_attemted_readouts=0;
    int current_search_id=0;
    void new_search() { current_search_id++; }// call once per root search ("go"), entries of older searches then age
    //if possible sets eval to the value in the table, returns true if found. If the board is found, but not evaluated yet(it has to be in the current branch for that or pruned away, if it was pruned, it was an arbitrary value(and hence we cannot make further conclusions, the idead to set the eval =0 is therefore wrong!))
    TT_readout is_retrivable_eval(const BB* const original, int requestes_depth);

    protected:
    virtual float value_for_victim_index(const TT_slot& entry) const;
    int find_victim_index(int hash,const TT_entry& candidate);// returns -1 if the candidate is the lease valuable, otherwise returns the index of the least valuable entry in the bucket
    public:
    void insert(TT_entry original);

    void get_number_of_entrys();

    void reset();
    void full_reset();//deletes all data and resets the table to its initial state

    void print_readout_delta(int insertions_before, int succ_readouts_before, int attempted_readouts_before) const;//prints insertions/attempted/successful readouts and hit ratio since the given baseline
    void print_depth_bound_type_histogram() const;//scans the whole table and prints, per depth that appears, how many entrys have each bound type
    };

// The regular per-game transposition table - same name, same size as before.
// Actual size lives in lib/Settings.hpp (TT_EXPONENT_FOR_SIZE/TT_BUCKET_SIZE).
using lookup_table = lookup_table_base<TT_EXPONENT_FOR_SIZE, TT_BUCKET_SIZE>;

#endif // LOOKUP_TABLE_HPP
