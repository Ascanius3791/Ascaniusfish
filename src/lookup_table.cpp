// OWNERSHIP=Ascanius
#ifndef LOOKUP_TABLE_CPP
#define LOOKUP_TABLE_CPP

#include "../lib/lookup_table.hpp"
#include <fstream>
#include <cstdint>

    template<int EXPONENT_FOR_SIZE, int BUCKET_SIZE>
    lookup_table_base<EXPONENT_FOR_SIZE, BUCKET_SIZE>::lookup_table_base()
    {
        reset();
        // full_reset();
    }

    template<int EXPONENT_FOR_SIZE, int BUCKET_SIZE>
    int lookup_table_base<EXPONENT_FOR_SIZE, BUCKET_SIZE>::value_for_victim_index(const TT_entry& entry) const
    {
        int lambda = 2;
                                // +-?
        return entry.pv_line.depth + lambda * entry.board.move;
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
        int min_value=value_for_victim_index(candidate);
        for(int i=0;i<bucket_size;i++)
        {
            int value=value_for_victim_index(bucket[i]);
            if(value<min_value)
            {
                min_value=value;
                victim_index=i;
            }
        }
        return victim_index;
    }

    template<int EXPONENT_FOR_SIZE, int BUCKET_SIZE>
    bool lookup_table_base<EXPONENT_FOR_SIZE, BUCKET_SIZE>::there_are_doubles()
    {
        int sum=0;
        for(int i=0;i<size;i++)
        for(int j=0;j<fill_count[i];j++)
        {
            for(int k=j+1;k<fill_count[i];k++)
            {
                if(are_equal(&table[i][j].board,&table[i][k].board))
                {
                    sum++;
                }
            }
        }
        std::cout << "There are " << sum << " doubles in the table!" << std::endl;
        //std::cout << "There are no doubles in the table!" << std::endl;
        if(sum>0)
        return true;
        return false;
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
            return TT_readout();//if the board is not found, we cannot make further conclusions
        }
        TT_readout readout;
        for(int i=0;i<fill_count_for_bucket;i++)
        {
            if((are_equal(&(bucket[i].board),original) && 0) || are_equal(&(bucket[i].board),original))//just comparing the hashes give at this stage no speedup, so we might as wel keep the cleaner version
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
        return readout;//if the board is not found, we cannot make further conclusions
    }

    template<int EXPONENT_FOR_SIZE, int BUCKET_SIZE>
    void lookup_table_base<EXPONENT_FOR_SIZE, BUCKET_SIZE>::insert(TT_entry new_entry)
            {
                bool is_already_in_table=0;
                number_of_inserions++;
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
                    if(are_equal(&old_entry.board,&new_entry.board))//if they are equal
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
                        break;
                    }
                }
                if(victim_index ==-1)
                {
                    return;//the new entry is the least valuable, so we do not add it to the table
                }
                if(is_already_in_table)
                {
                    return;//the new entry is already in the table, so we do not add it again
                }
                if(fill_count_for_bucket<bucket_size)
                {
                    fill_count[hash]++;
                }
                bucket[victim_index]=new_entry;
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
            }
        }
        for(int i=0;i<size;i++)
        {
            fill_count[i]=0;
        }
        number_of_inserions=0;
        number_of_succ_readouts=0;
        number_of_attemted_readouts=0;
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
                table[i][j].board = BB();
                table[i][j].zobrist_hash = 0;
                table[i][j].is_from_opening_book = 0;
            }
        }
        for(int i=0;i<size;i++)
        {
            fill_count[i]=0;
        }
        number_of_inserions=0;
        number_of_succ_readouts=0;
        number_of_attemted_readouts=0;
    };

    template<int EXPONENT_FOR_SIZE, int BUCKET_SIZE>
    size_t lookup_table_base<EXPONENT_FOR_SIZE, BUCKET_SIZE>::get_hash(uint64_t zobrist_hash) const
    {
        size_t index = zobrist_hash & mask;
        return index;
    }

    template<int EXPONENT_FOR_SIZE, int BUCKET_SIZE>
    int lookup_table_base<EXPONENT_FOR_SIZE, BUCKET_SIZE>::get_number_of_full_collisions() const//count how many entrys have the same zobrist hash, but are not equal
    {
        int sum=0;
        for(int i=0;i<size;i++)
        {
            const TT_entry* bucket = table[i];
            short fill_count_for_bucket = fill_count[i];
            for(int j=0;j<fill_count_for_bucket;j++)
            {
                for(int k=j+1;k<fill_count_for_bucket;k++)
                {
                    if(bucket[j].zobrist_hash==bucket[k].zobrist_hash)
                    {
                        if(!are_equal(&bucket[j].board,&bucket[k].board))
                        {
                            sum++;
                        }
                    }
                }
            }
        }
        return sum;
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

    int PTT::value_for_victim_index(const TT_entry& entry) const
    {
        // depth dominates; among equal depths, prefer keeping an exact bound over a
        // lower/upper one, since exact entries are more broadly reusable later
        return entry.pv_line.depth * 4 + (entry.pv_line.bound_type==0 ? 1 : 0);
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
        const uint32_t CURRENT_VERSION = 1;

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
                file.write(reinterpret_cast<const char*>(&table[i][j]), sizeof(TT_entry));
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

        if(!file.good() || magic_number != 0x50545401 || version != 1)
        {
            std::cout << "Error: '" << path << "' is not a valid/compatible PTT file!" << std::endl;
            return false;
        }

        uint64_t loaded=0;
        TT_entry entry;
        for(uint64_t i=0;i<entry_count && file.good();i++)
        {
            file.read(reinterpret_cast<char*>(&entry), sizeof(TT_entry));
            if(!file.good())
            break;
            insert(entry);// insert() already keeps whichever of (loaded, already in memory) has greater depth
            loaded++;
        }

        std::cout << "Loaded " << loaded << " PTT entries from: " << path << std::endl;
        return loaded == entry_count;
    }

#endif // LOOKUP_TABLE_CPP
