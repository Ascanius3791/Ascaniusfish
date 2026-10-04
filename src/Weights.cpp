// OWNERSHIP=Ascanius
#ifndef Weights_CPP
#define Weights_CPP

#include "../lib/Weights.hpp"
#include "../lib/weight_set.hpp"

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
    
    for(int piece=0;piece<6;piece++)
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

    for(int piece=0;piece<6;piece++)
    for(int i=0;i<8;i++)
    for(int j=0;j<8;j++)
    {
        file << piece_table_value_opening[piece][i*8+j] << " ";

    }
    for(int piece=0;piece<6;piece++)
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

    for(int piece=0;piece<6;piece++)
    for(int i=0;i<8;i++)
    for(int j=0;j<8;j++)
    {
        file >> piece_table_value_opening[piece][i*8+j];
    }
    for(int piece=0;piece<6;piece++)
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
    for(int piece=0;piece<6;piece++)
    for(int i=0;i<8;i++)
    for(int j=0;j<8;j++)
    {
        std::cout << piece_table_value_opening[piece][i*8+j] << " ";
    }
    for(int piece=0;piece<6;piece++)
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

int WEIGHTS::norm_to(const WEIGHTS& W) const
{
    float sum =0;
    sum+=std::pow(skip_depth_decrease_threshold-W.skip_depth_decrease_threshold,2);
    sum+=std::pow(value_of_attacked_square-W.value_of_attacked_square,2);
    sum+=std::pow(check_value-W.check_value,2);
    for(int piece=0;piece<6;piece++)
    for(int i=0;i<8;i++)
    for(int j=0;j<8;j++)
    {
        sum+=std::pow(piece_table_value_opening[piece][i*8+j]-W.piece_table_value_opening[piece][i*8+j],2);
    }
    for(int piece=0;piece<6;piece++)
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
        for(int piece=0;piece<6;piece++)
        for(int i=0;i<64;i++) 
        file << piece_table_value_opening[piece][i] << " ";
        for(int piece=0;piece<6;piece++)
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
    // Everything basic_eval() uses is the default weight set (#85): the weights/wN.txt
    // the Makefile's WEIGHTS_DEFAULT names (WEIGHTS_DEFAULT_FILE), compiled in by
    // lib/weights_default.hpp.
    Weight_Set_Info info;
    std::string error;
    if(!read_weight_set(WEIGHTS_DEFAULT_TEXT, *this, info, error))
    {
        std::cerr << "default weight set " << WEIGHTS_DEFAULT_FILE << ": " << error << std::endl;
        std::exit(1);
    }

    // not in a weight set: basic_eval() does not use them
    skip_depth_decrease_threshold = 50;
    value_of_attacked_square = 2;//number of moves avaliable
    check_value = 200;
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
