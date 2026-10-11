//
// Created by Owner on 2024/04/13.
//

#ifndef NEWDIRECTORY_BATTLERESULT_H
#define NEWDIRECTORY_BATTLERESULT_H

class BattleResult{
public:
	static constexpr int Capacity = 3 * 350;
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
	int actions[Capacity] = {};
	int damages[Capacity] = {};
	int isEnemy[Capacity] = {};
	int AtkBuffTurns[Capacity] = {};
	int BuffTurnss[Capacity] = {};
	int MagicMirrorTurns[Capacity] = {};
	int turns[Capacity] = {};
	bool initiative[Capacity] = {};
	int ehp[Capacity] = {};
	int ahp[Capacity] = {};
	int scTurn[Capacity] = {};
	int amp[Capacity] = {};
	int defenseFlag[Capacity] = {};
	uint64_t state[Capacity] = {};
};

#endif //NEWDIRECTORY_BATTLERESULT_H
