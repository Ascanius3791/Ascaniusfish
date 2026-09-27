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
    float lookup_table_base<EXPONENT_FOR_SIZE, BUCKET_SIZE>::value_for_victim_index(const TT_entry& entry) const
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
        short fill_count_for_bucket = fill_count[hash];
        if(fill_count_for_bucket<bucket_size)
        {
            return fill_count_for_bucket;
        }
        TT_entry* bucket = table[hash];
        if(bucket[0].initialized==0)
        {
            std::cout << "Error: trying to find victim index in an empty bucket!" << std::endl;
            exit(1);
        }
        int victim_index=-1;
        float min_value=value_for_victim_index(candidate);
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
        TT_entry* bucket = table[hash];
        short fill_count_for_bucket = fill_count[hash];

        if(fill_count_for_bucket==0)
        {
            TT_STATS_HOOK(tt_stats::on_probe(zobrist_hash, false, requestes_depth));
            return TT_readout();//if the board is not found, we cannot make further conclusions
        }
        TT_readout readout;
        for(int i=0;i<fill_count_for_bucket;i++)
        {
            if(bucket[i].zobrist_hash==zobrist_hash)//no full-board check backing this up anymore - a hash collision would silently misidentify the position
            {
                readout.is_found=1;
                readout.is_from_opening_book=bucket[i].is_from_opening_book;

                readout.pv_line=bucket[i].pv_line;
                readout.pv_line.eval=bucket[i].pv_line.eval;
                readout.pv_line.depth=bucket[i].pv_line.depth;
                readout.pv_line.bound_type=bucket[i].pv_line.bound_type;

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
                short fill_count_for_bucket = fill_count[hash];
                TT_entry* bucket = table[hash];

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
                    TT_entry& old_entry = bucket[i];
                    if(old_entry.zobrist_hash==new_entry.zobrist_hash)//if they are equal
                    {
                        is_already_in_table=1;
                        if(new_entry.pv_line.depth>old_entry.pv_line.depth)
                        {
                            old_entry=new_entry;
                        }
                        if(new_entry.pv_line.depth==old_entry.pv_line.depth)
                        if(new_entry.pv_line.bound_type==0 && old_entry.pv_line.bound_type!=0)//if the new entry is exact, but the old one is not, we can replace it
                        {
                            old_entry=new_entry;
                        }
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
                    fill_count[hash]++;
                }
                else
                {
                    TT_STATS_HOOK(tt_stats::on_discard(bucket[victim_index], tt_stats::EVICTED));
                }
                //moved, not copied: new_entry is dead after this, and a copy here
                //would clone the whole extension chain a second time (the first
                //clone is the by-value parameter itself).
                bucket[victim_index]=std::move(new_entry);
            }

    template<int EXPONENT_FOR_SIZE, int BUCKET_SIZE>
    void lookup_table_base<EXPONENT_FOR_SIZE, BUCKET_SIZE>::get_number_of_entrys()
    {
        int sum=0;
        int number_of_filled_buckets=0;
        for(int i=0;i<size;i++)
        {
            sum+=fill_count[i];
            if(fill_count[i]>0)
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
                table[i][j].initialized=0;
                table[i][j].pv_line.clear_extension();//give any extension chunks back to the pool
            }
        }
        for(int i=0;i<size;i++)
        {
            fill_count[i]=0;
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
                table[i][j].initialized=0;
                table[i][j].pv_line = PV_Line();
                table[i][j].zobrist_hash = 0;
                table[i][j].is_from_opening_book = 0;
                table[i][j].search_id = 0;
            }
        }
        for(int i=0;i<size;i++)
        {
            fill_count[i]=0;
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
            const TT_entry* bucket = table[i];
            short fill_count_for_bucket = fill_count[i];
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

    // force instantiation of the sizes actually used (lookup_table's size, PTT's base's size)
    template class lookup_table_base<TT_EXPONENT_FOR_SIZE, TT_BUCKET_SIZE>;
    template class lookup_table_base<PTT_EXPONENT_FOR_SIZE, PTT_BUCKET_SIZE>;

    float PTT::value_for_victim_index(const TT_entry& entry) const
    {
        // depth dominates; among equal depths, prefer keeping an exact bound over a
        // lower/upper one, since exact entries are more broadly reusable later
        return entry.pv_line.depth * 4 + (entry.pv_line.bound_type==0 ? 1 : 0);
    }

    // A TT_entry is no longer a flat blob (PV_Line owns a chunk chain), so the
    // PTT file stores each entry's scalars followed by exactly current_lenght
    // moves. Move is still a fixed-size POD, so the moves themselves round-trip
    // raw. Same-binary/platform assumption as before.
    static void write_entry(std::ofstream& file, const TT_entry& entry)
    {
        const uint8_t from_book = entry.is_from_opening_book ? 1 : 0;
        const int32_t search_id = entry.search_id;
        const int32_t depth = entry.pv_line.depth;
        const int32_t eval = entry.pv_line.eval;
        const int32_t bound_type = entry.pv_line.bound_type;
        const int32_t length = entry.pv_line.current_lenght;
        const uint8_t truncated = entry.pv_line.truncated ? 1 : 0;

        file.write(reinterpret_cast<const char*>(&entry.zobrist_hash), sizeof(entry.zobrist_hash));
        file.write(reinterpret_cast<const char*>(&from_book), sizeof(from_book));
        file.write(reinterpret_cast<const char*>(&search_id), sizeof(search_id));
        file.write(reinterpret_cast<const char*>(&depth), sizeof(depth));
        file.write(reinterpret_cast<const char*>(&eval), sizeof(eval));
        file.write(reinterpret_cast<const char*>(&bound_type), sizeof(bound_type));
        file.write(reinterpret_cast<const char*>(&length), sizeof(length));
        file.write(reinterpret_cast<const char*>(&truncated), sizeof(truncated));
        for(int k=0;k<length;k++)
        {
            const Move move = entry.pv_line.at(k);
            file.write(reinterpret_cast<const char*>(&move), sizeof(Move));
        }
    }

    static bool read_entry(std::ifstream& file, TT_entry& entry)
    {
        uint64_t zobrist_hash=0;
        int32_t search_id=0, depth=0, eval=0, bound_type=0, length=0;
        uint8_t from_book=0, truncated=0;

        file.read(reinterpret_cast<char*>(&zobrist_hash), sizeof(zobrist_hash));
        file.read(reinterpret_cast<char*>(&from_book), sizeof(from_book));
        file.read(reinterpret_cast<char*>(&search_id), sizeof(search_id));
        file.read(reinterpret_cast<char*>(&depth), sizeof(depth));
        file.read(reinterpret_cast<char*>(&eval), sizeof(eval));
        file.read(reinterpret_cast<char*>(&bound_type), sizeof(bound_type));
        file.read(reinterpret_cast<char*>(&length), sizeof(length));
        file.read(reinterpret_cast<char*>(&truncated), sizeof(truncated));
        if(!file.good() || length<0 || length>MAX_PV_Lenght)
        return false;

        entry = TT_entry();
        entry.initialized = true;
        entry.zobrist_hash = zobrist_hash;
        entry.is_from_opening_book = from_book!=0;
        entry.search_id = search_id;
        entry.pv_line.depth = depth;
        entry.pv_line.eval = eval;
        entry.pv_line.bound_type = bound_type;
        entry.pv_line.truncated = truncated!=0;

        int stored=0;
        for(int k=0;k<length;k++)
        {
            Move move;
            file.read(reinterpret_cast<char*>(&move), sizeof(Move));
            if(!file.good())
            return false;
            if(!entry.pv_line.set_move(k, move))
            break;//pool empty: keep the prefix, set_move already flagged it
            stored=k+1;
        }
        entry.pv_line.current_lenght = stored;
        return true;
    }

    bool PTT::save(const std::string& path) const
    {
        std::ofstream file(path, std::ios::binary);
        if(!file.is_open())
        {
            std::cout << "Error: could not open '" << path << "' for writing PTT!" << std::endl;
            return false;
        }

        const uint32_t MAGIC_NUMBER = 0x50545401; // "PTT" + version nibble
        // v3: PV_Line owns a chain of extension chunks, so a TT_entry is no
        // longer a flat blob and cannot be dumped raw - each entry is written
        // field by field with its moves flattened. Bumped from 2, which was the
        // raw-TT_entry format; a v2 file would be read as pointers.
        const uint32_t CURRENT_VERSION = 3;

        uint64_t entry_count = 0;
        for(int i=0;i<size;i++)
        entry_count += fill_count[i];

        file.write(reinterpret_cast<const char*>(&MAGIC_NUMBER), sizeof(MAGIC_NUMBER));
        file.write(reinterpret_cast<const char*>(&CURRENT_VERSION), sizeof(CURRENT_VERSION));
        file.write(reinterpret_cast<const char*>(&entry_count), sizeof(entry_count));

        for(int i=0;i<size;i++)
        {
            for(int j=0;j<fill_count[i];j++)
            {
                write_entry(file, table[i][j]);
            }
        }

        bool ok = file.good();
        file.close();
        if(ok)
        std::cout << "Saved " << entry_count << " PTT entries to: " << path << std::endl;
        else
        std::cout << "Error: failed while writing PTT to '" << path << "'!" << std::endl;
        return ok;
    }

    bool PTT::load(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        if(!file.is_open())
        {
            std::cout << "Warning: could not open '" << path << "' to load PTT (starting empty)." << std::endl;
            return false;
        }

        uint32_t magic_number=0, version=0;
        uint64_t entry_count=0;
        file.read(reinterpret_cast<char*>(&magic_number), sizeof(magic_number));
        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        file.read(reinterpret_cast<char*>(&entry_count), sizeof(entry_count));

        if(!file.good() || magic_number != 0x50545401 || version != 3)
        {
            std::cout << "Error: '" << path << "' is not a valid/compatible PTT file!" << std::endl;
            return false;
        }

        uint64_t loaded=0;
        TT_entry entry;
        for(uint64_t i=0;i<entry_count && file.good();i++)
        {
            if(!read_entry(file, entry))
            break;
            insert(std::move(entry));// insert() already keeps whichever of (loaded, already in memory) has greater depth
            loaded++;
        }

        std::cout << "Loaded " << loaded << " PTT entries from: " << path << std::endl;
        return loaded == entry_count;
    }

#endif // LOOKUP_TABLE_CPP
