//
// Created by Owner on 2024/02/05.
//

#include <cstdint>
#include <vector>
#include <iostream>
#include <cmath>
#include "BattleEmulator.h"

#include <array>

#include "lcg.h"
#include "Player.h"
#include "camera.h"
#include "debug.h"
#include "BattleResult.h"
#include "Equipment.h"
#include "InputBuilder.h"


thread_local int preHP[3] = {0, 0, 0};

/**
 * @brief 状態データから現在のコンボ情報をリセットします。
 *
 * この関数は64ビット整数で管理されている現在の状態データ (NowState) に対して、
 * コンボに関連するビット領域 (bit 40-52) をクリアします。
 *
 * @param NowState 現在の状態を保持する64ビット整数へのポインタ。
 */
void inline BattleEmulator::resetCombo(uint64_t* NowState){
	(*NowState) &= ~(0xFFF00000000);
}


constexpr int kaisinnP = 500;
constexpr int baseHP = 297;
constexpr double ShieldGuardP = 6.5;
constexpr double mitoreP = 0.3060;
constexpr int Ally_Level = 47;
constexpr double Enemy_level = 35.0;
constexpr int DragonSlashKaisinnP = kaisinnP / 2;
constexpr int WooshSlashKaisinnP = 100;
constexpr int multithrust3KaisinnP = DragonSlashKaisinnP / 3;
constexpr int multithrust4KaisinnP = DragonSlashKaisinnP / 4;

constexpr int determineTurn(const int level){
	return (level >= 10 && level <= 24)
		       ? 6
		       : (level >= 25 && level <= 49)
		       ? 7
		       : (level >= 50 && level <= 74)
		       ? 8
		       : (level >= 75 && level <= 99)
		       ? 9
		       : 0;
}

constexpr int SpecialChargeTurns = determineTurn(Ally_Level);


/**
 * @brief 味方のhpを基に、hpテーブルをコンパイル時に生成します。
 *
 * この関数は、基準値 (baseHP) を使用して縮小された割合に基づき整数値のテーブルを計算し、
 * それを配列として返します。
 *
 * @return 生成された9要素の整数配列。
 */
constexpr std::array<int, 9> makeProportionTable3(){
	std::array<int, 9> table{};
	// iを9から1までループし、対応する値を生成する
	for(int i = 9; i >= 1; --i){
		double multiplier = static_cast<double>(i) / 10.0;
		// base * multiplier の結果は正の数なので、static_cast<int>でfloor相当の効果が得られる
		int value = static_cast<int>(baseHP * multiplier);
		table[9 - i] = value + 1;
	}
	return table;
}

/**
 * @brief コンパイル時に生成されたHPの割合テーブル。
 *
 * 基準値 (baseHP) を使用して、割合に基づく9要素の整数値を含む配列。
 * この配列は makeProportionTable3 関数によって計算され、戦闘計算ロジックに利用されます。
 */
constexpr auto proportionTable3 = makeProportionTable3();

/**
 * @brief コンボ攻撃の処理を行い、ダメージ値を計算します。
 *
 * この関数は、現在の攻撃が直前の攻撃と連続しているかを判定し、コンボカウントに応じて
 * ダメージを増幅します。同時に内部状態を更新します。
 *
 * @param Id 攻撃を識別するための整数ID。
 * @param damage 基本となるダメージ値。
 * @param NowState 現在の状態を示す64ビット符号なし整数ポインタ。
 *
 * @return コンボの結果として増幅されたダメージ値。
 */
double BattleEmulator::processCombo(int32_t Id, double damage, uint64_t* NowState, bool update){
	// 上位32〜39ビット：前回の攻撃ID（0xffマスク）
	// 上位40〜43ビット：コンボカウンター（0xfマスク）
	auto previousAttack = ((*NowState) >> 32) & 0xff;
	auto comboCounter = ((*NowState) >> 40) & 0xf;

	if(previousAttack != 0 && previousAttack == Id){
		// 同一攻撃の場合
		if(update){
			++comboCounter;
		}
		// updateがfalseの場合は、実際のカウンターに変動させず、現在の値に対して1回分追加した扱いにする
		const auto effectiveCombo = update
			                            ? static_cast<int>(comboCounter)
			                            : static_cast<int>(comboCounter) + 1;
		if(effectiveCombo == 2){
			damage *= 1.2;
		}
		else if(effectiveCombo == 3){
			damage *= 1.5;
		}
		else if(effectiveCombo >= 4){
			damage *= 2.0;
		}
	}
	else if(update){
		// 異なる攻撃の場合、updateがtrueなら新しい攻撃情報として設定
		previousAttack = Id;
		comboCounter = 1;
	}

	resetCombo(NowState);
	(*NowState) |= (previousAttack << 32);
	(*NowState) |= (comboCounter << 40);
	return damage;
}

/**
 * @brief 指定されたアクションIDに対応するアクション名を取得します。
 *
 * この関数は、渡されたアクションIDに基づいて適切なアクション名を文字列として返します。
 * 対応するIDが存在しない場合は、"Unknown Action" が返されます。
 *
 * @param actionId アクションを識別するID。
 * @return アクションIDに対応するアクション名の文字列。
 */
const char* BattleEmulator::getActionName(int actionId){
	switch(actionId){
		case BattleEmulator::BUFF:
			return "Buff";
		case BattleEmulator::ATTACK_ENEMY:
			return "Attack";
		case BattleEmulator::MEDICINAL_HERBS:
			return "Medicinal Herbs";
		case BattleEmulator::PARALYSIS:
			return "Paralysis";
		case BattleEmulator::CURE_PARALYSIS:
			return "Cure Paralysis";
		case BattleEmulator::ULTRA_HIGH_SPEED_COMBO:
			return "High-Speed Combo";
		case BattleEmulator::SKY_ATTACK:
			return "Sky Attack";
		case BattleEmulator::CRITICAL_ATTACK:
			return "Critical Attack";
		case BattleEmulator::LAUGH:
			return "Laugh";
		case BattleEmulator::DISRUPTIVE_WAVE:
			return "Disruptive Wave";
		case BattleEmulator::BURNING_BREATH:
			return "Burning Breath";
		case BattleEmulator::DARK_BREATH:
			return "Dark Breath";
		case BattleEmulator::MORE_HEAL:
			return "More Heal(hoimu)";
		case BattleEmulator::MIDHEAL:
			return "Mid Heal(behoimi)";
		case BattleEmulator::FREEZING_BLIZZARD:
			return "freezing blizzard";
		case BattleEmulator::MERA_ZOMA:
			return "Mera Zoma";
		case BattleEmulator::DOUBLE_UP:
			return "!Double up";
		case BattleEmulator::MULTITHRUST:
			return "Multithrust";

		case BattleEmulator::ATTACK_ALLY:
			return "Attack";
		case BattleEmulator::HEAL:
			return "Heal";
		case BattleEmulator::DEFENCE:
			return "Defence";

		case BattleEmulator::MAGIC_MIRROR:
			return "magic mirror";

		case BattleEmulator::LIGHTNING_STORM:
			return "Lightning Storm";
		case BattleEmulator::LULLAB_EYE:
			return "Lullab Eye";
		case BattleEmulator::SLEEPING:
			return "Sleeping";
		case BattleEmulator::CURE_SLEEPING:
			return "Cure Sleeping";

		case BattleEmulator::FULLHEAL:
			return "Full heal(behoma)";
		case BattleEmulator::DEFENDING_CHAMPION:
			return "Defense Champion"; //defending champion
		case BattleEmulator::PSYCHE_UP:
		case PSYCHE_UP_ALLY:
			return "Psyche up";
		case BattleEmulator::MEDITATION:
			return "Meditation";
		case BattleEmulator::MAGIC_BURST:
			return "magic Burst";
		case BattleEmulator::RESTORE_MP:
			return "Restore MP";
		case BattleEmulator::MERCURIAL_THRUST:
			return "Mercurial Thrust";
		case BattleEmulator::TURN_SKIPPED:
			return "**Turn Skipped**";

		case BattleEmulator::SAGE_ELIXIR:
			return "Sage Elixir";
		case BattleEmulator::ELFIN_ELIXIR:
			return "Elfin Elixir";
		case BattleEmulator::MAGIC_WATER:
			return "Magic Water";
		case BattleEmulator::GOSPEL_SONG:
			return "gospel song";
		case BattleEmulator::FLEE_ALLY:
			return "Flee";
		case BattleEmulator::POISON_ATTACK:
			return "Poison Attack";
		case BattleEmulator::KASAP:
			return "Kasap(rukanann)";
		case BattleEmulator::SPECIAL_MEDICINE:
			return "Special Medicine";
		case BattleEmulator::MIRACLE_SLASH:
			return "Miracle Slash";
		case BattleEmulator::SWEET_BREATH:
			return "Sweet Breath";
		case BattleEmulator::DECELERATLE:
			return "Deceleratle(bomi)";
		case BattleEmulator::SPECIAL_ANTIDOTE:
			return "!Special Antidote";
		case BattleEmulator::ACROBATIC_STAR:
			return "Acrobatic Star";
		case BattleEmulator::CRACKLE:
		case CRACKLE_ENEMY:
			return "Crackle";
		case WOOSH_ALLY:
			return "Woosh";
		case DRAGON_SLASH:
			return "Dragon Slash";
		case ITEM_USE:
			return "Item Use";
		case CRACK_ALLY:
			return "Crack";
		case DOUBLE_TROUBLE:
			return "Double Trouble";
		case ZAMMLE:
			return "Zammlle";
		case INACTIVE_ENEMY:
			return "Inactive";
		case HEART_BREAKER:
			return "Heart breaker";
		case BLOCKENSPIEL:
			return "Blockenspiel";
		case DESPERATE_ATTACK:
			return "Desperate";
		case INACTIVE_ALLY:
			return "Inactive";
		default:
			return "Unknown Action";
	}
}


