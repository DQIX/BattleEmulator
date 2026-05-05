//
// Created by Owner on 2024/02/05.
//

#ifndef NEWDIRECTORY_BATTLEEMULATOR_H
#define NEWDIRECTORY_BATTLEEMULATOR_H


#include <cstdint>
#include <optional>
#include "Player.h"
#include "BattleResult.h"

class BattleEmulator {
public:
    static constexpr int TYPE_2A = 0;
    static constexpr int TYPE_2B = 1;
    static constexpr int TYPE_2C = 2;
    static constexpr int TYPE_2E = 3;
    static constexpr int TYPE_2D = 4;


    //0は配列の初期であり、永久欠番とする

    //2A
    static constexpr int ATTACK_ENEMY = 1;
    static constexpr int ULTRA_HIGH_SPEED_COMBO = 2;
    static constexpr int SWITCH_2B = 3;
    static constexpr int SWITCH_2C = 4;
    static constexpr int LIGHTNING_STORM = 5;
    static constexpr int CRITICAL_ATTACK = 6;

    //2C
    static constexpr int SKY_ATTACK = 8;
    static constexpr int MERA_ZOMA = 9; //kafrizzらしい
    static constexpr int FREEZING_BLIZZARD = 10; //ここえるふぶき
    static constexpr int SWITCH_2A = 11;
    static constexpr int LULLAB_EYE = 12; //あやしいひとみ
    static constexpr int SWITCH_2E = 13;


    //2B
    static constexpr int LAUGH = 15;
    static constexpr int DISRUPTIVE_WAVE = 16; //凍てつく波動
    static constexpr int BURNING_BREATH = 17; //やけつくいき
    static constexpr int DARK_BREATH = 18; //黒い霧
    static constexpr int SWITCH_2D = 19;

    static constexpr int INACTIVE_ENEMY = 21;
    static constexpr int INACTIVE_ALLY = 22;
    static constexpr int MEDICINAL_HERBS = 23;
    static constexpr int PARALYSIS = 24;
    static constexpr int ATTACK_ALLY = 25;
    static constexpr int HEAL = 26;
    static constexpr int DEFENCE = 27;
    static constexpr int CURE_PARALYSIS = 28;


    static constexpr int BUFF = 30;
    static constexpr int MAGIC_MIRROR = 31;
    static constexpr int MORE_HEAL = 32; //ベホイム
    static constexpr int DOUBLE_UP = 33; //すてみ
    static constexpr int MULTITHRUST = 34; //さみだれ
    static constexpr int SLEEPING = 35;
    static constexpr int MIDHEAL = 36; //ベホイミ
    static constexpr int FULLHEAL = 37; //ベホマ
    static constexpr int DEFENDING_CHAMPION = 38; //大防御

    //2D
    static constexpr int PSYCHE_UP = 39; //ためる(敵)

    static constexpr int CURE_SLEEPING = 40; //表示用

    //2E
    static constexpr int MEDITATION = 41; //瞑想
    static constexpr int MAGIC_BURST = 42; //マダンテ
    static constexpr int RESTORE_MP = 43; //いのり
    static constexpr int MERCURIAL_THRUST = 44; //しっぷう突き
    static constexpr int THUNDER_THRUST = 45; //一閃突き
    static constexpr int TURN_SKIPPED = 46; //スキップしたとき


    static constexpr int SAGE_ELIXIR = 47; //賢者の聖水
    static constexpr int ELFIN_ELIXIR = 48; //エルフののみぐすり
    static constexpr int MAGIC_WATER = 49; //まほうのせいすい
    static constexpr int SPECIAL_MEDICINE = 50; //特やくそう
    static constexpr int DEAD1 = 51; //ゴスペルソング
    static constexpr int GOSPEL_SONG = 52; //ゴスペルソング

    static constexpr int FLEE_ALLY = 53; //逃げる
    static constexpr int MIRACLE_SLASH = 54; //ミラクルソード
    static constexpr int SPECIAL_ANTIDOTE = 55; //特毒消し草
    static constexpr int ACROBATIC_STAR = 56; //アクロバットスター
    static constexpr int CRACKLE = 57; //ヒャダルコ
    static constexpr int WOOSH_ALLY = 58; //バギ
    static constexpr int DRAGON_SLASH = 59; //ドラゴン切り
    static constexpr int ITEM_USE = 60; //ドラゴン切り
    static constexpr int CRACK_ALLY = 61;
    static constexpr int PSYCHE_UP_ALLY = 62;
    static constexpr int INSULATE = 63;//バーハ


    static bool
    Main(int *position, int RunCount, const int32_t Gene[350], Player *players,
         BattleResult* result, uint64_t seed, const int eActions[350], const int damages[350], int mode, uint64_t *NowState);

    static std::string getActionName(int actionId);

    static int roundCustom(double value);

	static void ResetTurnProcessed();

	static int getTurnProcessed();

	static void processTurn();

	static void resetStartTurn();
	static int getStartTurn();


private:
    static int ProcessMagicBurst(int *position);

    static void ProcessRage(int *position, int baseDamage, const Player players[2]);

    static void RecalculateBuff(Player players[2]);

    static int CalculateMoreHealBase(const Player players[2]);

    static int CalculateMidHealBase(const Player players[2]);

    static int FUN_0208aecc(int *position, uint64_t *NowState);

    static void resetCombo(uint64_t *NowState);

    static double processCombo(int32_t Id, double damage, uint64_t *NowState);

    static int FUN_0207564c(int *position, int atk, int def);

    static int FUN_021e8458_typeC(int *position, double min, double max, double base);

    static int FUN_021e8458_typeD(int *position, double difference, double base);

    static int callAttackFun(int32_t Id, int *position, Player *players, int attacker, int defender, uint64_t *NowState);

    static double FUN_021dbc04(int baseHp, double maxHp);

    static int ProcessEnemyRandomAction2A(int *position);

    static int ProcessEnemyRandomAction44(int *position);

    static void process7A8(int *position, int baseDamage, Player players[2], int defender);
};


#endif //NEWDIRECTORY_BATTLEEMULATOR_H
