// OWNERSHIP=Ascanius
#ifndef SAFTY_CHECKS_CPP
#define SAFTY_CHECKS_CPP

#include "../lib/saefty_checks.hpp"

void check_for_symmetrical_evaluation(const BB* const original)
{
    BB* temp = new BB;
    copy_BB(original,temp);
    temp->white_move=!temp->white_move;
    if(basic_eval(original)!=basic_eval(temp))
    {
        std::cout << "The evaluation is not symmetrical" << std::endl;
        std::cout << "The evaluation of the original is: " << basic_eval(original) << std::endl;
        std::cout << "The evaluation of the temp is: " << basic_eval(temp) << std::endl;
        std::cout << "The board is: " << std::endl;
        print(original->Board);
        std::cout << "The temp board is: " << std::endl;
        print(temp->Board);
        exit(0);
    }
    delete temp;
}

void saefty_checks(const BB* const original)
{
    if(original)
    {
        check_for_symmetrical_evaluation(original);
    }

}














#endif
