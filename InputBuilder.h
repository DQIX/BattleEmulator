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
    static constexpr int TYPE_ATTACK_ALLY = -3;
    static constexpr int TYPE_HEAL = -2;
    static constexpr int TYPE_PSYCHE_UP_ENEMY = -6;
    static constexpr int TYPE_BUFF_ALLY = -7;
    static constexpr int TYPE_PSYCHE_UP_ALLY = -8;
    static constexpr int TYPE_MULTITHRUST = -9;
    static constexpr int TYPE_FULLHEAL = -10;
    static constexpr int TYPE_MORE_HEAL = -11;
    static constexpr int TYPE_BURNING_BREATH = -12;
    static constexpr int TYPE_HEART_BREAKER = -13;
    static constexpr int TYPE_DESPERATE_ATTACK = -14;
    static constexpr int TYPE_CRITICAL_ATTACK = -15;
    static constexpr int TYPE_CHAIN_SWING = -15;

    static constexpr char PREFIX_SPECIAL_MEDICINE = 'h';
    static constexpr char PREFIX_TYPE_MORE_HEAL = 'm';
    static constexpr char PREFIX_TYPE_FULL_HEAL = 'f';
    static constexpr char PREFIX_PSYCHE_UP_ENEMY = 'p';
    static constexpr char PREFIX_BUFF_ALLY = 'b';
    static constexpr char PREFIX_PSYCHE_UP_ALLY = 'Q';
    static constexpr char PREFIX_CRITICAL_ATTACK = 'd';
    static constexpr char PREFIX_CHAIN_SWING = 't';
    static constexpr char PREFIX_MULTITHRUST = 'm';
    //static constexpr char PREFIX_MULTITHRUST = 'm';


    void push(int damage, char prefix);

    std::vector<ResultStructure> makeStructure();
    void clear();

private:
    std::vector<InputEntry> inputs;

    void generateCombinations(size_t index, ResultStructure current, std::vector<ResultStructure> &results);
};

#endif //INPUTBUILDER_H
