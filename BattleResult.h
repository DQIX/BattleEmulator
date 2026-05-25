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


	static void add(BattleResult* obj1, int action, int damage, bool isEnemy, int turn,
	    bool player0_has_initiative, int ehp, int ahp, int amp){
		if(!obj1) return; // ← これが最重要
		const int pos = obj1->position;
		obj1->actions[pos] = action;
		obj1->damages[pos] = damage;
		obj1->isEnemy[pos] = isEnemy;
		obj1->turns[pos] = turn;
		obj1->initiative[pos] = player0_has_initiative;
		obj1->ehp[pos] = ehp;
		obj1->ahp[pos] = ahp;
		obj1->amp[pos] = amp;
		obj1->turn = turn;
		obj1->position = pos + 1;
	}

	int position = 0;
	int turn = 0;
	int actions[100] = {};
	int damages[100] = {};
	int isEnemy[100] = {};
	int turns[100] = {};
	bool initiative[100] = {};
	int ehp[100] = {};
	int ahp[100] = {};
	int scTurn[100] = {};
	int amp[100] = {};
	uint64_t state[100] = {};
};

#endif //NEWDIRECTORY_BATTLERESULT_H
