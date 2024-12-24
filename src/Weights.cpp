#ifndef Weights_CPP
#define Weights_CPP

#include "../lib/Weights.hpp"

#ifndef WEIGHTS_CPP
#define WEIGHTS_CPP
#include <iostream>
#include <fstream>
#include <string>
#include <cmath>

// members of WEIGHTS  

void WEIGHTS::change_values(int alpha, bool change_white_pawn_values, int probabiltiy_of_change)//need to specify for which colour, else the pawm values mab be not changes as intended(bad values for plack pawns favour whtie pieces)
{
    if(std::rand()%100<probabiltiy_of_change)
    skip_depth_decrease_threshold += alpha*(1-2*(std::rand()%2));
    if(std::rand()%100<probabiltiy_of_change)
    value_of_attacked_square += alpha*(1-2*(std::rand()%2));
    if(std::rand()%100<probabiltiy_of_change)
    check_value += alpha*(1-2*(std::rand()%2));
    
    for(int piece=0;piece<7;piece++)
    for(int i=0;i<8;i++)
    for(int j=0;j<8;j++)
    {
        if(!(change_white_pawn_values && piece==6 || !change_white_pawn_values && piece==0))
        {
            if(std::rand()%100<probabiltiy_of_change)
            piece_table_value_opening[piece][i*8+j] += alpha*(1-2*(std::rand()%2));
            if(std::rand()%100<probabiltiy_of_change)
            piece_table_value_endgame[piece][i*8+j] += alpha*(1-2*(std::rand()%2));
        }
        

    }
    
    for(int piece=0;piece<6;piece++)
    {
        if(std::rand()%100<probabiltiy_of_change)
        offensive_value[piece] += alpha*(1-2*(std::rand()%2));
    }
    for(int piece=0;piece<6;piece++)
    {
        if(std::rand()%100<probabiltiy_of_change)
        defensive_value[piece] += alpha*(1-2*(std::rand()%2));
    }
    if(std::rand()%100<probabiltiy_of_change)
    king_safety_value += 1*(1-2*(std::rand()%2));// *1 not *alpha, this value, should be changed as little as possible

    
}

void WEIGHTS::print_values_to_file(std::string filename)const
{
    std::ofstream file;
    file.open(filename);
    file << skip_depth_decrease_threshold << std::endl;
    file << value_of_attacked_square << std::endl;
    file << check_value << std::endl;

    for(int piece=0;piece<7;piece++)
    for(int i=0;i<8;i++)
    for(int j=0;j<8;j++)
    {
        file << piece_table_value_opening[piece][i*8+j] << " ";

    }
    for(int piece=0;piece<7;piece++)
    for(int i=0;i<8;i++)
    for(int j=0;j<8;j++)
    {
        file << piece_table_value_endgame[piece][i*8+j] << " ";

    }
    for(int piece=0;piece<6;piece++)
    {
        file << piece_value[piece] << " ";
    }
    for(int piece=0;piece<6;piece++)
    {
        file << offensive_value[piece] << " ";
    }
    for(int piece=0;piece<6;piece++)
    {
        file << defensive_value[piece] << " ";
    }
    file << king_safety_value << " ";
    file.close();

}

void WEIGHTS::read_values_from_file(std::string filename)
{
    std::ifstream file;
    file.open(filename);
    file >> skip_depth_decrease_threshold;
    file >> value_of_attacked_square;
    file >> check_value;

    for(int piece=0;piece<7;piece++)
    for(int i=0;i<8;i++)
    for(int j=0;j<8;j++)
    {
        file >> piece_table_value_opening[piece][i*8+j];
    }
    for(int piece=0;piece<7;piece++)
    for(int i=0;i<8;i++)
    for(int j=0;j<8;j++)
    {
        file >> piece_table_value_endgame[piece][i*8+j];
    }
    for(int piece=0;piece<6;piece++)
    {
        file >> piece_value[piece];
    }
    for(int piece=0;piece<6;piece++)
    {
        file >> offensive_value[piece];
    }
    for(int piece=0;piece<6;piece++)
    {
        file >> defensive_value[piece];
    }
    file >> king_safety_value;
    file.close();
}

void WEIGHTS::print_all_values()const
{
    std::cout << skip_depth_decrease_threshold << " ";
    std::cout << value_of_attacked_square << " ";
    std::cout << check_value << " ";
    for(int piece=0;piece<7;piece++)
    for(int i=0;i<8;i++)
    for(int j=0;j<8;j++)
    {
        std::cout << piece_table_value_opening[piece][i*8+j] << " ";
    }
    for(int piece=0;piece<7;piece++)
    for(int i=0;i<8;i++)
    for(int j=0;j<8;j++)
    {
        std::cout << piece_table_value_endgame[piece][i*8+j] << " ";
    }
    for(int piece=0;piece<6;piece++)
    {
        std::cout << piece_value[piece] << " ";
    }
    for(int piece=0;piece<6;piece++)
    {
        std::cout << offensive_value[piece] << " ";
    }
    for(int piece=0;piece<6;piece++)
    {
        std::cout << defensive_value[piece] << " ";
    }
    std::cout << king_safety_value << " ";

}

