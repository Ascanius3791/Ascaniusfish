#ifndef MAGICS_HPP
#define MAGICS_HPP
/**************************************\
 ======================================
        
         Plain magic bitboards
     implementation & demonstration
     
                  by
                  
           Code Monkey King

 ======================================
\**************************************/


// system headers
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string.h>
//now my own headers
#include "templates.cpp"

// bits manipulations
#define get_bit(bitboard, square) (bitboard & (1ULL << square))
#define set_bit(bitboard, square) (bitboard |= (1ULL << square))
#define pop_bit(bitboard, square) (get_bit(bitboard, square) ? (bitboard ^= (1ULL << square)) : 0)

// square encoding
enum {
    a8, b8, c8, d8, e8, f8, g8, h8,
    a7, b7, c7, d7, e7, f7, g7, h7,
    a6, b6, c6, d6, e6, f6, g6, h6,
    a5, b5, c5, d5, e5, f5, g5, h5,
    a4, b4, c4, d4, e4, f4, g4, h4,
    a3, b3, c3, d3, e3, f3, g3, h3,
    a2, b2, c2, d2, e2, f2, g2, h2,
    a1, b1, c1, d1, e1, f1, g1, h1, no_sq
};

// rook rellevant occupancy bits
int rook_rellevant_bits[64] = {
    12, 11, 11, 11, 11, 11, 11, 12,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    12, 11, 11, 11, 11, 11, 11, 12
};

// bishop rellevant occupancy bits
int bishop_rellevant_bits[64] = {
    6, 5, 5, 5, 5, 5, 5, 6,
    5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 7, 7, 7, 7, 5, 5,
    5, 5, 7, 9, 9, 7, 5, 5,
    5, 5, 7, 9, 9, 7, 5, 5,
    5, 5, 7, 7, 7, 7, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5,
    6, 5, 5, 5, 5, 5, 5, 6
};

// print bitboard
void print_bitboard(uint64_t bitboard)
{
    printf("\n");
    
    // loop over board ranks
    for (int rank = 0; rank < 8; rank++)
    {
        // loop over board files
        for (int file = 0; file < 8; file++)
        {
            // init board square
            int square = rank * 8 + file;
            
            // print ranks
            if (!file)
                printf("  %d ", 8 - rank);
            
            // print bit indexed by board square
            printf(" %d", get_bit(bitboard, square) ? 1 : 0);
        }
        
        printf("\n");
    }
    
    // print files
    printf("\n     a b c d e f g h\n\n");
    
}

// just a random number
unsigned int state = 1804289383;

// 32-bit number pseudo random generator
unsigned int generate_random_number()
{
	// XOR shift algorithm
	unsigned int x = state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	state = x;
	return x;
}

// generate random uint64_t number
uint64_t random_uint64_t()
{
    // init numbers to randomize
    uint64_t u1, u2, u3, u4;
    
    // randomize numbers
    u1 = (uint64_t)(generate_random_number()) & 0xFFFF;
    u2 = (uint64_t)(generate_random_number()) & 0xFFFF;
    u3 = (uint64_t)(generate_random_number()) & 0xFFFF;
    u4 = (uint64_t)(generate_random_number()) & 0xFFFF;
    
    // shuffle bits and return
    return u1 | (u2 << 16) | (u3 << 32) | (u4 << 48);
}

// get random few bits
uint64_t random_fewbits() {
    return random_uint64_t() & random_uint64_t() & random_uint64_t();
}

// count bits (Brian Kernighan's way)
int count_bits(uint64_t bitboard) {
  // bit count
  int count = 0;
  
  // pop bits untill bitboard is empty
  while (bitboard)
  {
      // increment count
      count++;
      
      // consecutively reset least significant 1st bit
      bitboard &= bitboard - 1;
  }
  
  // return bit count
  return count;
}

// get index of LS1B in bitboard
int get_ls1b_index(uint64_t bitboard) {
    // make sure bitboard is not empty
    if (bitboard != 0)
        // convert trailing zeros before LS1B to ones and count them
        return count_bits((bitboard & -bitboard) - 1);
    
    // otherwise
    else
        // return illegal index
        return -1;
}

