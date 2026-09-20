//
// Created by Owner on 2024/04/13.
//

#ifndef NEWDIRECTORY_BATTLERESULT_H
#define NEWDIRECTORY_BATTLERESULT_H

/**
 * @class BattleResult
 * @brief バトルの結果を記録・管理するクラス
 *
 * このクラスは、各ターンにおける行動やダメージ、バフやデバフの状態など、バトルの詳細な結果を記録します。
 * また、記録されたデータを更新するためのメソッドを提供します。
 */
class BattleResult{
public:
    // 349 commands plus the -1 sentinel; at most two records per turn in anonn.
    static constexpr int Capacity = 700;
    // 各メンバの内容を 0 にリセットする clear 関数
    void clear(){
        position = 0;
        turn = 0;
    }

    static void
    add(BattleResult* & obj1, int action, int damage, bool isEnemy, int BuffTurns, int PoisonTurns, int speedTurn,
        int turn,
        bool player0_has_initiative, int ehp, int ahp, uint64_t nowState, int scTurn, int amp, bool defenseFlag){
        if(!obj1) return; // ← これが最重要
        const int pos = obj1->position;

        obj1->actions[pos] = action;
        obj1->damages[pos] = damage;
        obj1->isEnemy[pos] = isEnemy;
        obj1->BuffTurnss[pos] = BuffTurns;
        obj1->PoisonTurns[pos] = PoisonTurns;
        obj1->SpeedTurn[pos] = speedTurn;
        obj1->turns[pos] = turn;
        obj1->initiative[pos] = player0_has_initiative;
        obj1->ehp[pos] = ehp;
        obj1->ahp[pos] = ahp;
        obj1->state[pos] = nowState;
        obj1->scTurn[pos] = scTurn;
        obj1->amp[pos] = amp;
        obj1->defenseFlag[pos] = defenseFlag;
        obj1->turn = turn;
        obj1->position = pos + 1;
    }

    int position = 0;
    int turn = 0;
    int actions[Capacity] = {};
    int damages[Capacity] = {};
    int isEnemy[Capacity] = {};
    int BuffTurnss[Capacity] = {};
    int PoisonTurns[Capacity] = {};
    int SpeedTurn[Capacity] = {};
    int turns[Capacity] = {};
    bool initiative[Capacity] = {};
    int ehp[Capacity] = {};
    int ahp[Capacity] = {};
    int scTurn[Capacity] = {};
    int amp[Capacity] = {};
    uint64_t state[Capacity] = {};
    bool defenseFlag[Capacity] = {};
};

#endif //NEWDIRECTORY_BATTLERESULT_H