thread_local int threadTurnProcessed = 0;
thread_local int startTurn = 0;

void BattleEmulator::resetStartTurn(){
	startTurn = 0;
}

int BattleEmulator::getStartTurn(){
	return startTurn;
}

void BattleEmulator::ResetTurnProcessed(){
	threadTurnProcessed = 0;
}

int BattleEmulator::getTurnProcessed(){
	return threadTurnProcessed;
}

inline void BattleEmulator::processTurn(){
	// ここでturnProcessedをインクリメントする処理を追加
	threadTurnProcessed++;
}


//#endif

constexpr int AttackTable2B[6] = {
	BattleEmulator::CRACKLE_ENEMY, BattleEmulator::PSYCHE_UP,
	BattleEmulator::DOUBLE_TROUBLE, BattleEmulator::PSYCHE_UP,
	BattleEmulator::ATTACK_ENEMY, BattleEmulator::SWITCH_2A,
};
/**
 * @brief バトルシミュレーションのメイン処理。
 *
 * この関数は、プレイヤーと敵の各ターン（forループ1回）ごとに行動を計算し、
 * バトル状態およびダメージ結果を更新します。
 *
 * 各パラメーターの詳細は以下の通りです:
 *
 * @param position
 *   lcg.cppで定義している線形合同法(LCG)の乱数生成に使用する現在の乱数の位置を表すポインタです。
 *   バトルエミュレーション側では直接インクリメントする場合もありますが、各種乱数取得関数に渡すと
 *   自動的にlcg.cpp内でインクリメントされ、乱数生成と連動した値が提供されます。
 *
 * @param RunCount
 *   この関数内のforループの回数として扱われ、1ターンはループ1回分のシミュレーションに相当します。
 *
 * @param Gene
 *   各ターンで味方が実行すべき行動を示す固定長の遺伝子テーブルです。
 *   配列のインデックスはターン数-1に対応しており、固定長(350要素)としてペクターを使わない理由は高速化のためです。
 *
 * @param players
 *   戦闘のhpなどのステータスを保持するプレイヤー（および敵）の配列です。
 *   配列の先頭要素(index=0)は味方、index=1は敵を表し、各ターンで行動・HP更新等が行われます。
 *   これは参照渡しであるため、関数実行後に更新されます。
 *
 * @param result
 *   バトル結果の蓄積先として使用するstd::optional型の参照です。
 *   modeが -1 の場合にBattleResultへ結果を蓄積しますが、空のoptionalを渡すとセグメンテーション違反が発生するため注意が必要です。
 *   また、resultは生成コストがそれなりにかかるため、使用しないシナリオではなるべく避けるよう推奨します。
 *
 * @param seed
 *   ゲームバトルの初期乱数シードで、主にデバッグ時にデバッカーから参照するために使用されています。
 *
 * @param eActions
 *   敵の行動パターンを示す配列ですが、ブランチによっては使用される可能性もあるため、名残的に定義されています。
 *
 * @param damages
 *   mainでchar*[]から適切にパースされ、内部でint[]に変換された中間表現のダメージ配列です。
 *   この配列の各要素により、各ターンの必須の行動が指定されます。
 *   modeが0以上の場合、要求された中間表現と一致しなかった場合には即座にシミュレーションを終了します。
 *
 * @param mode
 *   シミュレーション実行モードを指定する整数値です。詳細は以下の通りです:
 *   - mode >= 0 : 総当たりシミュレーションモード（damagesが使用される）。
 *   - mode == -1: バトル結果がBattleResultに蓄積される記録モード。
 *   - mode == -2: 結果は記録せず、最速実行のみのモード。
 *
 * @param NowState
 *   内部シミュレーション状態をビット単位で管理する64ビット整数へのポインタです。
 *   この状態は内部でのみ管理され、外部から変更されることは想定していません。
 *   各ビットの用途は以下の通りです:
 *      - 0～3   : Current Rotation Table (4bit)
 *      - 4～7   : Rotation Internal State (4bit)
 *      - 8～11  : Free Camera State (4bit)
 *      - 12～31 : Turn Count Processed (20bit)
 *      - 32～39 : Combo Previous Attack Id (2byte)
 *      - 40～47 : Combo Counter (1byte)
 *   合計で6バイト分の情報を管理しています。main.cpp内の説明とも合わせてご参照ください。
 *
 * @return
 *   modeが0以上の場合、実行中の（damagesに沿った）シミュレーションが要求に一致している場合はtrue、
 *   それ以外の場合はfalseを返します。（modeが -1 または -2の場合、常にfalse）
 */