// set occupancies
uint64_t set_occupancy(int index, int bits_in_mask, uint64_t attack_mask)
{
    // occupancy map
    uint64_t occupancy = 0ULL;
    
    // loop over the range of bits within attack mask
    for (int count = 0; count < bits_in_mask; count++)
    {
        // get LS1B index of attacks mask
        int square = get_ls1b_index(attack_mask);
        
        // pop LS1B in attack map
        pop_bit(attack_mask, square);
        
        // make sure occupancy is on board
        if (index & (1 << count))
            // populate occupancy map
            occupancy |= (1ULL << square);
    }
    
    // return occupancy map
    return occupancy;
}

// mask bishop attacks
uint64_t mask_bishop_attacks(int square)
{
    // attack bitboard
    uint64_t attacks = 0ULL;
    
    // init files & ranks
    int f, r;
    
    // init target files & ranks
    int tr = square / 8;
    int tf = square % 8;
    
    // generate attacks
    for (r = tr + 1, f = tf + 1; r <= 6 && f <= 6; r++, f++) attacks |= (1ULL << (r * 8 + f));
    for (r = tr + 1, f = tf - 1; r <= 6 && f >= 1; r++, f--) attacks |= (1ULL << (r * 8 + f));
    for (r = tr - 1, f = tf + 1; r >= 1 && f <= 6; r--, f++) attacks |= (1ULL << (r * 8 + f));
    for (r = tr - 1, f = tf - 1; r >= 1 && f >= 1; r--, f--) attacks |= (1ULL << (r * 8 + f));
    
    // return attack map for bishop on a given square
    return attacks;
}

// mask rook attacks
uint64_t mask_rook_attacks(int square)
{
    // attacks bitboard
    uint64_t attacks = 0ULL;
    
    // init files & ranks
    int f, r;
    
    // init target files & ranks
    int tr = square / 8;
    int tf = square % 8;
    
    // generate attacks
    for (r = tr + 1; r <= 6; r++) attacks |= (1ULL << (r * 8 + tf));
    for (r = tr - 1; r >= 1; r--) attacks |= (1ULL << (r * 8 + tf));
    for (f = tf + 1; f <= 6; f++) attacks |= (1ULL << (tr * 8 + f));
    for (f = tf - 1; f >= 1; f--) attacks |= (1ULL << (tr * 8 + f));
    
    // return attack map for bishop on a given square
    return attacks;
}

// bishop attacks
uint64_t bishop_attacks_on_the_fly(int square, uint64_t block)
{
    // attack bitboard
    uint64_t attacks = 0ULL;
    
    // init files & ranks
    int f, r;
    
    // init target files & ranks
    int tr = square / 8;
    int tf = square % 8;
    
    // generate attacks
    for (r = tr + 1, f = tf + 1; r <= 7 && f <= 7; r++, f++)
    {
        attacks |= (1ULL << (r * 8 + f));
        if (block & (1ULL << (r * 8 + f))) break;
    }
    
    for (r = tr + 1, f = tf - 1; r <= 7 && f >= 0; r++, f--)
    {
        attacks |= (1ULL << (r * 8 + f));
        if (block & (1ULL << (r * 8 + f))) break;
    }
    
    for (r = tr - 1, f = tf + 1; r >= 0 && f <= 7; r--, f++)
    {
        attacks |= (1ULL << (r * 8 + f));
        if (block & (1ULL << (r * 8 + f))) break;
    }
    
    for (r = tr - 1, f = tf - 1; r >= 0 && f >= 0; r--, f--)
    {
        attacks |= (1ULL << (r * 8 + f));
        if (block & (1ULL << (r * 8 + f))) break;
    }
    
    // return attack map for bishop on a given square
    return attacks;
}

// rook attacks
uint64_t rook_attacks_on_the_fly(int square, uint64_t block)
{
    // attacks bitboard
    uint64_t attacks = 0ULL;
    
    // init files & ranks
    int f, r;
    
    // init target files & ranks
    int tr = square / 8;
    int tf = square % 8;
    
    // generate attacks
    for (r = tr + 1; r <= 7; r++)
    {
        attacks |= (1ULL << (r * 8 + tf));
        if (block & (1ULL << (r * 8 + tf))) break;
    }
    
    for (r = tr - 1; r >= 0; r--)
    {
        attacks |= (1ULL << (r * 8 + tf));
        if (block & (1ULL << (r * 8 + tf))) break;
    }
    
    for (f = tf + 1; f <= 7; f++)
    {
        attacks |= (1ULL << (tr * 8 + f));
        if (block & (1ULL << (tr * 8 + f))) break;
    }
    
    for (f = tf - 1; f >= 0; f--)
    {
        attacks |= (1ULL << (tr * 8 + f));
        if (block & (1ULL << (tr * 8 + f))) break;
    }
    
    // return attack map for bishop on a given square
    return attacks;
}