int WEIGHTS::norm_to(WEIGHTS W)
{
    float sum =0;
    sum+=std::pow(skip_depth_decrease_threshold-W.skip_depth_decrease_threshold,2);
    sum+=std::pow(value_of_attacked_square-W.value_of_attacked_square,2);
    sum+=std::pow(check_value-W.check_value,2);
    for(int piece=0;piece<7;piece++)
    for(int i=0;i<8;i++)
    for(int j=0;j<8;j++)
    {
        sum+=std::pow(piece_table_value_opening[piece][i*8+j]-W.piece_table_value_opening[piece][i*8+j],2);
    }
    for(int piece=0;piece<7;piece++)
    for(int i=0;i<8;i++)
    for(int j=0;j<8;j++)
    {
        sum+=std::pow(piece_table_value_endgame[piece][i*8+j]-W.piece_table_value_endgame[piece][i*8+j],2);
    }
    for(int piece=0;piece<6;piece++)
    {
        sum+=std::pow(piece_value[piece]-W.piece_value[piece],2);
    }
    for(int piece=0;piece<6;piece++)
    {
        sum+=std::pow(offensive_value[piece]-W.offensive_value[piece],2);
    }
    for(int piece=0;piece<6;piece++)
    {
        sum+=std::pow(defensive_value[piece]-W.defensive_value[piece],2);
    }
    sum+=std::pow(king_safety_value-W.king_safety_value,2);
    return sqrt(sum/(7*8*8+3));
}

void WEIGHTS::append_weights_to_File(const std::string& filename)const {
    std::ofstream file;
    file.open(filename, std::ios::app);
    if (file.is_open()) {
        file << skip_depth_decrease_threshold << " " << value_of_attacked_square << " " << check_value << " ";
        for(int piece=0;piece<7;piece++)
        for(int i=0;i<64;i++) 
        file << piece_table_value_opening[piece][i] << " ";
        for(int piece=0;piece<7;piece++)
        for(int i=0;i<64;i++)
        file << piece_table_value_endgame[piece][i] << " ";

        for(int piece=0;piece<6;piece++)
        {
            file << piece_value[piece] << " ";
            file << offensive_value[piece] << " ";
            file << defensive_value[piece] << " ";
            
        }
        file << king_safety_value << std::endl;
        file.close();
    } else {
        std::cerr << "Unable to open file " << filename << std::endl;
    }
}

bool WEIGHTS::is_correct_password()const {
    std::string inputPassword;
    const std::string correctPassword = "9865";
    std::cout << "Enter password: ";
    std::cin >> inputPassword;
    return inputPassword == correctPassword;
}

void WEIGHTS::clearFileExceptFirstLine(const std::string& filename)const {
    if (!is_correct_password()) {
        std::cerr << "Incorrect password. Access denied." << std::endl;
        return;
    }

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Unable to open file " << filename << std::endl;
        return;
    }

    std::string line;
    getline(file, line);
    std::string firstLine = line;
    std::string restOfFile;
    int max_number_of_lines_to_display=10;
    while (getline(file, line) && max_number_of_lines_to_display-- > 0) {
        restOfFile += line + "\n";
    }
    file.close();

    std::cout << "File contents:\n" << firstLine << "\n" << restOfFile;
    std::cout << "\nDo you really want to delete all contents except the first line? (yes/no): ";
    std::string confirmation;
    std::cin >> confirmation;

    if (confirmation != "yes") {
        std::cout << "Operation canceled." << std::endl;
        return;
    }

    std::ofstream outFile(filename, std::ios::trunc);
    if (outFile.is_open()) {
        outFile << firstLine << std::endl;
        outFile.close();
        std::cout << "File " << filename << " has been cleared except for the first line." << std::endl;
    } else {
        std::cerr << "Unable to open file " << filename << std::endl;
    }
}

