//
// Created by Owner on 2024/02/05.
//

#include <cstdint>
#include <iostream>
#include <cmath>
#include "BattleEmulator.h"

#include <cassert>

#include "lcg.h"
#include "Player.h"
#include "camera.h"
#include "debug.h"
#include "BattleResult.h"
#include "Equipment.h"


thread_local int32_t actions[8];
thread_local dq9::freecam::fast::BattleActorRef actionActors[8];
thread_local dq9::freecam::fast::BattleActorRef actionTargets[8];
thread_local std::uint8_t actionPresentationSlot1ChildCounts[8]{};
thread_local std::uint16_t actionPresentationSlot1LastChildActionIds[8]{};
thread_local dq9::freecam::fast::BattleActorRef battleActorRefs[4];
thread_local int actionsPosition = 0;
thread_local int preHP[4] = {0, 0, 0, 0};
thread_local int multithrustDamageByTarget[4] = {0, 0, 0, 0};
thread_local bool player0_has_initiative = false;
thread_local bool TiggerSkyAttack = false;

namespace {
void AppendLastActionPresentationChildSlot1(const std::uint16_t dq9ActionId) noexcept {
    if (actionsPosition <= 0 || actionsPosition > 8) return;
    const std::size_t index = static_cast<std::size_t>(actionsPosition - 1);
    if (actionPresentationSlot1ChildCounts[index] != UINT8_MAX) {
        ++actionPresentationSlot1ChildCounts[index];
    }
    actionPresentationSlot1LastChildActionIds[index] = dq9ActionId;
}
}

#if defined(gerunikku)
namespace {
constexpr std::uint16_t kHeroBodyItemId = UINT16_C(0x3382);
constexpr std::uint16_t kHeroPrimaryWeaponItemId = UINT16_C(0x5021);

void InitializeBattleActorRefs() noexcept {
    using dq9::freecam::fast::BattleActorRef;
    using dq9::freecam::fast::BattleActorSide;
    battleActorRefs[0] = BattleActorRef{BattleActorSide::ally, 0};
    battleActorRefs[1] = BattleActorRef{BattleActorSide::enemy, 0};
    battleActorRefs[2] = BattleActorRef{BattleActorSide::enemy, 1};
    battleActorRefs[3] = BattleActorRef{BattleActorSide::enemy, 2};
}

bool InitializeCameraBattle() noexcept {
    using dq9::freecam::fast::BattleActorRef;
    using dq9::freecam::fast::BattleActorSide;
    const CameraPresentationActor roster[] = {
        {
            .actor = BattleActorRef{BattleActorSide::ally, 0},
            .worldX = 10641, .worldY = 12868, .worldZ = 18432,
            .presentationFlags = 0x00000002, .occupancyExpansionDepth = 0, .movementEnabled = true,
            .membershipKind = CameraMembershipKind::player,
            .membershipKeyA = kHeroBodyItemId, .membershipKeyB = kHeroPrimaryWeaponItemId,
            .battleWorldKnown = true, .battleWorldX = 0, .battleWorldY = 204, .battleWorldZ = 10240,
        },
        {
            .actor = BattleActorRef{BattleActorSide::enemy, 0},
            .worldX = -5320, .worldY = 0, .worldZ = -9216,
            .presentationFlags = 0x00000000, .occupancyExpansionDepth = 0, .movementEnabled = true,
            .membershipKind = CameraMembershipKind::monster,
            .membershipKeyA = 0x00c1, .battleMonsterId = 0x0118,
            .battleWorldKnown = true, .battleWorldX = -9009, .battleWorldY = 204, .battleWorldZ = -10240,
        },
        {
            .actor = BattleActorRef{BattleActorSide::enemy, 1},
            .worldX = 10641, .worldY = 0, .worldZ = -18432,
            .presentationFlags = 0x00000080, .occupancyExpansionDepth = 0, .movementEnabled = true,
            .membershipKind = CameraMembershipKind::monster,
            .membershipKeyA = 0x013a, .battleMonsterId = 0x013a,
            .battleWorldKnown = true, .battleWorldX = 0, .battleWorldY = 204, .battleWorldZ = -10240,
        },
        {
            .actor = BattleActorRef{BattleActorSide::enemy, 2},
            .worldX = 26604, .worldY = 0, .worldZ = -9216,
            .presentationFlags = 0x00000000, .occupancyExpansionDepth = 0, .movementEnabled = true,
            .membershipKind = CameraMembershipKind::monster,
            .membershipKeyA = 0x00c1, .battleMonsterId = 0x0118,
            .battleWorldKnown = true, .battleWorldX = 9009, .battleWorldY = 204, .battleWorldZ = -10240,
        },
    };
    return camera::ResetBattle(roster, sizeof(roster) / sizeof(roster[0]));
}

struct EnemySelection {
    int action;
    int target;
    int slot;
};

constexpr int kIronActions[6] = {
    BattleEmulator::WHIPPING_BOY,
    BattleEmulator::ATTACK_ENEMY,
    BattleEmulator::ATTACK_ENEMY,
    BattleEmulator::HELM_SPLITTER,
    BattleEmulator::KABUFF,
    BattleEmulator::DOUBLE_EDGED_SLASH,
};

constexpr int kGerunikuActions[6] = {
    BattleEmulator::GERUNIKKU_BAGIMA,
    BattleEmulator::GERUNIKKU_MERAMI,
    BattleEmulator::EERIE_LIGHT,
    BattleEmulator::MAGIC_MIRROR,
    BattleEmulator::GERUNIKKU_MEDAPANI,
    BattleEmulator::GERUNIKKU_BAGIMA_STRONG,
};

constexpr uint8_t kFallbackOrder[6][6] = {
    {0, 1, 2, 3, 4, 5},
    {1, 0, 2, 3, 4, 5},
    {2, 1, 0, 3, 4, 5},
    {3, 2, 1, 0, 4, 5},
    {4, 3, 2, 1, 0, 5},
    {5, 4, 3, 2, 1, 0},
};

inline int selectScheme1Slot(int *position) {
    // RandInt(256), lr: 0x0208aca8
    const auto roll = static_cast<uint32_t>(lcg::getTop32(position)) >> 24;
    if (roll < 68) return 0;
    if (roll < 126) return 1;
    if (roll < 174) return 2;
    if (roll < 212) return 3;
    if (roll < 239) return 4;
    return 5;
}

inline bool resolveIronSlot(int slot, int *position, Player players[4], bool guardAlreadyPlanned,
                            EnemySelection &selection) {
    switch (slot) {
        case 0: // ゲルニックかばう: handler 159
            if (!Player::isPlayerAlive(players[2]) || players[2].guardedBy >= 0 || guardAlreadyPlanned) return false;
            (*position)++; // max: 1, lr: 0x021ee074
            selection = {BattleEmulator::WHIPPING_BOY, 2, slot};
            return true;
        case 1:
        case 2: // 通常攻撃: formation-weighted single target
            if (!Player::isPlayerAlive(players[0])) return false;
            (*position)++; // max: 2, lr: 0x02156874
            (*position)++; // range: 3..4, lr: 0x0216139c
            (*position)++; // range: 6..8, lr: 0x021613b0
            selection = {BattleEmulator::ATTACK_ENEMY, 0, slot};
            return true;
        case 3: // かぶと割り: DEF をさらに下げられる対象だけ
            if (!Player::isPlayerAlive(players[0]) || players[0].BuffLevel <= -2) return false;
            (*position)++; // max: 2, lr: 0x02156874
            selection = {BattleEmulator::HELM_SPLITTER, 0, slot};
            return true;
        case 4: { // スクルト: handler 20, valid encounter group から1つ選ぶ
            int targets[3];
            int count = 0;
            for (int actor = 1; actor < 4; ++actor) {
                if (Player::isPlayerAlive(players[actor]) && players[actor].BuffLevel < 2) {
                    targets[count++] = actor;
                }
            }
            if (count == 0) return false;
            const int target = targets[lcg::getPercent(position, count)]; // max: validGroupCount, lr: 0x021ef980
            selection = {BattleEmulator::KABUFF, target, slot};
            return true;
        }
        case 5: // もろば斬り
            if (!Player::isPlayerAlive(players[0])) return false;
            (*position)++; // max: 2, lr: 0x02156874
            (*position)++; // range: 3..4, lr: 0x0216139c
            (*position)++; // range: 6..8, lr: 0x021613b0
            selection = {BattleEmulator::DOUBLE_EDGED_SLASH, 0, slot};
            return true;
        default:
            return false;
    }
}

inline EnemySelection selectIronAction(int *position, Player players[4], bool guardAlreadyPlanned) {
    const int originalSlot = selectScheme1Slot(position);
    EnemySelection selection{BattleEmulator::ATTACK_ENEMY, 0, -1};
    for (int i = 0; i < 6; ++i) {
        const int slot = kFallbackOrder[originalSlot][i];
        if (resolveIronSlot(slot, position, players, guardAlreadyPlanned, selection)) {
            return selection;
        }
    }
    // isCanActionTaken が全滅した場合の action 2 fallback。現戦闘では通常攻撃相当。
    (*position)++; // max: 2, lr: 0x02156874
    (*position)++; // range: 3..4, lr: 0x0216139c
    (*position)++; // range: 6..8, lr: 0x021613b0
    return selection;
}

inline bool gerunikuHasMp(const Player &boss, int cost) {
    return boss.mp == 255 || boss.mp >= cost;
}

inline bool resolveGerunikuSlot(int slot, int *position, Player players[4], EnemySelection &selection) {
    const Player &boss = players[2];
    switch (slot) {
        case 0: // バギマ(弱), handler 3: 1人partyではマホカンタ中なら不可
            if (!Player::isPlayerAlive(players[0]) || players[0].hasMagicMirror || !gerunikuHasMp(boss, 8)) return false;
            selection = {BattleEmulator::GERUNIKKU_BAGIMA, 0, slot};
            return true;
        case 1: // メラミ, handler 2
            if (!Player::isPlayerAlive(players[0]) || players[0].hasMagicMirror || !gerunikuHasMp(boss, 6)) return false;
            (*position)++; // max: 2, lr: 0x02156874
            selection = {BattleEmulator::GERUNIKKU_MERAMI, 0, slot};
            return true;
        case 2: // ぶきみなひかり, handler 89
            if (!Player::isPlayerAlive(players[0]) || players[0].magicResistanceLevel <= -2 || !gerunikuHasMp(boss, 6)) return false;
            (*position)++; // max: 2, lr: 0x02156874
            selection = {BattleEmulator::EERIE_LIGHT, 0, slot};
            return true;
        case 3: // マホカンタ, handler 47
            if (boss.hasMagicMirror || !gerunikuHasMp(boss, 4)) return false;
            selection = {BattleEmulator::GERUNIKKU_MAGIC_MIRROR, 2, slot};
            return true;
        case 4: // メダパニ, handler 152
            if (!Player::isPlayerAlive(players[0]) || players[0].confused || players[0].hasMagicMirror || !gerunikuHasMp(boss, 5)) return false;
            (*position)++; // max: 1, lr: 0x021ee074
            selection = {BattleEmulator::GERUNIKKU_MEDAPANI, 0, slot};
            return true;
        case 5: // バギマ(強), handler 3
            if (!Player::isPlayerAlive(players[0]) || players[0].hasMagicMirror || !gerunikuHasMp(boss, 8)) return false;
            selection = {BattleEmulator::GERUNIKKU_BAGIMA_STRONG, 0, slot};
            return true;
        default:
            return false;
    }
}

inline EnemySelection selectGerunikuAction(int *position, Player players[4], uint8_t &usedSlots) {
    const int originalSlot = selectScheme1Slot(position);
    EnemySelection selection{BattleEmulator::ATTACK_ENEMY, 0, -1};
    for (int i = 0; i < 6; ++i) {
        const int slot = kFallbackOrder[originalSlot][i];
        const uint8_t bit = static_cast<uint8_t>(1U << slot);
        if ((usedSlots & bit) != 0) {
            continue;
        }
        if (resolveGerunikuSlot(slot, position, players, selection)) {
            usedSlots = static_cast<uint8_t>(usedSlots | bit);
            return selection;
        }
    }
    // Six slots unusable: movementPattern の action 2 fallback。
    (*position)++; // max: 2, lr: 0x02156874
    (*position)++; // range: 3..4, lr: 0x0216139c
    (*position)++; // range: 6..8, lr: 0x021613b0
    return selection;
}
}
#endif

namespace {
[[nodiscard]] constexpr double HeroSpearLightningMultiplier(const int attacker, const int defender) noexcept {
#if defined(gerunikku)
    if (attacker != 0) return 1.0;
    if (defender == 2) return 1.25; // ゲルニック将軍: Lightning 125
    if (defender == 1 || defender == 3) return 0.5; // てっこうまじん: Lightning 050
#else
    (void)attacker;
    (void)defender;
#endif
    return 1.0;
}

[[nodiscard]] constexpr bool TargetIsBeast(const int defender) noexcept {
#if defined(gerunikku)
    // ゲルニック将軍=Bird、てっこうまじん=Material。
    (void)defender;
    return false;
#else
    (void)defender;
    return false;
#endif
}

[[nodiscard]] constexpr int TargetDeathResistancePercent(const int defender) noexcept {
#if defined(gerunikku)
    // てっこうまじん: Death 050 / ゲルニック将軍: Death 000.
    if (defender == 1 || defender == 3) return 50;
    if (defender == 2) return 0;
#else
    (void)defender;
#endif
    return 0;
}

[[nodiscard]] constexpr int TargetEvadePercent(const int defender) noexcept {
#if defined(gerunikku)
    // ゲルニック将軍: Evade 4% / てっこうまじん: Evade 0%.
    if (defender == 2) return 4;
    if (defender == 1 || defender == 3) return 0;
#else
    (void)defender;
#endif
    return 0;
}

[[nodiscard]] constexpr int TargetBlockPercent(const int defender) noexcept {
#if defined(gerunikku)
    // ゲルニック将軍: Block 0% / てっこうまじん: Block 4%.
    if (defender == 1 || defender == 3) return 4;
    if (defender == 2) return 0;
#else
    (void)defender;
#endif
    return 0;
}

struct PhysicalAvoidanceResult {
    bool evaded{};
    bool blocked{};

