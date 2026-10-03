// OWNERSHIP=Ascanius
#ifndef LOOKUP_TABLE_HPP
#define LOOKUP_TABLE_HPP
#include<vector>
#include<string>
#include"../src/Bitboards.cpp"
#include"../src/Settings.cpp"
#include"../src/move_generation.cpp"
#include"../src/zobrist.cpp"

#include<climits>
#include<map>


struct TT_entry
{
    bool initialized=0;
    uint64_t zobrist_hash;
    PV_Line pv_line;
    bool is_from_opening_book=0;
    int search_id=0;// which search (lookup_table_base::new_search) last wrote or refreshed this entry
};

struct TT_readout
{
    PV_Line pv_line;
    bool is_found=0;
    bool is_from_opening_book=0;
};

// EXPONENT_FOR_SIZE/BUCKET_SIZE are template parameters (not runtime ones) so that
// table/fill_count stay plain fixed-size C arrays for every table size we need,
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
    TT_entry table[size][bucket_size];
    short fill_count[size];

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
    virtual float value_for_victim_index(const TT_entry& entry) const;
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
