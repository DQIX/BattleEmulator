#include "ActionSearch.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numeric>
#include <vector>

#include "BattleEmulator.h"

namespace {
    constexpr uint32_t NO_PATH = std::numeric_limits<uint32_t>::max();

    // This is the fixed ally action menu already implemented by BattleEmulator::callAttackFun.
    // Search ordering and pruning may change, but the reachable action set does not.
    constexpr std::array<int32_t, 7> ALLY_ACTIONS = {
        BattleEmulator::ATTACK_ALLY,
        BattleEmulator::DRAGON_SLASH,
        BattleEmulator::CRACK_ALLY,
        BattleEmulator::HEAL,
        BattleEmulator::MEDICINAL_HERBS,
        BattleEmulator::DEFENCE,
        BattleEmulator::ACROBATIC_STAR,
    };

    struct Node {
        ActionSearchState state{};
        int64_t score = std::numeric_limits<int64_t>::min();
        uint64_t fingerprint = 0;
        uint32_t path = NO_PATH;
    };

    struct Candidate {
        ActionSearchState state{};
        int64_t score = std::numeric_limits<int64_t>::min();
        uint64_t fingerprint = 0;
        uint32_t parentPath = NO_PATH;
        uint8_t action = 0;
    };

    struct PathLink {
        uint32_t parent = NO_PATH;
        uint8_t action = 0;
    };

    struct BeamOutcome {
        std::vector<int32_t> actions;
        bool victory = false;
        bool timedOut = false;
        bool exact = true;
        int64_t score = std::numeric_limits<int64_t>::min();
        uint64_t expanded = 0;
    };

    uint64_t mix64(uint64_t x) {
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebULL;
        x ^= x >> 31;
        return x;
    }

    class FlatFingerprintSet {
    public:
        explicit FlatFingerprintSet(size_t expected) {
            size_t capacity = 8;
            const size_t target = std::max<size_t>(8, expected * 2);
            while (capacity < target) {
                capacity <<= 1;
            }
            slots.assign(capacity, 0);
            mask = capacity - 1;
        }

        bool insert(uint64_t value) {
            // fingerprintState never emits zero. Callers using other compact keys
            // reserve zero as the empty marker by adding one first.
            size_t index = static_cast<size_t>(mix64(value)) & mask;
            while (true) {
                uint64_t &slot = slots[index];
                if (slot == 0) {
                    slot = value;
                    return true;
                }
                if (slot == value) {
                    return false;
                }
                index = (index + 1) & mask;
            }
        }

    private:
        std::vector<uint64_t> slots;
        size_t mask = 0;
    };

    uint64_t fingerprintState(const ActionSearchState &state) {
        uint64_t hash = 0x243f6a8885a308d3ULL;
        const auto appendBytes = [&hash](const void *ptr, size_t size) {
            const auto *bytes = static_cast<const unsigned char *>(ptr);
            while (size >= sizeof(uint64_t)) {
                uint64_t word;
                std::memcpy(&word, bytes, sizeof(word));
                hash ^= word + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
                hash *= 0xd6e8feb86659fd93ULL;
                bytes += sizeof(word);
                size -= sizeof(word);
            }
            if (size != 0) {
                uint64_t tail = 0;
                std::memcpy(&tail, bytes, size);
                hash ^= tail + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
                hash *= 0xd6e8feb86659fd93ULL;
            }
        };
        appendBytes(&state.players[0], sizeof(Player));
        appendBytes(&state.players[1], sizeof(Player));
        hash ^= mix64(state.nowState + 0x517cc1b727220a95ULL);
        hash ^= mix64(static_cast<uint64_t>(static_cast<uint32_t>(state.position)) + 0x6eed0e9da4d94a4fULL);
        hash = mix64(hash);
        return hash == 0 ? 0xa0761d6478bd642fULL : hash;
    }

