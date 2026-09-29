#include "BattleEmulator.h"
#include "lcg.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iomanip>
#include <iostream>

namespace {
using BE = BattleEmulator;
using namespace dq9::freecam::fast;
thread_local uint64_t turnsProcessed = 0;
constexpr std::array<BattleActorRef, 5> actorRefs{{
    {BattleActorSide::ally, 0}, {BattleActorSide::ally, 1},
    {BattleActorSide::enemy, 0}, {BattleActorSide::enemy, 1}, {BattleActorSide::enemy, 2}
}};

void boundary(const BE::State& s, bool trace, const char* name, int actor = -1) {
    if (!trace) return;
    std::cout << "TRACE boundary turn=" << s.turn + 1 << " actor=" << actor
              << " consumed=" << s.position - 1 << " next=" << s.position << ' ' << name << '\n';
}

void skip(BE::State& s, bool trace, uint32_t lr, int count = 1) {
    if (trace) {
        for (int i = 0; i < count; ++i)
            std::cout << "TRACE RNG consume=" << s.position + i << " lr=0x" << std::hex << lr << std::dec << '\n';
    }
    s.position += count;
}

int roll(BE::State& s, bool trace, uint32_t lr, int maximum) {
    const int at = s.position;
    const int value = lcg::getPercent(&s.position, maximum);
    if (trace) std::cout << "TRACE RNG consume=" << at << " lr=0x" << std::hex << lr
                         << std::dec << " max=" << maximum << " result=" << value << '\n';
    return value;
}

int firstEnemy(const BE::State& s) {
    for (int i = 2; i < 5; ++i) if (s.players[i].alive()) return i;
    return -1;
}

int guestTarget(const BE::State& s) {
    // 021F954C: all three encounter enemies are guaranteed killable by the
    // estimated guest attack. Within that class, prefer greatest current HP;
    // ties preserve actor order. No extra target RNG is consumed.
    int target = firstEnemy(s);
    for (int i = target + 1; i < 5; ++i)
        if (s.players[i].alive() && s.players[i].hp > s.players[target].hp) target = i;
    return target;
}

int guestAction(BE::State& s, int& target, bool trace) {
    boundary(s, trace, "start guest AI", 1);
    skip(s, trace, 0x021f87f4);
    skip(s, trace, 0x021f884c, 21);
    skip(s, trace, 0x021f88ac, 21); // Even inclusive [0,0]/[100,100] ranges consume.
    skip(s, trace, 0x021f88d0);
    // ROM tactics 3 -> 021F8BC4; context+124 is float 0.6. For HP20/70,
    // the exact integer partition is <=12 / <=42. This is not a random heal
    // chance, and the 44 random draws above must still be consumed.
    int lowest = -1;
    if (s.players[1].mp >= 2) {
        for (int i = 0; i < 2; ++i) {
            const auto& a = s.players[i];
            if (!a.alive() || a.hp * 5 > a.maxHp * 3) continue;
            if (lowest < 0 || a.hp * s.players[lowest].maxHp < s.players[lowest].hp * a.maxHp) lowest = i;
        }
    }
    target = lowest >= 0 ? lowest : guestTarget(s);
    boundary(s, trace, "end guest AI", 1);
    return lowest >= 0 ? BE::HEAL : BE::ATTACK_ALLY;
}

bool resetCamera() {
    std::array<CameraPresentationActor, 5> roster{};
    constexpr int xyz[5][3]{{0,12868,18432},{21283,12868,18432},{-5320,0,-9216},{10641,0,-18432},{26604,0,-9216}};
    constexpr int battle[5][3]{{3072,204,10240},{-3072,204,10240},{-6963,204,-10240},{0,204,-10240},{6963,204,-10240}};
    constexpr int radii[5]{4096,4096,3276,8192,3276};
    for (int i = 0; i < 5; ++i) {
        auto& a = roster[i];
        a.actor = actorRefs[i];
        a.worldX = xyz[i][0]; a.worldY = xyz[i][1]; a.worldZ = xyz[i][2];
        a.presentationFlags = i < 2 ? 2 : 0;
        a.battleWorldKnown = true;
        a.battleWorldX = battle[i][0]; a.battleWorldY = battle[i][1]; a.battleWorldZ = battle[i][2];
        a.battleRadius = radii[i];
        if (i >= 2) {
            a.membershipKind = CameraMembershipKind::monster;
            a.membershipKeyA = a.battleMonsterId = i == 3 ? 0x124 : 0x122;
        }
        if (i == 1) {
            a.membershipKind = CameraMembershipKind::special;
            a.membershipKeyA = 19; // Live s019f.bact -> s019be.chr, not item/model inference.
        }
    }
    if (!camera::ResetBattle(roster.data(), roster.size())) return false;
    // Model pair is read from Slime.dst's equipment record, not an item-name
    // guess. The guest is bound separately to its ROM-mined special resource.
    ThreadContext().presentationMembershipProfiles[0] = ResolvePlayerProfile(2, 1);
    return ThreadContext().presentationMembershipProfiles[0] != kInvalidMembershipProfile;
}

struct OrderedActor { int actor; float speed; int action; int target; };
}