    [[nodiscard]] constexpr bool avoided() const noexcept { return evaded || blocked; }
};

[[nodiscard]] inline PhysicalAvoidanceResult ResolveHeroPhysicalAvoidance(int *position,
                                                                          const int defender) noexcept {
    // FUN_02158718: RandInt(100) < evade%. みかわし成立時は盾判定へ進まない。
    if (lcg::getPercent(position, 100) < TargetEvadePercent(defender)) {
        return {.evaded = true, .blocked = false};
    }
    // FUN_021585b0: RandInt(100) をfloat化し block% と比較。Block 0%でも呼ばれる。
    return {
        .evaded = false,
        .blocked = lcg::getPercent(position, 100) < TargetBlockPercent(defender),
    };
}


#if defined(gerunikku)
[[nodiscard]] inline bool EnemyLosesActionToCharm(int *position) noexcept {
    // FUN_021587cc: Charm byte 5 is converted to 5/100 and multiplied by the
    // target-side factor from FUN_02157b9c. This encounter's live threshold is
    // 0.186f, compared against float(RandInt(100)); therefore only roll 0 can pass.
    // RandInt(100), lr: 0x021588ec.
    if (lcg::getPercent(position, 100) != 0) return false;
    // 成立候補時のみ結果table {90,5,5} を選ぶ。
    // この戦闘で実装対象の「みとれて動けない」は先頭90% branch。
    // RandInt(100), lr: 0x02158964
    return lcg::getPercent(position, 100) < 90;
}
#endif
}

#if defined(gerunikku)
constexpr int Ally_Level = 48;
constexpr double Ally_TensionTable[4] = {1.5, 2.5, 4.0, 6.0};
// 実盾ガード率は6.5%。RandInt(100)は整数0..99へ切り捨てられるため、
// 比較閾値7（roll 0..6）で実効7%になる。
constexpr int shieldGuardP = 7;
constexpr int kaisinnP = 500;
constexpr int WooshSlashKaisinnP = 100;
constexpr int Enemy_level = 51;
constexpr int baseHP = 301;
#elif defined(hayate)
constexpr int Ally_Level = 49;
constexpr double Ally_TensionTable[4] = {1.5, 2.5, 4.0, 6.0};
// 実盾ガード率は6.5%。RandInt(100)は整数0..99へ切り捨てられるため、
// 比較閾値7（roll 0..6）で実効7%になる。
constexpr int shieldGuardP = 7;
constexpr int kaisinnP = 500;
constexpr int WooshSlashKaisinnP = 100;
constexpr int Enemy_level = 51;
#elif defined(gilyumei)
constexpr int Ally_Level = 49;
constexpr double Ally_TensionTable[4] = {1.5, 2.5, 4.0, 6.0};
// 実盾ガード率は6.5%。RandInt(100)は整数0..99へ切り捨てられるため、
// 比較閾値7（roll 0..6）で実効7%になる。
constexpr int shieldGuardP = 7;
constexpr int kaisinnP = 500;
constexpr int WooshSlashKaisinnP = 100;
constexpr int Enemy_level = 51;
#endif



/**
 * @brief 味方のhpを基に、hpテーブルをコンパイル時に生成します。
 *
 * この関数は、基準値 (baseHP) を使用して縮小された割合に基づき整数値のテーブルを計算し、
 * それを配列として返します。
 *
 * @return 生成された9要素の整数配列。
 */