bool BattleEmulator::Main(int* position, int RunCount, const int32_t Gene[350], Player* players,
                          BattleResult* result,
                          uint64_t seed, const int eActions[350], const int damages[350], int mode,
                          uint64_t* NowState){
	resetCombo(NowState);
	bool player0_has_initiative = false;
	int genePosition = 0;
	int exCounter = 0;
	uint64_t tmpState = 0ull;
	int defenseFlag = false;

	auto startPos = static_cast<int>(((*NowState) >> 12) & 0xfffff);
	if(startPos != 0){
		startPos++;
		RunCount += startPos;
	}
	else{
		startPos = 1;
		RunCount++;
	}


	for(int counterJ = startPos; counterJ < RunCount; ++counterJ){
		processTurn();
		if(genePosition != -1){
			genePosition = counterJ - 1;
		}
		//現在ターンを保存
		(*NowState) &= ~0xFFFFF000;
		(*NowState) |= (static_cast<uint64_t>(counterJ) << 12);

		if(players[0].dirtySpecialCharge){
			players[0].specialCharge = false;
			players[0].dirtySpecialCharge = false;
		}
		players[0].specialChargeTurn--;
		if(players[0].specialChargeTurn == -1){
			players[0].specialCharge = false;
		}

		resetCombo(NowState);

		tmpState = (*NowState);

#ifdef DEBUG2
		std::cout << "c: " << counterJ << ", " << (*position) << std::endl;
		if((*position) == 99){
			std::cout << "!!" << std::endl;
		}
#endif
		int ehp = players[1].hp;
		int ahp = players[0].hp;

		players[0].defence = 1.0;

		int32_t actions[3] = {0, 0, 0};
		int actionsPosition = 0;
		double speed0 = players[0].speed * lcg::floatRand051_1(position);
		double speed1 = players[1].speed * lcg::floatRand051_1(position);

		// 素早さを比較
		if(speed0 > speed1){
			player0_has_initiative = true;
		}
		else{
			player0_has_initiative = false;
		}

		auto counter = 0;
		int enemyAction = 0;
		int preAction = 0;
		enemyAction = ProcessEnemyRandomAction44(position);

		if(enemyAction == ATTACK_ENEMY || enemyAction == CRITICAL_ATTACK || enemyAction == HEART_BREAKER){
			if(!players[1].rage){
				(*position)++; //0x02156874 0x00000002 4 攻撃先
			}
			(*position) += 2;
		}
		if(enemyAction == DESPERATE_ATTACK || enemyAction == BURNING_BREATH){
			(*position) += 2;
		}
		(*position)++; //0x02160d64

		if(enemyAction == BLOCKENSPIEL){
			if(!players[1].rage){
				(*position)++; //0x02156874 0x00000002 4 攻撃先
			}
			(*position) += 2;
			players[1].defence = 0.5;
		}
		else{
			players[1].defence = 1.0;
		}

		int32_t actionTable = -1;
		int32_t requestedAction = -1;

		if(Gene[genePosition] == 0 || Gene[genePosition] == -1){
			genePosition = -1;
			//throw std::invalid_argument("GenePosition is invalid");
		}
		if(genePosition != -1 && Gene[genePosition] != 0 && Gene[genePosition] != -1){
			requestedAction = Gene[genePosition] & 0xffff;
			actionTable = Gene[genePosition];
			if(actionTable == TURN_SKIPPED || actionTable == SLEEPING || actionTable == CURE_SLEEPING || actionTable ==
				CURE_PARALYSIS || actionTable == PARALYSIS || actionTable == INACTIVE_ALLY){
				actionTable = ATTACK_ALLY;
			}
		}
		else{
			actionTable = ATTACK_ALLY;
		}


		if(actionTable == DEFENCE){
			players[0].defence = 0.5;
			defenseFlag = true;
		}
		else{
			defenseFlag = false;
		}

		if(!players[0].paralysis && actionTable == BattleEmulator::MERCURIAL_THRUST){
			player0_has_initiative = true;
		}

		// ソートされた結果を出力
		for(int t = 0; t < 2; ++t){
			if(!Player::isPlayerAlive(players[1])){
				break;
			}
			if(!Player::isPlayerAlive(players[0])){
				break;
			}

			int basedamage = 0;
			if((t == 0 && !player0_has_initiative) || (t == 1 && player0_has_initiative)){
				auto c = enemyAction;
				//--------start_FUN_02158dfc-------
				if(lcg::getPercent(position, 100) < mitoreP){
					//0x021588ec
					//次の乱数が90%以上(一致含む)なら見惚れないらしい。
					int mitore = lcg::getPercent(position, 100); //0x02158964
					if(mitore < 90){
						c = INACTIVE_ENEMY;
					}
				}
				(*position)++; //0x02159b10
				//--------end_FUN_02158dfc-------
				basedamage = callAttackFun(c, position, players, 1, 0, NowState);
				actions[actionsPosition++] = c;

				if(players[0].sleeping){
					actionTable = SLEEPING;
					players[0].defence = 1.0;
				}

				if(mode == -1){
					auto def1 = -1;
					if(players[0].BuffTurns > 0){
						def1 = players[0].BuffTurns;
					}
					else if(players[0].BuffLevel != 0){
						def1 = 0;
					}

					auto poi = -1;
					if(players[0].PoisonTurn > 0){
						poi = players[0].PoisonTurn;
					}
					else if(players[0].PoisonEnable == true){
						poi = 0;
					}

					auto agl = -1;
					if(players[0].speedTurn > 0){
						agl = players[0].speedTurn;
					}
					else if(players[0].speedLevel != 0){
						agl = 0;
					}
					else{
						agl = -1;
					}

					BattleResult::add(result, c, basedamage, true,
					                  def1, poi, agl, counterJ - 1,
					                  player0_has_initiative, ehp,
					                  ahp, tmpState, players[0].specialChargeTurn, players[0].mp, defenseFlag);
				}
				else if(mode != -1 && mode != -2){
					if(
						c == HEART_BREAKER ||
						c == ATTACK_ENEMY ||
						c == DESPERATE_ATTACK ||
						c == BLOCKENSPIEL ||
						c == BURNING_BREATH ||
						c == CRITICAL_ATTACK
					){
						if(damages[exCounter] == -1){
							startTurn = counterJ;
							return true;
						}

						if(c == BURNING_BREATH){
							if(damages[exCounter++] != InputBuilder::TYPE_BURNING_BREATH){
								return false;
							}
						}
						else if(damages[exCounter] == InputBuilder::TYPE_PSYCHE_UP_ENEMY){
							if(c != PSYCHE_UP){
								return false;
							}
							exCounter++;
						}
						else if(damages[exCounter++] != basedamage){
							return false;
						}
						if(damages[exCounter] == -1){
							//const int pendingActionIndex = (t == 1 && player0_has_initiative) ? counterJ : counterJ - 1;
							startTurn = counterJ;
							return true;
						}
					}
				}

				Player::reduceHp(players[0], basedamage);

				//--------start_FUN_021594bc-------
				if(Player::isPlayerAlive(players[0]) && Player::isPlayerAlive(players[1])){
					(*position) += 1;
				}
				else{
					break;
				}


				if(players[1].rage){
					players[1].rageTurns--;
					if(players[1].rageTurns <= 0){
						players[1].rage = false;
					}
				}


				//--------end_FUN_021594bc-------
			}
			else{
				int32_t action = actionTable & 0xffff;
				auto skipTurn = false;
				if(action == SLEEPING && !player0_has_initiative && !players[0].sleeping){
					skipTurn = true;
				}
				if(action == BattleEmulator::FLEE_ALLY){
					skipTurn = true;
				}
				if(!skipTurn){
					//--------start_FUN_02158dfc-------
					if(!players[0].paralysis && !players[0].sleeping){
						(*position) += 1;
					} else if (players[0].paralysis) {
						action = PARALYSIS;
						//if (players[0].paralysisTurns != 0) {
						players[0].paralysisTurns--;
						//}
						if (players[0].paralysisTurns <= 0) {
							int paralysisTable[4] = {62, 75, 87, 100};
							auto probability1 = paralysisTable[std::abs(players[0].paralysisTurns)];
							auto probability2 = lcg::getPercent(position, 100);
							if (probability1 >= (probability2 + (probability1 == 75 ? 1 : 0))) {
								// 0.5 < 0.65
								players[0].paralysis = false;
								players[0].paralysisLevel = 0;
								action = CURE_PARALYSIS;
							}
						} else {
							(*position) += 1;
						}
					}
					else if(players[0].sleeping){
						action = SLEEPING;
						players[0].defence = 1.0;

						players[0].sleepingTurn--;
						if(players[0].sleepingTurn <= 0){
							const int sleepTable[4] = {37, 62, 87, 100};
							auto probability1 = sleepTable[std::abs(players[0].sleepingTurn)];
							auto probability2 = lcg::getPercent(position, 100);
							if(probability1 >= probability2){
								players[0].sleeping = false;
								players[0].sleepingTurn = 0;
								action = CURE_SLEEPING;
							}
						}
						else{
							(*position)++;
						}
					}


					if (players[0].isStunned) {
						players[0].isStunned = false;
						players[0].defence = 1.0;
						action = INACTIVE_ALLY;
					}
					if(mode != -1 && mode != -2 && (requestedAction == INACTIVE_ALLY || action == INACTIVE_ALLY)){
						if(action != requestedAction){
							return false;
						}
					}

					//--------end_FUN_02158dfc-------
					basedamage = callAttackFun(action, position, players, 0, 1, NowState);
					actions[actionsPosition++] = action;

					if(mode == -1){
						auto def1 = -1;
						if(players[0].BuffTurns > 0){
							def1 = players[0].BuffTurns;
						}
						else if(players[0].BuffLevel != 0){
							def1 = 0;
						}

						auto poi = -1;
						if(players[0].PoisonTurn > 0){
							poi = players[0].PoisonTurn;
						}
						else if(players[0].PoisonEnable == true){
							poi = 0;
						}

						auto agl = -1;
						if(players[0].speedTurn > 0){
							agl = players[0].speedTurn;
						}
						else if(players[0].speedLevel != 0){
							agl = 0;
						}
						else{
							agl = -1;
						}

						BattleResult::add(result, action, basedamage, false,
						                  def1, poi, agl, counterJ - 1,
						                  player0_has_initiative, ehp, ahp,
						                  tmpState, players[0].specialChargeTurn, players[0].mp, defenseFlag);
					}
					if(action == HEAL || action == MORE_HEAL || action == MIDHEAL ||
						action == FULLHEAL || action == SPECIAL_MEDICINE || action == GOSPEL_SONG || action ==
						SPECIAL_ANTIDOTE){
						if(action == SPECIAL_MEDICINE || action == SPECIAL_ANTIDOTE){
							if(mode != -1 && mode != -2){
								if(damages[exCounter] != InputBuilder::TYPE_PRE_SPECIAL_MEDICINE){
									return false;
								}
								exCounter++;

								int currentDamage = damages[exCounter++];
								if(currentDamage != InputBuilder::TYPE_SPECIAL_MEDICINE &&
									currentDamage != basedamage &&
									currentDamage != (static_cast<int>(players[0].maxHp) - players[0].hp)){
									return false;
								}
								if(damages[exCounter] == -1){
									startTurn = counterJ;
									return true;
								}
							}
						}
						else if(mode != -1 && mode != -2){
							if(action == FULLHEAL){
								if(damages[exCounter++] != InputBuilder::TYPE_FULLHEAL){
									return false;
								}
							}
							if(action == MIDHEAL){
								if(damages[exCounter++] != InputBuilder::TYPE_MIDHEAL){
									return false;
								}
							}
							if(damages[exCounter] == -1){
								startTurn = counterJ;
								return true;
							}
						}
						Player::heal(players[0], basedamage);
					}
					else{
						Player::reduceHp(players[1], basedamage);

						if(mode != -1 && mode != -2){
							//休んだ場合は、入力後に休みが来る場合がある
							if((action == INACTIVE_ALLY) && (damages[exCounter] == InputBuilder::TYPE_PSYCHE_UP_ALLY || damages[exCounter] == InputBuilder::TYPE_BUFF_ALLY)){
								exCounter++;
							}else if(damages[exCounter] == InputBuilder::TYPE_PSYCHE_UP_ALLY){
								if(action != PSYCHE_UP_ALLY){
									return false;
								}
								exCounter++;
								if(damages[exCounter] == -1){
									startTurn = counterJ;
									return true;
								}
							}
							else if(damages[exCounter] == InputBuilder::TYPE_BUFF_ALLY){
								if(action != BUFF){
									return false;
								}
								exCounter++;
								if(damages[exCounter] == -1){
									startTurn = counterJ;
									return true;
								}
							}
							else if(action == ATTACK_ALLY || action == MIRACLE_SLASH || action == MULTITHRUST){
								if(damages[exCounter] == -1){
									startTurn = counterJ;
									return true;
								}
								//int need = ;
								if(damages[exCounter++] != InputBuilder::TYPE_ATTACK_ALLY){
									return false;
								}
								if(damages[exCounter++] != basedamage){
									return false;
								}
								if(damages[exCounter] == -1){
									startTurn = counterJ;
									return true;
								}
							}
						}
					}
					//--------start_FUN_021594bc-------
					if(Player::isPlayerAlive(players[0]) && Player::isPlayerAlive(players[1])){
						(*position) += 1;
						//TODO: 順序調べる


						players[0].AtkBuffTurn--;
						if(players[0].AtkBuffLevel != 0 && players[0].AtkBuffTurn <= 0){
							//0x0215a804 ATK
							const int probability[4] = {62, 75, 87, 100};
							auto probability1 = probability[std::abs(players[0].AtkBuffTurn)];
							auto probability2 = lcg::getPercent(position, 100);
							if(probability1 >= (probability2 + (probability1 == 75 ? 1 : 0))){
								players[0].AtkBuffLevel = 0;
								RecalculateBuff(players);
							}
						}
						players[0].BuffTurns--;
						if(players[0].BuffLevel != 0 && players[0].BuffTurns <= 0){
							//0x0215a8a8 DEF
							const int probability[4] = {62, 75, 87, 100};
							auto probability1 = probability[std::abs(players[0].BuffTurns)];
							auto probability2 = lcg::getPercent(position, 100);
							if(probability1 >= (probability2 + (probability1 == 75 ? 1 : 0))){
								players[0].BuffLevel = 0;
								RecalculateBuff(players);
							}
						}
					}
				}
				else{
					if(mode == -1){
						auto def1 = -1;
						if(players[0].BuffTurns > 0){
							def1 = players[0].BuffTurns;
						}
						else if(players[0].BuffLevel != 0){
							def1 = 0;
						}

						auto poi = -1;
						if(players[0].PoisonTurn > 0){
							poi = players[0].PoisonTurn;
						}
						else if(players[0].PoisonEnable == true){
							poi = 0;
						}

						auto agl = -1;
						if(players[0].speedTurn > 0){
							agl = players[0].speedTurn;
						}
						else if(players[0].speedLevel != 0){
							agl = 0;
						}
						else{
							agl = -1;
						}

						BattleResult::add(result, action, 0, false,
						                  def1, poi, agl, counterJ - 1,
						                  player0_has_initiative, ehp, ahp,
						                  tmpState, players[0].specialChargeTurn, players[0].mp, defenseFlag);
					}
				}
			}
			//--------end_FUN_021594bc-------
		}

		// 0x0215bd64 毒のダメージ処理
		if(players[0].PoisonTurn != -1){
			players[0].PoisonTurn++;
		}
		if(players[0].PoisonEnable == true){
			Player::reduceHp(players[0], static_cast<int>(players[0].maxHp) >> 4); // 16分の1
		}

		if(Player::isPlayerAlive(players[0]) && Player::isPlayerAlive(players[1])){
			(*position) += 1;
		}
		camera::Main(position, actions, NowState);

#ifdef DEBUG2
		//DEBUG_COUT2((*position));
		if((*position) == 395){
			//std::cout << "!!" << std::endl;
		}
#endif

		if(!Player::isPlayerAlive(players[1])){
			return false;
		}
		if(!Player::isPlayerAlive(players[0])){
			return false;
		}
	}
	if(mode != -1 && mode != -2){
		return true;
	}
	else{
		return false;
	}
}