void BattleEmulator::ResetTurnProcessed() noexcept { turnsProcessed = 0; }
uint64_t BattleEmulator::getTurnProcessed() noexcept { return turnsProcessed; }
int BattleEmulator::RawActorId(int actor) noexcept { return actor < 2 ? actor : 192 + actor - 2; }
const char* BattleEmulator::getActorName(int actor) noexcept {
    constexpr const char* names[]{"Hero", "Izayaaru", "SlimeA", "Cruelcumber", "SlimeB"};
    return actor >= 0 && actor < 5 ? names[actor] : "-";
}
const char* BattleEmulator::getActionName(int action) noexcept {
    switch (action) {
        case ATTACK_ENEMY: case ATTACK_ALLY: return "Attack";
        case DEFENCE: return "Defend";
        case FLEE_ALLY: return "FleeParty";
        case FLEE_ENEMY: return "FleeEnemy";
        case HEAL: return "Hoimi";
        default: return "Unsupported";
    }
}

bool BattleEmulator::InitializeBattle(State& state, int initialPosition) {
    if (initialPosition < 1 || initialPosition >= 6500) return false;
    state = {};
    state.position = initialPosition;
    state.players = {{{20,20,16,14,8,6,6,1,200,2}, {70,70,47,51,0,84,84,20,200,0},
                      {8,8,10,7,7,2,2,1,0,0}, {10,10,12,9,10,2,2,1,0,0}, {8,8,10,7,7,2,2,1,0,0}}};
    camera::BindRuntimeState(&state.cameraRuntime);
    const bool ok = resetCamera();
    camera::UnbindRuntimeState();
    return ok;
}

bool BattleEmulator::IsHeroCommandSelectable(const State& s, Command c) noexcept {
    if (s.finished || !s.players[0].alive() || c.bareHands) return false;
    if (c.action != ATTACK_ALLY && c.action != DEFENCE && c.action != FLEE_ALLY) return false;
    return c.action != ATTACK_ALLY || c.target < 0 || (c.target >= 1 && c.target <= 3 && s.players[c.target + 1].alive());
}

