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
    void push(int damage, char prefix);

    void clear();

    std::vector<ResultStructure> makeStructure();

private:
    std::vector<InputEntry> inputs;

    void generateCombinations(size_t index, ResultStructure current, std::vector<ResultStructure> &results);
};

#endif //INPUTBUILDER_H