double BattleEmulator::FUN_021dbc04(int baseHp, double maxHp){
	auto hp = static_cast<double>(baseHp);
	if(hp == 0){
		return 0;
	}
	return hp / maxHp;
}

//コンパイラが毎回コピーするコードを生成するからグローバルスコープに追い出しとく
constexpr int proportionTable2[9] = {90, 90, 64, 32, 16, 8, 4, 2, 1}; //最後の項目を調べるのは手動　P:\lua\isilyudaru\hissatuteki.lua
// double proportionTable1[9] = {0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 3.0, 0.2, 0.1};// 21/70が2.99999...になるから最初から20/70より大きい2.89にしちゃう
constexpr double Enemy_TensionTable[4] = {1.3, 2.0, 3.0, 4.5};
constexpr double Ally_TensionTable[4] = {1.5, 2.5, 4.0, 6.0};

constexpr int TensionLevel = 1 + static_cast<int>(Enemy_level / 10.0);
constexpr int Ally_TensionLevel = 1 + static_cast<int>(Ally_Level / 10.0);
/*
<?php
$base = 103;
$results = [];

for ($i = 9; $i >= 1; $i--) {
    $multiplier = $i / 10;
    $result = floor($base * $multiplier) + 1;
    $results[] = $result;
}

echo implode(", ", $results);
?>
*/


/**
 * @brief 各種バトルアクションを実行する関数。
 *
 * この関数は、指定された特技や行動 (Id) に基づいて、バトル中の攻撃や回復などの
 * 各種アクションを実行します。アクションの結果に応じてプレイヤーのステータス、
 * 立ち位置、特殊効果などが変更されます。
 *
 * @param Id 行動の種類を示す32ビット整数。各ケースに対応した特定のアクションを示します。
 * @param position 現在のlcgの位置を保持するポジションへのポインタ。
 * @param players プレイヤーデータを保持するPlayer構造体の配列。
 * @param attacker 攻撃者のインデックス (0または1)。
 * @param defender 防御者のインデックス (0または1)。
 * @param NowState 現在のバトル状態を保持する64ビット整数へのポインタ。
 * @return 実行した行動による最終的なダメージ総量を返します。
 */