constexpr std::array<int, 9> makeProportionTable3() {
    std::array<int, 9> table{};
    // iを9から1までループし、対応する値を生成する
    for (int i = 9; i >= 1; --i) {
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

constexpr int determineTurn(const int level) {
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
constexpr int Ally_TensionLevel = 1 + static_cast<int>(Ally_Level / 10.0);
constexpr int DragonSlashKaisinnP = kaisinnP / 2;
constexpr int multithrust3KaisinnP = DragonSlashKaisinnP / 3;
constexpr int multithrust4KaisinnP = DragonSlashKaisinnP / 4;
constexpr int TensionLevel = 1 + static_cast<int>(Enemy_level / 10.0);

thread_local int threadTurnProcessed = 0;
int startTurn = 0;

void BattleEmulator::resetStartTurn() {
    startTurn = 0;
}

int BattleEmulator::getStartTurn() {
    return startTurn;
}

void BattleEmulator::ResetTurnProcessed() {
    threadTurnProcessed = 0;
}

int BattleEmulator::getTurnProcessed() {
    return threadTurnProcessed;
}

inline void BattleEmulator::processTurn() {
    // ここでturnProcessedをインクリメントする処理を追加
    threadTurnProcessed++;
}


void inline BattleEmulator::resetCombo(uint64_t *NowState) {
    (*NowState) &= ~(0xFFF00000000);
}

double BattleEmulator::processCombo(int32_t Id, double damage, uint64_t *NowState) {
    auto previousAttack = ((*NowState) >> 32) & 0xff;
    auto comboCounter = ((*NowState) >> 40) & 0xf;
    if (previousAttack != 0) {
        if (previousAttack == Id) {
            comboCounter++;
            switch (comboCounter) {
                case 2:
                    damage *= 1.2;
                    break;
                case 3:
                    damage *= 1.5;
                    break;
                default:
                    damage *= 2.0;
                    break;
            }
        } else {
            previousAttack = Id;
            comboCounter = 1;
        }
    } else {
        previousAttack = Id;
        comboCounter = 1;
    }
    resetCombo(NowState);
    (*NowState) |= (previousAttack << 32);
    (*NowState) |= (comboCounter << 40);
    return damage;
}


std::string BattleEmulator::getActionName(int actionId) {
    switch (actionId) {
        case BattleEmulator::BUFF:
            return "Buff";
        case BattleEmulator::ATTACK_ENEMY:
            return "Attack";
        case BattleEmulator::ATTACK_ENEMY_A6:
            return "Attack A6";
        case BattleEmulator::ATTACK_ENEMY_A3:
            return "Attack 3a";
        case BattleEmulator::MEDICINAL_HERBS:
            return "Medicinal Herbs";
        case BattleEmulator::PARALYSIS:
            return "Paralysis";
        case BattleEmulator::CURE_PARALYSIS:
            return "Cure Paralysis";
        case BattleEmulator::CONFUSION_PARTY_ATTACK:
            return "Confusion Party Attack";
        case BattleEmulator::CONFUSION_CANT_DECIDE:
            return "Confused - Can't Decide";
        case BattleEmulator::CONFUSION_TO_PARALYSIS:
            return "Confused - Paralysis";
        case BattleEmulator::CONFUSION_FAILED_ATTACK:
            return "Confused - Failed Attack";
        case BattleEmulator::CONFUSION_FAILED_FLEE:
            return "Confused - Failed Flee";
        case BattleEmulator::CURE_CONFUSION:
            return "Cure Confusion";
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
            return "Double up";
        case BattleEmulator::MULTITHRUST:
            return "Multithrust";

        case BattleEmulator::ATTACK_ALLY:
            return "Attack";
        case BattleEmulator::HEAL:
            return "Heal";
        case BattleEmulator::DEFENCE:
            return "Defence";

        case BattleEmulator::MAGIC_MIRROR:
        case BattleEmulator::GERUNIKKU_MAGIC_MIRROR:
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
        case BattleEmulator::INSULATE:
            return "Insulate";
        case PSYCHE_UP_ALLY:
            return "Psyche up";
        case SPECIAL_MEDICINE:
            return "Special Medicine";
        case MULTISLASH:
            return "Multi Slash";
        case FLAME_SLASH:
            return "Flame Slash";
        case KACRACKLE_SLASH:
            return "Kacrackle Slash";
        case HATCHET_MAN:
            return "Hatchet Man";
        case UPWARD_SLICE:
            return "Upward Slice";
        case INACTIVE_ALLY:
            return "inactive";
        case KASWOOSH:
            return "Kaswosh";
        case LIGHTNING:
            return "Lightning";
        case THIN_AIR:
            return "Thin Air";
        case SCEPTER_BALL:
            return "Scepter Ball";
        case WHIPPING_BOY:
            return "Whipping Boy";
        case HELM_SPLITTER:
            return "Helm Splitter";
        case DOUBLE_EDGED_SLASH:
            return "Double-edged Slash";
        case GERUNIKKU_MERAMI:
            return "Merami";
        case GERUNIKKU_BAGIMA:
            return "Bagima";
        case EERIE_LIGHT:
            return "Eerie Light";
        case GERUNIKKU_MEDAPANI:
            return "Medapani";
        case GERUNIKKU_BAGIMA_STRONG:
            return "Bagima (strong)";
        default:
            return "Unknown Action";
    }
}

bool BattleEmulator::Main(int *position, int RunCount, const int32_t Gene[350], Player *players,
                         BattleResult* result,
                          uint64_t seed, const int eActions[350], const int damages[350], int mode,
                          uint64_t *NowState, const int heroTargetOverride, const bool traceBoundaries,
                          const int heroActionOverride, const bool initializeCameraBattle) {
#if defined(gerunikku)
    if (initializeCameraBattle) {
        InitializeBattleActorRefs();
        if (!InitializeCameraBattle()) return false;
    }
#endif
    assert(position != nullptr);
    assert(*position != 0);//positionは1始まりなので守ってね
    int genePosition = 0;
    int exCounter = 0;
    int exCounter1 = 0;
    uint64_t tmpState;

    auto startPos = static_cast<int>(((*NowState) >> 12) & 0xfffff);
    if (startPos != 0) {
        startPos++;
        RunCount += startPos;
    } else {
        startPos = 1;
        RunCount++;
    }
    for (int counterJ = startPos; counterJ < RunCount; ++counterJ) {
        auto defenseFlag = false;
        processTurn();
        if (genePosition != -1) {
            genePosition = counterJ - 1;
        }
        TiggerSkyAttack = false;
        //現在ターンを保存
        (*NowState) &= ~0xFFFFF000;
        (*NowState) |= (static_cast<uint64_t>(counterJ) << 12ULL);

        if (players[0].dirtySpecialCharge) {
            players[0].specialCharge = false;
            players[0].dirtySpecialCharge = false;
        }
        players[0].specialChargeTurn--;
        if (players[0].specialChargeTurn == -1) {
            players[0].specialCharge = false;
        }

        TiggerSkyAttack = false;

        resetCombo(NowState);

        tmpState = (*NowState);

#ifdef DEBUG2
        DEBUG_COUT2((*position));
        DEBUG_COUT2(counterJ);
        //THIS DEBUG CODE!
        if ((*position) == 28) { //THIS DEBUG CODE!
            std::cout << "!!" << std::endl;
        }
#endif
        #if defined(gerunikku)
        int ehp = players[2].hp;
        #else
        int ehp = players[1].hp;
        #endif
        int ahp = players[0].hp;

        players[0].defence = 1.0;

        // callAttackFun() overwrites every entry in [0, actionsPosition), and
        // camera::Main() is given that exact count. Clearing the unused tail on
        // every searched turn only writes memory that cannot be observed.
        actionsPosition = 0;
        std::fill(std::begin(actionPresentationSlot1ChildCounts),
                  std::end(actionPresentationSlot1ChildCounts), UINT8_C(0));
        std::fill(std::begin(actionPresentationSlot1LastChildActionIds),
                  std::end(actionPresentationSlot1LastChildActionIds), UINT16_C(0xffff));
        double speed0 = Player::isPlayerAlive(players[0]) && players[0].speed > 0
            ? players[0].speed * lcg::floatRand(position, 0.51, 1.0) // float, lr: 0x0215efac
            : -1.0;
        double speed1 = Player::isPlayerAlive(players[1]) && players[1].speed > 0
            ? players[1].speed * lcg::floatRand(position, 0.51, 1.0) // float, lr: 0x0215efac
            : -1.0;
        double speed2 = Player::isPlayerAlive(players[2]) && players[2].speed > 0
            ? players[2].speed * lcg::floatRand(position, 0.51, 1.0) // float, lr: 0x0215efac
            : -1.0;
        double speed3 = Player::isPlayerAlive(players[3]) && players[3].speed > 0
            ? players[3].speed * lcg::floatRand(position, 0.51, 1.0) // float, lr: 0x0215efac
            : -1.0;

        auto swap_if = [](double& a, double& b, int& ia, int& ib) {
            if (a < b) {
                std::swap(a, b);
                std::swap(ia, ib);
            }
        };

        int i0 = 0, i1 = 1, i2 = 2, i3 = 3;

        swap_if(speed0, speed1, i0, i1);
        swap_if(speed2, speed3, i2, i3);
        swap_if(speed0, speed2, i0, i2);
        swap_if(speed1, speed3, i1, i3);
        swap_if(speed1, speed2, i1, i2);

        int order[4] = {i0, i1, i2, i3};
        player0_has_initiative = order[0] == 0;

#if defined(gerunikku)
        // Encounter-group relation used by ゲルニックかばう is reset at turn start.
        // The selected guard affects target construction immediately, even when the
        // Iron's 03A1 action record executes after the hero action this turn.
        players[2].guardedBy = -1;
        EnemySelection plannedIron[4]{};
        bool plannedIronValid[4] = {false, false, false, false};
        bool guardAlreadyPlanned = false;

        // FUN_0215edbc creates turn records in randomized-speed order. Judgment-1
        // Iron actions are selected here; judgment-2 Geruniku actions are selected
        // only when each action record executes. FUN_02160cfc is called after the
        // first record of every enemy actor. Geruniku profile index 2 returns a fixed
        // extra-action count of 1; Iron profile index 0 returns 0. RandInt(2) is still
        // consumed in both cases.
        for (const int actor : order) {
            if (!Player::isPlayerAlive(players[actor]) || actor == 0) continue;
            if (actor == 1 || actor == 3) {
                plannedIron[actor] = selectIronAction(position, players, guardAlreadyPlanned);
                plannedIronValid[actor] = true;
                if (plannedIron[actor].action == WHIPPING_BOY) {
                    guardAlreadyPlanned = true;
                    players[2].guardedBy = actor;
                }
            }
            (*position)++; // max: 2, lr: 0x02160d64
        }
        uint8_t gerunikuUsedSlots = 0;
#endif

        int32_t actionTable = -1;
        int packedHeroTargetOverride = -1;

        if (heroActionOverride > 0) {
            actionTable = HeroActionId(heroActionOverride);
            packedHeroTargetOverride = HeroTargetId(heroActionOverride);
        } else {
            if (genePosition != -1 && (Gene[genePosition] == 0 || Gene[genePosition] == -1)) {
                genePosition = -1;
                //throw std::invalid_argument("GenePosition is invalid");
            }
            if (genePosition != -1 && Gene[genePosition] != 0 && Gene[genePosition] != -1) {
                actionTable = HeroActionId(Gene[genePosition]);
                packedHeroTargetOverride = HeroTargetId(Gene[genePosition]);
                if (actionTable == TURN_SKIPPED || actionTable == SLEEPING || actionTable == CURE_SLEEPING || actionTable ==
                    CURE_PARALYSIS || actionTable == PARALYSIS) {
                    actionTable = ATTACK_ALLY;
                    }
            } else {
                actionTable = ATTACK_ALLY;
            }
        }


        //途中で解除してもいいように2回チェックする
        if (players[0].sleeping) {
            actionTable = SLEEPING;
        } else if (!players[0].paralysis && !players[0].inactive && actionTable == BattleEmulator::MERCURIAL_THRUST) {
            player0_has_initiative = true;
        }

        if (actionTable == DEFENCE) {
            players[0].defence = 0.5;
            defenseFlag = true;
        }
        if (actionTable == DEFENDING_CHAMPION) {
            //例え寝てる場合でもmpが減る
            players[0].defence = 0.1;
            players[0].mp -= 3; //後攻睡眠などにより大防御の行動ができない場合は事前に減る
            defenseFlag = true;
        }

        auto preAction = 0;

        auto anyEnemyAlive = [&]() noexcept {
            return Player::isPlayerAlive(players[1]) || Player::isPlayerAlive(players[2]) ||
                   Player::isPlayerAlive(players[3]);
        };

        auto primaryHeroTarget = [&]() noexcept {
            const int turnTargetOverride = packedHeroTargetOverride >= 1
                ? packedHeroTargetOverride
                : heroTargetOverride;
            if (turnTargetOverride >= 1 && turnTargetOverride <= 3 &&
                Player::isPlayerAlive(players[turnTargetOverride])) {
                return turnTargetOverride;
            }
            if (Player::isPlayerAlive(players[2])) return 2;
            if (Player::isPlayerAlive(players[1])) return 1;
            if (Player::isPlayerAlive(players[3])) return 3;
            return -1;
        };

        auto isHealingAction = [](const int action) noexcept {
            return action == HEAL || action == MEDICINAL_HERBS || action == MORE_HEAL ||
                   action == MIDHEAL || action == FULLHEAL || action == SPECIAL_MEDICINE ||
                   action == GOSPEL_SONG;
        };

        auto isGuardableHeroAction = [](const int action) noexcept {
            switch (action) {
                case ATTACK_ALLY:
                case MULTITHRUST:
                case MERCURIAL_THRUST:
                case THUNDER_THRUST:
                case BEAST_THRUST:
                case VITAL_POINT_THRUST:
                case ZAKI:
                case ZARAKI:
                case DRAGON_SLASH:
                case MIRACLE_SLASH:
                case FLAME_SLASH:
                case KACRACKLE_SLASH:
                case HATCHET_MAN:
                case UPWARD_SLICE:
                    return true;
                default:
                    return false;
            }
        };

        auto addResult = [&](const int action, const int damage, const bool isEnemy) {
            if (mode != -1) return;
            int atkTurn = players[0].AtkBuffTurn > 0 ? players[0].AtkBuffTurn
                : (players[0].AtkBuffLevel != 0 ? 0 : -1);
            int buffTurn = players[0].BuffTurns > 0 ? players[0].BuffTurns
                : (players[0].BuffLevel != 0 ? 0 : -1);
            int mirrorTurn = players[0].MagicMirrorTurn > 0 ? players[0].MagicMirrorTurn
                : (players[0].hasMagicMirror ? 0 : -1);
            BattleResult::add(result, action, damage, isEnemy, atkTurn, buffTurn, mirrorTurn,
                              counterJ - 1, player0_has_initiative, ehp, ahp, tmpState,
                              players[0].specialChargeTurn, players[0].mp, defenseFlag);
        };

        auto enemyDamageIsTracked = [](const int action) noexcept {
            switch (action) {
                case ATTACK_ENEMY:
                case HELM_SPLITTER:
                case DOUBLE_EDGED_SLASH:
                case GERUNIKKU_MERAMI:
                case GERUNIKKU_BAGIMA:
                case GERUNIKKU_BAGIMA_STRONG:
                    return true;
                default:
                    return false;
            }
        };

        // return: -1=mismatch, 0=continue, 1=request matched through sentinel.
        auto validateEnemy = [&](const int action, const int damage) noexcept {
            if (mode == -1 || mode == -2) return 0;
            const int need = eActions[exCounter1++];
            if (need == -1) {
                startTurn = counterJ - 1;
                return 1;
            }
            if (need != action) return -1;
            if (enemyDamageIsTracked(action)) {
                if (damages[exCounter] == -1) {
                    startTurn = counterJ - 1;
                    return 1;
                }
                if (damages[exCounter++] != damage) return -1;
            }
            return 0;
        };

        auto postEnemyAction = [&](const int actor) {
            const bool mirrorRecoveryWasActive = players[actor].hasMagicMirror &&
                                                 players[actor].MagicMirrorRecoveryTurn > 0;
            if (players[actor].hasMagicMirror && players[actor].MagicMirrorTurn > 0) {
                --players[actor].MagicMirrorTurn; // FUN_0215b174: combat +0x61
                if (players[actor].MagicMirrorTurn == 0) {
                    players[actor].MagicMirrorRecoveryTurn = 4; // combat +0x84
                }
            }
            if (Player::isPlayerAlive(players[0]) && anyEnemyAlive()) {
                (*position)++; // max: 100, lr: 0x02159d40
                if (mirrorRecoveryWasActive && players[actor].hasMagicMirror) {
                    --players[actor].MagicMirrorRecoveryTurn;
                    constexpr int probability[4] = {100, 87, 75, 62};
                    const int probability1 = probability[players[actor].MagicMirrorRecoveryTurn];
                    const int probability2 = lcg::getPercent(position, 100); // lr: 0x0215a050
                    if (probability1 >= probability2 + (probability1 == 75 ? 1 : 0)) {
                        players[actor].hasMagicMirror = false;
                        players[actor].MagicMirrorTurn = 0;
                        players[actor].MagicMirrorRecoveryTurn = 0;
                        AppendLastActionPresentationChildSlot1(
                            dq9::freecam::fast::metadata::kMagicMirrorRecoveryPresentationChildActionId
                        );
                    }
                }
            }
            if (players[actor].rage) {
                --players[actor].rageTurns;
                if (players[actor].rageTurns <= 0) players[actor].rage = false;
            }
        };

        auto traceBoundary = [&](const char *label) {
            DEBUG_TRACE_IF(traceBoundaries,
                           std::cout << "TRACE boundary " << label << " position=" << *position << '\n');
        };

        for (const int actor : order) {
            if (!Player::isPlayerAlive(players[actor])) {
                continue;
            }

            switch (actor) {
                case 0:
                    {
                        int action = actionTable & 0xffff;
                        bool skipTurn = false;
                        if (action == SLEEPING && !player0_has_initiative && !players[0].sleeping) {
                            skipTurn = true;
                        }
                        if (action == FLEE_ALLY) skipTurn = true;

                        if (!skipTurn) {
                            traceBoundary("start FUN_02158dfc");
                            // FUN_02158dfc player pre-action path.
                            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive && !players[0].confused) {
                                (*position)++; // max: 100, lr: 0x02159b10
                            } else if (players[0].inactive) {
                                players[0].inactive = false;
                                action = INACTIVE_ALLY;
                                players[0].defence = 1.0;
                                (*position)++; // max: 100, lr: 0x02159b10
                            } else if (players[0].paralysis) {
                                action = PARALYSIS;
                                --players[0].paralysisTurns;
                                if (players[0].paralysisTurns <= 0) {
                                    constexpr int paralysisTable[4] = {62, 75, 87, 100};
                                    const auto probability1 = paralysisTable[std::abs(players[0].paralysisTurns)];
                                    const auto probability2 = lcg::getPercent(position, 100); // lr: 0x02159b10
                                    if (probability1 >= probability2 + (probability1 == 75 ? 1 : 0)) {
                                        players[0].paralysis = false;
                                        players[0].paralysisLevel = 0;
                                        action = CURE_PARALYSIS;
                                    }
                                } else {
                                    (*position)++; // max: 100, lr: 0x02159b10
                                }
                            } else if (players[0].sleeping) {
                                action = SLEEPING;
                                --players[0].sleepingTurn;
                                if (players[0].sleepingTurn <= 0) {
                                    constexpr int sleepTable[4] = {37, 62, 87, 100};
                                    const auto probability1 = sleepTable[std::abs(players[0].sleepingTurn)];
                                    const auto probability2 = lcg::getPercent(position, 100); // lr: 0x02159b10
                                    if (probability1 >= probability2) {
                                        players[0].sleeping = false;
                                        action = CURE_SLEEPING;
                                    }
                                } else {
                                    (*position)++; // max: 100, lr: 0x02159b10
                                }
                            } else if (players[0].confused) {
                                const int recoveryRoll = lcg::getPercent(position, 100); // lr: 0x02159b10
                                if (players[0].confusionTurns < 0) {
                                    // FUN_02159abc uses DAT_02159c58 = {1.0, .875, .75, .625}
                                    // with a strict tableValue > RandInt(100)/100 comparison.
                                    constexpr int recoveryTable[4] = {100, 87, 75, 62};
                                    const int recoveryIndex = std::clamp(-players[0].confusionTurns - 1, 0, 3);
                                    const int probability1 = recoveryTable[recoveryIndex];
                                    if (probability1 >= recoveryRoll + (probability1 == 75 ? 1 : 0)) {
                                        players[0].confused = false;
                                        players[0].confusionTurns = -1;
                                        action = CURE_CONFUSION;
                                    } else {
                                        ++players[0].confusionTurns;
                                    }
                                }

                                if (players[0].confused) {
                                    players[0].defence = 1.0;
                                    defenseFlag = false;
                                    // FUN_02160dfc first does RandInt(2), lr: 0x02160e14.
                                    // With this one-person party FUN_0216017c(..., 4) < 2, so
                                    // the result is discarded and the four-entry table is forced.
                                    (*position)++; // max: 2, lr: 0x02160e14
                                    switch (lcg::getPercent(position, 4)) { // lr: 0x02160f10
                                        case 0: action = CONFUSION_CANT_DECIDE; break;   // DQ9 0x00DD
                                        case 1: action = CONFUSION_TO_PARALYSIS; break; // DQ9 0x0393
                                        case 2: action = CONFUSION_FAILED_ATTACK; break;// DQ9 0x00DE
                                        default: action = CONFUSION_FAILED_FLEE; break;  // DQ9 0x0396
                                    }
                                }
                            }

                            int target = primaryHeroTarget();
                            if (target < 0) return false;
                            bool targetWasGuardRedirect = false;
                            if (target == 2 && isGuardableHeroAction(action) && players[2].guardedBy >= 0 &&
                                Player::isPlayerAlive(players[players[2].guardedBy])) {
                                target = players[2].guardedBy;
                                targetWasGuardRedirect = true;
                            }

                            traceBoundary("end FUN_02158dfc");
                            traceBoundary("start FUN_021ebd9c_ct");
                            const int basedamage = callAttackFun(action, position, players, 0, target, NowState,
                                                                 targetWasGuardRedirect);
                            traceBoundary("end FUN_021ebd9c_ct");
                            addResult(action, basedamage, false);
                            if (isHealingAction(action)) {
                                Player::heal(players[0], basedamage);
                            } else if (action == MULTITHRUST) {
                                for (int enemy = 1; enemy <= 3; ++enemy) {
                                    Player::reduceHp(players[enemy], multithrustDamageByTarget[enemy]);
                                }
                                if (mode != -1 && mode != -2) {
                                    if (damages[exCounter] == -1) {
                                        startTurn = counterJ - 1;
                                        return true;
                                    }
                                    if (damages[exCounter++] != basedamage) return false;
                                }
                            } else if (target >= 1 && target <= 3) {
                                Player::reduceHp(players[target], basedamage);
                                if (mode != -1 && mode != -2 &&
                                    (action == ATTACK_ALLY || action == MERCURIAL_THRUST)) {
                                    if (damages[exCounter] == -1) {
                                        startTurn = counterJ - 1;
                                        return true;
                                    }
                                    if (damages[exCounter++] != basedamage) return false;
                                }
                            }

                            traceBoundary("start FUN_021594bc");
                            // FUN_021594bc -> FUN_0215b174 at 0x0215957c.
                            // Medapani sets combat+0x5e=3. The primary confusion counter
                            // is decremented after each action; when it expires, the game
                            // starts the four-step recovery table at combat+0x81=4.
                            if (players[0].confused && players[0].confusionTurns > 0) {
                                --players[0].confusionTurns;
                                if (players[0].confusionTurns == 0) players[0].confusionTurns = -4;
                            }

                            // FUN_021594bc player post-action path. Preserve the existing
                            // buff-duration logic; only the enemy-alive predicate is widened
                            // from the old one-enemy build to all three encounter actors.
                            if (Player::isPlayerAlive(players[0]) && anyEnemyAlive()) {
                                const bool mirrorRecoveryWasActive = players[0].hasMagicMirror &&
                                                                     players[0].MagicMirrorRecoveryTurn > 0;
                                if (players[0].hasMagicMirror && players[0].MagicMirrorTurn > 0) {
                                    --players[0].MagicMirrorTurn; // FUN_0215b174: combat +0x61
                                    if (players[0].MagicMirrorTurn == 0) {
                                        players[0].MagicMirrorRecoveryTurn = 4; // combat +0x84
                                    }
                                }
                                (*position)++; // max: 100, lr: 0x02159d40

                                if (mirrorRecoveryWasActive && players[0].hasMagicMirror) {
                                    --players[0].MagicMirrorRecoveryTurn;
                                    constexpr int probability[4] = {100, 87, 75, 62};
                                    const int probability1 = probability[players[0].MagicMirrorRecoveryTurn];
                                    const int probability2 = lcg::getPercent(position, 100); // lr: 0x0215a050
                                    if (probability1 >= probability2 + (probability1 == 75 ? 1 : 0)) {
                                        players[0].hasMagicMirror = false;
                                        players[0].MagicMirrorTurn = 0;
                                        players[0].MagicMirrorRecoveryTurn = 0;
                                        AppendLastActionPresentationChildSlot1(
                                            dq9::freecam::fast::metadata::kMagicMirrorRecoveryPresentationChildActionId
                                        );
                                    }
                                }

                                --players[0].AtkBuffTurn;
                                if (players[0].AtkBuffLevel != 0 && players[0].AtkBuffTurn <= 0) {
                                    constexpr int probability[4] = {62, 75, 87, 100};
                                    const int probability1 = probability[std::abs(players[0].AtkBuffTurn)];
                                    const int probability2 = lcg::getPercent(position, 100); // lr: 0x0215a804
                                    if (probability1 >= probability2 + (probability1 == 75 ? 1 : 0)) {
                                        players[0].AtkBuffLevel = 0;
                                        RecalculateBuff(players, 0);
                                    }
                                }

                                --players[0].BuffTurns;
                                if (players[0].BuffLevel != 0 && players[0].BuffTurns <= 0) {
                                    constexpr int probability[4] = {62, 75, 87, 100};
                                    const int probability1 = probability[std::abs(players[0].BuffTurns)];
                                    const int probability2 = lcg::getPercent(position, 100); // lr: 0x0215a8a8
                                    if (probability1 >= probability2 + (probability1 == 75 ? 1 : 0)) {
                                        players[0].BuffLevel = 0;
                                        RecalculateBuff(players, 0);
                                    }
                                }

                                --players[0].InsulateTurns;
                                if (players[0].InsulateLevel != 0 && players[0].InsulateTurns <= 0) {
                                    constexpr int probability[4] = {62, 75, 87, 100};
                                    const int probability1 = probability[std::abs(players[0].InsulateTurns)];
                                    const int probability2 = lcg::getPercent(position, 100); // lr: 0x0215ac74
                                    if (probability1 >= probability2 + (probability1 == 75 ? 1 : 0)) {
                                        players[0].InsulateLevel = 0;
                                    }
                                }
                            }
                            traceBoundary("end FUN_021594bc");
                        } else {
                            addResult(action, 0, false);
                        }
                        break;
                    }
                case 1:
                case 3:
                    {
#if defined(gerunikku)
                        if (!plannedIronValid[actor]) break;
                        traceBoundary("start FUN_02158dfc");
                        if (EnemyLosesActionToCharm(position)) {
                            traceBoundary("end FUN_02158dfc");
                            traceBoundary("start FUN_021ebd9c_ct");
                            const int basedamage = callAttackFun(INACTIVE_ENEMY, position, players, actor, 0, NowState);
                            traceBoundary("end FUN_021ebd9c_ct");
                            addResult(INACTIVE_ENEMY, basedamage, true);
                            const int validation = validateEnemy(INACTIVE_ENEMY, basedamage);
                            if (validation < 0) return false;
                            if (validation > 0) return true;
                            traceBoundary("start FUN_021594bc");
                            postEnemyAction(actor);
                            traceBoundary("end FUN_021594bc");
                            break;
                        }
                        (*position)++; // max: 100, lr: 0x02159b10
                        const EnemySelection selection = plannedIron[actor];
                        traceBoundary("end FUN_02158dfc");
                        traceBoundary("start FUN_021ebd9c_ct");
                        const int basedamage = callAttackFun(selection.action, position, players, actor,
                                                             selection.target, NowState);
                        traceBoundary("end FUN_021ebd9c_ct");
                        addResult(selection.action, basedamage, true);
                        if (basedamage > 0 && selection.target >= 0 && selection.target < 4) {
                            Player::reduceHp(players[selection.target], basedamage);
                        }
                        const int validation = validateEnemy(selection.action, basedamage);
                        if (validation < 0) return false;
                        if (validation > 0) return true;
                        traceBoundary("start FUN_021594bc");
                        postEnemyAction(actor);
                        traceBoundary("end FUN_021594bc");
#endif
                        break;
                    }
                case 2:
                    {
#if defined(gerunikku)
                        for (int bossActionIndex = 0; bossActionIndex < 2; ++bossActionIndex) {
                            if (!Player::isPlayerAlive(players[2]) || !Player::isPlayerAlive(players[0])) break;
                            traceBoundary("start FUN_02158dfc");
                            if (EnemyLosesActionToCharm(position)) {
                                traceBoundary("end FUN_02158dfc");
                                traceBoundary("start FUN_021ebd9c_ct");
                                const int basedamage = callAttackFun(INACTIVE_ENEMY, position, players, 2, 0, NowState);
                                traceBoundary("end FUN_021ebd9c_ct");
                                addResult(INACTIVE_ENEMY, basedamage, true);
                                const int validation = validateEnemy(INACTIVE_ENEMY, basedamage);
                                if (validation < 0) return false;
                                if (validation > 0) return true;
                                traceBoundary("start FUN_021594bc");
                                postEnemyAction(2);
                                traceBoundary("end FUN_021594bc");
                                continue;
                            }
                            const EnemySelection selection = selectGerunikuAction(position, players, gerunikuUsedSlots);
                            (*position)++; // max: 100, lr: 0x02159b10
                            traceBoundary("end FUN_02158dfc");
                            traceBoundary("start FUN_021ebd9c_ct");
                            const int basedamage = callAttackFun(selection.action, position, players, 2,
                                                                 selection.target, NowState);
                            traceBoundary("end FUN_021ebd9c_ct");
                            addResult(selection.action, basedamage, true);
                            if (basedamage > 0 && selection.target >= 0 && selection.target < 4) {
                                Player::reduceHp(players[selection.target], basedamage);
                            }
                            const int validation = validateEnemy(selection.action, basedamage);
                            if (validation < 0) return false;
                            if (validation > 0) return true;
                            traceBoundary("start FUN_021594bc");
                            postEnemyAction(2);
                            traceBoundary("end FUN_021594bc");
                        }
#endif
                        break;
                    }
                default:
                    break;
            }

            if (!Player::isPlayerAlive(players[0])) return false;
            if (!anyEnemyAlive()) return false;
        }

        if (Player::isPlayerAlive(players[0]) && anyEnemyAlive()) {
            DEBUG_TRACE_IF(traceBoundaries,
                           std::cout << "TRACE rng lr=0x0215962c consume=" << *position << '\n');
            (*position)++; // max: 100, lr: 0x0215962c
        }
        camera::Main(position, actions, actionActors, actionTargets,
                     actionPresentationSlot1ChildCounts,
                     actionPresentationSlot1LastChildActionIds,
                     actionsPosition,
                     NowState, player0_has_initiative, TiggerSkyAttack, traceBoundaries);
    }
    if (mode != -1 && mode != -2) {
        startTurn = RunCount - 2;
        return true;
    } else {
        return false;
    }
}

bool BattleEmulator::InitializeSearchState(SearchState* state, const Player initialPlayers[4],
                                           const int initialPosition) {
    if (state == nullptr || initialPlayers == nullptr || initialPosition < 1) return false;
    for (int i = 0; i < 4; ++i) state->players[i] = initialPlayers[i];
    state->position = initialPosition;
    state->nowState = 0;

#if defined(gerunikku)
    InitializeBattleActorRefs();
    if (!InitializeCameraBattle()) return false;
#endif
    state->cameraRuntime = camera::CaptureRuntimeState();
    return true;
}

bool BattleEmulator::IsHeroCommandSelectable(const SearchState& state,
                                             const SearchCommand command) noexcept {
    const Player& hero = state.players[0];
    if (hero.hp <= 0) return false;

    if (command.target != -1) {
        if (command.target < 1 || command.target > 3) return false;
        if (state.players[command.target].hp <= 0) return false;
    }

    switch (command.action) {
        case MIDHEAL:
            return hero.mp >= 4;
        case DEFENDING_CHAMPION:
            return hero.mp >= 2;
        case MAGIC_MIRROR:
            return hero.mp >= 4;
        case MORE_HEAL:
            return hero.mp >= 8;
        case FULLHEAL:
            return hero.mp >= 24;
        case SPECIAL_MEDICINE:
            return hero.SpecialMedicineCount > 0;
        case MAGIC_WATER:
            return hero.MagicWaterCount > 0;
        case SAGE_ELIXIR:
            return hero.SageElixirCount > 0;
        case ELFIN_ELIXIR:
            return hero.ElfinElixirCount > 0;
        case BUFF:
            return hero.mp >= 3;
        case MULTITHRUST:
            return hero.mp >= 4;
        case GOSPEL_SONG:
            return hero.specialCharge && hero.specialChargeTurn >= 1;
        case INSULATE:
            return hero.mp >= 4;
        case VITAL_POINT_THRUST:
            return hero.mp >= 3;
        case ZAKI:
            return hero.mp >= 5;
        case ZARAKI:
            return hero.mp >= 10;
        default:
            return true;
    }
}

bool BattleEmulator::StepSearchState(const SearchState& source, const SearchCommand command,
                                     SearchState* destination,
                                     BattleResult* result, const bool traceBoundaries) {
    if (destination == nullptr || command.action <= 0) return false;

    const bool searchFastPath = result == nullptr && !traceBoundaries;

    for (int i = 0; i < 4; ++i) destination->players[i] = source.players[i];
    destination->position = source.position;
    destination->nowState = source.nowState;
    if (searchFastPath) {
        // Search owns both snapshots. Copy the parent camera state once into
        // the child slot, then let production camera code mutate that child
        // snapshot directly instead of copying through thread-local storage
        // twice per edge.
        destination->cameraRuntime = source.cameraRuntime;
        camera::BindRuntimeState(&destination->cameraRuntime);
    } else {
        camera::RestoreRuntimeState(source.cameraRuntime);
    }

    const int mode = result != nullptr ? -1 : -2;
    if (result != nullptr) *result = BattleResult{};
    const int packedAction = PackHeroAction(command.action, command.target);
    Main(&destination->position, 1, nullptr, destination->players, result,
         0, nullptr, nullptr, mode, &destination->nowState,
         -1, traceBoundaries, packedAction, false);

    if (searchFastPath) {
        camera::UnbindRuntimeState();
    } else {
        destination->cameraRuntime = camera::CaptureRuntimeState();
    }
    return true;
}

bool BattleEmulator::StepSearchStateInPlace(SearchState* state, const SearchCommand command,
                                            BattleResult* result, const bool traceBoundaries) {
    if (state == nullptr || command.action <= 0) return false;

    const bool searchFastPath = result == nullptr && !traceBoundaries;
    if (searchFastPath) camera::BindRuntimeState(&state->cameraRuntime);
    else camera::RestoreRuntimeState(state->cameraRuntime);

    const int mode = result != nullptr ? -1 : -2;
    if (result != nullptr) *result = BattleResult{};
    const int packedAction = PackHeroAction(command.action, command.target);
    Main(&state->position, 1, nullptr, state->players, result,
         0, nullptr, nullptr, mode, &state->nowState,
         -1, traceBoundaries, packedAction, false);

    if (searchFastPath) camera::UnbindRuntimeState();
    else state->cameraRuntime = camera::CaptureRuntimeState();
    return true;
}

double BattleEmulator::FUN_021dbc04(int baseHp, double maxHp) {
    auto hp = static_cast<double>(baseHp);
    if (hp == 0) {
        return 0;
    }
    return hp / maxHp;
}

//コンパイラが毎回コピーするコードを生成するからグローバルスコープに追い出しとく
const int proportionTable2[9] = {90, 90, 64, 32, 16, 8, 4, 2, 1}; //最後の項目を調べるのは手動　P:\lua\isilyudaru\hissatuteki.lua
// double proportionTable1[9] = {0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 3.0, 0.2, 0.1};// 21/70が2.99999...になるから最初から20/70より大きい2.89にしちゃう
constexpr double Enemy_TensionTable[4] = {1.3, 2.0, 3.0, 4.5}; //一部の敵は特殊テンションテーブルを倍率として使う


int BattleEmulator::callAttackFun(int32_t Id, int *position, Player *players, int attacker, int defender,
                                  uint64_t *NowState, const bool targetWasGuardRedirect) {
    for (int j = 0; j < 4; ++j) {
        preHP[j] = players[j].hp;
        multithrustDamageByTarget[j] = 0;
    }
    actions[actionsPosition] = Id;
    actionActors[actionsPosition] = attacker >= 0 && attacker < 4 ? battleActorRefs[attacker] : dq9::freecam::fast::BattleActorRef{};
    actionTargets[actionsPosition] = defender >= 0 && defender < 4 ? battleActorRefs[defender] : dq9::freecam::fast::BattleActorRef{};
    actionPresentationSlot1ChildCounts[actionsPosition] = 0;
    actionPresentationSlot1LastChildActionIds[actionsPosition] = UINT16_C(0xffff);
    ++actionsPosition;
    int baseDamage = 0;
    double tmp, tmp1 = 0;
    bool kaisinn = false;
    bool hasKaisinn = false;
    bool kaihi = false;
    bool tate = false;
    bool vitalPointInstantDeath = false;
    PhysicalAvoidanceResult physicalAvoidance{};
    int OffensivePower = players[attacker].defaultATK;
    int percent_tmp;
    auto totalDamage = 0;
    auto attackCount = 0;
    bool defenseFlag = false; //防御した場合0x021e81a0のほうが優先度高いらしい。なんで
    switch (Id & 0xffff) {
        case THIN_AIR:
            {
                (*position)+=2;
                (*position)++;//会心
                (*position)++;//不明
                (*position)++;//回避
                baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
                auto tmp2 = 34.0 * lcg::floatRand(position, -0.1, 0.1) + 34;
                baseDamage = static_cast<int>(tmp2);
                baseDamage = static_cast<int>(Equipments::applyDamageReduction(baseDamage, Attribute::Wind));
                (*position)++;//0x02158ac4
                (*position)++; //不明 0x021e54fc
                process7A8(position, baseDamage, players, defender);
                resetCombo(NowState);
            }
            break;
        case BattleEmulator::INSULATE:
            players[0].mp -= 4;
            (*position)++; // randIntRange(3,4), lr: 0x0216139c
            (*position)++; // randIntRange(6,8), lr: 0x021613b0
            (*position)++; // 関係ない
            (*position)++; // 会心判定
            (*position)++; // 回避

            baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            if (baseDamage == 0) {
                baseDamage = lcg::getPercent(position, 2); //0x021e81a0
            }
            if (baseDamage != 0) {
                (*position)++; //不明 0x021e54fc
            }

            if (!players[0].specialCharge && !players[0].sleeping && !players[0].paralysis) {
                (*position)++; //0x021ed7a8 必殺チャージ(敵) 0%
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
                }
            }

            if (players[0].InsulateLevel != 2) {
                players[0].InsulateLevel++;
                players[0].InsulateTurns = 6;
                RecalculateBuff(players, attacker);
            }

            baseDamage = 0;
            resetCombo(NowState);
            break;
        case PSYCHE_UP_ALLY:
            (*position)++; // randIntRange(3,4), lr: 0x0216139c
            (*position)++; // randIntRange(6,8), lr: 0x021613b0
            (*position)++; // 0x021ec6f8
            (*position)++; // 0x02158584 会心
            (*position)++; // 0x02157f58 偽回避
            baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            if (baseDamage == 0) {
                baseDamage = lcg::getPercent(position, 2); //0x021e81a0
            }
            if (players[attacker].TensionLevel < 3 || (players[attacker].TensionLevel == 3 && lcg::getPercent(position, 2) == 0)) {
                //0x02087fb4 テンション
                players[attacker].TensionLevel++;
            }
            if (!players[0].specialCharge && !players[0].sleeping && !players[0].paralysis) {
                (*position)++; //0x021ed7a8 必殺チャージ(敵) 0%
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
                }
            }
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case GOSPEL_SONG:
            (*position)++; // randIntRange(3,4), lr: 0x0216139c
            (*position)++; // randIntRange(6,8), lr: 0x021613b0
            (*position)++; //0x02158584 会心
            (*position)++; //0x021ec6f8 不明
            (*position)++; //0x02157f58 ニセ回避
            baseDamage = FUN_0207564c(position, players[attacker].atk, players[attacker].def);
            if(baseDamage == 0){
                baseDamage = lcg::getPercent(position, 2); //0x021e81a0
            }
            if(baseDamage != 0){
                (*position)++; //不明 0x021e54fc
            }
            baseDamage = std::max(static_cast<int>(std::round(players[attacker].maxHp * 0.4)), 75);//(*code 24) 021e1cc4
            resetCombo(NowState);
            players[0].specialCharge = false;
            if(players[0].BuffLevel < 0){
                players[0].BuffLevel = 0;
                players[0].BuffTurns = -1;
                RecalculateBuff(players, 0);
            }
            break;
        case SPECIAL_MEDICINE:
            players[attacker].SpecialMedicineCount--;
            (*position)++; // randIntRange(3,4), lr: 0x0216139c
            (*position)++; // randIntRange(6,8), lr: 0x021613b0
            (*position)++; //0x021ec6f8 不明
            (*position)++; //0x02158584 会心
            (*position)++; //0x02157f58 ニセ回避
            baseDamage = FUN_021e8458_typeC(position, 105, 105, 15);
            (*position)++; //0x021e54fc
            if (!players[0].specialCharge) {
                (*position)++; //0x021ed7a8
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
                }
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
            if (!players[attacker].specialCharge) {
                (*position)++; //0x021ed7a8
                if (lcg::getPercent(position, 100) < 1) {
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
        case ELFIN_ELIXIR:
            players[attacker].ElfinElixirCount--;
            (*position) += 2;
            (*position)++; //0x021ec6f8 不明
            (*position)++; //0x02158584 会心
            (*position)++; //0x02157f58 ニセ回避
            baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            if (baseDamage == 0) {
                baseDamage = lcg::getPercent(position, 2); //0x021e81a0
            }
            if (baseDamage != 0) {
                (*position)++; //不明 0x021e54fc
            }
            if (!players[attacker].specialCharge) {
                (*position)++; //0x021ed7a8
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
                }
            }
            players[attacker].mp = players[attacker].maxMp;
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case SAGE_ELIXIR:
            players[attacker].SageElixirCount--;
            (*position) += 2;
            (*position)++; //0x021ec6f8 不明
            (*position)++; //0x02158584 会心
            (*position)++; //0x02157f58 ニセ回避
            baseDamage = FUN_021e8458_typeC(position, 95, 95, 5);
            (*position)++; //不明 0x021e54fc
            if (!players[attacker].specialCharge) {
                (*position)++; //0x021ed7a8
                if (lcg::getPercent(position, 100) < 1) {
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
        case THUNDER_THRUST: {
            players[attacker].mp -= 8;
            (*position) += 2;
            (*position)++; //不明 0x021ec6f8
            if (targetWasGuardRedirect) {
                (*position)++; // planned 03A1 target redirect, max:1, lr: 0x021ea6bc
            }
            (*position)++; //会心
            physicalAvoidance = ResolveHeroPhysicalAvoidance(position, defender);
            kaihi = physicalAvoidance.avoided();
            (*position)++; //0x02157f58 ニセ回避
            FUN_0207564c(position, players[attacker].atk, players[defender].def);
            const int thunderSelectorRoll = lcg::getPercent(position, 2); // lr: 0x021d9f48
            const bool thunderSelectorSucceeded = thunderSelectorRoll == 0;
            if (thunderSelectorSucceeded) {
                tmp = OffensivePower * lcg::floatRand(position, 0.95, 1.05);
                // Selector 45's success-side float RNG is consumed even when the hit was
                // already blocked. Weapon-element resistance is not applied to this direct result.
                baseDamage = static_cast<int>(tmp);
            } else {
                kaihi = true;
            }

            if (kaihi) {
                baseDamage = 0;
            } else {
                if (baseDamage != 0) {
                    ProcessRage(position, baseDamage, players, defender);
                    (*position)++; // damageによる状態回復, max: 100, lr: 0x02158ac4
                    (*position)++; // action後状態判定, max: 100, lr: 0x021e54fc
                }
            }
            if (thunderSelectorSucceeded) {
                // Selector success performs these three actor checks even when Block/Evade
                // forced the final damage to zero. Measured Block+success seed 0x43 consumes
                // exactly these six RNG calls while skipping 0x02158ac4/0x021e54fc.
                for (int rageActor = 1; rageActor < 4; ++rageActor) {
                    (*position)++; // rage判定, max: 100, lr: 0x021eb8c8
                    (*position)++;//(void)lcg::intRangeRand(position, 2, 4); // max: 3, lr: 0x021eb8f0
                }
            }
            if (!players[attacker].specialCharge && lcg::getPercent(position, 100) < 1) {
                players[attacker].specialCharge = true;
                players[attacker].specialChargeTurn = SpecialChargeTurns;
            }
            resetCombo(NowState);
            break;
        }
        case BattleEmulator::ZAKI: {
            players[attacker].mp -= 5;
            (*position)++; // RandIntRange(3,4), lr: 0x0216139c
            (*position)++; // RandIntRange(6,8), lr: 0x021613b0
            (*position)++; // max:100, lr: 0x021ec6f8
            if (targetWasGuardRedirect) {
                (*position)++; // planned 03A1 target redirect, max:1, lr: 0x021ea6bc
            }
            (*position)++; // critical RandInt(10000), threshold 100, lr: 0x02158584

            const int zakiDeathResistance = TargetDeathResistancePercent(defender);
            const int zakiDeathRoll = lcg::getPercent(position, 100); // lr: 0x02157f58; Death 0でも消費
            if (zakiDeathResistance > 0 && zakiDeathRoll < 30) {
                // 成功時だけgeneric physical baseを通す。内部damage値であり、
                // 武器Lightning倍率は掛からない。
                baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
                (*position)++; // max:100, lr: 0x021e54fc
                (*position)++; // max:100, lr: 0x021edaf4
                // 内部damageとは別の死亡side effect。
                players[defender].hp = 0;
            } else {
                baseDamage = 0;
                (*position)++; // max:100, lr: 0x021edaf4
            }
            resetCombo(NowState);
            break;
        }
        case BattleEmulator::ZARAKI: {
            players[attacker].mp -= 10;
            (*position)++; // RandIntRange(3,4), lr: 0x0216139c
            (*position)++; // RandIntRange(6,8), lr: 0x021613b0
            (*position)++; // critical RandInt(10000), threshold 100, lr: 0x02158584
            (*position)++; // max:100, lr: 0x021ec6f8
            if (targetWasGuardRedirect) {
                (*position)++; // planned 03A1 target redirect, max:1, lr: 0x021ea6bc
            }

            // Base status value 57 with Death 050 becomes round(28.5)=29.
            const int zarakiDeathResistance = TargetDeathResistancePercent(defender);
            const int zarakiDeathRoll = lcg::getPercent(position, 100); // lr: 0x02157f58; Death 0でも消費
            if (zarakiDeathResistance > 0 && zarakiDeathRoll < 29) {
                baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
                (*position)++; // max:100, lr: 0x021e54fc
                (*position)++; // max:100, lr: 0x021edaf4
                players[defender].hp = 0;
            } else {
                baseDamage = 0;
                (*position)++; // max:100, lr: 0x021edaf4
            }
            resetCombo(NowState);
            break;
        }
        case RESTORE_MP:
            (*position) += 2;
            (*position)++; //不明　0x021ec6f8
            (*position)++; //会心
            (*position)++; //ニセ回避 0x02157f58
            FUN_0207564c(position, players[attacker].atk, players[attacker].def);
            (*position)++; //不明 0x021e54fc
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case MAGIC_BURST:
            (*position) += 2;
            (*position)++; //会心
            (*position)++; //不明　0x021ec6f8
            (*position)++; //ニセ回避 0x02157f58
            FUN_0207564c(position, players[attacker].atk, players[defender].def);
            baseDamage = ProcessMagicBurst(position);
            (*position)++; //不明 0x021e54fc

            if (players[defender].TensionLevel == 4) {
                tmp = baseDamage * 0.5;
            } else {
                tmp = baseDamage;
            }
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                tmp *= players[0].defence;
            }
            baseDamage = static_cast<int>((tmp));

            process7A8(position, baseDamage, players, defender);
            resetCombo(NowState);
            break;
        case MEDITATION:
            (*position) += 2;
            (*position)++; //不明　0x021ec6f8
            (*position)++; //会心
            (*position)++; //ニセ回避 0x02157f58
            (*position)++; //float: 0x021e88d8 0x80000000 00000000 1204
            (*position)++; //不明 0x021e54fc
            baseDamage = 0;
            resetCombo(NowState);
            return 500;
        case DEFENDING_CHAMPION:
            (*position) += 2;
            (*position)++; //不明　0x021ec6f8
            (*position)++; //会心
            (*position)++; //ニセ回避 0x02157f58
            baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            if (baseDamage == 0) {
                //0x021e81a0
                baseDamage = lcg::getPercent(position, 2); //0x021e81a0
            }
            if (baseDamage != 0) {
                (*position)++; //0x021e54fc
            }
            if (!players[0].specialCharge) {
                // 不明
                (*position)++; //0x021ed7a8 必殺チャージ(敵)
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
                }
            }
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case DARK_BREATH:
            (*position) += 2;
            (*position)++; //会心
            (*position)++; //不明
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                if (lcg::getPercent(position, 100) < 2) {
                    kaihi = true;
                }
            }
            (*position)++; //ニセ回避 0x02157f58
            baseDamage = FUN_021e8458_typeD(position, 10, 65);
            tmp = Equipments::applyDamageReduction(baseDamage, Attribute::Darkness);
            tmp *= 1.0 - (players[0].InsulateLevel * 0.25);

            if (players[defender].TensionLevel == 4) {
                tmp *= 0.5;
            }

            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                tmp *= players[defender].defence;
            }
            baseDamage = static_cast<int>((tmp));
            if (!kaihi) {
                (*position)++; //0x021e54fc 不明
            } else {
                baseDamage = 0;
            }
            process7A8(position, baseDamage, players, defender);
            break;
        case PSYCHE_UP:
            (*position) += 2;
            (*position)++; //不明
            (*position)++; //会心
            (*position)++; //ニセ回避 0x02157f58
            FUN_0207564c(position, players[attacker].atk, players[attacker].def);
            players[attacker].TensionLevel++;
            baseDamage = 0;
            resetCombo(NowState);
            break;
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

            if (!players[0].specialCharge && !players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                (*position)++; //0x021ed7a8
            }

            if (!players[1].rage) {
                (*position)++; //0x021eb8c8
            }
            (*position)++; //? 0x021eb8f0
            if (!players[0].specialCharge && !players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                if (lcg::getPercent(position, 100) < 1) {
                    // 0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
                }
            }
            resetCombo(NowState);
            return 999;
            break;
        case DISRUPTIVE_WAVE:
            //TODO: 必殺チャージ無しのときに7a8があるかどうか調べる
            (*position) += 2;
            (*position)++; //0x021ec6f8 会心
            (*position)++; //0x021ec6f8 不明
            (*position)++; //ニセ回避 0x02157f58
            baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
            if (baseDamage == 0) {
                //0ダメージだと特殊消費がはいるっぽい。凍てつく波動だけの仕様であることを祈る
                baseDamage = lcg::getPercent(position, 2); // 0x021e81a0
            }
            //ダメージが0の場合0x021e54fcは発生しない模様。
            if (baseDamage != 0) {
                (*position)++; //不明 0x021e54fc
            }
            process7A8(position, 0, players, defender); //必殺チャージ(敵)　0x021ed7a8

            players[0].hasMagicMirror = false;
            players[0].MagicMirrorTurn = -1;
            players[0].MagicMirrorRecoveryTurn = 0;
            players[0].AtkBuffLevel = 0;
            players[0].AtkBuffTurn = -1;
            players[0].BuffLevel = 0;
            players[0].BuffTurns = -1;
            players[0].TensionLevel = 0;
            players[0].InsulateLevel = 0;
            players[0].InsulateTurns = -1;


            RecalculateBuff(players, defender);
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case MIDHEAL:
            players[attacker].mp -= 4;
            (*position) += 2;
            (*position)++; //0x021ec6f8 不明
            if (lcg::getPercent(position, 0x2710) < 100) {
                kaisinn = true;
            }
            (*position)++; //回避

            baseDamage = FUN_021e8458_typeD(position, 10, CalculateMidHealBase(players, attacker));
            if (kaisinn) {
                tmp1 *= lcg::floatRand(position, 1.5, 2.0); //TODO
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

            baseDamage = static_cast<int>((tmp));

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
            if (!players[0].paralysis) {
                if (!players[attacker].specialCharge && lcg::getPercent(position, 100) < 1) {
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
                }
            }
            resetCombo(NowState);
            break;
        case LULLAB_EYE:
            (*position) += 2;
            (*position)++; //0x021ec6f8 不明
            (*position)++; //会心 0x02158584
            (*position)++; //ニセ回避 0x02157f58
            baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
            if (baseDamage == 0) {
                //0x021e81a0
                baseDamage = lcg::getPercent(position, 2);
            }
            if (baseDamage != 0) {
                (*position)++; //不明 0x021e54fc
            }
            if (!players[0].paralysis) {
                players[0].sleeping = true;
                players[0].sleepingTurn = 2;
                players[0].TensionLevel = 0;
            }
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case LIGHTNING_STORM:
            (*position) += 2;
            (*position)++; //会心 0x02158584
            (*position)++; //0x021ec6f8 不明
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                //TODO
                if (lcg::getPercent(position, 100) < shieldGuardP) {
                    //盾ガード 0x021586fc
                    tate = true;
                }
            }
            (*position)++; //ニセ回避 0x02157f58 100%
            baseDamage = FUN_021e8458_typeD(position, 15, 80);
            tmp = Equipments::applyDamageReduction(baseDamage, Attribute::ThunderExplosion);
            if (tate) {
                baseDamage = 0;
            } else {
                (*position)++; //?? 0x02158ac4
                (*position)++; //?? 0x021e54fc
                if (baseDamage != 0 && players[0].sleeping) {
                    players[0].sleeping = false;
                    players[0].sleepingTurn = -1;
                }
                if (players[defender].TensionLevel == 4) {
                    tmp *= 0.5;
                }
                if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                    tmp *= players[defender].defence;
                }
                baseDamage = static_cast<int>((tmp));
            }
            process7A8(position, baseDamage, players, defender);

            resetCombo(NowState);
            break;
        case MULTITHRUST:
            players[attacker].mp -= 4;
            attackCount = lcg::intRangeRand(position, 3, 4);
            (*position)++;
            {
                int aliveTargets[3]{};
                int aliveTargetCount = 0;
                for (int enemy = 1; enemy <= 3; ++enemy) {
                    if (Player::isPlayerAlive(players[enemy])) {
                        aliveTargets[aliveTargetCount++] = enemy;
                    }
                }
                if (aliveTargetCount == 0) {
                    resetCombo(NowState);
                    return 0;
                }

                int hitTargets[4]{};
                for (int hit = 0; hit < attackCount; ++hit) {
                    // ROM lr=0x02155b24. All hit targets are selected before
                    // the first damage calculation. The RandInt max is the
                    // number of living enemies on the opposing side.
                    hitTargets[hit] = aliveTargets[lcg::getPercent(position, aliveTargetCount)];
                }
            hasKaisinn = false;
            for (int i = 0; i < attackCount; ++i) {
                const int hitDefender = hitTargets[i];
                kaihi = false;
                kaisinn = false;
                (*position)++; //0x021ec6f8 不明
                if (attackCount == 4) {
                    if (lcg::getPercent(position, 0x2710) < multithrust4KaisinnP) {
                        kaisinn = true;
                        hasKaisinn = true;
                    }
                } else {
                    if (lcg::getPercent(position, 0x2710) < multithrust3KaisinnP) {
                        kaisinn = true;
                        hasKaisinn = true;
                    }
                }
                physicalAvoidance = ResolveHeroPhysicalAvoidance(position, hitDefender);
                kaihi = physicalAvoidance.avoided();

                (*position)++; //ニセ回避 0x02157f58 100%
                baseDamage = FUN_0207564c(position, players[attacker].atk, players[hitDefender].def);

                tmp = floor(baseDamage * 0.5);
                if (kaisinn == true) {
                    tmp1 = tmp * lcg::floatRand(position, 1.5, 2.0);
                }

                if (players[attacker].TensionLevel != 0) {
                    //TODO ダメージが正しいか調べる 特殊県産式の引数も調べる https://dragonquest9.com/?%E3%83%80%E3%83%A1%E3%83%BC%E3%82%B8%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6#tension
                    tmp *= Ally_TensionTable[players[attacker].TensionLevel - 1];
                    tmp += (players[attacker].TensionLevel * Ally_TensionLevel);
                }

                if (kaisinn) {
                    if (tmp * 1.2000 <= tmp1) {
                        tmp = tmp1;
                    } else {
                        tmp *= 1.2000;
                    }
                }

                //ここの小数点以下は引き継がれる
                tmp *= HeroSpearLightningMultiplier(attacker, hitDefender);
                baseDamage = static_cast<int>((tmp));

                if (!kaihi) {
                    ProcessRage(position, baseDamage, players, hitDefender);
                    (*position)++; //目を覚ました
                    (*position)++; //不明 0x021e54fc
                } else {
                    baseDamage = 0;
                }

                preHP[hitDefender] = std::max(0, preHP[hitDefender] - baseDamage);
                multithrustDamageByTarget[hitDefender] += baseDamage;
                totalDamage += baseDamage;
                if (preHP[1] <= 0 && preHP[2] <= 0 && preHP[3] <= 0) {
                    return totalDamage;
                }
            }
            if (hasKaisinn) {
                (*position) += 2;
            }
            if (preHP[defender] > 0) {
                if (!players[attacker].specialCharge && lcg::getPercent(position, 100) < 1) {
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
                }
            }
            //0x021ec6f8が多分の残りの攻撃回数だけ発生する

            if (players[attacker].TensionLevel != 0) {
                players[attacker].TensionLevel = 0;
            }
            resetCombo(NowState);
            return totalDamage;
            }
        case MERA_ZOMA:
            (*position) += 2;
            (*position)++; //0x021ec6f8 不明
            (*position)++; //敵の会心判定
            if (players[0].hasMagicMirror) {
                if (lcg::getPercent(position, 0x2710) < 100) {
                    //こっちの会心判定
                    kaisinn = true;
                }
                (*position)++; //盾ガード 0x021586fc 0%
                (*position)++; //ニセ回避 0x02157f58 100%
                tmp = BattleEmulator::FUN_021e8458_typeD(position, 12, 190);
                if (kaisinn) {
                    tmp *= lcg::floatRand(position, 1.5, 2.0);
                }
                tmp *= 1.25;
                tmp = processCombo(Id & 0xffff, tmp, NowState);
                baseDamage = static_cast<int>((tmp));
                (*position)++; //不明 0x021e54fc
                ProcessRage(position, baseDamage, players, defender);
            } else {
                if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                    if (lcg::getPercent(position, 100) < shieldGuardP) {
                        //TODO 盾の条件調べる
                        tate = true;
                    }
                }
                (*position)++; //ニセ回避 0x02157f58 100%
                baseDamage = FUN_021e8458_typeD(position, 12, 116);
                tmp = Equipments::applyDamageReduction(baseDamage, Attribute::Fire);
                if (players[defender].TensionLevel == 4) {
                    tmp *= 0.5;
                }
                if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                    tmp *= players[defender].defence;
                }
                tmp = processCombo(Id & 0xffff, tmp, NowState);
                baseDamage = static_cast<int>((tmp));
                if (!tate) {
                    (*position)++; //0x021e54fc 不明
                } else {
                    baseDamage = 0;
                }
                process7A8(position, baseDamage, players, defender); //必殺チャージ(敵)　0x021ed7a8
            }
            break;
        case KASWOOSH:
            (*position) += 2;
            (*position)++; //0x021ec6f8 不明
            (*position)++; //敵の会心判定
            (*position)++; //ニセ回避 0x02157f58 100%
            baseDamage = FUN_021e8458_typeD(position, 50, 80);
            tmp = Equipments::applyDamageReduction(baseDamage, Attribute::Wind);
            if (players[defender].TensionLevel == 4) {
                tmp *= 0.5;
            }
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                tmp *= players[defender].defence;
            }
            tmp = processCombo(Id & 0xffff, tmp, NowState);
            baseDamage = static_cast<int>((tmp));
            (*position)++; //0x021e54fc 不明
            process7A8(position, baseDamage, players, defender); //必殺チャージ(敵)　0x021ed7a8
            break;
        case LIGHTNING:
            (*position) += 2;
            (*position)++; //0x021ec6f8 不明
            (*position)++; //敵の会心判定
            (*position)++; //ニセ回避 0x02157f58 100%
            baseDamage = FUN_021e8458_typeD(position, 8, 36);

            tmp = static_cast<double>(baseDamage);

            if (players[attacker].TensionLevel != 0) {
                //TODO ダメージが正しいか調べる 特殊県産式の引数も調べる https://dragonquest9.com/?%E3%83%80%E3%83%A1%E3%83%BC%E3%82%B8%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6#tension
                tmp *= Enemy_TensionTable[players[attacker].TensionLevel - 1];
                tmp += (players[attacker].TensionLevel * TensionLevel); //4 = 1*(1+(30/10))
                players[attacker].TensionLevel = 0;
            }
            
            tmp = Equipments::applyDamageReduction(tmp, Attribute::ThunderExplosion);
            if (players[defender].TensionLevel == 4) {
                tmp *= 0.5;
            }
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                tmp *= players[defender].defence;
            }
            tmp = processCombo(Id & 0xffff, tmp, NowState);
            baseDamage = static_cast<int>((tmp));
            (*position)++; //0x021e54fc 不明
            process7A8(position, baseDamage, players, defender); //必殺チャージ(敵)　0x021ed7a8
            break;
        case BattleEmulator::FREEZING_BLIZZARD:
            (*position) += 2;
            (*position)++; // 会心判定
            (*position)++; //0x021ec6f8 不明
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                if (lcg::getPercent(position, 100) < 2) {
                    //0x021587b0 本物みかわし
                    kaihi = true;
                }
            }
            (*position)++; // 0x02157f58 ニセ回避

            baseDamage = FUN_021e8458_typeD(position, 15, 75);
            tmp = Equipments::applyDamageReduction(baseDamage, Attribute::Ice);
            tmp *= 1.0 - (players[0].InsulateLevel * 0.25);
            if (!kaihi) {
                (*position)++; //0x021e54fc 不明
            } else {
                tmp = 0;
            }
            if (players[defender].TensionLevel == 4) {
                tmp *= 0.5;
            }
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                tmp *= players[defender].defence;
            }
            baseDamage = static_cast<int>((tmp));
            process7A8(position, baseDamage, players, defender);
            resetCombo(NowState);
            break;
        case BattleEmulator::DOUBLE_UP:
            (*position) += 2;
            (*position)++; //関係ない
            (*position)++; //会心
            (*position)++; //回避
            baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            if (baseDamage == 0) {
                baseDamage = lcg::getPercent(position, 2); //0x021e81a0
            }
            (*position)++; //かぶとわりの判定　0x021e3e7c
            if (baseDamage != 0) {
                (*position)++; //不明 0x021e54fc
            }
            if (!players[attacker].specialCharge) {
                (*position)++; //必殺(敵)　0x021ed7a8
                if (!players[0].paralysis) {
                    if (lcg::getPercent(position, 100) < 1) {
                        players[attacker].specialCharge = true;
                        players[attacker].specialChargeTurn = SpecialChargeTurns;
                    }
                }
            }

            if (players[0].AtkBuffLevel != 2) {
                players[0].AtkBuffLevel += 2;
                players[0].AtkBuffLevel = std::min(players[0].AtkBuffLevel, 2);
                players[0].AtkBuffTurn = 6;
            }

            if (players[0].BuffLevel != -2) {
                players[0].BuffLevel--;
                players[0].BuffTurns = 7;
            }

            RecalculateBuff(players, attacker);
            resetCombo(NowState);
            baseDamage = 0;
            break;
        case BattleEmulator::MORE_HEAL:
            players[attacker].mp -= 8;
            (*position) += 2;
            (*position)++; //関係ない
            if (lcg::getPercent(position, 0x2710) < 100) {
                kaisinn = true;
            }
            (*position)++; //回避

            baseDamage = FUN_021e8458_typeD(position, 20, CalculateMoreHealBase(players, attacker));
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
            baseDamage = static_cast<int>((tmp));

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
            if (!players[0].paralysis) {
                if (!players[attacker].specialCharge && lcg::getPercent(position, 100) < 1) {
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
                }
            }
            resetCombo(NowState);
            break;
        case BattleEmulator::MAGIC_MIRROR:
        case BattleEmulator::GERUNIKKU_MAGIC_MIRROR:
            if (players[attacker].mp != 255) {
                players[attacker].mp = std::max(0, players[attacker].mp - 4);
            }
            (*position) += 5;
            baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            if (baseDamage == 0) {
                baseDamage = lcg::getPercent(position, 2); //0x021e81a0
            }
            if (baseDamage != 0) {
                (*position)++; //不明 0x021e54fc
            }
            if (attacker == 0 && !players[0].specialCharge) {
                (*position)++; //0x021ed7a8
                if (lcg::getPercent(position, 100) < 1) {
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
                }
            }
            // Real ROM: Mirror Shield sets live combat status +0x14 bit 0x200.
            players[attacker].hasMagicMirror = true;
            players[attacker].MagicMirrorTurn = 5; // FUN_020891f0: combat +0x61
            players[attacker].MagicMirrorRecoveryTurn = 0; // combat +0x84
            resetCombo(NowState);
            baseDamage = 0;
            break;
        case BattleEmulator::CRITICAL_ATTACK:
            (*position) += 2;
            (*position)++; // アクロバットスターとか

            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                if (lcg::getPercent(position, 100) < 2) {
                    kaihi = true;
                }
                if (!kaihi && lcg::getPercent(position, 100) < shieldGuardP) {
                    tate = true;
                }
            }
            (*position)++; //回避

            FUN_0207564c(position, players[attacker].atk, players[defender].def);
            baseDamage = static_cast<int>((players[1].defaultATK * lcg::floatRand(position, 0.8500, 0.9500)));

            //TODO: この処理を直す
            if (baseDamage != 0) {
                players[0].sleeping = false;
                players[0].sleepingTurn = -1;
            }

            if (kaihi) {
                if (!players[0].paralysis && !players[0].sleeping && !players[0].specialCharge && !players[0].inactive) {
                    (*position)++; //0x021ed7a8
                }
                baseDamage = 0;
            } else if (tate) {
                if (!players[0].paralysis && !players[0].sleeping && !players[0].specialCharge && !players[0].inactive) {
                    (*position)++; //0x021ed7a8
                }
                baseDamage = 0;
            } else {
                if (baseDamage == 0) {
                    baseDamage = lcg::getPercent(position, 2); //TODO: 0x021e81a0
                }

                if (baseDamage != 0) {
                    (*position)++; //目を覚ました
                    (*position)++; //不明
                }
                if (players[defender].TensionLevel == 4) {
                    tmp = baseDamage * 0.5;
                } else {
                    tmp = baseDamage;
                }
                if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                    tmp *= players[defender].defence;
                }
                baseDamage = static_cast<int>((tmp));

                process7A8(position, baseDamage, players, defender);
            }


            players[defender].sleeping = false;
            players[defender].sleepingTurn = -1;


            resetCombo(NowState);
            break;
        case BattleEmulator::BUFF:
            players[0].mp -= 3;
            (*position) += 2;
            (*position)++; // 関係ない
            (*position)++; // 会心判定
            (*position)++; // 回避

            baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            if (baseDamage == 0) {
                baseDamage = lcg::getPercent(position, 2); //0x021e81a0
            }
            if (baseDamage != 0) {
                (*position)++; //不明 0x021e54fc
            }

            if (!players[0].specialCharge && !players[0].sleeping && !players[0].paralysis) {
                (*position)++; //0x021ed7a8 必殺チャージ(敵) 0%
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
                }
            }

            if (players[0].BuffLevel != 2) {
                players[0].BuffLevel++;
                players[0].BuffTurns = 7;
                RecalculateBuff(players, attacker);
            }

            baseDamage = 0;
            resetCombo(NowState);
            break;
        case BattleEmulator::SCEPTER_BALL:
            (*position) += 2;
            (*position) += 2;
            for (int i = 0; i < 2; ++i) {
                if (preHP[defender] > totalDamage) {
                    //hp0時特殊消費
                    defenseFlag = false;
                    kaihi = false;
                    tate = false;
                    (*position)++; // アクロバットスターとか

                    (*position)++; //会心
                    if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                        if (lcg::getPercent(position, 100) < 2) {
                            // = 10%
                            kaihi = true;
                        }
                        if (!kaihi && lcg::getPercent(position, 100) < shieldGuardP) {
                            //TODO 盾の条件調べる 盾ガード
                            tate = true;
                        }
                    }
                    (*position)++; //回避

                    tmp = FUN_0207564c(position, players[attacker].atk, players[defender].def) * 0.5 + 37;
                    baseDamage = static_cast<int>(tmp);

                    if (kaihi || tate) {
                        if (!players[0].paralysis && !players[0].sleeping && !players[0].specialCharge && !players[0].inactive) {
                            (*position)++; //0x021ed7a8
                        }
                        baseDamage = 0;
                    } else {
                        if (baseDamage == 0) {
                            //&& players[0].defence != 0.1
                            baseDamage = lcg::getPercent(position, 2); //TODO: 0x021e81a0
                            if (baseDamage == 1) {
                                defenseFlag = true;
                            }
                        }
                        tmp = static_cast<double>(baseDamage);
                        if (players[defender].TensionLevel == 4) {
                            tmp *= 0.5;
                        }

                        if (!defenseFlag && !players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                            tmp = tmp * players[defender].defence;
                        }
                        baseDamage = static_cast<int>((tmp));

                        if (baseDamage != 0) {
                            (*position)++; //目を覚ました
                            (*position)++; //不明
                        }


                        if (baseDamage != 0 && players[0].sleeping) {
                            players[0].sleeping = false;
                            players[0].sleepingTurn = -1;
                        }

                        //hp0時特殊消費
                        if (preHP[defender] > (totalDamage + baseDamage)) {
                            process7A8(position, baseDamage, players, defender);
                        }
                    }
                } else {
                    //hp0時特殊消費
                    baseDamage = 0;
                    (*position)++; //0x021ec6f8
                }
                totalDamage += baseDamage;
            }

            resetCombo(NowState);
            return totalDamage;
        case BattleEmulator::MEDICINAL_HERBS:
            players[0].SpecialMedicineCount--;
            (*position) += 2;
            (*position)++; // 関係ない
            (*position)++; // 会心判定
            (*position)++; // 回避
            baseDamage = FUN_021e8458_typeC(position, 35.0, 35.0, 5.0);
            (*position)++; // 不明
            if (!players[attacker].specialCharge) {
                (*position)++; // 必殺チャージ(敵)　0%
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021ed7a8
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
                }
            }
            resetCombo(NowState);
            break;
        case BattleEmulator::SLEEPING:
        case BattleEmulator::CURE_SLEEPING:
            (*position) += 2;
            (*position)++; //関係ない
            (*position)++; //会心
            (*position)++; //回避
            FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            (*position)++; //不明
            if (!players[attacker].specialCharge && !players[attacker].sleeping && !players[attacker].paralysis) {
                (*position)++; //必殺チャージ(敵)
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021ed7a8
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
                }
            }
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case BattleEmulator::DEFENCE:
            (*position) += 2;
            (*position)++; //関係ない
            (*position)++; //会心
            (*position)++; //回避
            baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            if (baseDamage == 0) {
                baseDamage = lcg::getPercent(position, 2); //0x021e81a0
            }
            if (baseDamage != 0) {
                (*position)++; //不明 0x021e54fc
            }
            if (!players[attacker].specialCharge && !players[attacker].sleeping && !players[attacker].paralysis) {
                (*position)++; //必殺チャージ(敵) 0x021ed7a8
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
                }
            }
            baseDamage = 0;
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
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
                }
            }
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case BattleEmulator::LAUGH:
            (*position) += 2;
            (*position)++; //関係ない
            (*position)++; //会心
            (*position)++; //回避
            FUN_0207564c(position, players[attacker].atk, players[attacker].def);
            (*position)++; //不明
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case BattleEmulator::BURNING_BREATH:
            (*position) += 2;
            (*position)++; //会心
            (*position)++; //関係ない
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                //TODO 眠ってるときのやけつくいき
                if (lcg::getPercent(position, 100) < 2) {
                    //0x021587b0 回避
                    kaihi = true;
                }
            }
