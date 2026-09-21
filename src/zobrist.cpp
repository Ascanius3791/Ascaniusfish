// OWNERSHIP=Ascanius
#ifndef ZOBRIST_CPP
#define ZOBRIST_CPP

#include "../lib/zobrist.hpp"

Zobrist::Zobrist()
    {
        int seed = 123456789; 
        std::mt19937_64 rng(seed);

        for (int piece = 0; piece < 12; ++piece)
        {
            for (int square = 0; square < 64; ++square)
            {
                pieceKeys[piece][square] = rng();
            }
        }

        sideKey = rng();

        for (int rights = 0; rights < 16; ++rights)
        {
            castlingKeys[rights] = rng();
        }

        for (int file = 0; file < 8; ++file)
        {
            enPassantKeys[file] = rng();
        }
    }

int Zobrist::castling_rights_to_index(const BB& board)
{
    int index = (board.castle[1][1] << 3) | (board.castle[1][0] << 2) |(board.castle[0][1] << 1) | board.castle[0][0];
    return index;
}

int Zobrist::en_passant_to_index(const BB& board)
{
    if (board.en_passant == 0)
        return -1; // No en passant square
    int file = __builtin_ctzll(board.en_passant) % 8;
    return file;
}

void Zobrist::update_metadata_part_of_zobrist_hash(const BB& old_board, BB& new_board)
{
    // Update castling rights
    int old_castling_rights = castling_rights_to_index(old_board);
    int new_castling_rights = castling_rights_to_index(new_board);
    new_board.zobrist_hash ^= castlingKeys[old_castling_rights]; // Remove the old castling rights
    new_board.zobrist_hash ^= castlingKeys[new_castling_rights]; // Add the new castling rights

    // Update en passant square
    int old_en_passant_index = en_passant_to_index(old_board);
    int new_en_passant_index = en_passant_to_index(new_board);
    if (old_en_passant_index != -1)
        new_board.zobrist_hash ^= enPassantKeys[old_en_passant_index]; // Remove the old en passant square
    if (new_en_passant_index != -1)
        new_board.zobrist_hash ^= enPassantKeys[new_en_passant_index]; // Add the new en passant square
    adjust_for_side_to_move(new_board); // Update the side to move

}

uint64_t Zobrist::compute_Zobrist_Hash(const BB& board)
    {
        uint64_t hash = 0;

        for (int piece = 0; piece < 12; ++piece)
        {
            uint64_t bitboard = board.Board[piece];
            while (bitboard)
            {
                int square = __builtin_ctzll(bitboard);
                hash ^= pieceKeys[piece][square];
                bitboard &= bitboard - 1;
            }
        }

        if (!board.white_move)
        {
            hash ^= sideKey;
        }

        int castlingRights = castling_rights_to_index(board);
        hash ^= castlingKeys[castlingRights];

        if (board.en_passant)
        {
            int file = __builtin_ctzll(board.en_passant) % 8;
            hash ^= enPassantKeys[file];
        }

        return hash;
    }

void Zobrist::update_standart_move_of_zobrist_hash(const BB& old_board, BB& new_board, int piece_type, int from, int to, bool is_capture, int promotion_piece_type, bool is_en_passant_capture)
{
    // Remove the piece from its original square
    new_board.zobrist_hash ^= pieceKeys[piece_type][from];

    // Add the piece to its new square
    new_board.zobrist_hash ^= pieceKeys[piece_type][to];

    // If it's a capture, remove the captured piece from its square
    if (is_capture)
    {
        if(is_en_passant_capture)
        {
            bool WM = old_board.white_move;
            int captured_pawn_square = to + (WM ? -8 : 8); // The square of the captured pawn in en passant
            int captured_pawn_type = WM ? 6 : 0; // Captured pawn type based on the side to move
            new_board.zobrist_hash ^= pieceKeys[captured_pawn_type][captured_pawn_square];
            return;
        }
        
        int enemy_piece_type = find_enemy_piece_type_on_sq(old_board.Board, to, old_board.white_move);
        if (DEBUG_MODE ==1 && enemy_piece_type == -1)
        {
            std::cout << "Error: No enemy piece found on square " << to << " for capture." << std::endl;
            exit(1);
        }
        new_board.zobrist_hash ^= pieceKeys[enemy_piece_type][to];
    }

    // If it's a promotion, remove the pawn and add the promoted piece
    if (promotion_piece_type != -1)
    {
        new_board.zobrist_hash ^= pieceKeys[piece_type][to]; // Remove the pawn from its new square
        new_board.zobrist_hash ^= pieceKeys[promotion_piece_type][to]; // Add the promoted piece to its new square
    }
}


void Zobrist::update_zobrist_hash(const BB& old_board, BB& new_board, int piece_type, int from, int to, bool is_capture, int promotion_piece_type, bool is_en_passant_capture)
{
    // Update the standard move part of the hash
    update_standart_move_of_zobrist_hash(old_board, new_board, piece_type, from, to, is_capture, promotion_piece_type, is_en_passant_capture);

    // Update the metadata part of the hash (castling rights and en passant square)
    update_metadata_part_of_zobrist_hash(old_board, new_board);
}