bool BattleEmulator::StepBattle(State& s, Command command, BattleResult* result, bool trace) {
    if (!IsHeroCommandSelectable(s, command) || s.position >= 6500) return false;
    const bool defend = command.action == DEFENCE;
    const bool fleeParty = command.action == FLEE_ALLY;
    std::array<OrderedActor, 5> order{};
    int count = 0;
    boundary(s, trace, "start turn");
    for (int i = 0; i < 5; ++i) {
        if (!s.players[i].alive()) continue;
        const int at = s.position;
        // ROM soft-float rounds each operation. The existing lcg header's
        // floatRand051_1 declaration has no definition and was unused.
        const float unit = static_cast<float>(static_cast<uint32_t>(lcg::getTop32(&s.position))) * (1.0f / 4294967296.0f);
        const float spread = (1.0f - 0.51f) * unit;
        const float speed = s.players[i].speed * (0.51f + spread);
        int action = i == 0 ? command.action : ATTACK_ALLY;
        if (i >= 2) action = s.players[i].enemySlot >= (i == 3 ? 3 : 2) ? FLEE_ENEMY : ATTACK_ENEMY;
        order[count++] = {i, speed, action, -1};
        if (trace) std::cout << "TRACE speed actor=" << i << " consume=" << at << " value=" << std::setprecision(9) << speed << '\n';
    }
    std::stable_sort(order.begin(), order.begin()+count, [](const auto& a, const auto& b) {
        const bool af = a.action == FLEE_ENEMY, bf = b.action == FLEE_ENEMY;
        return af != bf ? af : a.speed > b.speed;
    });
    boundary(s, trace, "start enemy selection");
    for (int i = 0; i < count; ++i) {
        auto& q = order[i];
        if (q.actor < 2) continue;
        ++s.players[q.actor].enemySlot;
        if (q.action == ATTACK_ENEMY) q.target = roll(s, trace, 0x02156874, 4) / 2;
        else q.target = q.actor;
        skip(s, trace, 0x0216139c); skip(s, trace, 0x021613b0); skip(s, trace, 0x02160d64);
        if (trace) std::cout << "TRACE select actor=" << q.actor << " action=" << q.action << " target=" << q.target << '\n';
    }
    boundary(s, trace, "end enemy selection");
    std::array<int32_t, 5> cameraActions{};
    std::array<BattleActorRef, 5> cameraActors{}, cameraTargets{};
    int cameraCount = 0;
    for (int i = 0; i < count; ++i) {
        auto q = order[i];
        if (!s.players[q.actor].alive() || (fleeParty && q.actor < 2)) continue;
        if (firstEnemy(s) < 0 || !s.players[0].alive()) break;
        BattleRecord record{};
        record.turn = s.turn + 1; record.actor = q.actor; record.rngBefore = s.position - 1;
        if (q.actor == 1) q.action = guestAction(s, q.target, trace);
        if (q.actor == 0) {
            q.target = q.action == ATTACK_ALLY ? command.target + 1 : 0;
            if (q.action == ATTACK_ALLY && (q.target < 2 || q.target >= 5 || !s.players[q.target].alive())) q.target = firstEnemy(s);
        }
        record.action = q.action; record.target = q.target;
        boundary(s, trace, "start actor", q.actor);
        boundary(s, trace, "start FUN_02158dfc", q.actor);
        if (q.actor >= 2) for (int ally = 0; ally < 2; ++ally) if (s.players[ally].alive()) skip(s, trace, 0x021588ec);
        skip(s, trace, 0x02159b10);
        boundary(s, trace, "end FUN_02158dfc", q.actor);
        boundary(s, trace, "start FUN_021ebd9c_ct", q.actor);
        skip(s, trace, 0x0216139c); skip(s, trace, 0x021613b0); skip(s, trace, 0x021ec6f8);
        const bool attack = q.action == ATTACK_ALLY || q.action == ATTACK_ENEMY;
        const bool heal = q.action == HEAL;
        const int critThreshold = attack ? s.players[q.actor].criticalRate : heal ? 100 : 0;
        const bool critical = roll(s, trace, 0x02158584, 10000) < critThreshold;
        bool evaded = false;
        if (attack) {
            evaded = roll(s, trace, 0x021587b0, 100) < s.players[q.target].evadeRate;
            if (!evaded) skip(s, trace, 0x021586fc); // All observed shields have guard rate 0.
        }
        skip(s, trace, 0x02157f58);
        int damage = 0;
        boundary(s, trace, "start base damage", q.actor);
        if (heal) {
            s.players[q.actor].mp -= 2;
            damage = static_cast<int>(35 + lcg::floatRand(&s.position, -5, 5));
            if (critical) damage = static_cast<int>(damage * lcg::floatRand(&s.position, 1.5, 2));
        } else {
            damage = FUN_0207564c(&s.position, s.players[q.actor].atk, s.players[q.target].def);
            if (critical) damage = static_cast<int>(std::max(damage * 1.2,
                s.players[q.actor].atk * lcg::floatRand(&s.position, .95, 1.05)));
        }
        boundary(s, trace, "end base damage", q.actor);
        if (attack && !evaded && damage == 0) damage = roll(s, trace, 0x021e81a0, 2);
        if (attack && defend && q.target == 0) damage /= 2;
        record.rawDamage = damage;
        if (evaded) {
            damage = 0;
            if (q.target == 1) skip(s, trace, 0x021ed7a8);
        } else if (attack) {
            // Slime/Cruelcumber rage check is reached only by a nonlethal hit.
            // Fresh 0x0822b3: hero->Slime A, Ctable checkpoints 73 and 74.
            if (q.target >= 2 && damage > 0 && damage < s.players[q.target].hp) {
                skip(s, trace, 0x021eb8c8); skip(s, trace, 0x021eb8f0);
            }
            if (damage > 0) { skip(s, trace, 0x02158ac4); skip(s, trace, 0x021e54fc); }
            // This encounter's level-1 hero does not enter the charge rolls;
            // Izayaaru does, including positive incoming damage (31/46 above).
            // The zero-damage rescue result still reaches this guest charge roll
            // (fresh 0x0822b3, turn 2: rescue #168 -> charge #169).
            if (q.target == 1) skip(s, trace, 0x021ed7a8);
            if (q.actor == 1) skip(s, trace, 0x021edaf4);
            Player::reduceHp(s.players[q.target], damage);
        } else if (heal) {
            skip(s, trace, 0x021e54fc);
            for (int enemy = 2; enemy < 5; ++enemy) if (s.players[enemy].alive()) {
                skip(s, trace, 0x021eb8c8); skip(s, trace, 0x021eb8f0);
            }
            skip(s, trace, 0x021edaf4);
            Player::heal(s.players[q.target], damage);
        } else {
            if (damage > 0) skip(s, trace, 0x021e54fc);
            damage = 0;
            if (q.action == FLEE_ENEMY) s.players[q.actor].escaped = true;
        }
        boundary(s, trace, "end FUN_021ebd9c_ct", q.actor);
        s.heroDead = !s.players[0].alive();
        s.finished = s.heroDead || firstEnemy(s) < 0;
        if (!s.finished) skip(s, trace, 0x02159d40);
        record.damage = damage; record.critical = critical; record.evaded = evaded; record.rngAfter = s.position - 1;
        for (int j = 0; j < 5; ++j) {
            record.hp[j] = s.players[j].hp; record.mp[j] = s.players[j].mp; record.escaped[j] = s.players[j].escaped;
        }
        if (result != nullptr) result->records.push_back(record);
        cameraActions[cameraCount] = q.action;
        cameraActors[cameraCount] = actorRefs[q.actor];
        cameraTargets[cameraCount++] = actorRefs[q.target];
        boundary(s, trace, "end actor", q.actor);
        if (s.finished) break;
    }
    if (!s.finished) skip(s, trace, 0x0215962c);
    boundary(s, trace, "start camera");
    camera::BindRuntimeState(&s.cameraRuntime);
    camera::Main(&s.position, cameraActions.data(), cameraActors.data(), cameraTargets.data(),
                 nullptr, nullptr, cameraCount, &s.nowState, false, false, trace);
    camera::UnbindRuntimeState();
    boundary(s, trace, "end camera");
    ++s.turn; ++turnsProcessed;
    return true;
}