/**************************************\

        Generating magic numbers
        
\**************************************/

// find magic number
uint64_t find_magic(int square, int relevant_bits, int bishop) {
    // define occupancies array
    uint64_t occupancies[4096];

    // define attacks array
    uint64_t attacks[4096];

    // define used indicies array
    uint64_t used_attacks[4096];
    
    // mask piece attack
    uint64_t mask_attack = bishop ? mask_bishop_attacks(square) : mask_rook_attacks(square);

    // occupancy variations
    int occupancy_variations = 1 << relevant_bits;

    // loop over the number of occupancy variations
    for(int count = 0; count < occupancy_variations; count++) {
        // init occupancies
        occupancies[count] = set_occupancy(count, relevant_bits, mask_attack);
        
        // init attacks
        attacks[count] = bishop ? bishop_attacks_on_the_fly(square, occupancies[count]) :
                                  rook_attacks_on_the_fly(square, occupancies[count]);
    }

    // test magic numbers
    for(int random_count = 0; random_count < 100000000; random_count++)
    {
        // init magic number candidate
        uint64_t magic = random_fewbits();

        // skip testing magic number if inappropriate
        if(count_bits((mask_attack * magic) & 0xFF00000000000000ULL) < 6) continue;

        // reset used attacks array
        memset(used_attacks, 0ULL, sizeof(used_attacks));
        
        // init count & fail flag
        int count, fail;
        
        // test magic index
        for (count = 0, fail = 0; !fail && count < occupancy_variations; count++) {
            // generate magic index
            int magic_index = (int)((occupancies[count] * magic) >> (64 - relevant_bits));
          
            // if got free index
            if(used_attacks[magic_index] == 0ULL)
                // assign corresponding attack map
                used_attacks[magic_index] = attacks[count];

            // otherwise fail on  collision
            else if(used_attacks[magic_index] != attacks[count]) fail = 1;
        }
        
        // return magic if it works
        if(!fail) return magic;
    }
    
    // on fail
    printf("***Failed***\n");
    return 0ULL;
}

// init magics
void init_magics()
{ 
  // loop over 64 board squares
  for(int square = 0; square < 64; square++)
    find_magic(square, rook_rellevant_bits[square], 0);
  
  
  // loop over 64 board squares
  for(int square = 0; square < 64; square++)
    find_magic(square, bishop_rellevant_bits[square], 1);
  
}


/**************************************\

        Initializing attack table
        
\**************************************/

// masks
uint64_t bishop_masks[64];
uint64_t rook_masks[64];

// attacks
uint64_t bishop_attacks[64][512];
uint64_t rook_attacks[64][4096];

// rook magic numbers
const uint64_t rook_magics[64] = {
    0xa8002c000108020ULL,
    0x6c00049b0002001ULL,
    0x100200010090040ULL,
    0x2480041000800801ULL,
    0x280028004000800ULL,
    0x900410008040022ULL,
    0x280020001001080ULL,
    0x2880002041000080ULL,
    0xa000800080400034ULL,
    0x4808020004000ULL,
    0x2290802004801000ULL,
    0x411000d00100020ULL,
    0x402800800040080ULL,
    0xb000401004208ULL,
    0x2409000100040200ULL,
    0x1002100004082ULL,
    0x22878001e24000ULL,
    0x1090810021004010ULL,
    0x801030040200012ULL,
    0x500808008001000ULL,
    0xa08018014000880ULL,
    0x8000808004000200ULL,
    0x201008080010200ULL,
    0x801020000441091ULL,
    0x800080204005ULL,
    0x1040200040100048ULL,
    0x120200402082ULL,
    0xd14880480100080ULL,
    0x12040280080080ULL,
    0x100040080020080ULL,
    0x9020010080800200ULL,
    0x813241200148449ULL,
    0x491604001800080ULL,
    0x100401000402001ULL,
    0x4820010021001040ULL,
    0x400402202000812ULL,
    0x209009005000802ULL,
    0x810800601800400ULL,
    0x4301083214000150ULL,
    0x204026458e001401ULL,
    0x40204000808000ULL,
    0x8001008040010020ULL,
    0x8410820820420010ULL,
    0x1003001000090020ULL,
    0x804040008008080ULL,
    0x12000810020004ULL,
    0x1000100200040208ULL,
    0x430000a044020001ULL,
    0x280009023410300ULL,
    0xe0100040002240ULL,
    0x200100401700ULL,
    0x2244100408008080ULL,
    0x8000400801980ULL,
    0x2000810040200ULL,
    0x8010100228810400ULL,
    0x2000009044210200ULL,
    0x4080008040102101ULL,
    0x40002080411d01ULL,
    0x2005524060000901ULL,
    0x502001008400422ULL,
    0x489a000810200402ULL,
    0x1004400080a13ULL,
    0x4000011008020084ULL,
    0x26002114058042ULL,
};