#if defined(SUPER)
            (*position)++;
            if (false) {
#else
            if (lcg::getPercent(position, 100) < 13 && !kaihi) {
#endif

                if (players[defender].paralysisLevel == 3) {
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
                if (baseDamage == 0) {
                    baseDamage = lcg::getPercent(position, 2); // 0x021e81a0
                }

                if (baseDamage != 0) {
                    //TODO 0ダメージのときの消費を調べる
                    (*position)++; //0x021e54fc
                }
            }
            if (!players[0].paralysis && !players[0].sleeping && !players[0].specialCharge && !players[0].inactive) {
                (*position)++; //0x021ed7a8 必殺(敵)
            }
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case BattleEmulator::KABUFF:
            if (players[attacker].mp != 255) {
                players[attacker].mp = std::max(0, players[attacker].mp - 6);
            }
            (*position) += 2;
            (*position)++; // 関係ない
            (*position)++; // 会心判定
            (*position)++; // 回避

            baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[defender].def);
            if (baseDamage == 0) {
                baseDamage = lcg::getPercent(position, 2); //0x021e81a0
            }
            if (baseDamage != 0) {
                (*position)++; //不明 0x021e54fc
            }

            if (Player::isPlayerAlive(players[defender]) && players[defender].BuffLevel < 2) {
                players[defender].BuffLevel++;
                players[defender].BuffTurns = 7;
                RecalculateBuff(players, defender);
            }

            baseDamage = 0;
            resetCombo(NowState);
            break;
        case BattleEmulator::WHIPPING_BOY:
            (*position) += 5;
            baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[defender].def);
            if (baseDamage == 0) {
                baseDamage = lcg::getPercent(position, 2);
            }
            if (baseDamage != 0) {
                (*position)++;
            }
            players[defender].guardedBy = attacker;
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case BattleEmulator::EERIE_LIGHT:
            if (players[attacker].mp != 255) {
                players[attacker].mp = std::max(0, players[attacker].mp - 6);
            }
            (*position)++; // RandIntRange(3,4), lr: 0x0216139c
            (*position)++; // RandIntRange(6,8), lr: 0x021613b0
            (*position)++; // max: 100, lr: 0x021ec6f8
            (*position)++; // max: 10000, lr: 0x02158584
            // seed 0x1A turn 3 failure path: threshold=50, roll=87.
            // Eerie Light is a status operation; it does not run physical damage.
            if (lcg::getPercent(position, 100) < 50) { // lr: 0x02157f58
                if (players[defender].magicResistanceLevel > -2) {
                    --players[defender].magicResistanceLevel;
                }
                // seed 0x2A実測: 成功時もgeneric physical-baseを通る。
                // ATK125/DEF282では0なのでfloat RNGなしで021e81a0へ進み、
                // 021e81a0の0/1結果を内部damageとして扱う。1ならHP damageには
                // 反映しないが、ROMは021e54fcを通ってから021ed7a8へ進む。
                baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
                if (baseDamage == 0) {
                    baseDamage = lcg::getPercent(position, 2); // lr: 0x021e81a0
                }
                if (baseDamage != 0) {
                    (*position)++; // lr: 0x021e54fc
                }
                (*position)++; // max: 100, lr: 0x021ed7a8
            } else {
                (*position)++; // max: 100, lr: 0x021ed7a8
            }
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case BattleEmulator::GERUNIKKU_MEDAPANI:
            if (players[attacker].mp != 255) {
                players[attacker].mp = std::max(0, players[attacker].mp - 5);
            }
            (*position)++; // RandIntRange(3,4), lr: 0x0216139c
            (*position)++; // RandIntRange(6,8), lr: 0x021613b0
            (*position)++; // 0x021ec6f8
            (*position)++; // 0x02158584
            // Base success 25.0 * this player's confusion resistance multiplier 0.75,
            // rounded by +0.5 then truncation in FUN_021581f8 => 19.
            // RandInt(100), lr: 0x02157f58.
            if (lcg::getPercent(position, 100) < 19) {
                players[defender].confused = true;
                players[defender].confusionTurns = 3;
                // メダパニ成立時は、そのターンに選択済みの「ぼうぎょ」を解除する。
                // seed 0x1A 実測: 後続の通常攻撃は raw physical damage 5 のまま通る。
                players[defender].defence = 1.0;
                // Successful status application enters the zero-damage result path.
                (*position)++; // max: 2, lr: 0x021e81a0
                (*position)++; // max: 100, lr: 0x021e54fc
            } else {
                (*position)++; // max: 100, lr: 0x021ed7a8
            }
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case BattleEmulator::CONFUSION_CANT_DECIDE:
        case BattleEmulator::CONFUSION_FAILED_ATTACK:
        case BattleEmulator::CONFUSION_FAILED_FLEE:
        case BattleEmulator::CURE_CONFUSION:
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case BattleEmulator::CONFUSION_TO_PARALYSIS:
            // DQ9 action 0x0393, operation type 24.
            // 0x021ffda8 -> FUN_021de52c -> FUN_02088b68.
            // FUN_02088b68 sets the primary status countdown to 3 at 0x02088bc0.
            players[attacker].confused = false;
            players[attacker].confusionTurns = -1;
            players[attacker].paralysis = true;
            players[attacker].paralysisTurns = 3;
            players[attacker].paralysisLevel = 0;
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case BattleEmulator::GERUNIKKU_MERAMI:
            (*position) += 2;
            (*position)++; // 0x021ec6f8
            (*position)++; // 0x02158584
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                if (lcg::getPercent(position, 100) < shieldGuardP) {
                    tate = true;
                }
            }
            (*position)++; // 0x02157f58
            baseDamage = FUN_021e8458_typeD(position, 10, 62);
            tmp = Equipments::applyDamageReduction(baseDamage, Attribute::Fire);
            tmp *= 1.0 - 0.25 * players[defender].magicResistanceLevel;
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                tmp *= players[defender].defence;
            }
            baseDamage = tate ? 0 : static_cast<int>(tmp);
            if (!tate) {
                (*position)++; // 0x021e54fc
            }
            process7A8(position, baseDamage, players, defender);
            break;
        case BattleEmulator::GERUNIKKU_BAGIMA:
            (*position) += 2;
            (*position)++; // 0x02158584
            (*position)++; // 0x021ec6f8
            (*position)++; // 0x02157f58
            baseDamage = FUN_021e8458_typeD(position, 15, 29);
            tmp = Equipments::applyDamageReduction(baseDamage, Attribute::Wind);
            tmp *= 1.0 - 0.25 * players[defender].magicResistanceLevel;
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                tmp *= players[defender].defence;
            }
            baseDamage = static_cast<int>(tmp);
            (*position)++; // 0x021e54fc
            process7A8(position, baseDamage, players, defender);
            break;
        case BattleEmulator::GERUNIKKU_BAGIMA_STRONG:
            (*position) += 2;
            (*position)++; // 0x021ec6f8
            (*position)++; // 0x02157f58
            baseDamage = FUN_021e8458_typeD(position, 21, 44);
            tmp = Equipments::applyDamageReduction(baseDamage, Attribute::Wind);
            tmp *= 1.0 - 0.25 * players[defender].magicResistanceLevel;
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                tmp *= players[defender].defence;
            }
            baseDamage = static_cast<int>(tmp);
            (*position)++; // 0x021e54fc
            process7A8(position, baseDamage, players, defender);
            break;
        case BattleEmulator::ATTACK_ENEMY:
        case BattleEmulator::SKY_ATTACK:
        case ATTACK_ENEMY_A6:
        case BattleEmulator::HELM_SPLITTER:
        case BattleEmulator::DOUBLE_EDGED_SLASH:
            (*position) += 2;
            (*position)++; // アクロバットスターとか

            (*position)++; //会心
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                if (lcg::getPercent(position, 100) < 2) {
                    kaihi = true;
                }
                if (!kaihi && lcg::getPercent(position, 100) < shieldGuardP) {
                    tate = true;
                }
            }
            (*position)++; //回避

            baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);

            if (kaihi) {
                //                if (baseDamage == 0) {
                //                    (*position)++;//0x021e81a0
                //                }

                if (!players[0].paralysis && !players[0].sleeping && !players[0].specialCharge && !players[0].inactive) {
                    (*position)++; //0x021ed7a8
                }
                baseDamage = 0;
            } else if (tate) {
                //                if (baseDamage == 0) {
                //                    (*position)++;//0x021e81a0
                //                }

                if (!players[0].paralysis && !players[0].sleeping && !players[0].specialCharge && !players[0].inactive) {
                    (*position)++; //0x021ed7a8
                }
                baseDamage = 0;
            } else {
                tmp = static_cast<double>(baseDamage);
                if ((Id & 0xffff) == BattleEmulator::SKY_ATTACK) {
                    tmp = floor(tmp * 1.5);
                }
                if ((Id & 0xffff) == BattleEmulator::DOUBLE_EDGED_SLASH) {
                    // Selector 36: trunc(1.5 * incoming), selector-side RNGなし。
                    tmp = floor(tmp * 1.5);
                }

                //テンションがある場合、この時点でオフセットが計算されて、最低4ダメージが保証されて下の0x021e81a0でダメージがある判定になる。
                //1*(1+(30/10))で4ダメージが保証されるけど、事前に計算して定数にしとく。
                if (players[attacker].TensionLevel != 0) {
                    //TODO ダメージが正しいか調べる 特殊県産式の引数も調べる https://dragonquest9.com/?%E3%83%80%E3%83%A1%E3%83%BC%E3%82%B8%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6#tension
                    tmp *= Enemy_TensionTable[players[attacker].TensionLevel - 1];
                    tmp += (players[attacker].TensionLevel * TensionLevel); //4 = 1*(1+(30/10))
                    players[attacker].TensionLevel = 0;
                }

                baseDamage = static_cast<int>((tmp));


                //防御が適応される時期を調べる
                if (baseDamage == 0) {
                    // && players[0].defence != 0.1
                    baseDamage = lcg::getPercent(position, 2); //TODO: 0x021e81a0
                    if (baseDamage == 1) {
                        defenseFlag = true;
                    }
                }

                if (baseDamage != 0 && (Id & 0xffff) == UPWARD_SLICE) {
                    (*position)++; // 0x021e34e8??
                }

                if (baseDamage != 0) {
                    tmp = static_cast<double>(baseDamage);
                    tmp = processCombo(Id & 0xffff, tmp, NowState);
                    baseDamage = static_cast<int>((tmp));

                    if ((Id & 0xffff) == UPWARD_SLICE && !players[defender].inactive) {
                        players[defender].inactive = true;
                    }
                }

                if (players[defender].TensionLevel == 4) {
                    tmp = baseDamage * 0.5;
                } else {
                    tmp = baseDamage;
                }

                if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                    tmp *= players[defender].defence;
                }
                baseDamage = static_cast<int>((tmp));

                if (baseDamage != 0 && (Id & 0xffff) == BattleEmulator::HELM_SPLITTER) {
                    // 実ROM順序: final damage -> 防御低下判定 -> 被ダメージ状態解除 -> 共通後処理。
                    // attack record +0x32 == -1. この主人公buildでは combat+0x50 == 50。
                    // RandInt(100), lr: 0x021e3e7c
                    if (lcg::getPercent(position, 100) < 50 && players[defender].BuffLevel > -2) {
                        --players[defender].BuffLevel;
                        players[defender].BuffTurns = 7; // runtime +0x6f=6 と既存turn表現の対応。
                        RecalculateBuff(players, defender);
                    }
                }


                if (baseDamage != 0) {
                    // FUN_02158a08: damaging attack can cure confusion.
                    // For a player target FUN_02075b04(1)=0.5 and DAT_02158b20=100.0,
                    // so RandInt(100) < 50 clears status bit 0x20 via FUN_02088cf8.
                    if (defender == 0 && players[defender].confused) {
                        if (lcg::getPercent(position, 100) < 50) { // lr: 0x02158ac4
                            players[defender].confused = false;
                            players[defender].confusionTurns = -1;
                        }
                    } else {
                        (*position)++; // max: 100, lr: 0x02158ac4
                    }
                    (*position)++; // max: 100, lr: 0x021e54fc
                } else {
                    if (Id == SKY_ATTACK) {
                        TiggerSkyAttack = true;
                    }
                }


                if (baseDamage != 0 && players[0].sleeping) {
                    players[0].sleeping = false;
                    players[0].sleepingTurn = -1;
                }

                process7A8(position, baseDamage, players, defender);

                if (baseDamage != 0 && (Id & 0xffff) == BattleEmulator::DOUBLE_EDGED_SLASH) {
                    // 0x00AF: final damage の25%を切り捨てて使用者へ反動。追加RNGなし。
                    Player::reduceHp(players[attacker], baseDamage / 4);
                }
            }

            players[attacker].TensionLevel = 0;

            break;
        case BattleEmulator::INACTIVE_ALLY:
        case BattleEmulator::PARALYSIS:
        case BattleEmulator::INACTIVE_ENEMY:
            (*position) += 2;
            (*position)++; //関係ない
            (*position)++; //会心
            (*position)++; //回避
            FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            (*position)++; //不明 0x021e54fc
            baseDamage = 0;
            resetCombo(NowState);
            players[attacker].TensionLevel = 0;
            break;
        case BattleEmulator::HEAL:
            players[attacker].mp -= 2;
            (*position) += 2;
            (*position)++; //関係ない
            if (lcg::getPercent(position, 0x2710) < 100) {
                kaisinn = true;
            }
            (*position)++; //回避
            baseDamage = FUN_021e8458_typeD(position, 5, 35);
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
            baseDamage = static_cast<int>((tmp));
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
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                if (!players[attacker].specialCharge && lcg::getPercent(position, 100) < 1) {
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
                }
            }
            resetCombo(NowState);
            break;
        case BattleEmulator::ATTACK_ALLY:
        case BattleEmulator::MERCURIAL_THRUST:
        case BattleEmulator::BEAST_THRUST:
        case BattleEmulator::VITAL_POINT_THRUST:
            if ((Id & 0xffff) == BattleEmulator::VITAL_POINT_THRUST) {
                players[attacker].mp -= 3;
            }
            (*position) += 2;
            (*position)++; // max:100, lr: 0x021ec6f8
            if (targetWasGuardRedirect) {
                (*position)++; // planned 03A1 target redirect, max:1, lr: 0x021ea6bc
            }
            //会心
            percent_tmp = lcg::getPercent(position, 0x2710);
            if (((Id & 0xffff) == BattleEmulator::ATTACK_ALLY && percent_tmp < 500) ||
                (((Id & 0xffff) == BattleEmulator::MERCURIAL_THRUST ||
                  (Id & 0xffff) == BattleEmulator::BEAST_THRUST) && percent_tmp < 250) ||
                ((Id & 0xffff) == BattleEmulator::VITAL_POINT_THRUST && percent_tmp < 125)) {
                kaisinn = true;
            }

            physicalAvoidance = ResolveHeroPhysicalAvoidance(position, defender);
            kaihi = physicalAvoidance.avoided();

            (*position)++; //回避
            baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);

            if ((Id & 0xffff) == BattleEmulator::MERCURIAL_THRUST) {
                tmp = floor(baseDamage * 0.75);
            } else if ((Id & 0xffff) == BattleEmulator::BEAST_THRUST && TargetIsBeast(defender)) {
                // Selector 10: beast target only, trunc(1.5 * incoming). Selector RNGなし。
                tmp = floor(baseDamage * 1.5);
            } else if ((Id & 0xffff) == BattleEmulator::VITAL_POINT_THRUST) {
                // Selector 11: trunc(0.5 * incoming). Selector側の追加RNGなし。
                tmp = floor(baseDamage * 0.5);
            } else {
                tmp = static_cast<double>(baseDamage);
            }

            if (kaisinn) {
                //0x020759ec
                if ((Id & 0xffff) == BattleEmulator::MERCURIAL_THRUST) {
                    tmp1 *= lcg::floatRand(position, 1.5, 2.0);
                } else {
                    tmp1 = OffensivePower * lcg::floatRand(position, 0.95, 1.05);
                }
            }

            if (players[attacker].TensionLevel != 0) {
                //TODO ダメージが正しいか調べる 特殊県産式の引数も調べる https://dragonquest9.com/?%E3%83%80%E3%83%A1%E3%83%BC%E3%82%B8%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6#tension
                tmp *= Ally_TensionTable[players[attacker].TensionLevel - 1];
                tmp += (players[attacker].TensionLevel * Ally_TensionLevel);
                players[attacker].TensionLevel = 0;
            }

            if (kaisinn) {
                if (tmp * 1.2000 <= tmp1) {
                    tmp = tmp1;
                } else {
                    tmp *= 1.2000;
                }
            }

            tmp *= HeroSpearLightningMultiplier(attacker, defender);
            baseDamage = static_cast<int>((tmp));

            vitalPointInstantDeath = false;
            if (!kaihi && (Id & 0xffff) == BattleEmulator::VITAL_POINT_THRUST) {
                const int deathResistance = TargetDeathResistancePercent(defender);
                if (deathResistance > 0) {
                    // FUN_021e4e9c: 12.5 * (Death / 100), strict roll < threshold.
                    // てっこうまじん Death 050 => 6.25, integer roll 0..6 succeeds.
                    // RandInt(100), lr: 0x021e4f04.
                    vitalPointInstantDeath = lcg::getPercent(position, 100) < 7;
                }
            }

            if (!kaihi) {
                ProcessRage(position, baseDamage, players, defender);
                (*position)++; //目を覚ました
                (*position)++; //不明
            } else {
                baseDamage = 0;
            }
            if (kaisinn) {
                if (!players[1].rage) {
                    (*position)++; //会心時特殊処理　0x021e54fc
                    (*position)++; //会心時特殊処理　0x021eb8c8
                } else {
                    (*position)++; //会心時特殊処理　既に怒り狂ってる場合は1消費になる
                }
            }
            if (players[defender].hp - baseDamage >= 0) {
                if (!players[attacker].specialCharge && lcg::getPercent(position, 100) < 1) {
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
                }
            }
            if (vitalPointInstantDeath) {
                // 実機では物理damage表示/後続RNGを通常どおり処理した後、対象HPが0になる。
                // caller側の reduceHp(baseDamage) より前に死亡状態を反映する。
                players[defender].hp = 0;
            }
            resetCombo(NowState);
            break;
        default:
            std::cout << "error!!!!! " << (Id & 0xffff) << std::endl;
    }
    return baseDamage;
}

