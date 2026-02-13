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
class BattleResult {
public:
    // 各メンバの内容を 0 にリセットする clear 関数
    void clear() {
        position = 0;
        turn = 0;
    }

    static void
    add(BattleResult* obj, int action, int damage, bool isEnemy, int BuffTurns, int PoisonTurns,
        int speedTurn, int turn,
        bool player0_has_initiative, int ehp, int ahp, uint64_t nowState, int scTurn, int amp, bool defenseFlag, bool sleepFlag) {
        if (!obj) return; // ← これが最重要

        const int pos = obj->position;
        obj->actions[pos] = action;
        obj->damages[pos] = damage;
        obj->isEnemy[pos] = isEnemy;
        obj->BuffTurnss[pos] = BuffTurns;
        obj->PoisonTurns[pos] = PoisonTurns;
        obj->SpeedTurn[pos] = speedTurn;
        obj->turns[pos] = turn;
        obj->initiative[pos] = player0_has_initiative;
        obj->ehp[pos] = ehp;
        obj->ahp[pos] = ahp;
        obj->state[pos] = nowState;
        obj->scTurn[pos] = scTurn;
        obj->amp[pos] = amp;
        obj->defenseFlag[pos] = defenseFlag;
        obj->sleepFlag[pos] = sleepFlag;
        obj->turn = turn;
        obj->position = pos + 1;
    }

    int position = 0;
    int turn = 0;
    int actions[1000] = {};
    int damages[1000] = {};
    int isEnemy[1000] = {};
    int BuffTurnss[1000] = {};
    int PoisonTurns[1000] = {};
    int SpeedTurn[1000] = {};
    int turns[1000] = {};
    bool initiative[1000] = {};
    int ehp[1000] = {};
    int ahp[1000] = {};
    int scTurn[1000] = {};
    int amp[1000] = {};
    uint64_t state[1000] = {};
    bool defenseFlag[1000] = {};
    bool sleepFlag[1000] = {};
};

#endif //NEWDIRECTORY_BATTLERESULT_H
