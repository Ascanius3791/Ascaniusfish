#ifndef PYTHON_COMMUNICATION_CPP
#define PYTHON_COMMUNICATION_CPP
#include"../lib/python_communication.hpp"

FILE* open_python_script(std::string script_name, std::string FEN) {
    std::string command = "python3 " + script_name + " \"" + FEN + "\"";
    FILE* pipe = popen(command.c_str(), "w");
    if (!pipe) {
        std::cerr << "Failed to start Python script" << std::endl;
    }
    return pipe;
}

void write_to_python_script(FILE* pipe, std::string uci) {
    if (pipe) {
        fprintf(pipe, "%s\n", uci.c_str());
        fflush(pipe);
    } else {
        std::cerr << "Pipe is not open" << std::endl;
    }
}

void close_python_script(FILE* pipe) {
    if (pipe) {
        pclose(pipe);
    } else {
        std::cerr << "Pipe is not open" << std::endl;
    }
}















#endif // PYTHON_COMMUNICATION_CPP