int BattleEmulator::callAttackFun(int32_t Id, int* position, Player* players, int attacker, int defender,
                                  uint64_t* NowState){
	for(int j = 0; j < 2; ++j){
		preHP[j] = players[j].hp;
	}
	int baseDamage = 0;
	double tmp = 0;
	double tmp1 = 0;
	bool kaisinn = false;
	bool hasKaisinn = false;
	bool kaihi = false;
	bool tate = false;
	int OffensivePower = players[attacker].defaultATK;
	int percent_tmp;
	auto totalDamage = 0;
	auto attackCount = 0;
	bool defenseFlag = false; //防御した場合0x021e81a0のほうが優先度高いらしい。なんで
	switch(Id & 0xffff){
		case FULLHEAL:
			players[attacker].mp -= 24;
			(*position) += 2;
			(*position)++; //不明　0x021ec6f8
			(*position)++; //会心
			(*position)++; //ニセ回避 0x02157f58
			baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
			if (baseDamage == 0) {
				baseDamage = lcg::getPercent(position, 2); //0x021e81a0
			}
			if (baseDamage != 0) {
				(*position)++; //関係ない 0x021e54fc???
			}

			//0x021eb8c8, randIntRange: 0x021eb8f0 怒り狂っている場合←の消費が発生しない。

			if (!players[0].specialCharge && !players[0].paralysis && !players[0].sleeping) {
				(*position)++; //0x021ed7a8
			}

			if (!players[1].rage) {
				(*position)++; //0x021eb8c8
			}
			(*position)++; //? 0x021eb8f0
			if (!players[0].specialCharge && !players[0].paralysis && !players[0].sleeping) {
				if (lcg::getPercent(position, 100) < 1) {
					// 0x021edaf4
					players[attacker].specialCharge = true;
					players[attacker].specialChargeTurn = 8;
				}
			}
			players[attacker].mp -= 28;
			resetCombo(NowState);
			return 999;
			break;
		case BattleEmulator::CRITICAL_ATTACK:
			(*position) += 2;
			(*position)++; // アクロバットスターとか

			if(!players[0].paralysis && !players[0].sleeping && !players[0].isStunned){
				if(lcg::getPercent(position, 100) < 2){
					kaihi = true;
				}
				if(!kaihi && lcg::getPercent(position, 100) < ShieldGuardP){
					tate = true;
				}
			}
			(*position)++; //回避

			FUN_0207564c(position, players[attacker].atk, players[defender].def);
			baseDamage = static_cast<int>(floor(players[1].defaultATK * lcg::floatRand(position, 0.8500, 0.9500)));

			//TODO: この処理を直す
			if(baseDamage != 0){
				players[0].sleeping = false;
				players[0].sleepingTurn = -1;
			}

			if(kaihi){
				if(!players[0].paralysis && !players[0].sleeping && !players[0].specialCharge && !players[0].isStunned){
					(*position)++; //0x021ed7a8
				}
				baseDamage = 0;
			}
			else if(tate){
				if(!players[0].paralysis && !players[0].sleeping && !players[0].specialCharge && !players[0].isStunned){
					(*position)++; //0x021ed7a8
				}
				baseDamage = 0;
			}
			else{
				if(baseDamage == 0){
					baseDamage = lcg::getPercent(position, 2); //TODO: 0x021e81a0
				}

				if(baseDamage != 0){
					(*position)++; //目を覚ました
					(*position)++; //不明
				}
				if(players[defender].TensionLevel == 4){
					tmp = baseDamage * 0.5;
				}
				else{
					tmp = baseDamage;
				}
				if(!players[0].paralysis && !players[0].sleeping && !players[0].isStunned){
					tmp *= players[defender].defence;
				}
				baseDamage = static_cast<int>(floor(tmp));

				process7A8(position, baseDamage, players, defender);
			}


			players[defender].sleeping = false;
			players[defender].sleepingTurn = -1;


			resetCombo(NowState);
			break;
		case BattleEmulator::MORE_HEAL:
            players[attacker].mp -= 8;
            (*position) += 2;
            (*position)++; //関係ない
            if (lcg::getPercent(position, 0x2710) < 100) {
                kaisinn = true;
            }
            (*position)++; //回避

            baseDamage = FUN_021e8458_typeD(position, 20, CalculateMoreHealBase(players));
            if (kaisinn) {
                tmp1 = baseDamage * lcg::floatRand(position, 1.5, 2.0); //TODO
            } else {
                tmp1 = baseDamage;
            }

            if (players[attacker].TensionLevel != 0) {
                //TODO ダメージが正しいか調べる 特殊県産式の引数も調べる https://dragonquest9.com/?%E3%83%80%E3%83%A1%E3%83%BC%E3%82%B8%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6#tension
                tmp = baseDamage * Ally_TensionTable[players[attacker].TensionLevel - 1];
                tmp += (players[attacker].TensionLevel * Ally_TensionLevel);
                players[attacker].TensionLevel = 0;
            } else {
                tmp = baseDamage;
            }

            if (kaisinn) {
                if (tmp * 1.2000 <= tmp1) {
                    tmp = tmp1;
                } else {
                    tmp *= 1.2000;
                }
            }
            baseDamage = static_cast<int>(floor(tmp));

            (*position)++; //不明
            if (!players[attacker].specialCharge) {
                (*position)++; //関係ない
            }
            //0x021eb8c8, randIntRange: 0x021eb8f0 怒り狂っている場合←の消費が発生しない。
            if (!players[1].rage) {
                (*position)++;
            }
            (*position)++; //?
            if (kaisinn) {
                if (!players[1].rage) {
                    (*position)++; //会心時特殊処理　0x021e54fc
                    (*position)++; //会心時特殊処理　0x021eb8c8
                } else {
                    (*position)++; //会心時特殊処理　既に怒り狂ってる場合は1消費になる
                }
            }
            if (!players[0].paralysis && !players[0].isStunned) {
                if (!players[attacker].specialCharge && lcg::getPercent(position, 100) < 1) {
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 8;
                }
            }
            resetCombo(NowState);
            break;
		case BattleEmulator::CURE_PARALYSIS:
			(*position) += 2;
			(*position)++; //関係ない
			(*position)++; //会心
			(*position)++; //回避
			FUN_0207564c(position, players[attacker].defaultATK, players[defender].def); // 0x021e81a0要検討
			(*position)++; //不明 0x021e54fc?
			if (!players[attacker].specialCharge && !players[attacker].sleeping && !players[attacker].paralysis) {
				(*position)++; //必殺チャージ(敵)
				if (lcg::getPercent(position, 100) < 1) {
					//0x021ed7a8
					players[attacker].specialCharge = true;
					players[attacker].specialChargeTurn = 8;
				}
			}
			baseDamage = 0;
			resetCombo(NowState);
			break;
		case BattleEmulator::INACTIVE_ENEMY:
		case BattleEmulator::PARALYSIS:
		case BattleEmulator::INACTIVE_ALLY:
			(*position) += 2;
			(*position)++; //関係ない
			(*position)++; //会心
			(*position)++; //回避
			baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
			if(baseDamage == 0){
				baseDamage = lcg::getPercent(position, 2); //0x021e81a0
			}
			if(baseDamage != 0){
				(*position)++; //不明 0x021e54fc
			}
			players[attacker].TensionLevel = 0;
			baseDamage = 0;
			break;
		case MIDHEAL:
			players[attacker].mp -= 4;
			(*position) += 2;
			(*position)++; //0x021ec6f8 不明
			if(lcg::getPercent(position, 0x2710) < DragonSlashKaisinnP){
				kaisinn = true;
			}
			(*position)++; //回避

			baseDamage = FUN_021e8458_typeD(position, 10, CalculateMidHealBase(players));
			tmp = static_cast<double>(baseDamage);
			if(kaisinn){
				tmp *= lcg::floatRand(position, 1.5, 2.0); //TODO
			}
			if(players[attacker].TensionLevel != 0){
				//TODO ダメージが正しいか調べる 特殊県産式の引数も調べる https://dragonquest9.com/?%E3%83%80%E3%83%A1%E3%83%BC%E3%82%B8%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6#tension
				tmp *= Ally_TensionTable[players[attacker].TensionLevel - 1];
				tmp += (players[attacker].TensionLevel * Ally_TensionLevel);
				players[attacker].TensionLevel = 0;
			}
			baseDamage = static_cast<int>(floor(tmp));

			(*position)++; //不明
			if(!players[attacker].specialCharge){
				(*position)++; //関係ない
			}
			//0x021eb8c8, randIntRange: 0x021eb8f0 怒り狂っている場合←の消費が発生しない。
			if(!players[1].rage){
				(*position)++;
			}
			(*position)++; //?
			if(kaisinn){
				if(!players[1].rage){
					(*position)++; //会心時特殊処理　0x021e54fc
					(*position)++; //会心時特殊処理　0x021eb8c8
				}
				else{
					(*position)++; //会心時特殊処理　既に怒り狂ってる場合は1消費になる
				}
			}
			if(!players[0].paralysis && !players[0].isStunned){
				if(!players[attacker].specialCharge && lcg::getPercent(position, 100) < 1){
					players[attacker].specialCharge = true;
					players[attacker].specialChargeTurn = SpecialChargeTurns;
				}
			}
			resetCombo(NowState);
			break;
		case BattleEmulator::BURNING_BREATH:
			(*position) += 2;
			(*position)++; //会心
			(*position)++; //関係ない
			if(!players[0].paralysis && !players[0].sleeping && !players[0].isStunned){
				//TODO 眠ってるときのやけつくいき
				if(lcg::getPercent(position, 100) < 2){
					//0x021587b0 回避
					kaihi = true;
				}
			}
			if (lcg::getPercent(position, 100) < 13 && !kaihi) {
				if(players[defender].paralysisLevel == 3){
					//std::cerr << "paralysisLevel == 2" << std::endl;
				}
				if(players[defender].TensionLevel != 4){
					players[defender].paralysis = true;
					players[defender].paralysisTurns = 4;
					players[defender].paralysisLevel++;
					players[0].sleeping = false;
					players[0].sleepingTurn = -1;
					players[0].TensionLevel = 0;
				}
				baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
				if(baseDamage == 0){
					baseDamage = lcg::getPercent(position, 2); // 0x021e81a0
				}

				if(baseDamage != 0){
					//TODO 0ダメージのときの消費を調べる
					(*position)++; //0x021e54fc
				}
			}
			if(!players[0].paralysis && !players[0].sleeping && !players[0].specialCharge && !players[0].isStunned){
				(*position)++; //0x021ed7a8 必殺(敵)
			}
			baseDamage = 0;
			resetCombo(NowState);
			break;
		case MULTITHRUST:
			players[attacker].mp -= 4;
			attackCount = lcg::intRangeRand(position, 3, 4);
			(*position)++;
			(*position) += attackCount;
			hasKaisinn = false;
			for(int i = 0; i < attackCount; ++i){
				kaihi = false;
				kaisinn = false;
				(*position)++; //0x021ec6f8 不明
				if(attackCount == 4){
					if(lcg::getPercent(position, 0x2710) < multithrust4KaisinnP){
						kaisinn = true;
						hasKaisinn = true;
					}
				}
				else{
					if(lcg::getPercent(position, 0x2710) < multithrust3KaisinnP){
						kaisinn = true;
						hasKaisinn = true;
					}
				}
				// if(lcg::getPercent(position, 100) < 2){
				// 	kaihi = true;
				// }
				(*position)++;
				if(lcg::getPercent(position, 100) < 8){
					tate = true;
				}
				(*position)++; //ニセ回避 0x02157f58 100%
				baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);

				tmp = floor(baseDamage * 0.5);

				if(kaisinn == true){
					tmp1 = tmp * lcg::floatRand(position, 1.5, 2.0);
				}

				if(players[attacker].TensionLevel != 0){
					//TODO ダメージが正しいか調べる 特殊県産式の引数も調べる https://dragonquest9.com/?%E3%83%80%E3%83%A1%E3%83%BC%E3%82%B8%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6#tension
					tmp *= Ally_TensionTable[players[attacker].TensionLevel - 1];
					tmp += (players[attacker].TensionLevel * Ally_TensionLevel);
				}

				if(kaisinn){
					if(tmp * 1.2000 <= tmp1){
						tmp = tmp1;
					}
					else{
						tmp *= 1.2000;
					}
				}

				tmp = tmp * 1.1; //1.1倍は雷属性になってるから
				tmp *= players[defender].defence;
				baseDamage = static_cast<int>(floor(tmp));

				if(players[defender].TensionLevel == 4){
					tmp = baseDamage * 0.5;
					baseDamage = static_cast<int>(floor(tmp));
				}

				if(!tate){
					ProcessRage(position, baseDamage, players);
					(*position)++; //目を覚ました
					(*position)++; //不明 0x021e54fc
				}
				else{
					baseDamage = 0;
				}


				preHP[1] = std::max(0, preHP[1] - baseDamage);
				totalDamage += baseDamage;
				if(preHP[1] <= 0){
					return totalDamage;
				}
			}
			if(hasKaisinn){
				(*position) += 2;
			}
			if(preHP[1] > 0){
				if(!players[attacker].specialCharge && lcg::getPercent(position, 100) < 1){
					players[attacker].specialCharge = true;
					players[attacker].specialChargeTurn = SpecialChargeTurns;
				}
			}
			//0x021ec6f8が多分の残りの攻撃回数だけ発生する

			if(players[attacker].TensionLevel != 0){
				players[attacker].TensionLevel = 0;
			}

			resetCombo(NowState);
			return totalDamage;
		case PSYCHE_UP_ALLY:
			(*position) += 2;
			(*position)++; // 0x021ec6f8
			(*position)++; // 0x02158584 会心
			(*position)++; // 0x02157f58 偽回避
			baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
			if(baseDamage == 0){
				(*position)++; //1a8
			}
			if(players[attacker].TensionLevel < 3 || (players[attacker].TensionLevel == 3 && lcg::getPercent(position, 2) == 0)){
				//0x02087fb4 テンション
				players[attacker].TensionLevel++;
			}
			if(!players[0].specialCharge && !players[0].sleeping && !players[0].paralysis && !players[0].isStunned){
				(*position)++; //0x021ed7a8 必殺チャージ(敵) 0%
				if(lcg::getPercent(position, 100) < 1){
					//0x021edaf4
					players[attacker].specialCharge = true;
					players[attacker].specialChargeTurn = SpecialChargeTurns;
				}
			}
			baseDamage = 0;
			resetCombo(NowState);
			break;
		case ZAMMLE:
			(*position) += 2;
			(*position)++; // 0x021ec6f8
			(*position)++; // 会心
			(*position)++; // 回避
			baseDamage = FUN_021e8458_typeD(position, 15.0, 40.0);

			//テンションのほうが先
			if(players[attacker].TensionLevel != 0){
				//TODO ダメージが正しいか調べる 特殊県産式の引数も調べる https://dragonquest9.com/?%E3%83%80%E3%83%A1%E3%83%BC%E3%82%B8%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6#tension
				tmp = baseDamage * Enemy_TensionTable[players[attacker].TensionLevel - 1];
				tmp += (players[attacker].TensionLevel * TensionLevel);
				players[attacker].TensionLevel = 0;
			}
			else{
				tmp = static_cast<double>(baseDamage);
			}

			tmp = Equipments::applyDamageReduction(tmp, Attribute::Darkness);

			if(!players[0].paralysis && !players[0].sleeping && !players[0].isStunned){
				tmp *= players[defender].defence;
			}

			if(players[defender].TensionLevel == 4){
				tmp *= 0.5;
			}

			baseDamage = static_cast<int>(floor(tmp));

			(*position)++; // 0x021e54fc
			process7A8(position, baseDamage, players, defender);
			break;
		case BattleEmulator::DOUBLE_TROUBLE:
			(*position) += 2;
			for(int i = 0; i < 2; ++i){
				if(preHP[defender] > totalDamage){
					//hp0時特殊消費
					defenseFlag = false;
					kaihi = false;
					tate = false;
					(*position)++; // アクロバットスターとか

					(*position)++; //会心
					if(!players[0].paralysis && !players[0].sleeping && !players[0].isStunned){
						if(lcg::getPercent(position, 100) < 2){
							// = 10%
							kaihi = true;
						}
						if(!kaihi && lcg::getPercent(position, 100) < ShieldGuardP){
							//TODO 盾の条件調べる 盾ガード
							tate = true;
						}
					}
					(*position)++; //回避

					baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);

					if(kaihi || tate){
						if(!players[0].paralysis && !players[0].sleeping && !players[0].specialCharge && !players[0].isStunned){
							(*position)++; //0x021ed7a8
						}
						baseDamage = 0;
					}
					else{
						tmp = floor(baseDamage * 0.75);

						if(baseDamage != 0){
							tmp = processCombo(Id & 0xffff, tmp, NowState, i == 1);
							tmp = floor(tmp);
						}

						if(players[attacker].TensionLevel != 0){
							//TODO ダメージが正しいか調べる 特殊県産式の引数も調べる https://dragonquest9.com/?%E3%83%80%E3%83%A1%E3%83%BC%E3%82%B8%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6#tension
							tmp *= Enemy_TensionTable[players[attacker].TensionLevel - 1];
							tmp += (players[attacker].TensionLevel * TensionLevel);
						}

						baseDamage = static_cast<int>(floor(tmp));
						if(baseDamage == 0){
							//&& players[0].defence != 0.1
							baseDamage = lcg::getPercent(position, 2); //TODO: 0x021e81a0
							if(baseDamage == 1){
								defenseFlag = true;
							}
						}
						tmp = static_cast<double>(baseDamage);
						if(!defenseFlag && !players[0].paralysis && !players[0].sleeping && !players[0].isStunned){
							tmp = tmp * players[defender].defence;
						}
						baseDamage = static_cast<int>(floor(tmp));

						if(players[defender].TensionLevel == 4){
							tmp = baseDamage * 0.5;
							baseDamage = static_cast<int>(floor(tmp));
						}

						if(baseDamage != 0){
							(*position)++; //目を覚ました
							(*position)++; //不明
						}


						if(baseDamage != 0 && players[0].sleeping){
							players[0].sleeping = false;
							players[0].sleepingTurn = -1;
						}

						//hp0時特殊消費
						if(preHP[defender] > (totalDamage + baseDamage)){
							process7A8(position, baseDamage, players, defender);
						}
					}
				}
				else{
					//hp0時特殊消費
					baseDamage = 0;
					(*position)++; //0x021ec6f8
				}
				totalDamage += baseDamage;
			}

			if(players[attacker].TensionLevel != 0){
				players[attacker].TensionLevel = 0;
			}
			return totalDamage;
		case PSYCHE_UP:
			(*position) += 2;
			(*position)++; //不明
			(*position)++; //会心
			(*position)++; //ニセ回避 0x02157f58
			FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
			if(players[attacker].TensionLevel < 3 || (players[attacker].TensionLevel == 3 && lcg::getPercent(position, 2) == 0)){
				//0x02087fb4 テンション
				players[attacker].TensionLevel++;
			}
			baseDamage = 0;
			resetCombo(NowState);
			break;
		case BattleEmulator::BUFF:
			players[0].mp -= 3;
			(*position) += 2;
			(*position)++; // 関係ない
			(*position)++; // 会心判定
			(*position)++; // 回避

			baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
			if(baseDamage == 0){
				baseDamage = lcg::getPercent(position, 2); //0x021e81a0
			}
			if(baseDamage != 0){
				(*position)++; //不明 0x021e54fc
			}

			if(!players[0].specialCharge && !players[0].sleeping && !players[0].paralysis && !players[0].isStunned){
				(*position)++; //0x021ed7a8 必殺チャージ(敵) 0%
				if(lcg::getPercent(position, 100) < 1){
					//0x021edaf4
					players[attacker].specialCharge = true;
					players[attacker].specialChargeTurn = SpecialChargeTurns;
				}
			}

			if(players[0].BuffLevel != 2){
				players[0].BuffLevel++;
				players[0].BuffTurns = 7;
				RecalculateBuff(players);
			}

			baseDamage = 0;
			resetCombo(NowState);
			break;
		case BattleEmulator::DOUBLE_UP:
			(*position) += 2;
			(*position)++; //関係ない
			(*position)++; //会心
			(*position)++; //回避
			baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
			if(baseDamage == 0){
				baseDamage = lcg::getPercent(position, 2); //0x021e81a0
			}
			(*position)++; //かぶとわりの判定　0x021e3e7c
			if(baseDamage != 0){
				(*position)++; //不明 0x021e54fc
			}
			if(!players[attacker].specialCharge){
				(*position)++; //必殺(敵)　0x021ed7a8
				if(!players[0].paralysis && !players[0].isStunned){
					if(lcg::getPercent(position, 100) < 1){
						players[attacker].specialCharge = true;
						players[attacker].specialChargeTurn = SpecialChargeTurns;
					}
				}
			}

			if(players[0].AtkBuffLevel != 2){
				players[0].AtkBuffLevel += 2;
				players[0].AtkBuffLevel = std::min(players[0].AtkBuffLevel, 2);
				players[0].AtkBuffTurn = 6;
			}

			if(players[0].BuffLevel != -2){
				players[0].BuffLevel--;
				players[0].BuffTurns = 7;
			}

			RecalculateBuff(players);
			resetCombo(NowState);
			baseDamage = 0;
			break;
		case CRACKLE_ENEMY:
			(*position) += 2;
			(*position)++; //会心
			(*position)++; //関係ない 0x021ec6f8
			if(!players[0].paralysis && !players[0].sleeping && !players[0].isStunned){
				//TODO
				if(lcg::getPercent(position, 100) < ShieldGuardP){
					//盾ガード 0x021586fc
					tate = true;
				}
			}
			(*position)++; //偽回避 0x02157f58
			baseDamage = FUN_021e8458_typeD(position, 8, 33);

			//テンションのほうが先
			if(players[attacker].TensionLevel != 0){
				//TODO ダメージが正しいか調べる 特殊県産式の引数も調べる https://dragonquest9.com/?%E3%83%80%E3%83%A1%E3%83%BC%E3%82%B8%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6#tension
				tmp = baseDamage * Enemy_TensionTable[players[attacker].TensionLevel - 1];
				tmp += (players[attacker].TensionLevel * TensionLevel);
				players[attacker].TensionLevel = 0;
			}
			else{
				tmp = static_cast<double>(baseDamage);
			}

			tmp = Equipments::applyDamageReduction(tmp, Attribute::Ice);
			if(!tate){
				(*position)++; //武器固有の処理 0x021e54fc
			}
			else{
				tmp = 0.0;
			}

			if(!players[0].paralysis && !players[0].sleeping && !players[0].isStunned){
				tmp *= players[defender].defence;
			}
			if(players[defender].TensionLevel == 4){
				tmp *= 0.5;
			}

			baseDamage = static_cast<int>(floor(tmp));

			process7A8(position, baseDamage, players, defender);
			resetCombo(NowState);
			break;
		case SPECIAL_MEDICINE:
		case SPECIAL_ANTIDOTE:
			(*position) += 2;
			(*position)++; //0x021ec6f8 不明
			(*position)++; //0x02158584 会心
			(*position)++; //0x02157f58 ニセ回避
			baseDamage = FUN_021e8458_typeC(position, 105, 105, 15);
			(*position)++; //0x021e54fc
			if(!players[0].specialCharge){
				(*position)++; //0x021ed7a8
				if(lcg::getPercent(position, 100) < 1){
					//0x021edaf4
					players[attacker].specialCharge = true;
					players[attacker].specialChargeTurn = SpecialChargeTurns;
				}
			}
			if((Id & 0xffff) == SPECIAL_ANTIDOTE){
				players[attacker].PoisonEnable = false;
				players[attacker].PoisonTurn = -1;
				players[attacker].SpecialAntidoteCount--;
			}
			else{
				players[attacker].SpecialMedicineCount--;
			}
			resetCombo(NowState);
			break;
		case MAGIC_WATER:
			players[attacker].MagicWaterCount--;
			(*position) += 2;
			(*position)++; //0x021ec6f8 不明
			(*position)++; //0x02158584 会心
			(*position)++; //0x02157f58 ニセ回避
			baseDamage = FUN_021e8458_typeC(position, 33, 33, 3);
			(*position)++; //不明 0x021e54fc
			if(!players[attacker].specialCharge){
				(*position)++; //0x021ed7a8
				if(lcg::getPercent(position, 100) < 1){
					//0x021edaf4
					players[attacker].specialCharge = true;
					players[attacker].specialChargeTurn = SpecialChargeTurns;
				}
			}
			players[attacker].mp += baseDamage;
			players[attacker].mp = std::min(players[attacker].mp, players[attacker].maxMp);
			baseDamage = 0;
			resetCombo(NowState);
			break;
		case BattleEmulator::DEFENCE:
			(*position) += 2;
			(*position)++; //関係ない
			(*position)++; //会心
			(*position)++; //回避
			baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
			if(baseDamage == 0){
				baseDamage = lcg::getPercent(position, 2); //0x021e81a0
			}
			if(baseDamage != 0){
				(*position)++; //不明 0x021e54fc
			}
			if(!players[attacker].specialCharge && !players[attacker].sleeping && !players[attacker].paralysis){
				(*position)++; //必殺チャージ(敵) 0x021ed7a8
				if(lcg::getPercent(position, 100) < 1){
					//0x021edaf4
					players[attacker].specialCharge = true;
					players[attacker].specialChargeTurn = SpecialChargeTurns;
				}
			}
			baseDamage = 0;
			resetCombo(NowState);
			break;
		case DESPERATE_ATTACK:{
			(*position) += 2;
			(*position)++; //会心
			(*position)++; //0x021ec6f8
			(*position)++; //0x00000064 回避
			//TODO: ダメージ
			FUN_0207564c(position, players[attacker].atk, players[defender].def);
			auto dm = lcg::floatRand(position, 0.9000, 1.1000);
			constexpr double bairitu = 15 * 2.5;
			auto bairitu2 = 1.0;
			baseDamage = static_cast<int>(dm * bairitu);
			if(baseDamage != 0){
				(*position)++; //目を覚ました
				(*position)++; //不明
			}
			if(players[defender].TensionLevel == 4){
				tmp = baseDamage * 0.5;
				baseDamage = static_cast<int>(tmp);
			}
			process7A8(position, baseDamage, players, defender);
			resetCombo(NowState);
			break;
		}
		case BattleEmulator::ATTACK_ENEMY:
		case BattleEmulator::SKY_ATTACK:
		case BattleEmulator::POISON_ATTACK:
		case BattleEmulator::BLOCKENSPIEL:
		case HEART_BREAKER:{
			(*position) += 2;
			(*position)++;
			(*position)++; //会心
			if(!players[0].paralysis && !players[0].sleeping && !players[0].isStunned){
				if(!players[defender].acrobaticStar && lcg::getPercent(position, 100) < 2){
					kaihi = true;
				}
				if(!kaihi && lcg::getPercent(position, 100) < ShieldGuardP){
					tate = true;
				}
			}
			(*position)++; //回避

			baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
			if(tate || kaihi){
				baseDamage = 0;
			}
			else{
				tmp = static_cast<double>(baseDamage);
				if((Id & 0xffff) == BattleEmulator::BLOCKENSPIEL){
					tmp = floor(tmp * 0.5);
				}

				//テンションがある場合、この時点でオフセットが計算されて、最低4ダメージが保証されて下の0x021e81a0でダメージがある判定になる。
				//1*(1+(30/10))で4ダメージが保証されるけど、事前に計算して定数にしとく。
				if(players[attacker].TensionLevel != 0){
					//TODO ダメージが正しいか調べる 特殊県産式の引数も調べる https://dragonquest9.com/?%E3%83%80%E3%83%A1%E3%83%BC%E3%82%B8%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6#tension
					tmp *= Enemy_TensionTable[players[attacker].TensionLevel - 1];
					tmp += (players[attacker].TensionLevel * 4); //4 = 1*(1+(30/10))
				}


				baseDamage = static_cast<int>((tmp));


				//防御が適応される時期を調べる
				if(baseDamage == 0){
					// && players[0].defence != 0.1
					baseDamage = lcg::getPercent(position, 2); //TODO: 0x021e81a0
					if(baseDamage == 1){
						defenseFlag = true;
					}
				}

				//0x021e34e8
				//TODO: 0ダメージ時の順番
				if((Id & 0xffff) == HEART_BREAKER && lcg::getPercent(position, 100) < 18.7500){//0x021e34e8
					/**
					float: 0x020756e4 00000000 0x414e0000 193
					0x021e81a0 0x00000002 194
					0damage, 021e81a0: 1f
					0x021e34e8 0x00000064 195
					0x02158ac4 0x00000064 196
					doku, 02158ad0: 100/0
					 */
					players[defender].isStunned = true;
				}


				if(baseDamage != 0){
					tmp = static_cast<double>(baseDamage);
					tmp = processCombo(Id & 0xffff, tmp, NowState, true);
					baseDamage = static_cast<int>(floor(tmp));
				}

				if(!defenseFlag && !players[0].paralysis && !players[0].sleeping && !players[0].isStunned){
					tmp = baseDamage * players[defender].defence;
					baseDamage = static_cast<int>(floor(tmp));
				}

				if(players[defender].TensionLevel == 4){
					tmp = baseDamage * 0.5;
					baseDamage = static_cast<int>(floor(tmp));
				}

				if(baseDamage != 0){
					(*position)++; //目を覚ました
					(*position)++; //不明
				}


				if(baseDamage != 0 && players[0].sleeping){
					players[0].sleeping = false;
					players[0].sleepingTurn = -1;
				}
			}
			if(players[attacker].TensionLevel != 0){
				players[attacker].TensionLevel = 0;
			}
			process7A8(position, baseDamage, players, defender);
			break;
		}
		case BattleEmulator::HEAL:
			players[attacker].mp -= 2;
			(*position) += 2;
			(*position)++; //関係ない
			if(lcg::getPercent(position, 0x2710) < 100){
				kaisinn = true;
			}
			(*position)++; //回避
			baseDamage = FUN_021e8458_typeD(position, 5, 35);
			if(kaisinn){
				tmp = baseDamage * lcg::floatRand(position, 1.5, 2.0);
				baseDamage = static_cast<int>(floor(tmp));
			}
			(*position)++; //不明
			if(!players[attacker].specialCharge){
				(*position)++; //関係ない
			}
			//0x021eb8c8, randIntRange: 0x021eb8f0 怒り狂っている場合←の消費が発生しない。
			if(!players[1].rage){
				(*position)++;
			}
			(*position)++; //?
			if(kaisinn){
				if(!players[1].rage){
					(*position)++; //会心時特殊処理　0x021e54fc
					(*position)++; //会心時特殊処理　0x021eb8c8
				}
				else{
					(*position)++; //会心時特殊処理　既に怒り狂ってる場合は1消費になる
				}
			}
			if(!players[0].paralysis && !players[0].sleeping && !players[0].isStunned){
				if(!players[attacker].specialCharge && lcg::getPercent(position, 100) < 1){
					players[attacker].specialCharge = true;
					players[attacker].specialChargeTurn = SpecialChargeTurns;
				}
			}
			resetCombo(NowState);
			break;
		case GOSPEL_SONG:
			(*position) += 2;
			(*position)++; //0x02158584 会心
			(*position)++; //0x021ec6f8 不明
			(*position)++; //0x02157f58 ニセ回避
			baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
			if(baseDamage == 0){
				baseDamage = lcg::getPercent(position, 2); //0x021e81a0
			}
			if(baseDamage != 0){
				(*position)++; //不明 0x021e54fc
			}
			baseDamage = static_cast<int>(std::round(players[attacker].maxHp * 0.4));
			resetCombo(NowState);
			players[0].specialCharge = false;
			break;
		case BattleEmulator::ATTACK_ALLY:
		case BattleEmulator::MERCURIAL_THRUST:
		case BattleEmulator::MIRACLE_SLASH:
		case DRAGON_SLASH:
		case ITEM_USE:
			(*position) += 2;
			(*position)++;
			//会心
			percent_tmp = lcg::getPercent(position, 0x2710);
			if(
				((Id & 0xffff) == BattleEmulator::ATTACK_ALLY && percent_tmp < kaisinnP) ||
				((Id & 0xffff) == BattleEmulator::DRAGON_SLASH && percent_tmp < DragonSlashKaisinnP) ||
				((Id & 0xffff) == BattleEmulator::MERCURIAL_THRUST && percent_tmp < DragonSlashKaisinnP)
			){
				kaisinn = true;
			}

			(*position)++;//みかわし
			//みかわし(相手)
			if(lcg::getPercent(position, 100) < 8){
				tate = true;
			}

			(*position)++; //回避
			baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);

			if((Id & 0xffff) == ITEM_USE){
				baseDamage = 0;
			}

			if((Id & 0xffff) == BattleEmulator::MERCURIAL_THRUST){
				tmp = floor(baseDamage * 0.75);
			}
			else{
				tmp = static_cast<double>(baseDamage);
			}

			if(kaisinn){
				//0x020759ec
				if((Id & 0xffff) == BattleEmulator::MERCURIAL_THRUST || (Id & 0xffff) == BattleEmulator::MIRACLE_SLASH || (Id & 0xffff) == DRAGON_SLASH){
					tmp1 *= lcg::floatRand(position, 1.5, 2.0);
				}
				else{
					tmp1 = OffensivePower * lcg::floatRand(position, 0.95, 1.05);
				}
			}

			//ミラクルのやつ
			if((Id % 0xffff) == BattleEmulator::MIRACLE_SLASH){
				players[attacker].mp -= 4;
				tmp *= 1.25;
			}

			tmp *= 1.1;//雷属性

			if(players[attacker].TensionLevel != 0){
				//TODO ダメージが正しいか調べる 特殊県産式の引数も調べる https://dragonquest9.com/?%E3%83%80%E3%83%A1%E3%83%BC%E3%82%B8%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6#tension
				tmp *= Ally_TensionTable[players[attacker].TensionLevel - 1];
				tmp += (players[attacker].TensionLevel * Ally_TensionLevel);
				players[attacker].TensionLevel = 0;
			}

			if(kaisinn){
				if(tmp * 1.2000 <= tmp1){
					tmp = tmp1;
				}
				else{
					tmp *= 1.2000;
				}
			}

			tmp *= players[defender].defence;
			baseDamage = static_cast<int>(floor(tmp));

			if(players[defender].TensionLevel == 4){
				tmp = baseDamage * 0.5;
				baseDamage = static_cast<int>(floor(tmp));
			}

			if(!tate){
				ProcessRage(position, baseDamage, players);
				(*position)++; //目を覚ました
				(*position)++; //不明
			}
			else{
				baseDamage = 0;
			}
			if(kaisinn){
				if(!players[1].rage){
					(*position)++; //会心時特殊処理　0x021e54fc
					(*position)++; //会心時特殊処理　0x021eb8c8
				}
				else{
					(*position)++; //会心時特殊処理　既に怒り狂ってる場合は1消費になる
				}
			}
			if(players[defender].hp - baseDamage >= 0){
				if(!players[attacker].specialCharge && lcg::getPercent(position, 100) < 1){
					players[attacker].specialCharge = true;
					players[attacker].specialChargeTurn = SpecialChargeTurns;
				}
			}
			//ミラクルのやつ
			if((Id % 0xffff) == BattleEmulator::MIRACLE_SLASH && baseDamage != 0){
				tmp = static_cast<double>(baseDamage);
				tmp *= 0.25;
				Player::heal(players[attacker], static_cast<int>(floor(tmp)));
			}
			resetCombo(NowState);
			break;
		default:
			std::cout << "error!!!!! " << (Id & 0xffff) << std::endl;
	}
	return baseDamage;
}

