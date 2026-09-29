#pragma once

#include <algorithm>
#include <cstdint>

// Slime.dst actor state. Escaped enemies retain their HP but leave the roster
// used for subsequent battle actions; their presentation records remain.
struct Player {
    int hp{};
    int maxHp{};
    int atk{};
    int def{};
    int speed{};
    int mp{};
    int maxMp{};
    int level{};
    int criticalRate{};
    int evadeRate{};
    int enemySlot{};
    bool escaped{};

    [[nodiscard]] bool alive() const noexcept { return hp > 0 && !escaped; }
    static void reduceHp(Player& actor, int amount) noexcept { actor.hp = std::max(0, actor.hp - amount); }
    static void heal(Player& actor, int amount) noexcept { actor.hp = std::min(actor.maxHp, actor.hp + amount); }
};
