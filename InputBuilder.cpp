#include "InputBuilder.h"

#include "BattleEmulator.h"

/**
 * ダメージ値と接頭辞を基に入力エントリを生成し、候補リストを構築して内部リストに追加します。
 *
 * @param damage ダメージ値を表す整数。特定の値が特定のアクションを決定します。
 *               -5: 特やくそう
 *               -2: ボミオス
 *               -3: ルカナン
 *               -4: あまいいき
 *                0: 敵への攻撃
 * @param prefix キャラクターの接頭辞を表す文字。
 *               'm': 睡眠状態を表すアクション。
 *               'a': 指定された条件に応じて、味方攻撃または敵攻撃を決定。
 */
void InputBuilder::push(int damage, const char prefix) {
    InputEntry entry;
    entry.damage = damage;

    if (prefix == 'd') {
        entry.candidates.push_back(BattleEmulator::SPECIAL_ANTIDOTE);
    }else if (damage == -5) {
        entry.candidates.push_back(BattleEmulator::SPECIAL_MEDICINE);
    } else if (damage == -2) {
        entry.candidates.push_back(BattleEmulator::DECELERATLE);
    } else if (damage == -3) {
        entry.candidates.push_back(BattleEmulator::KASAP);
    } else if (damage == -4) {
        entry.candidates.push_back(BattleEmulator::SWEET_BREATH);
    } else if (damage == 0) {
        entry.candidates.push_back(BattleEmulator::ATTACK_ENEMY);
    } else if (prefix == 'm') {
        entry.candidates.push_back(BattleEmulator::SLEEPING);
    } else

#if defined(BattleEmulatorLV19)
    if (prefix == 'a') {
        if (damage > 30 && damage <= 60) {
            entry.candidates.push_back(BattleEmulator::MIRACLE_SLASH);
        } else {
            entry.candidates.push_back(BattleEmulator::ATTACK_ALLY);
        }
    } else {
        entry.candidates.push_back(BattleEmulator::ATTACK_ENEMY);
    }
#elif defined(BattleEmulatorLV15) || defined(BattleEmulatorLV13)
        if (prefix == 'a') {
            entry.candidates.push_back(BattleEmulator::ATTACK_ALLY);
        } else {
            entry.candidates.push_back(BattleEmulator::ATTACK_ENEMY);
        }
#endif

    if (entry.candidates.empty()) {
        std::cerr << "WARNING: A damage value of 0 " << damage << " has no applicable range\n";
    }
    inputs.push_back(entry);
}


void InputBuilder::clear() {
    inputs.clear();
}

/**
 * 入力データから構造体の組み合わせを生成します。各入力エントリの候補数を確認し、
 * 曖昧な候補が4つ以上存在する場合は例外をスローします。
 * また、有効な候補の全組み合わせを再帰的手法で構築します。
 *
 * @return 構造体の全ての可能な組み合わせを格納したベクター。
 *         各構造体は計算結果やアクション情報を含みます。
 * @throws std::runtime_error 曖昧候補が4つ以上の場合にスローされ、
 *                            組み合わせ爆発のリスクを軽減します。
 */
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

/**
 * インデックスに基づいて入力組み合わせを生成し、結果リストに追加します。
 * 再帰的に実行され、すべての可能な組み合わせを列挙します。
 *
 * @param index 現在処理中の入力エントリのインデックス。
 * @param current 現在の進行中の結果構造。
 *                AII_damage配列はすべての入力ダメージを順番通りに保持します。
 *                Edamage配列は敵側に影響を与えるダメージを保持します。
 *                Aactionsは味方行動を表すIDを保持し、
 *                Adamageはその行動に関連づけられたダメージを保持します。
 * @param results すべての生成された結果構造を保存する出力リスト。
 */
void InputBuilder::generateCombinations(size_t index, ResultStructure current, std::vector<ResultStructure> &results) {
    if (index == inputs.size()) {
        results.push_back(current);
        return;
    }

    // 入力の順番情報を保持するため、各入力のダメージを1度だけ追加
    if (inputs[index].damage != -16) {
        current.AII_damage[current.AII_damageCounter++] = inputs[index].damage;
    }

    const InputEntry &entry = inputs[index];
    for (int candidate: entry.candidates) {
        ResultStructure next = current; // 既にAII_damageが追加済み
        if (candidate == BattleEmulator::ATTACK_ENEMY || candidate == BattleEmulator::KASAP || candidate ==
            BattleEmulator::DECELERATLE || candidate == BattleEmulator::SWEET_BREATH) {
            next.Edamage[next.EdamageCounter++] = entry.damage;
        } else {
            next.Aactions[next.AactionsCounter++] = candidate;
            next.Adamage[next.AdamageCounter++] = entry.damage;
        }
        generateCombinations(index + 1, next, results);
    }
}