//パーセントは絶対に100%にならないから誤差-1
int BattleEmulator::FUN_021e8458_typeC(int* position, double min, double max, double base){
	//0x02075724
	auto result = lcg::floatRand(position, min, max);
	result += lcg::floatRand(position, -base, base);
	return static_cast<int>(floor(result));
}

//パーセントは絶対に100%にならないから誤差-1
int BattleEmulator::FUN_021e8458_typeD(int* position, double difference, double base){
	//0x021e8668
	auto result = lcg::floatRand(position, -difference, difference);
	result += base;
	return static_cast<int>(floor(result));
}


#if !defined(__EMSCRIPTEN__)

#if defined(_MSC_VER) && !defined(__clang__)
#include <__msvc_int128.hpp>
using u128 = std::_Unsigned128;
#else
using u128 = unsigned __int128;
#endif


int BattleEmulator::FUN_0207564c(int* position, int atk, int def){
	[[assume(atk >= 0)]];
	[[assume(def >= 0)]];
	int base = 2 * atk - def;
	if(base <= 0) [[unlikely]] {
		return 0;
	}

	int64_t atk1_fp = static_cast<int64_t>(base) << 30;
	int64_t atk2_fp = static_cast<int64_t>(atk) << 28; // atk * 1/16

	int64_t result_fp;

	if(atk1_fp > atk2_fp) [[likely]] {
		int64_t atk4_fp = atk1_fp >> 4; // /16

		// floatRand(-atk4, atk4): -atk4 + top/2^32 * (2*atk4)
		uint32_t r1 = lcg::getTop32(position);
		auto spread_u = static_cast<uint64_t>(
			(static_cast<u128>(r1) * static_cast<u128>(static_cast<uint64_t>(atk4_fp))) >> 31);
		int64_t spread = static_cast<int64_t>(spread_u) - atk4_fp;

		// floatRandAttack(-1, 1): -1 + top/2^31
		uint32_t r2 = lcg::getTop32(position);
		int64_t attack = (static_cast<int64_t>(r2) << 1) - (1ll << 32);

		result_fp = atk1_fp + spread + attack;
	}
	else{
		// floatRand(0, atk2)
		uint32_t r = lcg::getTop32(position);
		auto result_u = static_cast<uint64_t>(
			(static_cast<u128>(r) * static_cast<u128>(static_cast<uint64_t>(atk2_fp))) >> 32);
		result_fp = static_cast<int64_t>(result_u);
	}

	if(result_fp <= 0) [[unlikely]] {
		return 0;
	}

	return static_cast<int>(result_fp >> 32);
}

