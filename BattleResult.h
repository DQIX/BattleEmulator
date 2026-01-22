//
// Created by Owner on 2024/04/13.
//

#ifndef NEWDIRECTORY_BATTLERESULT_H
#define NEWDIRECTORY_BATTLERESULT_H
#include <cassert>

class BattleResult {

public:
    // 各メンバの内容を 0 にリセットする clear 関数
    inline void clear() {
        position = 0;
        turn = 0;
    }

    static inline void
    add(
        BattleResult* obj,
        int action,
        int damage,
        bool isEnemy,
        int turn,
        bool player0_has_initiative,
        int ehp,
        int ahp,
        uint64_t nowState,
        int scTurn,
        int amp
    ) {
        if (!obj) return;  // ← これが最重要

        const int pos = obj->position;

        obj->actions[pos]    = action;
        obj->damages[pos]    = damage;
        obj->isEnemy[pos]    = isEnemy;
        obj->turns[pos]      = turn;
        obj->initiative[pos]= player0_has_initiative;
        obj->ehp[pos]        = ehp;
        obj->ahp[pos]        = ahp;
        obj->state[pos]      = nowState;
        obj->scTurn[pos]     = scTurn;
        obj->amp[pos]        = amp;

        obj->turn = turn;
        obj->position = pos + 1;
    }

    int position = 0;
    int turn = 0;
    int actions[1000] = {};
    int damages[1000] = {};
    int isEnemy[1000] = {};
    int turns[1000] = {};
    bool initiative[1000] = {};
    int ehp[1000] = {};
    int ahp[1000] = {};
    int scTurn[1000] = {};
    int amp[1000] = {};
    uint64_t state[1000] = {};
};

#endif //NEWDIRECTORY_BATTLERESULT_H