//パーセントは絶対に100%にならないから誤差-1
int BattleEmulator::FUN_021e8458_typeC(int *position, double min, double max, double base) {
    //0x02075724
    auto result = lcg::floatRand(position, min, max);
    result += lcg::floatRand(position, -base, base);
    return static_cast<int>((result));
}

//パーセントは絶対に100%にならないから誤差-1
int BattleEmulator::FUN_021e8458_typeD(int *position, double difference, double base) {
    //0x021e8668
    auto result = lcg::floatRand(position, -difference, difference);
    result += base;
    return static_cast<int>((result));
}


#if !defined(__EMSCRIPTEN__)

#if defined(_MSC_VER) && !defined(__clang__)
#include <__msvc_int128.hpp>
using u128 = std::_Unsigned128;
#else
using u128 = unsigned __int128;
#endif


int BattleEmulator::FUN_0207564c(int *position, int atk, int def) {
    [[assume(atk >= 0)]];
    [[assume(def >= 0)]];
    int base = 2 * atk - def;
    if (base <= 0) [[unlikely]] {
        return 0;
    }

    int64_t atk1_fp = static_cast<int64_t>(base) << 30;
    int64_t atk2_fp = static_cast<int64_t>(atk) << 28; // atk * 1/16

    int64_t result_fp;

    if (atk1_fp > atk2_fp) [[likely]] {
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
    } else {
        // floatRand(0, atk2)
        uint32_t r = lcg::getTop32(position);
        auto result_u = static_cast<uint64_t>(
            (static_cast<u128>(r) * static_cast<u128>(static_cast<uint64_t>(atk2_fp))) >> 32);
        result_fp = static_cast<int64_t>(result_u);
    }

    if (result_fp <= 0) [[unlikely]] {
        return 0;
    }

    return static_cast<int>(result_fp >> 32);
}

