// OWNERSHIP=Ascanius
#ifndef LOOKUP_TABLE_CPP
#define LOOKUP_TABLE_CPP

#include "../lib/lookup_table.hpp"
#include "../lib/tt_stats.hpp"
#include <fstream>
#include <cstdint>
#include <utility>

    template<int EXPONENT_FOR_SIZE, int BUCKET_SIZE>
    lookup_table_base<EXPONENT_FOR_SIZE, BUCKET_SIZE>::lookup_table_base()
    {
        reset();
        // full_reset();
    }

    template<int EXPONENT_FOR_SIZE, int BUCKET_SIZE>
    float lookup_table_base<EXPONENT_FOR_SIZE, BUCKET_SIZE>::value_for_victim_index(const TT_slot& entry) const
    {
#ifndef LOOKUP_TABLE_AGE_PENALTY
#define LOOKUP_TABLE_AGE_PENALTY 2.0f
#endif
        // age in searches, not plies: within one search only depth counts
        int age = entry.is_from_opening_book ? 0 : current_search_id - entry.search_id;
        return entry.pv_line.depth - LOOKUP_TABLE_AGE_PENALTY * age;
    }

    template<int EXPONENT_FOR_SIZE, int BUCKET_SIZE>
    int lookup_table_base<EXPONENT_FOR_SIZE, BUCKET_SIZE>::find_victim_index(int hash, const TT_entry& candidate)// returns -1 if the candidate is the least valuable, otherwise returns the index of the least valuable entry in the bucket
    {
        //std::vector<TT_entry>& bucket = table[hash_0][hash_1][hash_2];
        //if the bucket is (partially) unfilled return the index of the first unfilled entry
        short fill_count_for_bucket = table[hash].fill_count();
        if(fill_count_for_bucket<bucket_size)
        {
            return fill_count_for_bucket;
        }
        TT_slot* bucket = table[hash].slot;
        int victim_index=-1;
        float min_value=value_for_victim_index(TT_slot(candidate));
        for(int i=0;i<bucket_size;i++)
        {
            float value=value_for_victim_index(bucket[i]);
            if(value<=min_value)// a tie evicts too: with age-based scoring, entries from the same search at the same depth tie exactly, and rejecting the candidate outright (old strict '<') starved the table of fresh same-depth results all game
            {
                min_value=value;
                victim_index=i;
            }
        }
        return victim_index;
    }

    template<int EXPONENT_FOR_SIZE, int BUCKET_SIZE>
    TT_readout lookup_table_base<EXPONENT_FOR_SIZE, BUCKET_SIZE>::is_retrivable_eval(const BB* const original, int requestes_depth)//if possible sets eval to the value in the table, returns true if found. If the board is found, but not evaluated yet(it has to be in the current branch for that or pruned away, if it was pruned, it was an arbitrary value(and hence we cannot make further conclusions, the idead to set the eval =0 is therefore wrong!))
    {
        //number_of_repetitions needs a rework
        number_of_attemted_readouts++;
        uint64_t zobrist_hash=original->zobrist_hash;
        size_t hash = get_hash(zobrist_hash);
        TT_bucket& bucket = table[hash];
        short fill_count_for_bucket = bucket.fill_count();

        if(fill_count_for_bucket==0)
        {
            TT_STATS_HOOK(tt_stats::on_probe(zobrist_hash, false, requestes_depth));
            return TT_readout();//if the board is not found, we cannot make further conclusions
        }
        TT_readout readout;
        for(int i=0;i<fill_count_for_bucket;i++)
        {
            if(bucket.holds(i, zobrist_hash))//no full-board check backing this up anymore - a hash collision would silently misidentify the position
            {
                readout.is_found=1;
                readout.is_from_opening_book=bucket.slot[i].is_from_opening_book;

                readout.pv_line=bucket.slot[i].pv_line;
                readout.pv_line.eval=bucket.slot[i].pv_line.eval;
                readout.pv_line.depth=bucket.slot[i].pv_line.depth;
                readout.pv_line.bound_type=bucket.slot[i].pv_line.bound_type;

                number_of_succ_readouts++;
            }
        }
        TT_STATS_HOOK(tt_stats::on_probe(zobrist_hash, readout.is_found, requestes_depth));
        return readout;//if the board is not found, we cannot make further conclusions
    }

    template<int EXPONENT_FOR_SIZE, int BUCKET_SIZE>
    void lookup_table_base<EXPONENT_FOR_SIZE, BUCKET_SIZE>::insert(TT_entry new_entry)
            {
                bool is_already_in_table=0;
                number_of_inserions++;
                new_entry.search_id=current_search_id;// before find_victim_index, which values the candidate too
                size_t zobrist_hash=new_entry.zobrist_hash;
                size_t hash = get_hash(zobrist_hash);
                TT_bucket& bucket = table[hash];
                short fill_count_for_bucket = bucket.fill_count();

                //quickly check if the board can even be added to the table
                short victim_index=find_victim_index(hash,new_entry);
                //the commented out line may be very useful, but NOT good for debugging I suggest  to test it later
                // if the current entry is the victim it could only help if it has high depth and still low value- this may even be impossible
                // but im not sure. but for performance this line should be included!
                // if(victim_index==-1)
                // {
                //     return;//the new entry is the least valuable, so we do not add it to the table
                // }
                for(int i=0;i<fill_count_for_bucket;i++)
                {
                    TT_slot& old_entry = bucket.slot[i];
                    if(bucket.holds(i, new_entry.zobrist_hash))//if they are equal
                    {
                        is_already_in_table=1;
                        const int16_t mark=tt_mark(old_entry.pv_line);// the user's mark (#101) belongs to the position: a search result that replaces the entry keeps it
                        const TT_slot held=old_entry;// a marked entry takes only an improvement (tt_improves_marked(), #100)
                        if((new_entry.pv_line.depth>old_entry.pv_line.depth && (tt_proven(new_entry.pv_line) || !tt_proven(old_entry.pv_line))) || (tt_proven(new_entry.pv_line) && !tt_proven(old_entry.pv_line)))//a proof holds at every depth: it replaces an unproven entry however deep, and only a deeper proof replaces it
                        {
                            old_entry=new_entry;
                        }
                        if(new_entry.pv_line.depth==old_entry.pv_line.depth)
                        if(new_entry.pv_line.bound_type==0 && old_entry.pv_line.bound_type!=0)//if the new entry is exact, but the old one is not, we can replace it
                        {
                            old_entry=new_entry;
                        }
                        if(mark>0) old_entry = tt_improves_marked(new_entry.pv_line, held.pv_line) ? TT_slot(new_entry) : held;
                        set_tt_mark(old_entry.pv_line, std::max(mark, tt_mark(old_entry.pv_line)));
                        old_entry.search_id=current_search_id;// still needed by this search: don't let it age
                        break;
                    }
                }
                if(victim_index ==-1)
                {
                    TT_STATS_HOOK(if(!is_already_in_table) tt_stats::on_discard(new_entry, tt_stats::REJECTED));
                    return;//the new entry is the least valuable, so we do not add it to the table
                }
                if(is_already_in_table)
                {
                    return;//the new entry is already in the table, so we do not add it again
                }
                TT_STATS_HOOK(tt_stats::on_store(new_entry));
                if(fill_count_for_bucket<bucket_size)
                {
                    bucket.key[0]++;//the fill count
                }
                else
                {
                    TT_STATS_HOOK(TT_entry evicted; evicted.zobrist_hash=(bucket.key[victim_index] & key_bits) | (zobrist_hash & ~key_bits); evicted.pv_line=bucket.slot[victim_index].pv_line; tt_stats::on_discard(evicted, tt_stats::EVICTED));
                }
                bucket.key[victim_index]=(bucket.key[victim_index] & ~key_bits) | (zobrist_hash & key_bits);//keeps key[0]'s fill count
                bucket.slot[victim_index]=new_entry;
            }

    template<int EXPONENT_FOR_SIZE, int BUCKET_SIZE>
    void lookup_table_base<EXPONENT_FOR_SIZE, BUCKET_SIZE>::get_number_of_entrys()
    {
        int sum=0;
        int number_of_filled_buckets=0;
        for(int i=0;i<size;i++)
        {
            sum+=table[i].fill_count();
            if(table[i].fill_count()>0)
            number_of_filled_buckets++;
        }
        std::cout << "Number of filled buckets: " << number_of_filled_buckets << std::endl;
        std::cout << "Number of empty buckets: " << size-number_of_filled_buckets << std::endl;
        std::cout << "Number of entrys in the lookup table: " << sum << std::endl;
        std::cout << "The average number of entrys per bucket is: " << ((double)sum)/size << std::endl;
    }

    template<int EXPONENT_FOR_SIZE, int BUCKET_SIZE>
    void lookup_table_base<EXPONENT_FOR_SIZE, BUCKET_SIZE>::reset()//sets all entrys to uninitialized but does not delete the data. this saves time.
    {
        for(int i=0;i<size;i++)
        {
            for(int j=0;j<bucket_size;j++)
            {
                table[i].key[j]=0;//and with key[0] the fill count
            }
        }
        number_of_inserions=0;
        number_of_succ_readouts=0;
        number_of_attemted_readouts=0;
        current_search_id=0;
        TT_STATS_HOOK(tt_stats::on_reset());
    };

    template<int EXPONENT_FOR_SIZE, int BUCKET_SIZE>
    void lookup_table_base<EXPONENT_FOR_SIZE, BUCKET_SIZE>::full_reset()//deletes all data and resets the table to its initial state
    {
        for(int i=0;i<size;i++)
        {
            for(int j=0;j<bucket_size;j++)
            {
                table[i].key[j] = 0;
                table[i].slot[j] = TT_slot();
            }
        }
        number_of_inserions=0;
        number_of_succ_readouts=0;
        number_of_attemted_readouts=0;
        current_search_id=0;
    };

    template<int EXPONENT_FOR_SIZE, int BUCKET_SIZE>
    size_t lookup_table_base<EXPONENT_FOR_SIZE, BUCKET_SIZE>::get_hash(uint64_t zobrist_hash) const
    {
        size_t index = zobrist_hash & mask;
        return index;
    }

    template<int EXPONENT_FOR_SIZE, int BUCKET_SIZE>
    void lookup_table_base<EXPONENT_FOR_SIZE, BUCKET_SIZE>::print_readout_delta(int insertions_before, int succ_readouts_before, int attempted_readouts_before) const
    {
        int insertions_delta = number_of_inserions - insertions_before;
        int succ_delta = number_of_succ_readouts - succ_readouts_before;
        int attempted_delta = number_of_attemted_readouts - attempted_readouts_before;
        std::cout << "Lookup table insertions this move: " << insertions_delta << std::endl;
        std::cout << "Lookup table attempted readouts this move: " << attempted_delta << std::endl;
        std::cout << "Lookup table successful readouts this move: " << succ_delta << std::endl;
        if(attempted_delta > 0)
            std::cout << "Lookup table readout success ratio this move: " << ((double)succ_delta)/attempted_delta << std::endl;
        else
            std::cout << "Lookup table readout success ratio this move: N/A (no attempted readouts)" << std::endl;
    }

    template<int EXPONENT_FOR_SIZE, int BUCKET_SIZE>
    void lookup_table_base<EXPONENT_FOR_SIZE, BUCKET_SIZE>::print_depth_bound_type_histogram() const
    {
        std::map<int, std::map<int,int>> histogram;   // depth -> bound_type -> count
        for(int i=0;i<size;i++)
        {
            const TT_slot* bucket = table[i].slot;
            short fill_count_for_bucket = table[i].fill_count();
            for(int j=0;j<fill_count_for_bucket;j++)
            {
                histogram[bucket[j].pv_line.depth][bucket[j].pv_line.bound_type]++;
            }
        }
        std::cout << "Lookup table depth / bound-type histogram:" << std::endl;
        for(const auto& [depth, bound_counts] : histogram)
        {
            std::cout << "  depth " << depth << ": ";
            for(const auto& [bound_type, count] : bound_counts)
            {
                std::string label = bound_type==0 ? "exact" : bound_type==-1 ? "lower" : bound_type==1 ? "upper" : "unset";
                std::cout << label << "=" << count << " ";
            }
            std::cout << std::endl;
        }
    }

    // force instantiation of the size actually used (lookup_table's size)
    template class lookup_table_base<TT_EXPONENT_FOR_SIZE, TT_BUCKET_SIZE>;

#endif // LOOKUP_TABLE_CPP
