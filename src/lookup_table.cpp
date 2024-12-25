#ifndef LOOKUP_TABLE_CPP
#define LOOKUP_TABLE_CPP

#include "../lib/lookup_table.hpp"

   
    inline uint64_t lookup_table::hash_0(const uint64_t u)
    {
        return __builtin_popcountll(u);
    }
    
    inline uint64_t lookup_table::hash_0(const uint64_t board[12])//return number of non king pieces
        {
            uint64_t n=board[0]|board[1]|board[2]|board[3]|board[4]|board[6]|board[7]|board[8]|board[9]|board[10];//here the kings are irrellevant, since they are always there.
            return hash_0(n);
        }

    inline uint64_t lookup_table::hash_1(const uint64_t u)
    {
        uint64_t n = u;
        n ^= n >> 33;
        n *= 0xff51afd7ed558ccd;
        n ^= n >> 33;
        n *= 0xc4ceb9fe1a85ec53;
        n ^= n >> 33;
        return n;
    }
    
    inline uint64_t lookup_table::hash_1(const uint64_t board[12])
    {
        uint64_t n=board[0]|board[1]|board[2]|board[3]|board[4]|board[5]|board[6]|board[7]|board[8]|board[9]|board[10]|board[11];
        return hash_1(n);
    }
    
    inline uint64_t lookup_table::hash_2(const uint64_t u)
    {
        uint64_t n = u;
        n ^= n >> 30;
        n *= 0xbf58476d1ce4e5b9;
        n ^= n >> 27;
        n *= 0x94d049bb133111eb;
        n ^= n >> 31;
        return n;
    }
    
    inline uint64_t lookup_table::hash_2(const uint64_t board[12])
    {
        uint64_t n=board[0]|board[1]|board[2]|board[3]|board[4]|board[5]|board[6]|board[7]|board[8]|board[9]|board[10]|board[11];
        return hash_2(n);
    }
    //we shoule keep track of the 50-move rule resets, then this could be the first hash index. this should be very!good, as then only relevant positions will be compared
    
    bool lookup_table::there_are_doubles()
    {
        int sum=0;
        for(int i=0;i<size_of_dimension_0;i++)
        for(int j=0;j<size_of_dimension_1;j++)
        for(int k=0;k<size_of_dimension_2;k++)
        {
            for(int l=0;l<size[i][j][k];l++)
            {
                for(int m=l+1;m<size[i][j][k];m++)
                {
                    if(are_equal(&table[i][j][k][l],&table[i][j][k][m]))
                    {
                        sum++;
                    }
                }
            }
        }
        std::cout << "There are " << sum << " doubles in the table!" << std::endl;
        //std::cout << "There are no doubles in the table!" << std::endl;
        return false;
    }

    
    bool lookup_table::is_retrivable_eval(const BB* const original, int requestes_depth)//if possible sets eval to the value in the table, returns true if found. If the board is found, but not evaluated yet(it has to be in the current branch for that or pruned away, if it was pruned, it was an arbitrary value(and hence we cannot make further conclusions, the idead to set the eval =0 is therefore wrong!))
    {
        
        //number_of_repetitions needs a rework
        number_of_attemted_readouts++;
        int h_v_0=hash_0(original->Board)%size_of_dimension_0;
        int h_v_1=hash_1(original->Board)%size_of_dimension_1;
        int h_v_2=hash_2(original->Board)%size_of_dimension_2;
        for(int i=0;i<size[h_v_0][h_v_1][h_v_2];i++)
        {
            if(are_equal(&(table[h_v_0][h_v_1][h_v_2][i]),original))//if they are equal
            {
                if(table[h_v_0][h_v_1][h_v_2][i].depth_of_eval>=requestes_depth)// and the depth is suficcent
                {
                    if(table[h_v_0][h_v_1][h_v_2][i].is_evaluated==0)
                    return false;//if the board is found, but not evaluated yet, we cannot make further conclusions, as it could hvae been pruned (id does not need to be a loop!)
                    number_of_succ_readouts++;
                    original->eval=table[h_v_0][h_v_1][h_v_2][i].eval;
                    return true;
                }
                else 
                return false;
                
            }
        }
        return false;
    }


    void lookup_table::insert(BB original, int given_depth)
            {
                bool is_already_in_table=0;
                number_of_inserions++;
                uint16_t h_v_0=hash_0(original.Board)%size_of_dimension_0;
                uint16_t h_v_1=hash_1(original.Board)%size_of_dimension_1;
                uint16_t h_v_2=hash_2(original.Board)%size_of_dimension_2;
    
                for(int i=0;i<size[h_v_0][h_v_1][h_v_2];i++)
                {
                    if(are_equal(&(table[h_v_0][h_v_1][h_v_2][i]),&original))
                    {
                        if(is_retrivable_eval(&original,given_depth)&& original.is_evaluated)
                        {
                            std::cout << "Error, this should not be retrivable,because then this func should never have neen called!";
                            exit(0);
                        }
                        if(given_depth<=table[h_v_0][h_v_1][h_v_2][i].depth_of_eval)
                        {
                            if(given_depth!=0)
                            exit(0);
                            std::cout << "given_depth, temp.depth_of_eval: " << given_depth << " " << table[h_v_0][h_v_1][h_v_2][i].depth_of_eval << std::endl;
                            std::cout << "It is possible, that the table tries to insert values of lesser depth, so this code is necessary. you may removve this message";
                            return;
                        }
                        is_already_in_table=1;
                        if(given_depth>table[h_v_0][h_v_1][h_v_2][i].depth_of_eval || original.eval>=INT_MAX-max_mating_seq || original.eval<=INT_MIN+max_mating_seq)
                        if(original.is_evaluated)
                        {
                            table[h_v_0][h_v_1][h_v_2][i].eval=original.eval;
                            table[h_v_0][h_v_1][h_v_2][i].is_evaluated=1;
                        }
                        break;
                    }
                }
                
                
                if(!is_already_in_table)
                {
                    if(original.eval>=INT_MAX-max_mating_seq || original.eval<=INT_MIN+max_mating_seq)
                    original.depth_of_eval=max_mating_seq;
                    
                    table[h_v_0][h_v_1][h_v_2].push_back(original);
                    
                    
                }
                
                if(++size[h_v_0][h_v_1][h_v_2]>100)
                {
                    //std::cout << "Warning: hash table is getting large, consider increasing size_of_dimension_0, size_of_dimension_1 or size_of_dimension_2" << std::endl;
                    table[h_v_0][h_v_1][h_v_2].erase(table[h_v_0][h_v_1][h_v_2].begin());
                }
            }

    void lookup_table::get_number_of_entrys()
    {
        int sum=0;
        for(int i=0;i<size_of_dimension_0;i++)
        for(int j=0;j<size_of_dimension_1;j++)
        for(int k=0;k<size_of_dimension_2;k++)
        {
            sum+=size[i][j][k];
        }
        std::cout << "Number of entrys in the lookup table: " << sum << std::endl;
        std::cout << "The average number of entrys per bucket is: " << float(sum)/(size_of_dimension_0*size_of_dimension_1*size_of_dimension_2) << std::endl;
    }  

    void lookup_table::reset()
    {
        for(int i=0;i<size_of_dimension_0;i++)
        for(int j=0;j<size_of_dimension_1;j++)
        for(int k=0;k<size_of_dimension_2;k++)
        {
            table[i][j][k].clear();
            size[i][j][k]=0;
        }
        number_of_inserions=0;
        number_of_succ_readouts=0;
        number_of_attemted_readouts=0;
    };





















#endif // LOOKUP_TABLE_CPP