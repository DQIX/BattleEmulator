#include "InputBuilder.h"

#include "BattleEmulator.h"

void InputBuilder::push(int damage) {
    InputEntry entry;
    entry.damage = damage;

    entry.candidates.push_back(300);

    if (entry.candidates.empty()) {
        std::cerr << "WARNING: A damage value of 0 " << damage << " has no applicable range\n";
    }
    inputs.push_back(entry);
}

void InputBuilder::clear() {
    inputs.clear();
}


std::vector<ResultStructure> InputBuilder::makeStructure() {
    int ambiguousCount = 0;
    for (const auto &entry: inputs) {
        if (entry.candidates.size() > 1)
            ++ambiguousCount;
    }
    if (ambiguousCount >= 4) {
        throw std::runtime_error("Error: Risk of combination explosion with three or more ambiguous inputs.\n");
    }

    std::vector<ResultStructure> results;
    ResultStructure current;
    generateCombinations(0, current, results);
    return results;
}

void InputBuilder::generateCombinations(size_t index, ResultStructure current, std::vector<ResultStructure> &results) {
    if (index == inputs.size()) {
        results.push_back(current);
        return;
    }

    // 入力の順番情報を保持するため、各入力のダメージを1度だけ追加
    current.AII_damage[current.AII_damageCounter++] = inputs[index].damage;

    const InputEntry &entry = inputs[index];
    for (int candidate: entry.candidates) {
        ResultStructure next = current; // 既にAII_damageが追加済み
        if (candidate == BattleEmulator::ATTACK_ENEMY || candidate == BattleEmulator::KASAP || candidate ==
            BattleEmulator::DECELERATLE || candidate == 300) {
            next.Edamage[next.EdamageCounter++] = entry.damage;
        } else {
            next.Aactions[next.AactionsCounter++] = candidate;
            next.Adamage[next.AdamageCounter++] = entry.damage;
        }
        generateCombinations(index + 1, next, results);
    }
}

