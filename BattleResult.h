//
// Created by Owner on 2024/04/13.
//

#ifndef NEWDIRECTORY_BATTLERESULT_H
#define NEWDIRECTORY_BATTLERESULT_H

class BattleResult{
public:
	// 各メンバの内容を 0 にリセットする clear 関数
	void clear(){
		position = 0;
		turn = 0;
	}


	static void
	add(BattleResult* obj1, int action, int damage, bool isEnemy, int AtkBuffTurn, int BuffTurns, int MagicMirrorTurn, int turn,
	    bool player0_has_initiative, int ehp, int ahp, uint64_t nowState, int scTurn, int amp, int defenseFlag){
		if(!obj1) return; // ← これが最重要
		const int pos = obj1->position;
		obj1->actions[pos] = action;
		obj1->damages[pos] = damage;
		obj1->isEnemy[pos] = isEnemy;
		obj1->AtkBuffTurns[pos] = AtkBuffTurn;
		obj1->BuffTurnss[pos] = BuffTurns;
		obj1->MagicMirrorTurns[pos] = MagicMirrorTurn;
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
	int actions[1000] = {};
	int damages[1000] = {};
	int isEnemy[1000] = {};
	int AtkBuffTurns[1000] = {};
	int BuffTurnss[1000] = {};
	int MagicMirrorTurns[1000] = {};
	int turns[1000] = {};
	bool initiative[1000] = {};
	int ehp[1000] = {};
	int ahp[1000] = {};
	int scTurn[1000] = {};
	int amp[1000] = {};
	int defenseFlag[1000] = {};
	uint64_t state[1000] = {};
	int actorIndex[1000] = {};
	int actorMp[1000] = {};
	int aiResourceGateMask[1000] = {};
	int aiOriginalSlot[1000] = {};
	int aiResolvedSlot[1000] = {};
};

#endif //NEWDIRECTORY_BATTLERESULT_H
