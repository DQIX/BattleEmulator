#include "InputBuilder.h"

#include "BattleEmulator.h"

void InputBuilder::push(int damage, const char prefix) {
    InputEntry entry;
    entry.damage = damage;

    if (prefix == 'd') {
        entry.candidates.push_back(BattleEmulator::DEFENCE);
    } else if (prefix == 'h') {
        if (damage == 0) {
            push(-17, 'n'); //さみだれづきフォローアップ
            entry.candidates.push_back(BattleEmulator::HEAL_ENEMY);
        } else {
            push(-5, 'n');
            entry.candidates.push_back(BattleEmulator::HEAL);
        }
    } else if (prefix == 'y') {
        push(-15, 'n');
        entry.candidates.push_back(BattleEmulator::MEDICINAL_HERBS);
    } else if (prefix == 'a') {
        push(-6, 'n'); //攻撃フォローアップ
        entry.candidates.push_back(BattleEmulator::ATTACK_ALLY);
    } else if (prefix == 's' || prefix == 'm') {
        push(-10, 'n'); //さみだれづきフォローアップ
        entry.candidates.push_back(BattleEmulator::MULTITHRUST_ENEMY);
    } else if (prefix == 'i' || prefix == 'c') {
        push(-12, 'n'); //さみだれづきフォローアップ
        entry.candidates.push_back(BattleEmulator::BOLT_CUTTER);
    } else if (damage >= 28) {
        push(-17, 'n'); //さみだれづきフォローアップ
        entry.candidates.push_back(BattleEmulator::HEAL_ENEMY);
    } else if (prefix == 'n' || prefix == '\0') {
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
    if (!(inputs[index].damage == -11)) {
        current.AII_damage[current.AII_damageCounter++] = inputs[index].damage;
    }

    const InputEntry &entry = inputs[index];
    for (int candidate: entry.candidates) {
        ResultStructure next = current; // 既にAII_damageが追加済み
        if (candidate <= -6) {
        } else if (candidate == BattleEmulator::ATTACK_ENEMY || candidate == BattleEmulator::DRAIN_MAGIC || candidate ==
                   BattleEmulator::BUFF_ENEMY || candidate == BattleEmulator::UNKNOWN_ACTION || candidate ==
                   BattleEmulator::MULTITHRUST_ENEMY || candidate == BattleEmulator::BOLT_CUTTER || candidate ==
                   BattleEmulator::HEAL_ENEMY) {
            next.Edamage[next.EdamageCounter++] = entry.damage;
        } else {
            next.Aactions[next.AactionsCounter++] = candidate;
            next.Adamage[next.AdamageCounter++] = entry.damage;
        }
        generateCombinations(index + 1, next, results);
    }
}