#if !defined(__EMSCRIPTEN__)
#if defined(_MSC_VER) && !defined(__clang__)
#include <__msvc_int128.hpp>
using u128 = std::_Unsigned128;
#else
using u128 = unsigned __int128;
#endif

// Existing fixed-point optimized damage function: preserved, not re-derived
// from the decompiler's float-shaped representation.
int BattleEmulator::FUN_0207564c(int *position, int atk, int def) {
    [[assume(atk >= 0)]];
    [[assume(def >= 0)]];
    int base = 2 * atk - def;
    if (base <= 0) [[unlikely]] return 0;
    int64_t atk1_fp = static_cast<int64_t>(base) << 30;
    int64_t atk2_fp = static_cast<int64_t>(atk) << 28;
    int64_t result_fp;
    if (atk1_fp > atk2_fp) [[likely]] {
        int64_t atk4_fp = atk1_fp >> 4;
        uint32_t r1 = lcg::getTop32(position);
        auto spread_u = static_cast<uint64_t>((static_cast<u128>(r1) * static_cast<u128>(static_cast<uint64_t>(atk4_fp))) >> 31);
        int64_t spread = static_cast<int64_t>(spread_u) - atk4_fp;
        uint32_t r2 = lcg::getTop32(position);
        int64_t attack = (static_cast<int64_t>(r2) << 1) - (1ll << 32);
        result_fp = atk1_fp + spread + attack;
    } else {
        uint32_t r = lcg::getTop32(position);
        auto result_u = static_cast<uint64_t>((static_cast<u128>(r) * static_cast<u128>(static_cast<uint64_t>(atk2_fp))) >> 32);
        result_fp = static_cast<int64_t>(result_u);
    }
    if (result_fp <= 0) [[unlikely]] return 0;
    return static_cast<int>(result_fp >> 32);
}
#else
int BattleEmulator::FUN_0207564c(int *position, int atk, int def) {
    [[assume(atk >= 0)]];
    [[assume(def >= 0)]];
    double result;
    const double atk1 = (2*atk - def) * 0.25;
    if (atk1 <= 0) [[unlikely]] return 0;
    auto atk2 = atk * 0.0625;
    if (atk1 > atk2) [[likely]] {
        auto atk4 = atk1 * 0.0625;
        result = atk1 + lcg::floatRand(position, -atk4, atk4);
        result = result + lcg::floatRandAttack(position);
    } else result = lcg::floatRand(position, 0.0, atk2);
    if (result <= 0) [[unlikely]] return 0;
    return static_cast<int>(result);
}
#endif