// bishop magic number
const uint64_t bishop_magics[64] = {
    0x89a1121896040240ULL,
    0x2004844802002010ULL,
    0x2068080051921000ULL,
    0x62880a0220200808ULL,
    0x4042004000000ULL,
    0x100822020200011ULL,
    0xc00444222012000aULL,
    0x28808801216001ULL,
    0x400492088408100ULL,
    0x201c401040c0084ULL,
    0x840800910a0010ULL,
    0x82080240060ULL,
    0x2000840504006000ULL,
    0x30010c4108405004ULL,
    0x1008005410080802ULL,
    0x8144042209100900ULL,
    0x208081020014400ULL,
    0x4800201208ca00ULL,
    0xf18140408012008ULL,
    0x1004002802102001ULL,
    0x841000820080811ULL,
    0x40200200a42008ULL,
    0x800054042000ULL,
    0x88010400410c9000ULL,
    0x520040470104290ULL,
    0x1004040051500081ULL,
    0x2002081833080021ULL,
    0x400c00c010142ULL,
    0x941408200c002000ULL,
    0x658810000806011ULL,
    0x188071040440a00ULL,
    0x4800404002011c00ULL,
    0x104442040404200ULL,
    0x511080202091021ULL,
    0x4022401120400ULL,
    0x80c0040400080120ULL,
    0x8040010040820802ULL,
    0x480810700020090ULL,
    0x102008e00040242ULL,
    0x809005202050100ULL,
    0x8002024220104080ULL,
    0x431008804142000ULL,
    0x19001802081400ULL,
    0x200014208040080ULL,
    0x3308082008200100ULL,
    0x41010500040c020ULL,
    0x4012020c04210308ULL,
    0x208220a202004080ULL,
    0x111040120082000ULL,
    0x6803040141280a00ULL,
    0x2101004202410000ULL,
    0x8200000041108022ULL,
    0x21082088000ULL,
    0x2410204010040ULL,
    0x40100400809000ULL,
    0x822088220820214ULL,
    0x40808090012004ULL,
    0x910224040218c9ULL,
    0x402814422015008ULL,
    0x90014004842410ULL,
    0x1000042304105ULL,
    0x10008830412a00ULL,
    0x2520081090008908ULL,
    0x40102000a0a60140ULL,
};

// init slider pieces attacks
void init_sliders_attacks(int is_bishop)
{
    // loop over 64 board squares
    for (int square = 0; square < 64; square++)
    {
        // init bishop & rook masks
        bishop_masks[square] = mask_bishop_attacks(square);
        rook_masks[square] = mask_rook_attacks(square);
        
        // init current mask
        uint64_t mask = is_bishop ? mask_bishop_attacks(square) : mask_rook_attacks(square);
        
        // count attack mask bits
        int bit_count = count_bits(mask);
        
        // occupancy variations count
        int occupancy_variations = 1 << bit_count;
        
        // loop over occupancy variations
        for (int count = 0; count < occupancy_variations; count++)
        {
            // bishop
            if (is_bishop)
            {
                // init occupancies, magic index & attacks
                uint64_t occupancy = set_occupancy(count, bit_count, mask);
                uint64_t magic_index = occupancy * bishop_magics[square] >> 64 - bishop_rellevant_bits[square];
                bishop_attacks[square][magic_index] = bishop_attacks_on_the_fly(square, occupancy);                
            }
            
            // rook
            else
            {
                // init occupancies, magic index & attacks
                uint64_t occupancy = set_occupancy(count, bit_count, mask);
                uint64_t magic_index = occupancy * rook_magics[square] >> 64 - rook_rellevant_bits[square];
                rook_attacks[square][magic_index] = rook_attacks_on_the_fly(square, occupancy);                
            }
        }
    }
}

