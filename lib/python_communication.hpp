#ifndef PYTHON_COMMUNICATION_HPP
#define PYTHON_COMMUNICATION_HPP
#include<iostream>
#include<fstream>
#include<string>
#include"../src/Bitboard_initialisations.cpp"

FILE* open_python_script(std::string script_name ="display_board.py", std::string FEN=Standart_FEN);

void write_to_python_script(FILE* pipe, std::string uci);

void close_python_script(FILE* pipe);


















#endif // PYTHON_COMMUNICATION_HPP