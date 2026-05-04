//
// Created by ESv87g9gvea4 on 2025/03/24.
//

#ifndef INPUTBUILDER_H
#define INPUTBUILDER_H


#include <iostream>
#include <vector>

#include "ResultStructure.h"

class InputBuilder {
public:
    static constexpr int TYPE_SPECIAL_MEDICINE = -5;
    static constexpr int TYPE_ATTACK = -6;
    static constexpr int TYPE_SPECIAL_ANTIDOTE = -10;
    static constexpr int TYPE_BUFF_ALLY = -7;
    static constexpr int TYPE_PSYCHE_UP_ALLY = -8;
    static constexpr int TYPE_PRE_SPECIAL_MEDICINE = -4;
    static constexpr int TYPE_MULTITHRUST = -9;
    static constexpr int TYPE_INACTIVE = -11;
    static constexpr int TYPE_EERIE_LIGHT = -12;
    static constexpr int TYPE_WEB = -12;

    static constexpr char PREFIX_SPECIAL_MEDICINE = 'h';
    static constexpr char PREFIX_ATTACK = 't';
    static constexpr char PREFIX_SPECIAL_ANTIDOTE = 'd';
    static constexpr char PREFIX_BUFF_ALLY = 'b';
    static constexpr char PREFIX_PSYCHE_UP_ALLY = 'p';
    static constexpr char PREFIX_MULTITHRUST = 'm';
    static constexpr char PREFIX_INACTIVE = 'y';
    static constexpr char PREFIX_EERIE_LIGHT = 'a';
    static constexpr char PREFIX_WEB = 'r';


    void push(int damage, char prefix);

    std::vector<ResultStructure> makeStructure();
    void clear();

private:
    std::vector<InputEntry> inputs;

    void generateCombinations(size_t index, ResultStructure current, std::vector<ResultStructure> &results);
};

#endif //INPUTBUILDER_H
