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
    static constexpr int TYPE_PRE_SPECIAL_MEDICINE = -4;
    static constexpr int TYPE_MAGIC_BARRIER = -6;
    static constexpr int TYPE_WHIRLWIND = -7;
    static constexpr int TYPE_BUFF_ALLY = -8;
    static constexpr int TYPE_PSYCHE_UP_ALLY = -9;
    static constexpr int TYPE_MULTITHRUST = -10;
    static constexpr int TYPE_DAZZLE = -11;
    static constexpr int TYPE_BOOM = -12;
    static constexpr int TYPE_INACTIVE = -13;

    static constexpr char PREFIX_SPECIAL_MEDICINE = 'h';
    static constexpr char PREFIX_MAGIC_BARRIER = 'm';
    static constexpr char PREFIX_BUFF_ALLY = 'b';
    static constexpr char PREFIX_PSYCHE_UP_ALLY = 'Q';
    static constexpr char PREFIX_MULTITHRUST = 'm';
    static constexpr char PREFIX_WHIRLWIND = 't';
    static constexpr char PREFIX_DAZZLE = 'd';
    static constexpr char PREFIX_BOOM = 'i';
    static constexpr char PREFIX_INACTIVE = 'y';


    void push(int damage, char prefix);
    void clear();

    std::vector<ResultStructure> makeStructure();

private:
    std::vector<InputEntry> inputs;

    void generateCombinations(size_t index, ResultStructure current, std::vector<ResultStructure> &results);
};

#endif //INPUTBUILDER_H
