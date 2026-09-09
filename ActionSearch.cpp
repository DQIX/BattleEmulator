#include "ActionSearch.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "BattleEmulator.h"

namespace {
    constexpr int kMaxDepth = 349;
    constexpr std::size_t kBeamWidth = 4096;

    // These are the ally commands with implemented transitions in this emulator branch.
    // Resource/status filtering below only removes commands that cannot be issued from
    // the current state; it does not introduce substitute transitions.
    constexpr std::array<int, 7> kLegalActions = {
        BattleEmulator::ATTACK_ALLY,
        BattleEmulator::DRAGON_SLASH,
        BattleEmulator::CRACK_ALLY,
        BattleEmulator::HEAL,
        BattleEmulator::DEFENCE,
        BattleEmulator::MEDICINAL_HERBS,
        BattleEmulator::ACROBATIC_STAR,
    };

    struct PathLink {
        uint32_t parent = 0;
        uint8_t actionIndex = 0;
    };

    struct Node {
        Player players[2];
        uint64_t state = 0;
        int position = 1;
        uint32_t path = 0;
        int depth = 0;
        int64_t score = std::numeric_limits<int64_t>::min();
    };

    uint64_t mix64(uint64_t value) {
        value ^= value >> 30;
        value *= 0xbf58476d1ce4e5b9ULL;
        value ^= value >> 27;
        value *= 0x94d049bb133111ebULL;
        value ^= value >> 31;
        return value;
    }

    int64_t evaluate(const Node &node) {
        const Player &ally = node.players[0];
        const Player &enemy = node.players[1];
        const int progress = std::max(0, enemy.maxHp - enemy.hp);

        int64_t score = static_cast<int64_t>(progress) * 2048;
        score += static_cast<int64_t>(ally.hp) * 64;
        score += static_cast<int64_t>(std::max(0, ally.mp)) * 24;
        score += static_cast<int64_t>(std::max(0, ally.medicinal_herbs_count)) * 32;
        score += ally.specialCharge ? 768 : 0;
        score += ally.acrobaticStar ? 1024 : 0;
        score -= ally.paralysis ? 4096 : 0;
        score -= ally.inactive ? 3072 : 0;
        score -= enemy.rage ? 512 : 0;

        // Keep enough pre-emptive healing/defence lines in the beam instead of
        // allowing pure damage greed to erase them one turn before they matter.
        if (ally.hp < 35) {
            score -= static_cast<int64_t>(35 - ally.hp) * 1024;
        }
        if (ally.hp < 20) {
            score -= static_cast<int64_t>(20 - ally.hp) * 2048;
        }

        // The low bits are only a deterministic tie breaker. Different RNG/camera
        // states therefore survive ties without overruling one point of real score.
        const uint64_t diversity = mix64(node.state ^ (static_cast<uint64_t>(node.position) << 32));
        return score * 4096 + static_cast<int64_t>(diversity & 0xfffULL);
    }

    int legalActionIndices(const Player &ally, std::array<uint8_t, kLegalActions.size()> &out) {
        int count = 0;

        // While unable to act every ordinary command reaches the same authoritative
        // paralysis/inactive transition. Keep one canonical representative instead
        // of spending almost the whole beam on duplicate states.
        if (ally.paralysis || ally.inactive) {
            out[count++] = 0;
            return count;
        }

        out[count++] = 0; // Attack
        out[count++] = 1; // Dragon Slash
        if (ally.mp >= 3) {
            out[count++] = 2; // Crack
        }
        if (ally.mp >= 2) {
            out[count++] = 3; // Heal
        }
        out[count++] = 4; // Defence
        if (ally.medicinal_herbs_count > 0) {
            out[count++] = 5; // Medicinal Herbs
        }
        // Main decrements specialChargeTurn at turn start, so a value of zero has
        // already expired before the command is executed.
        if (ally.specialCharge && ally.specialChargeTurn > 0 && !ally.acrobaticStar) {
            out[count++] = 6; // Acrobatic Star
        }
        return count;
    }

    bool betterVictory(const Node &candidate, const Node &incumbent) {
        if (candidate.depth != incumbent.depth) {
            return candidate.depth < incumbent.depth;
        }
        if (candidate.players[0].hp != incumbent.players[0].hp) {
            return candidate.players[0].hp > incumbent.players[0].hp;
        }
        if (candidate.players[0].mp != incumbent.players[0].mp) {
            return candidate.players[0].mp > incumbent.players[0].mp;
        }
        return candidate.score > incumbent.score;
    }