    int64_t scoreState(const ActionSearchState &state, int depth) {
        const Player &ally = state.players[0];
        const Player &enemy = state.players[1];
        if (!Player::isPlayerAlive(ally)) {
            return std::numeric_limits<int64_t>::min() / 4;
        }

        if (!Player::isPlayerAlive(enemy)) {
            // Depth is handled lexicographically by the layered search. This term only
            // breaks ties between wins on the same layer.
            return 4'000'000'000'000LL
                   + static_cast<int64_t>(ally.hp) * 1'000'000LL
                   + static_cast<int64_t>(ally.mp) * 25'000LL
                   + static_cast<int64_t>(ally.medicinal_herbs_count) * 8'000LL
                   + (ally.specialCharge ? 120'000LL : 0LL)
                   + (ally.acrobaticStar ? 80'000LL : 0LL);
        }

        // Enemy HP is the strongest signal. The remaining terms deliberately keep
        // survivable/resource-rich states in the beam when raw damage is close.
        const int damageDone = std::max(0, enemy.maxHp - enemy.hp);
        int64_t score = static_cast<int64_t>(damageDone) * 10'000'000LL;
        score += static_cast<int64_t>(ally.hp) * 110'000LL;
        score += static_cast<int64_t>(std::max(0, ally.mp)) * 22'000LL;
        score += static_cast<int64_t>(ally.medicinal_herbs_count) * 18'000LL;
        score += static_cast<int64_t>(ally.SpecialMedicineCount) * 3'000LL;
        score += static_cast<int64_t>(ally.AtkBuffLevel) * 90'000LL;
        score += static_cast<int64_t>(ally.BuffLevel) * 35'000LL;
        score += static_cast<int64_t>(ally.speedLevel) * 18'000LL;
        score += ally.specialCharge ? 500'000LL : 0LL;
        score += ally.acrobaticStar ? 650'000LL : 0LL;
        score -= ally.paralysis ? 1'800'000LL : 0LL;
        score -= ally.inactive ? 1'100'000LL : 0LL;
        score -= ally.sleeping ? 1'100'000LL : 0LL;

        // A quadratic danger penalty prevents a purely damage-greedy beam from
        // throwing away the healing/defence branch immediately before a lethal hit.
        constexpr int dangerHp = 28;
        if (ally.hp < dangerHp) {
            const int deficit = dangerHp - ally.hp;
            score -= static_cast<int64_t>(deficit) * deficit * 55'000LL;
        }
        score -= static_cast<int64_t>(depth) * 1'000LL;
        return score;
    }

    ActionSearchState step(const ActionSearchState &source, uint64_t seed, int32_t action) {
        ActionSearchState next = source;
        // The formal array extent decays to a pointer. With RunCount=1 and
        // logicalTurnStart=true Main reads only Gene[0], so do not clear 350 ints
        // for every single expansion.
        const int32_t gene[2] = {action, -1};
        BattleEmulator::Main(&next.position, 1, gene, next.players, nullptr, seed,
                             nullptr, nullptr, -2, &next.nowState, true);
        return next;
    }

    std::vector<int32_t> materializePath(const std::vector<PathLink> &links, uint32_t parent,
                                         int32_t finalAction) {
        std::vector<int32_t> reversed;
        reversed.reserve(64);
        reversed.push_back(finalAction);
        while (parent != NO_PATH) {
            const PathLink &link = links[parent];
            reversed.push_back(static_cast<int32_t>(link.action));
            parent = link.parent;
        }
        std::reverse(reversed.begin(), reversed.end());
        return reversed;
    }

    uint64_t diversityBucket(const Candidate &candidate) {
        const Player &ally = candidate.state.players[0];
        const Player &enemy = candidate.state.players[1];
        uint64_t bucket = static_cast<uint64_t>(std::max(0, enemy.hp) / 10);
        bucket = bucket * 17 + static_cast<uint64_t>(std::max(0, ally.hp) / 6);
        bucket = bucket * 11 + static_cast<uint64_t>(std::max(0, ally.mp) / 2);
        bucket = bucket * 17 + static_cast<uint64_t>(candidate.state.position & 15);
        bucket = bucket * 2 + static_cast<uint64_t>(ally.specialCharge);
        bucket = bucket * 2 + static_cast<uint64_t>(ally.acrobaticStar);
        bucket = bucket * 2 + static_cast<uint64_t>(ally.paralysis);
        return bucket;
    }

    BeamOutcome runBeam(const ActionSearchState &start, uint64_t seed, int maxDepth, size_t beamWidth,
                        std::chrono::steady_clock::time_point deadline) {
        BeamOutcome outcome;
        std::vector<Node> beam;
        beam.reserve(beamWidth);
        Node root;
        root.state = start;
        root.fingerprint = fingerprintState(start);
        root.score = scoreState(start, 0);
        beam.push_back(root);

        std::vector<PathLink> links;
        links.reserve(beamWidth * static_cast<size_t>(std::min(maxDepth, 64)));

        std::vector<int32_t> bestPartial;
        int64_t bestPartialScore = root.score;

        for (int depth = 1; depth <= maxDepth && !beam.empty(); ++depth) {
            std::vector<Candidate> candidates;
            candidates.reserve(beam.size() * ALLY_ACTIONS.size());
            FlatFingerprintSet seen(beam.size() * ALLY_ACTIONS.size());

            bool haveWinner = false;
            int64_t bestWinnerScore = std::numeric_limits<int64_t>::min();
            std::vector<int32_t> bestWinner;

            for (const Node &parent: beam) {
                // Do not impose legality/resource rules here. The action set is an
                // input specification; BattleEmulator is the authority for what an
                // explicit action does in the current state.
                for (const int32_t action: ALLY_ACTIONS) {
                    Candidate candidate;
                    candidate.state = step(parent.state, seed, action);
                    ++outcome.expanded;
                    candidate.parentPath = parent.path;
                    candidate.action = static_cast<uint8_t>(action);
                    candidate.fingerprint = fingerprintState(candidate.state);

                    if (!seen.insert(candidate.fingerprint)) {
                        continue;
                    }
                    if (!Player::isPlayerAlive(candidate.state.players[0])) {
                        continue;
                    }

                    candidate.score = scoreState(candidate.state, depth);
                    if (!Player::isPlayerAlive(candidate.state.players[1])) {
                        if (!haveWinner || candidate.score > bestWinnerScore) {
                            haveWinner = true;
                            bestWinnerScore = candidate.score;
                            bestWinner = materializePath(links, candidate.parentPath, action);
                        }
                        continue;
                    }

                    if (candidate.score > bestPartialScore) {
                        bestPartialScore = candidate.score;
                        bestPartial = materializePath(links, candidate.parentPath, action);
                    }
                    candidates.push_back(candidate);

                    if ((outcome.expanded & 1023ULL) == 0 && std::chrono::steady_clock::now() >= deadline) {
                        outcome.timedOut = true;
                        outcome.exact = false;
                        if (haveWinner) {
                            outcome.victory = true;
                            outcome.score = bestWinnerScore;
                            outcome.actions = std::move(bestWinner);
                        } else {
                            outcome.score = bestPartialScore;
                            outcome.actions = std::move(bestPartial);
                        }
                        return outcome;
                    }
                }
            }

            if (haveWinner) {
                outcome.victory = true;
                outcome.score = bestWinnerScore;
                outcome.actions = std::move(bestWinner);
                return outcome;
            }
            if (candidates.empty()) {
                break;
            }

            if (candidates.size() > beamWidth) {
                outcome.exact = false;
            }

            const size_t keep = std::min(beamWidth, candidates.size());
            const size_t poolSize = std::min(candidates.size(), std::max(keep, keep * 4));
            std::vector<size_t> order(candidates.size());
            std::iota(order.begin(), order.end(), size_t{0});
            const auto better = [&candidates](size_t lhs, size_t rhs) {
                if (candidates[lhs].score != candidates[rhs].score) {
                    return candidates[lhs].score > candidates[rhs].score;
                }
                return candidates[lhs].fingerprint < candidates[rhs].fingerprint;
            };
            if (poolSize < order.size()) {
                std::nth_element(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(poolSize),
                                 order.end(), better);
                order.resize(poolSize);
            }
            std::sort(order.begin(), order.end(), better);

            std::vector<size_t> chosen;
            chosen.reserve(keep);
            std::vector<uint8_t> chosenFlag(candidates.size(), 0);
            const size_t diversityQuota = keep / 8;
            FlatFingerprintSet buckets(diversityQuota + 1);
            for (const size_t index: order) {
                if (chosen.size() >= diversityQuota) break;
                const uint64_t bucket = diversityBucket(candidates[index]);
                if (buckets.insert(bucket + 1)) {
                    chosen.push_back(index);
                    chosenFlag[index] = 1;
                }
            }
            for (const size_t index: order) {
                if (chosen.size() >= keep) break;
                if (chosenFlag[index]) continue;
                chosen.push_back(index);
                chosenFlag[index] = 1;
            }

            std::vector<Node> next;
            next.reserve(chosen.size());
            for (const size_t index: chosen) {
                const Candidate &candidate = candidates[index];
                links.push_back({candidate.parentPath, candidate.action});
                Node node;
                node.state = candidate.state;
                node.score = candidate.score;
                node.fingerprint = candidate.fingerprint;
                node.path = static_cast<uint32_t>(links.size() - 1);
                next.push_back(node);
            }
            beam.swap(next);

            if (std::chrono::steady_clock::now() >= deadline) {
                outcome.timedOut = true;
                outcome.exact = false;
                break;
            }
        }

        outcome.score = bestPartialScore;
        outcome.actions = std::move(bestPartial);
        return outcome;
    }
}

ActionSearchResult ActionSearch::Run(const ActionSearchState &start, uint64_t seed, int maxTurns,
                                     int timeBudgetMs) {
    ActionSearchResult result;
    result.score = scoreState(start, 0);
    if (maxTurns <= 0 || timeBudgetMs <= 0 || !Player::isPlayerAlive(start.players[0])) {
        return result;
    }
    if (!Player::isPlayerAlive(start.players[1])) {
        result.victory = true;
        return result;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeBudgetMs);
    constexpr std::array<size_t, 3> widths = {2048, 8192, 32768};

    int incumbentDepth = maxTurns + 1;
    std::vector<int32_t> incumbent;
    int64_t incumbentScore = std::numeric_limits<int64_t>::min();

    for (const size_t width: widths) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        const int depthLimit = result.victory ? std::min(maxTurns, incumbentDepth - 1) : maxTurns;
        if (depthLimit <= 0) {
            break;
        }

        BeamOutcome outcome = runBeam(start, seed, depthLimit, width, deadline);
        result.expanded += outcome.expanded;
        result.completedBeamWidth = static_cast<int>(width);

        if (outcome.victory) {
            const int depth = static_cast<int>(outcome.actions.size());
            if (!result.victory || depth < incumbentDepth ||
                (depth == incumbentDepth && outcome.score > incumbentScore)) {
                result.victory = true;
                incumbentDepth = depth;
                incumbentScore = outcome.score;
                incumbent = std::move(outcome.actions);
            }
            if (outcome.exact) {
                break;
            }
        } else {
            if (result.victory && outcome.exact && !outcome.timedOut) {
                // This widening run exhaustively closed every depth below the
                // incumbent under the current state-dedup model.
                break;
            }
            if (!result.victory && outcome.score > result.score) {
                result.score = outcome.score;
                incumbent = std::move(outcome.actions);
            }
        }

        if (outcome.timedOut) break;
    }

    if (result.victory) {
        result.score = incumbentScore;
    }
    result.length = std::min(static_cast<int>(incumbent.size()), 349);
    for (int i = 0; i < result.length; ++i) {
        result.actions[i] = incumbent[static_cast<size_t>(i)];
    }
    if (result.length < 350) result.actions[result.length] = -1;
    return result;
}
