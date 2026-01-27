#include "InputBuilder.h"

#include "BattleEmulator.h"

void InputBuilder::push(int damage, const char prefix) {
    InputEntry entry;
    entry.damage = damage;

    if (damage == -2) {
        entry.candidates.push_back(BattleEmulator::DRAIN_MAGIC);
    }else if (damage == -3) {
        entry.candidates.push_back(BattleEmulator::BUFF_ENEMY);
    }else if (prefix == 'h') {
        entry.candidates.push_back(BattleEmulator::SPECIAL_MEDICINE);
    }else if (prefix == 'a') {
        entry.candidates.push_back(BattleEmulator::ATTACK_ALLY);
    }else if (prefix == 't') {
        entry.candidates.push_back(-6); //攻撃フォローアップ
    }else if (prefix == 'n') {
        entry.candidates.push_back(BattleEmulator::UNKNOWN_ACTION); //不明
    }

    if (entry.candidates.empty()) {
        std::cerr << "WARNING: A damage value of " << damage << " has no applicable range\n";
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
        if (candidate == -6) {

        }else
        if (candidate == BattleEmulator::ATTACK_ENEMY || candidate == BattleEmulator::DRAIN_MAGIC || candidate ==
            BattleEmulator::BUFF_ENEMY || candidate == BattleEmulator::UNKNOWN_ACTION) {
            next.Edamage[next.EdamageCounter++] = entry.damage;
        } else {
            next.Aactions[next.AactionsCounter++] = candidate;
            next.Adamage[next.AdamageCounter++] = entry.damage;
        }
        generateCombinations(index + 1, next, results);
    }
}