    void reconstruct(const Node &node, const std::vector<PathLink> &paths, ActionSearchResult &result) {
        result.length = node.depth;
        uint32_t path = node.path;
        for (int i = node.depth - 1; i >= 0; --i) {
            const PathLink &link = paths[path];
            result.actions[static_cast<std::size_t>(i)] = kLegalActions[link.actionIndex];
            path = link.parent;
        }
        if (result.length < static_cast<int>(result.actions.size())) {
            result.actions[static_cast<std::size_t>(result.length)] = -1;
        }
        result.allyHp = node.players[0].hp;
        result.enemyHp = node.players[1].hp;
    }
}

ActionSearchResult ActionSearch::Run(const Player startPlayers[2], uint64_t seed, int startPosition,
                                     uint64_t startState, int64_t budgetMicros) {
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();
    const auto deadline = started + std::chrono::microseconds(std::max<int64_t>(1, budgetMicros));

    ActionSearchResult result;

    Node root{};
    root.players[0] = startPlayers[0];
    root.players[1] = startPlayers[1];
    root.state = startState;
    root.position = startPosition;
    root.depth = 0;
    root.path = 0;
    root.score = evaluate(root);

    std::vector<PathLink> paths;
    paths.reserve(1'000'000);
    paths.push_back({0, 0}); // path 0 is the root sentinel

    std::vector<Node> current;
    std::vector<Node> next;
    current.reserve(kBeamWidth);
    next.reserve(kBeamWidth * kLegalActions.size());
    current.push_back(root);

    Node best = root;
    Node bestVictory{};
    bool hasVictory = startPlayers[1].hp == 0 && startPlayers[0].hp != 0;
    if (hasVictory) {
        bestVictory = root;
    }

    uint64_t explored = 0;
    bool expired = false;

    for (int depth = 0; depth < kMaxDepth && !current.empty() && !hasVictory; ++depth) {
        next.clear();
        bool victoryAtThisDepth = false;

        for (const Node &parent: current) {
            std::array<uint8_t, kLegalActions.size()> actionIndices{};
            const int actionCount = legalActionIndices(parent.players[0], actionIndices);

            for (int a = 0; a < actionCount; ++a) {
                if ((explored & 0x3ffULL) == 0 && Clock::now() >= deadline) {
                    expired = true;
                    break;
                }

                const uint8_t actionIndex = actionIndices[static_cast<std::size_t>(a)];
                const int32_t gene[350] = {kLegalActions[actionIndex], -1};

                Node child = parent;
                child.depth = parent.depth + 1;
                child.path = static_cast<uint32_t>(paths.size());
                paths.push_back({parent.path, actionIndex});

                BattleEmulator::Main(&child.position, 1, gene, child.players, nullptr, seed,
                                     nullptr, nullptr, -2, &child.state, true);
                ++explored;
                child.score = evaluate(child);

                if (child.players[0].hp != 0 && child.players[1].hp == 0) {
                    if (!victoryAtThisDepth || betterVictory(child, bestVictory)) {
                        bestVictory = child;
                    }
                    victoryAtThisDepth = true;
                    continue;
                }
                if (child.players[0].hp == 0) {
                    continue;
                }

                if (child.score > best.score) {
                    best = child;
                }
                next.push_back(child);
            }
            if (expired) {
                break;
            }
        }

        if (victoryAtThisDepth) {
            hasVictory = true;
            break;
        }
        if (expired) {
            break;
        }
        if (next.empty()) {
            break;
        }

        if (next.size() > kBeamWidth) {
            auto middle = next.begin() + static_cast<std::ptrdiff_t>(kBeamWidth);
            std::nth_element(next.begin(), middle, next.end(), [](const Node &lhs, const Node &rhs) {
                return lhs.score > rhs.score;
            });
            next.resize(kBeamWidth);
        }

        // High-value states are expanded first, which improves the incumbent when
        // the fixed deadline lands in the middle of a layer.
        std::sort(next.begin(), next.end(), [](const Node &lhs, const Node &rhs) {
            return lhs.score > rhs.score;
        });
        current.swap(next);
    }

    const Node &chosen = hasVictory ? bestVictory : best;
    reconstruct(chosen, paths, result);
    result.victory = hasVictory;
    result.explored = explored;
    result.elapsedMicros = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count();
    return result;
}