#else

int BattleEmulator::FUN_0207564c(int *position, int atk, int def) {
    [[assume(atk >= 0)]];
    [[assume(def >= 0)]];
    double result;
    const double atk1 = (2*atk - def) * 0.25;
    if (atk1 <= 0) [[unlikely]] {
        return 0;
    }
    auto atk2 = atk * 0.0625;
    if (atk1 > atk2) [[likely]] {
        auto atk4 = atk1 * 0.0625;
        result = atk1 + lcg::floatRand(position, -atk4, atk4);
        result = result + lcg::floatRandAttack(position);
    } else {
        result = lcg::floatRand(position, 0.0, atk2);
    }
    if (result <= 0) [[unlikely]] {
        return 0;
    }
    return static_cast<int>((result));
    //return 0;
}

#endif

void BattleEmulator::process7A8(int *position, int baseDamage, Player players[4], int defender) {
    if (players[defender].paralysis || players[defender].sleeping || players[defender].specialCharge || players[defender].inactive || players[defender].hp <= baseDamage) {
        return;
    }
    if (baseDamage == 0) {
        (*position)++;
        return;
    }
    auto percent_tmp = lcg::getPercent(position, 100);
    double tmp = baseDamage;

    auto baseDamage_tmp = static_cast<int>((tmp));
    for (int i = 0; i < 9; ++i) {
        if (baseDamage_tmp >= proportionTable3[i]) {
            if (percent_tmp < proportionTable2[i]) {
                players[defender].specialCharge = true;
                players[defender].specialChargeTurn = 8;
            }
            break;
        }
    }
}

