#ifndef LOOKUP_TABLE_HPP
#define LOOKUP_TABLE_HPP
#include<vector>
#include"../src/Bitboards.cpp"
#include"../src/Settings.cpp"

#include<climits>
    
class lookup_table
{
    
    inline uint64_t hash_0(const uint64_t u);

    inline uint64_t hash_0(const uint64_t board[12]);//return number of non king pieces

    inline uint64_t hash_1(const uint64_t u);

    inline uint64_t hash_1(const uint64_t board[12]);

    inline uint64_t hash_2(const uint64_t u);

    inline uint64_t hash_2(const uint64_t board[12]);
        //we shoule keep track of the 50-move rule resets, then this could be the first hash index. this should be very!good, as then only relevant positions will be compared
    static const int size_of_dimension_0=5,size_of_dimension_1=1000, size_of_dimension_2=1000;//5 because there wont be all that mny caputes (hash_0 gives the amount of pieces left))
    std::vector<BB> table[size_of_dimension_0][size_of_dimension_1][size_of_dimension_2];
    int size[size_of_dimension_0][size_of_dimension_1][size_of_dimension_2];
    public:
    int number_of_inserions=0, number_of_succ_readouts=0, number_of_attemted_readouts=0;
    bool there_are_doubles();
    //if possible sets eval to the value in the table, returns true if found. If the board is found, but not evaluated yet(it has to be in the current branch for that or pruned away, if it was pruned, it was an arbitrary value(and hence we cannot make further conclusions, the idead to set the eval =0 is therefore wrong!))
    bool is_retrivable_eval(const BB* const original, int requestes_depth);


    void insert(BB original, int given_depth);

    void get_number_of_entrys();

    void reset();
    };
















#endif // LOOKUP_TABLE_HPP