void Zobrist::update_zobrist_hash_castling_piece_position(const BB& old_board, BB& new_board, bool King_side)
{
    // Determine the piece type and squares based on the color and side
    bool WM = old_board.white_move;
    int KING = WM ? 5 : 11; // King piece type
    int ROOK = WM ? 1 : 7; // Rook piece type
    int king_from = WM ? 4 : 60; // e1 or e8
    int king_to = King_side ? (WM ? 6 : 62) : (WM ? 2 : 58); // g1/g8 or c1/c8
    int rook_from = King_side ? (WM ? 7 : 63) : (WM ? 0 : 56); // h1/h8 or a1/a8
    int rook_to = King_side ? (WM ? 5 : 61) : (WM ? 3 : 59); // f1/f8 or d1/d8

    uint64_t& new_hash = new_board.zobrist_hash;
    new_hash ^= pieceKeys[KING][king_from]; // Remove the king from its original square
    new_hash ^= pieceKeys[KING][king_to]; // Add the king to its new square
    new_hash ^= pieceKeys[ROOK][rook_from]; // Remove the rook from its original square
    new_hash ^= pieceKeys[ROOK][rook_to]; // Add the rook to its new square

    // Update the metadata part of the hash (castling rights and en passant square)
    update_metadata_part_of_zobrist_hash(old_board, new_board);
}

bool Zobrist::has_correct_zobrist_hash(const BB& board)
{
    uint64_t computed_hash = compute_Zobrist_Hash(board);
    return computed_hash == board.zobrist_hash;
}

void Zobrist::adjust_for_side_to_move(BB& new_board)
{
    new_board.zobrist_hash ^= sideKey;
}


void FEN_to_BB(const std::string FEN, BB* const original)
{
    for(int i=0;i<12;i++)
    original->Board[i]=0;
    original->castle[0][0]=0;//these used to be set to 1, but i belive 0 is the correct initilisation, as we set it to 1, if K,Q,k,q is in the FEN
    original->castle[0][1]=0;
    original->castle[1][0]=0;
    original->castle[1][1]=0;
    original->en_passant=0;
    
    int i=0;
    int j=0;
    int i_on_previous_iteration;
    while(FEN[i]!=' ')
    {
        i_on_previous_iteration=i;
        if(FEN[i]=='/')
        {
            i++;
        }
        if(FEN[i]>='1' && FEN[i]<='8')
        {
            j+=FEN[i]-'0';
            i++;
        }
             
        int virtual_file=j%8;
        int virtual_rank=7-j/8;
        
        if(FEN[i]=='P')
        {
            original->Board[0] |= 1ULL<<(virtual_file+8*virtual_rank);
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
            
        }
        if(FEN[i]=='R')
        {
            original->Board[1] |= 1ULL<<(virtual_file+8*virtual_rank);
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
        if(FEN[i]=='N')
        {
            original->Board[2] |= 1ULL<<(virtual_file+8*virtual_rank);
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
        if(FEN[i]=='B')
        {
            original->Board[3] |= 1ULL<<(virtual_file+8*virtual_rank);
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
        if(FEN[i]=='Q')
        {
            original->Board[4] |= 1ULL<<(virtual_file+8*virtual_rank);
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
        if(FEN[i]=='K')
        {
            original->Board[5] |= 1ULL<<(virtual_file+8*virtual_rank);
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
        if(FEN[i]=='p')
        {
            original->Board[6] |= 1ULL<<(virtual_file+8*virtual_rank);
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
        if(FEN[i]=='r')
        {
            original->Board[7] |= 1ULL<<(virtual_file+8*virtual_rank);
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
        if(FEN[i]=='n')
        {
            original->Board[8] |= 1ULL<<(virtual_file+8*virtual_rank);
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
        if(FEN[i]=='b')
        {
            original->Board[9] |= 1ULL<<(virtual_file+8*virtual_rank);
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
        if(FEN[i]=='q')
        {
            original->Board[10] |= 1ULL<<(virtual_file+8*virtual_rank);;
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
        if(FEN[i]=='k')
        {
            original->Board[11] |= 1ULL<<(virtual_file+8*virtual_rank);;
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
        if(i_on_previous_iteration==i)
        {
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
    }
    
    i++;
    if(FEN[i]=='w')
    original->white_move=1;
    else
    original->white_move=0;
    i+=2;
    if(FEN[i]=='K')
    {
        original->castle[1][1]=1;
        i++;
    }
    if(FEN[i]=='Q')
    {
        original->castle[1][0]=1;
        i++;
    }
    if(FEN[i]=='k')
    {
        original->castle[0][1]=1;
        i++;
    }
    if(FEN[i]=='q')
    {
        original->castle[0][0]=1;
        i++;
    }
    if(FEN[i]=='-')
    {
        original->castle[0][1]=0;
        original->castle[0][0]=0;
        original->castle[1][1]=0;
        original->castle[1][0]=0;
        i=i+2;
    }
    else i++;
    if(FEN[i]=='-')
    original->en_passant=0;
    else
    {
        original->en_passant=1ULL << (FEN[i]-'a')+(8*(FEN[i+1]-'1'));
        i++;
    }
    i+=2;
    original->halfmoves_since_last_capture_or_pawn_move=0;
    while(FEN[i]>='0' && FEN[i]<='9')
    {
        original->halfmoves_since_last_capture_or_pawn_move=original->halfmoves_since_last_capture_or_pawn_move*10+(FEN[i]-'0');
        i++;
    }
    i++;
    original->move=0;
    while(i<(int)FEN.size() && FEN[i]>='0' && FEN[i]<='9')
    {
        original->move=original->move*10+(FEN[i]-'0');
        i++;
    }

    //compure zobrist hash would be nice but is not allowed-
    // zobrist is declared much later and needs BITBOARDS to be complete
    // it is much less pain to add the zobrist call later where it is actually needed
    original->zobrist_hash=Zobrist::compute_Zobrist_Hash(*original);
}


#endif // ZOBRIST_HPP