#include <array>
#include <cstddef>

constexpr std::size_t TABLE_MAX = 256;
template<std::size_t N>
constexpr std::array<int, TABLE_MAX>
makeProbabilityTable(const std::array<int, N>& ratios,
                     const std::array<int, N>& ids)
{
    std::array<int, TABLE_MAX> table{};

    std::size_t index = 0;

    for (std::size_t i = 0; i < N; ++i)
    {
        for (int j = 0; j < ratios[i]; ++j)
        {
            table[index++] = ids[i];  // ← ここが重要
        }
    }

    return table;
}

template<std::size_t N>
constexpr int sum(const std::array<int, N>& arr)
{
    int s = 0;
    for (int v : arr) s += v;
    return s;
}

constexpr std::array<int, 6> ratios = {
    0x44,
    0x3a,
    0x30,
    0x26,
    0x1b,
    0x11  // 239 + 17 = 256
};

//鉄鉱まじん
constexpr std::array<int, 6> ids = {
    BattleEmulator::WHIPPING_BOY,
    BattleEmulator::ATTACK_ENEMY,
    BattleEmulator::ATTACK_ENEMY,
    BattleEmulator::HELM_SPLITTER,
    BattleEmulator::KABUFF,//mp切れ 2回目以降 HELM_SPLITTER
    BattleEmulator::DOUBLE_EDGED_SLASH,
};