WEIGHTS::WEIGHTS()
{
    for(int piece=0;piece<7;piece++)
    for(int i=0;i<8;i++)
    for(int j=0;j<8;j++)
    {
        if(piece==0 || piece==6)
        {
            piece_table_value_opening[0][i*8+j] = i*30;              
            piece_table_value_opening[6][i*8+j] = (7-i)*30;
            piece_table_value_endgame[0][i*8+j] = i*i*5;
            piece_table_value_endgame[6][i*8+j] = (7-i)/(7-i)*5;
        }
        else
        {
            if(i==1 || i==6)
            {
                piece_table_value_opening[piece][i*8+j] = 10;
                piece_table_value_endgame[piece][i*8+j] = 5;//this is half, of the avove, in an endgame the positioning of a pice is less important, compaed to the middlegame. the endgme, vs middlegame is to be computed by the enemys material, for each colour
            }
            if(i==2 || i==5)
            {
                piece_table_value_opening[piece][i*8+j] = 15;
                piece_table_value_endgame[piece][i*8+j] = 8;
            }
            if(i==3 || i==4)
            {
                piece_table_value_opening[piece][i*8+j] = 20;
                piece_table_value_endgame[piece][i*8+j] = 10;

            }
        }
        
        if(j==1 || j==6)
        {
            piece_table_value_opening[piece][i*8+j] += 20;
            piece_table_value_endgame[piece][i*8+j] += 4;
        } 
        if(j==2 || j==5)
        {
            piece_table_value_opening[piece][i*8+j] += 40;
            piece_table_value_endgame[piece][i*8+j] += 14;
        }
        if(j==3 || j==4)
        {
            piece_table_value_opening[piece][i*8+j] += 60;
            piece_table_value_endgame[piece][i*8+j] += 24;
        }
        
        if(piece==5)
        {
            piece_table_value_opening[piece][i*8+j] = -piece_table_value_opening[piece][i*8+j];
            piece_table_value_endgame[piece][i*8+j] =  2*piece_table_value_endgame[piece][i*8+j];//redundant, but to clarify, in the endgame the king belongs in the center
        }

    }

    //adjust rook values
    for(int i=0;i<8;i++)
    for(int j=0;j<8;j++)
    {
        piece_table_value_opening[1][i*8+j] = std::min(abs(i-4),abs(i-3))*10;// the further out the better
        piece_table_value_endgame[1][i*8+j] = std::min(abs(i-4),abs(i-3))*5;
        if(j==1 || j==6)
        {
            piece_table_value_opening[1][i*8+j] += 8;
            piece_table_value_endgame[1][i*8+j] += 4;
        } 
        if(j==2 || j==5)
        {
            piece_table_value_opening[1][i*8+j] += 16;
            piece_table_value_endgame[1][i*8+j] += 8;
        }
        if(j==3 || j==4)
        {
            piece_table_value_opening[1][i*8+j] += 24;
            piece_table_value_endgame[1][i*8+j] += 12;
        }

    }

    //adjust bishop values
    for(int i=0;i<8;i++)
    for(int j=0;j<8;j++)
    {   
        piece_table_value_opening[3][i*8+j] = std::min(abs(i-3),abs(i-4))*10;// the further out the better
        piece_table_value_endgame[3][i*8+j] = std::min(abs(i-3),abs(i-4))*5;
        if(j==1 || j==6)
        {
            piece_table_value_opening[3][i*8+j] += 8;
            piece_table_value_endgame[3][i*8+j] += 4;
        }
        if(j==2 || j==5)
        {
            piece_table_value_opening[3][i*8+j] += 16;
            piece_table_value_endgame[3][i*8+j] += 8;
        }
        if(j==3 || j==4)
        {
            piece_table_value_opening[3][i*8+j] += 24;
            piece_table_value_endgame[3][i*8+j] += 12;
        }
        if(i==0 || i==7)
        {
            piece_table_value_opening[3][i*8+j] -=20;
            piece_table_value_endgame[3][i*8+j] -= 4;
        }
        
    }

    //queen in the openign is mix, of rook and bishop
    for(int i=0;i<8;i++)
    for(int j=0;j<8;j++)
    {
        piece_table_value_opening[4][i*8+j] = (piece_table_value_opening[1][i*8+j]+piece_table_value_opening[3][i*8+j])/2;
    }
    skip_depth_decrease_threshold = 50;
    value_of_attacked_square = 2;//number of moves avaliable
    check_value = 200;
    piece_value[0] = 100;
    piece_value[1] = 500;
    piece_value[2] = 300;
    piece_value[3] = 300;
    piece_value[4] = 900;
    piece_value[5] = 350;
    offensive_value[0] = 100;
    offensive_value[1] = 500;
    offensive_value[2] = 300;
    offensive_value[3] = 300;
    offensive_value[4] = 900;
    offensive_value[5] = 350;
    defensive_value[0] = 150;
    defensive_value[1] = 400;
    defensive_value[2] = 300;
    defensive_value[3] = 300;
    defensive_value[4] = 500;
    defensive_value[5] = 350;
    king_safety_value = 10;
    punishment_for_double_pawn = 10;
    punishment_for_isolated_pawn = 10;
    punishment_for_trippled_pawn = 30;//also get punishmeht for doubles pawns
    pawn_supporting_value = 15;
    value_of_king_safety_for_sorting = 50;//this is a factor!//it should not be changed, untill




};

//other functions of WEIGHTS

void reset_weights_txt_and_History_of_Weight()
    {
        WEIGHTS_OG.clearFileExceptFirstLine();
        WEIGHTS_OG.print_values_to_file("weights.txt");
    }



















#endif // WEIGHTS_CPP





















#endif // Weights_CPP