#else

int BattleEmulator::FUN_0207564c(int* position, int atk, int def){
	[[assume(atk >= 0)]];
	[[assume(def >= 0)]];
	double result;
	const double atk1 = (2 * atk - def) * 0.25;
	if(atk1 <= 0) [[unlikely]] {
		return 0;
	}
	auto atk2 = atk * 0.0625;
	if(atk1 > atk2) [[likely]] {
		auto atk4 = atk1 * 0.0625;
		result = atk1 + lcg::floatRand(position, -atk4, atk4);
		result = result + lcg::floatRandAttack(position);
	}
	else{
		result = lcg::floatRand(position, 0.0, atk2);
	}
	if(result <= 0) [[unlikely]] {
		return 0;
	}
	return static_cast<int>((result));
	//return 0;
}

#endif

void BattleEmulator::process7A8(int* position, int baseDamage, Player players[2], int defender){
	if(players[defender].paralysis || players[defender].sleeping || players[defender].specialCharge || players[defender].isStunned
		|| players[defender].hp <= baseDamage){
		return;
	}
	if(baseDamage == 0){
		(*position)++;
		return;
	}
	auto percent_tmp = lcg::getPercent(position, 100);
	double tmp = baseDamage;

	auto baseDamage_tmp = static_cast<int>(floor(tmp));
	for(int i = 0; i < 9; ++i){
		if(baseDamage_tmp >= proportionTable3[i]){
			if(percent_tmp < proportionTable2[i]){
				players[defender].specialCharge = true;
				players[defender].specialChargeTurn = SpecialChargeTurns;
			}
			break;
		}
	}
}