static_assert(sum(ratios) == TABLE_MAX, "Ratio sum must be 256");

constexpr auto actionTable = makeProbabilityTable(ratios, ids);


int BattleEmulator::ProcessEnemyRandomAction2A(int *position) {
    //0x0208aca8
    int rnd = static_cast<int>(static_cast<uint32_t>(lcg::getTop32(position)) >> 24);
    return actionTable[rnd];
}

int BattleEmulator::FUN_0208aecc(int* position, uint64_t* nowState)
{
    // 現在ステート取得 (4bit〜7bit)
    uint8_t pre = ((*nowState >> 4) & 0xF);
    if (pre == 3) {
        pre = 0;
    }

    // LCG の下位 1bit
    uint8_t lcgBit = lcg::getSeed(position);

    // 出力値
    auto output = static_cast<uint8_t>(pre * 2 + lcgBit);
    assert(output <= 5);

    // 次ステート更新
    uint8_t next = pre + 1;
    *nowState = (*nowState & ~0xF0) | (static_cast<uint64_t>(next) << 4);

    return output;
}

int BattleEmulator::CalculateMoreHealBase(const Player players[4], int actor) {
    //ベホイミ
    double tmp1 = (players[actor].HealPower - 200) * 0.5194;
    auto tmp2 = static_cast<int>((tmp1));
    return 185 + tmp2;
}

int BattleEmulator::CalculateMidHealBase(const Player players[4], int actor) {
    //ｂ
    double tmp1 = (players[actor].HealPower - 100) * 0.2392;
    auto tmp2 = static_cast<int>((tmp1));
    return 85 + tmp2;
}

void BattleEmulator::RecalculateBuff(Player players[4], int actor) {
    // 定数の倍率を格納した配列
    const double ATKMultipliers[] = {0.5, 0.75, 1.0, 1.25, 1.5};
    const double DEFMultipliers[] = {0.25, 0.5, 1.0, 1.5, 2.0};

    // BuffLevel に +2 して配列インデックスに変換
    int index = players[actor].BuffLevel + 2;

    // インデックスが範囲外でないかチェック（-2 <= BuffLevel <= 2 の範囲であることを確認）
    if (index >= 0 && index < 5) {
        players[actor].def = static_cast<int>((players[actor].defaultDEF * DEFMultipliers[index]));
    }

    int index1 = players[actor].AtkBuffLevel + 2;
    if (index1 >= 0 && index1 < 5) {
        players[actor].atk = static_cast<int>((players[actor].defaultATK * ATKMultipliers[index1]));
    }
}

void BattleEmulator::ProcessRage(int *position, int baseDamage, Player players[4], int defender) {
    // Rage transition belongs to enemy actors in this battle model.  The old
    // generic defender implementation incorrectly allowed enemy attacks on the
    // hero (actor 0) to consume rage RNG at the 50% / 25% HP thresholds.
    assert(defender != 0);

    // if (kaisinn) {
    //     return;
    // }

    int hp_before = preHP[defender];
    int hp_after  = preHP[defender] - baseDamage;
    int maxHp     = players[defender].maxHp;

    if (hp_after < 0) {
        hp_after = 0;
    }

    //    if (percent1 < 0.5) {
    //        if (percent >= 0.5) {
    if (hp_after * 2 < maxHp) {
        if (hp_before * 2 >= maxHp) {
            if (!players[defender].rage) {
                (*position)++;
                players[defender].rage = true;
                players[defender].rageTurns = lcg::intRangeRand(position, 2, 4);
            } else {
                (*position)++;
            }
        } else {
            // if (percent1 < 0.25) {
            //     if (percent >= 0.25) {
            if (hp_after * 4 < maxHp){
                if (hp_before * 4 >= maxHp){
                    if (!players[defender].rage) {
                        (*position)++;
                        (*position)++;
                    } else {
                        (*position)++;
                    }
                }
            }
        }
    }
}
int BattleEmulator::ProcessMagicBurst(int *position) {
    auto rand1 = lcg::floatRand(position, 0.9, 1.0);
    auto rand2 = lcg::floatRand(position, 0.9, 1.1);
    auto damage = 30 * 6 * rand1;
    auto guaranteed = 160 * rand2;
    if (damage > guaranteed) {
        return static_cast<int>((damage));
    } else {
        return static_cast<int>((guaranteed));
    }
}
