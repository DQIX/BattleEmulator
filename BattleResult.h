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
    add(std::optional<BattleResult> &obj1, int action, int damage, bool isEnemy, int BuffTurns, int PoisonTurns, int speedTurn, int turn,
        bool player0_has_initiative, int ehp, int ahp, uint64_t nowState, int scTurn, int amp,bool defenseFlag, bool isInactiveFlag) {
        if (obj1.has_value()) {
            BattleResult& obj = obj1.value();
            obj.actions[obj.position] = action;
            obj.damages[obj.position] = damage;
            obj.isEnemy[obj.position] = isEnemy;
            obj.BuffTurnss[obj.position] = BuffTurns;
            obj.PoisonTurns[obj.position] = PoisonTurns;
            obj.SpeedTurn[obj.position] = speedTurn;
            obj.turns[obj.position] = turn;
            obj.initiative[obj.position] = player0_has_initiative;
            obj.ehp[obj.position] = ehp;
            obj.ahp[obj.position] = ahp;
            obj.state[obj.position] = nowState;
            obj.scTurn[obj.position] = scTurn;
            obj.amp[obj.position] = amp;
            obj.defenseFlag[obj.position] = defenseFlag;
            obj.isInactiveFlag[obj.position] = isInactiveFlag;
            obj.turn = turn;
            obj.position++;
        }
    }

    int position = 0;
    int turn = 0;
    int actions[400] = {};
    int damages[400] = {};
    int isEnemy[400] = {};
    int BuffTurnss[400] = {};
    int PoisonTurns[400] = {};
    int SpeedTurn[400] = {};
    int turns[400] = {};
    bool initiative[400] = {};
    int ehp[400] = {};
    int ahp[400] = {};
    int scTurn[400] = {};
    int amp[400] = {};
    uint64_t state[400] = {};
    bool defenseFlag[400] = {};
    bool isInactiveFlag[400] = {};
};

#endif //NEWDIRECTORY_BATTLERESULT_H