int BattleEmulator::ProcessEnemyRandomAction2A(int* position){
	//0x0208aca8
	const int patternTable[6] = {43, 42, 43, 43, 42, 43};
	//125
	//43+42+43+43
	int rand = lcg::getPercent(position, 0x100) + 1;

	//std::cout << "actions rand: " << rand << std::endl;

	for(int i = 0; i < 6; ++i){
		if(rand <= patternTable[i]){
			return i;
		}
		rand = rand - patternTable[i];
	}
	return 5;
}


#include <array>
#include <cstddef>

constexpr std::size_t TABLE_MAX = 256;

template <std::size_t N>
constexpr std::array<int, TABLE_MAX>
makeProbabilityTable(const std::array<int, N>& ratios,
                     const std::array<int, N>& ids){
	std::array<int, TABLE_MAX> table{};

	std::size_t index = 0;

	for(std::size_t i = 0; i < N; ++i){
		for(int j = 0; j < ratios[i]; ++j){
			table[index++] = ids[i]; // ← ここが重要
		}
	}

	return table;
}


template <std::size_t N>
constexpr int sum(const std::array<int, N>& arr){
	int s = 0;
	for(int v : arr) s += v;
	return s;
}

constexpr std::array<int, 6> ratios = {
	68,
	58,
	48,
	38,
	27,
	17 // 239 + 17 = 256
};

constexpr std::array<int, 6> ids = {
	BattleEmulator::HEART_BREAKER,
	BattleEmulator::ATTACK_ENEMY,
	BattleEmulator::DESPERATE_ATTACK,
	BattleEmulator::BLOCKENSPIEL,
	BattleEmulator::BURNING_BREATH,
	BattleEmulator::CRITICAL_ATTACK
};

static_assert(sum(ratios) == TABLE_MAX, "Ratio sum must be 256");

constexpr auto actionTable = makeProbabilityTable(ratios, ids);


int BattleEmulator::ProcessEnemyRandomAction44(int* position){
	//0x0208aca8
	int rnd = lcg::getPercent(position, 0x100);
	return actionTable[rnd];
}


int BattleEmulator::CalculateMoreHealBase(Player* players){
	//ベホイミ
	double tmp1 = (players[0].HealPower - 200) * 0.5194;
	auto tmp2 = static_cast<int>(floor(tmp1));
	return 185 + tmp2;
}

int BattleEmulator::CalculateMidHealBase(Player* players){
	//ｂ
	double tmp1 = (players[0].HealPower - 100) * 0.2392;
	auto tmp2 = static_cast<int>(floor(tmp1));
	return 85 + tmp2;
}

void BattleEmulator::RecalculateBuff(Player* players){
	// 定数の倍率を格納した配列
	const double ATKMultipliers[] = {0.5, 0.75, 1.0, 1.25, 1.5};
	const double DEFMultipliers[] = {0.25, 0.5, 1.0, 1.5, 2.0};

	// BuffLevel に +2 して配列インデックスに変換
	int index = players[0].BuffLevel + 2;

	// インデックスが範囲外でないかチェック（-2 <= BuffLevel <= 2 の範囲であることを確認）
	if(index >= 0 && index < 5){
		players[0].def = static_cast<int>(floor(players[0].defaultDEF * DEFMultipliers[index]));
	}

	int index1 = players[0].AtkBuffLevel + 2;
	if(index >= 0 && index < 5){
		players[0].atk = static_cast<int>(floor(players[0].defaultATK * ATKMultipliers[index1]));
	}
}

void BattleEmulator::ProcessRage(int* position, int baseDamage, Player* players){
	//多分ジャダーマだけ
	// if (kaisinn) {
	//     return;
	// }
	auto percent1 = FUN_021dbc04(preHP[1] - baseDamage, players[1].maxHp);
	if(percent1 < 0.5){
		double percent = FUN_021dbc04(preHP[1], players[1].maxHp);
		if(percent >= 0.5){
			if(!players[1].rage){
				(*position)++;
				players[1].rage = true;
				players[1].rageTurns = lcg::intRangeRand(position, 2, 4);
			}
			else{
				(*position)++;
			}
		}
		else{
			if(percent1 < 0.25){
				if(percent >= 0.25){
					if(!players[1].rage){
						(*position)++;
						(*position)++;
					}
					else{
						(*position)++;
					}
				}
			}
		}
	}
}

int BattleEmulator::ProcessMagicBurst(int* position){
	auto rand1 = lcg::floatRand(position, 0.9, 1.0);
	auto rand2 = lcg::floatRand(position, 0.9, 1.1);
	auto damage = 30 * 6 * rand1;
	auto guaranteed = 160 * rand2;
	if(damage > guaranteed){
		return static_cast<int>(floor(damage));
	}
	else{
		return static_cast<int>(floor(guaranteed));
	}
}

int BattleEmulator::FUN_0208aecc(int* position, uint64_t* NowState){
	uint64_t previousState = ((*NowState) >> 4) & 0xf;
	if(previousState == 3){
		previousState = 0;
	}
	uint64_t r0_var2 = lcg::getSeed(position);
	uint64_t r3_var3 = previousState;
	uint64_t r2_var5 = r0_var2 & 0x1;
	uint64_t r0_var6 = r3_var3 << 0x1 & 0xFFFFFFFF;
	uint64_t r1_var7 = r0_var6 & 0xff;
	uint64_t r0_var8 = r3_var3 + 0x1;
	uint64_t r3_var9 = r1_var7 + r2_var5;
	previousState = static_cast<int>(r0_var8);
	uint64_t r3_var12 = r3_var9 & 0xff;
	(*NowState) &= ~0xf0;
	(*NowState) |= (previousState << 4);
	return static_cast<int>(r3_var12);
}