// lookup bishop attacks 
uint64_t get_bishop_attacks(int square, uint64_t occupancy) {
	
	// calculate magic index
	occupancy &= bishop_masks[square];
	occupancy *=  bishop_magics[square];
	occupancy >>= 64 - bishop_rellevant_bits[square];
	
	// return rellevant attacks
	return bishop_attacks[square][occupancy];
	
}

// lookup rook attacks 
uint64_t get_rook_attacks(int square, uint64_t occupancy) {
	
	// calculate magic index
	occupancy &= rook_masks[square];
	occupancy *=  rook_magics[square];
	occupancy >>= 64 - rook_rellevant_bits[square];
	
	// return rellevant attacks
	return rook_attacks[square][occupancy];
}


// attacks by color (from Ascanius)
uint64_t attacks_by_col(const uint64_t Board[12], bool by_white_pieces)
    {
        uint64_t occupancy = Board[0] | Board[1] | Board[2] | Board[3] | Board[4] | Board[5] | Board[6] | Board[7] | Board[8] | Board[9] | Board[10] | Board[11];
        uint64_t attacks=0;
        for(int i=0;i<64;i++)
        {
            //the pawns stuff looks weird, but is correct, that is well tested! the BP/Wp Templates seem to have been mae with checks in mind, so they seem reversed
            if(by_white_pieces)
            if(Board[0] & (1Ull << i))
            attacks |= BP_template[i];
            if(!by_white_pieces)
            if(Board[6] & (1Ull << i))
            attacks |= WP_template[i];

            if((Board[1+6*!by_white_pieces] | Board[4+6*!by_white_pieces]) & (1Ull << i))
            attacks |= get_rook_attacks(i,occupancy);
            if(Board[2+6*!by_white_pieces] & (1Ull << i))
            attacks |= Kn_template[i];
            if((Board[3+6*!by_white_pieces] | Board[4+6*!by_white_pieces]) & (1Ull << i))
            attacks |= get_bishop_attacks(i,occupancy);
            
            if(Board[5+6*!by_white_pieces] & (1Ull << i))
            attacks |= K_template[i];
        }
        return attacks;
    }

uint64_t attacked_squares(const uint64_t Board[12], bool for_white)
{
    uint64_t Attacked_squares=0;
    uint64_t own_pawns=Board[0+!6*for_white];
    uint64_t own_knights=Board[2+!6*for_white];
    uint64_t own_bishops=Board[3+!6*for_white]|Board[4+!6*for_white];
    uint64_t own_rooks=Board[1+!6*for_white]|Board[4+!6*for_white];
    uint64_t own_king=Board[5+!6*for_white];
    
    uint64_t occupancy=Board[0]|Board[1]|Board[2]|Board[3]|Board[4]|Board[5]|Board[6]|Board[7]|Board[8]|Board[9]|Board[10]|Board[11];
    while(own_pawns)
    {
        int i=find_and_delete_trailling_1(own_pawns);
        if(for_white)
        {
            Attacked_squares |= BP_template[i];
        }
        else
        {
            Attacked_squares |= WP_template[i];
        }
    }
    
    while(own_knights)
    {
        int i=find_and_delete_trailling_1(own_knights);
        Attacked_squares |= Kn_template[i];
    }
    
    while(own_bishops)
    {
        int i=find_and_delete_trailling_1(own_bishops);
        Attacked_squares |= get_bishop_attacks(i,occupancy);
    }
    
    while(own_rooks)
    {
        int i=find_and_delete_trailling_1(own_rooks);
        Attacked_squares |= get_rook_attacks(i,occupancy);
    }
    
    int i=find_and_delete_trailling_1(own_king);
    Attacked_squares |= K_template[i];
    return Attacked_squares;

}









#endif // MAGICS_HPP


