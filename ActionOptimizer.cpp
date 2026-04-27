//
// RNG event driven deterministic action search.
//

#include "ActionOptimizer.h"

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

#include "BattleEmulator.h"
#include "lcg.h"

struct ActionEntry {
    int action;
    bool (*condition)(const Genome &);
};

constexpr ActionEntry ACTION_TABLE[] = {
    {BattleEmulator::ATTACK_ALLY, [](const Genome &) { return true; }},
    {BattleEmulator::DRAGON_SLASH, [](const Genome &) { return true; }},
    {BattleEmulator::DEFENCE, [](const Genome &) { return true; }},
    {BattleEmulator::FLEE_ALLY, [](const Genome &) { return true; }},
    {BattleEmulator::SPECIAL_ANTIDOTE,
     [](const Genome &g) {
         return g.AllyPlayer.SpecialMedicineCount >= 1 &&
                g.AllyPlayer.PoisonEnable;
     }},
    {BattleEmulator::SPECIAL_MEDICINE,
     [](const Genome &g) {
         return g.AllyPlayer.SpecialMedicineCount >= 1 &&
                !g.AllyPlayer.PoisonEnable;
     }},
    {BattleEmulator::HEAL,
     [](const Genome &g) { return g.AllyPlayer.mp >= 2; }},
    {BattleEmulator::CRACK_ALLY,
     [](const Genome &g) { return g.AllyPlayer.mp >= 3; }},
    {BattleEmulator::WOOSH_ALLY,
     [](const Genome &g) { return g.AllyPlayer.mp >= 3; }},
    {BattleEmulator::ACROBATIC_STAR,
     [](const Genome &g) {
         return g.AllyPlayer.specialCharge &&
                g.AllyPlayer.specialChargeTurn != 0;
     }}
};

static uint32_t Node_Used;

namespace {
    constexpr int NO_SOLUTION_TURN = 100;
    constexpr int EVENT_SCAN_RANGE = 768;
    constexpr int BEAM_WIDTH = 2048;
    constexpr int BEAM_DEPTH = 26;
    constexpr int ROLLOUT_COUNT = 4000;
    constexpr int ROLLOUT_CHOICES = 10;

#if defined(BattleEmulatorLV13)
    constexpr int ALLY_CRITICAL_THRESHOLD = 200;
#else
    constexpr int ALLY_CRITICAL_THRESHOLD = 500;
#endif

    enum class EventType : int {
        AttackCritical,
        DragonCritical,
        SpellCritical,
        SpecialCharge,
        AcrobaticCounter
    };

    struct RNGEvent {
        int position = 0;
        EventType type = EventType::AttackCritical;
    };

    struct SearchCandidate {
        Genome genome{};
        int action = -1;
        int score = std::numeric_limits<int>::min();
    };

    struct SearchContext {
        uint64_t seed = 0;
        int nodeBudget = 0;
        int nodesVisited = 0;
        bool exhausted = false;
        std::vector<RNGEvent> events;
        Genome bestGenome{};
        bool bestIsSolution = false;
    };

    void initializeGenomeActions(Genome &genome) {
        std::fill(std::begin(genome.actions), std::end(genome.actions), -1);
    }

    bool isSolution(const Genome &genome) {
        return genome.EnemyPlayer.hp <= 0 && genome.AllyPlayer.hp > 0;
    }

    bool isBetterProgress(const Genome &candidate, const Genome &best) {
        const bool candidateSolved = isSolution(candidate);
        const bool bestSolved = isSolution(best);

        if (candidateSolved != bestSolved) {
            return candidateSolved;
        }
        if (candidateSolved) {
            if (candidate.turn != best.turn) {
                return candidate.turn < best.turn;
            }
            if (candidate.AllyPlayer.hp != best.AllyPlayer.hp) {
                return candidate.AllyPlayer.hp > best.AllyPlayer.hp;
            }
            if (candidate.EnemyPlayer.hp != best.EnemyPlayer.hp) {
                return candidate.EnemyPlayer.hp < best.EnemyPlayer.hp;
            }
            return candidate.position < best.position;
        }
        if (candidate.EnemyPlayer.hp != best.EnemyPlayer.hp) {
            return candidate.EnemyPlayer.hp < best.EnemyPlayer.hp;
        }
        if (candidate.AllyPlayer.hp != best.AllyPlayer.hp) {
            return candidate.AllyPlayer.hp > best.AllyPlayer.hp;
        }
        if (candidate.AllyPlayer.mp != best.AllyPlayer.mp) {
            return candidate.AllyPlayer.mp > best.AllyPlayer.mp;
        }
        return candidate.position < best.position;
    }

    void updateBestGenome(SearchContext &context, const Genome &candidate) {
        if (!context.bestIsSolution && context.bestGenome.turn == 0) {
            context.bestGenome = candidate;
            context.bestIsSolution = isSolution(candidate);
            return;
        }
        if (isBetterProgress(candidate, context.bestGenome)) {
            context.bestGenome = candidate;
            context.bestIsSolution = isSolution(candidate);
        }
    }

    int percentAt(int position, int max) {
        int probePosition = position;
        return lcg::getPercent(&probePosition, max);
    }

    void addEvent(std::vector<RNGEvent> &events, int position, EventType type) {
        events.push_back({position, type});
    }

    std::vector<RNGEvent> scanEvents(int startPosition) {
        std::vector<RNGEvent> events;
        events.reserve(EVENT_SCAN_RANGE / 3);
        const int endPosition = startPosition + EVENT_SCAN_RANGE;

        for (int position = startPosition; position < endPosition; ++position) {
            const int percent10000 = percentAt(position, 0x2710);
            if (percent10000 < ALLY_CRITICAL_THRESHOLD) {
                addEvent(events, position, EventType::AttackCritical);
            }
            if (percent10000 < ALLY_CRITICAL_THRESHOLD / 2) {
                addEvent(events, position, EventType::DragonCritical);
            }
            if (percent10000 < 100) {
                addEvent(events, position, EventType::SpellCritical);
            }

            const int percent100 = percentAt(position, 100);
            if (percent100 < 1) {
                addEvent(events, position, EventType::SpecialCharge);
            }
            if (percent100 >= 50 && percent100 < 75) {
                addEvent(events, position, EventType::AcrobaticCounter);
            }
        }

        std::sort(events.begin(), events.end(), [](const RNGEvent &lhs, const RNGEvent &rhs) {
            if (lhs.position != rhs.position) {
                return lhs.position < rhs.position;
            }
            return static_cast<int>(lhs.type) < static_cast<int>(rhs.type);
        });
        return events;
    }

    int eventWeight(EventType type) {
        switch (type) {
            case EventType::AttackCritical:
                return 18000;
            case EventType::DragonCritical:
                return 22000;
            case EventType::SpellCritical:
                return 9000;
            case EventType::SpecialCharge:
                return 26000;
            case EventType::AcrobaticCounter:
                return 14000;
        }
        return 0;
    }

    int actionBaseScore(int action) {
        switch (action) {
            case BattleEmulator::DRAGON_SLASH:
                return 700;
            case BattleEmulator::ATTACK_ALLY:
                return 650;
            case BattleEmulator::ACROBATIC_STAR:
                return 1200;
            case BattleEmulator::CRACK_ALLY:
            case BattleEmulator::WOOSH_ALLY:
                return 900;
            case BattleEmulator::HEAL:
                return 760;
            case BattleEmulator::SPECIAL_MEDICINE:
            case BattleEmulator::SPECIAL_ANTIDOTE:
                return 980;
            case BattleEmulator::DEFENCE:
                return 520;
            case BattleEmulator::FLEE_ALLY:
                return 700;
            default:
                return 0;
        }
    }

    bool advanceGenome(const Genome &currentGenome, int action, uint64_t seed, Genome &nextGenome) {
        nextGenome = currentGenome;
        nextGenome.actions[currentGenome.turn - 1] = action;
        nextGenome.Initialized = true;

        Player copiedPlayers[2] = {currentGenome.AllyPlayer, currentGenome.EnemyPlayer};
        int position = currentGenome.position;
        uint64_t nowState = currentGenome.state;

        BattleEmulator::Main(&position, currentGenome.turn - currentGenome.processed, nextGenome.actions, copiedPlayers,
                             nullptr, seed, nullptr, nullptr, -2, &nowState);

        nextGenome.position = position;
        nextGenome.state = nowState;
        nextGenome.turn = currentGenome.turn + 1;
        nextGenome.processed = currentGenome.turn;
        nextGenome.AllyPlayer = copiedPlayers[0];
        nextGenome.EnemyPlayer = copiedPlayers[1];
        return nextGenome.AllyPlayer.hp > 0;
    }

    bool isLegalAction(const Genome &genome, int action) {
        for (const auto &entry: ACTION_TABLE) {
            if (entry.action == action) {
                return entry.condition(genome);
            }
        }
        return false;
    }

    int crossedEventScore(const SearchContext &context, int fromPosition, int toPosition) {
        int score = 0;
        for (const auto &event: context.events) {
            if (event.position < fromPosition) {
                continue;
            }
            if (event.position >= toPosition) {
                break;
            }
            score += eventWeight(event.type);
        }
        return score;
    }

    int distanceToNextEventPenalty(const SearchContext &context, int position) {
        for (const auto &event: context.events) {
            if (event.position >= position) {
                return event.position - position;
            }
        }
        return EVENT_SCAN_RANGE;
    }

    int evaluateCandidate(const Genome &currentGenome, const Genome &nextGenome, int action,
                          const SearchContext &context) {
        const int damageDealt = currentGenome.EnemyPlayer.hp - nextGenome.EnemyPlayer.hp;
        const int selfDamage = currentGenome.AllyPlayer.hp - nextGenome.AllyPlayer.hp;

        int score = actionBaseScore(action);
        score += damageDealt * 28;
        score -= selfDamage * 24;
        score += nextGenome.AllyPlayer.hp * 5;
        score += nextGenome.AllyPlayer.mp * 2;
        score += crossedEventScore(context, currentGenome.position, nextGenome.position);
        score -= distanceToNextEventPenalty(context, nextGenome.position) * 12;

        if (nextGenome.EnemyPlayer.hp <= 0) {
            score += 2'000'000 - nextGenome.turn * 4096;
        }
        if (nextGenome.AllyPlayer.specialCharge) {
            score += 5000;
        }
        if (action == BattleEmulator::HEAL || action == BattleEmulator::SPECIAL_MEDICINE ||
            action == BattleEmulator::SPECIAL_ANTIDOTE) {
            score += (currentGenome.AllyPlayer.maxHp - currentGenome.AllyPlayer.hp) * 70;
        }
        if (action == BattleEmulator::ACROBATIC_STAR && nextGenome.AllyPlayer.acrobaticStar) {
            score += 16000;
        }
        if (nextGenome.AllyPlayer.sleeping || nextGenome.AllyPlayer.paralysis) {
            score -= 6000;
        }
        if (action == BattleEmulator::HEAL &&
            currentGenome.AllyPlayer.hp > currentGenome.AllyPlayer.maxHp * 7 / 10) {
            score -= 2500;
        }
        return score;
    }

    int collectCandidates(const Genome &currentGenome, SearchContext &context,
                          std::array<SearchCandidate, std::size(ACTION_TABLE)> &candidates) {
        int count = 0;
        if (currentGenome.AllyPlayer.sleeping || currentGenome.AllyPlayer.paralysis) {
            Genome nextGenome{};
            initializeGenomeActions(nextGenome);
            if (advanceGenome(currentGenome, BattleEmulator::ATTACK_ALLY, context.seed, nextGenome)) {
                candidates[count].genome = nextGenome;
                candidates[count].action = BattleEmulator::ATTACK_ALLY;
                candidates[count].score = evaluateCandidate(currentGenome, nextGenome, BattleEmulator::ATTACK_ALLY,
                                                             context);
                ++count;
            }
            return count;
        }

        for (const auto &entry: ACTION_TABLE) {
            if (!entry.condition(currentGenome)) {
                continue;
            }
            Genome nextGenome{};
            initializeGenomeActions(nextGenome);
            if (!advanceGenome(currentGenome, entry.action, context.seed, nextGenome)) {
                continue;
            }
            candidates[count].genome = nextGenome;
            candidates[count].action = entry.action;
            candidates[count].score = evaluateCandidate(currentGenome, nextGenome, entry.action, context);
            ++count;
        }

        std::sort(candidates.begin(), candidates.begin() + count,
                  [](const SearchCandidate &lhs, const SearchCandidate &rhs) {
                      if (lhs.score != rhs.score) {
                          return lhs.score > rhs.score;
                      }
                      return lhs.action < rhs.action;
                  });
        return count;
    }

    bool cannotKillWithOptimisticBound(const Genome &genome, int remainingActions) {
        constexpr int OPTIMISTIC_DAMAGE_PER_TURN = 320;
        return genome.EnemyPlayer.hp > remainingActions * OPTIMISTIC_DAMAGE_PER_TURN;
    }

    bool depthFirstSearch(SearchContext &context, const Genome &currentGenome, int remainingActions) {
        if (context.nodesVisited >= context.nodeBudget) {
            context.exhausted = true;
            return false;
        }

        ++context.nodesVisited;
        updateBestGenome(context, currentGenome);

        if (isSolution(currentGenome)) {
            return true;
        }
        if (currentGenome.AllyPlayer.hp <= 0 || remainingActions <= 0) {
            return false;
        }
        if (cannotKillWithOptimisticBound(currentGenome, remainingActions)) {
            return false;
        }
        if (context.bestIsSolution && currentGenome.turn >= context.bestGenome.turn) {
            return false;
        }

        std::array<SearchCandidate, std::size(ACTION_TABLE)> candidates{};
        const int candidateCount = collectCandidates(currentGenome, context, candidates);
        context.nodesVisited += candidateCount;

        for (int i = 0; i < candidateCount; ++i) {
            const Genome &candidate = candidates[i].genome;
            updateBestGenome(context, candidate);
            if (isSolution(candidate)) {
                return true;
            }
            if (depthFirstSearch(context, candidate, remainingActions - 1)) {
                return true;
            }
            if (context.exhausted) {
                return false;
            }
        }
        return false;
    }

    void runBeam(SearchContext &context, const Genome &initialGenome) {
        std::vector<Genome> beam;
        std::vector<SearchCandidate> nextLayer;
        beam.reserve(BEAM_WIDTH);
        nextLayer.reserve(BEAM_WIDTH * std::size(ACTION_TABLE));
        beam.push_back(initialGenome);

        for (int depth = 0; depth < BEAM_DEPTH && context.nodesVisited < context.nodeBudget; ++depth) {
            nextLayer.clear();
            for (const auto &genome: beam) {
                std::array<SearchCandidate, std::size(ACTION_TABLE)> candidates{};
                const int candidateCount = collectCandidates(genome, context, candidates);
                context.nodesVisited += candidateCount;

                for (int i = 0; i < candidateCount; ++i) {
                    updateBestGenome(context, candidates[i].genome);
                    nextLayer.push_back(candidates[i]);
                    if (isSolution(candidates[i].genome)) {
                        return;
                    }
                }
                if (context.nodesVisited >= context.nodeBudget) {
                    context.exhausted = true;
                    return;
                }
            }

            if (nextLayer.empty()) {
                return;
            }
            std::sort(nextLayer.begin(), nextLayer.end(),
                      [](const SearchCandidate &lhs, const SearchCandidate &rhs) {
                          if (lhs.score != rhs.score) {
                              return lhs.score > rhs.score;
                          }
                          if (lhs.genome.EnemyPlayer.hp != rhs.genome.EnemyPlayer.hp) {
                              return lhs.genome.EnemyPlayer.hp < rhs.genome.EnemyPlayer.hp;
                          }
                          return lhs.action < rhs.action;
                      });

            const int keepCount = std::min(static_cast<int>(nextLayer.size()), BEAM_WIDTH);
            beam.clear();
            for (int i = 0; i < keepCount; ++i) {
                beam.push_back(nextLayer[i].genome);
            }
        }
    }

    uint64_t mix(uint64_t value) {
        value ^= value >> 30;
        value *= 0xbf58476d1ce4e5b9ULL;
        value ^= value >> 27;
        value *= 0x94d049bb133111ebULL;
        value ^= value >> 31;
        return value;
    }

    void runDeterministicRollouts(SearchContext &context, const Genome &initialGenome) {
        for (int iteration = 0;
             iteration < ROLLOUT_COUNT && context.nodesVisited < context.nodeBudget; ++iteration) {
            Genome currentGenome = initialGenome;
            for (int depth = 0;
                 depth < BEAM_DEPTH && currentGenome.AllyPlayer.hp > 0 && currentGenome.EnemyPlayer.hp > 0 &&
                 context.nodesVisited < context.nodeBudget; ++depth) {
                std::array<SearchCandidate, std::size(ACTION_TABLE)> candidates{};
                const int candidateCount = collectCandidates(currentGenome, context, candidates);
                context.nodesVisited += candidateCount;
                if (candidateCount == 0) {
                    break;
                }

                const int selectableCount = std::min(candidateCount, ROLLOUT_CHOICES);
                const auto selector = mix(context.seed ^ (static_cast<uint64_t>(iteration) << 32) ^
                                          static_cast<uint64_t>(depth));
                const int index = static_cast<int>(selector % static_cast<uint64_t>(selectableCount));
                currentGenome = candidates[index].genome;
                updateBestGenome(context, currentGenome);
                if (isSolution(currentGenome)) {
                    return;
                }
            }
        }
    }

    void runPatternRollouts(SearchContext &context, const Genome &initialGenome) {
        constexpr std::array<std::array<int, 20>, 16> patterns{{
            {BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::CRACK_ALLY, BattleEmulator::SPECIAL_MEDICINE,
             BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::DRAGON_SLASH, BattleEmulator::ACROBATIC_STAR,
             BattleEmulator::DRAGON_SLASH, BattleEmulator::ATTACK_ALLY, BattleEmulator::FLEE_ALLY,
             BattleEmulator::CRACK_ALLY, BattleEmulator::ATTACK_ALLY, BattleEmulator::ACROBATIC_STAR,
             BattleEmulator::WOOSH_ALLY, BattleEmulator::ATTACK_ALLY, BattleEmulator::CRACK_ALLY,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::DRAGON_SLASH, BattleEmulator::ATTACK_ALLY},
            {BattleEmulator::ATTACK_ALLY, BattleEmulator::ACROBATIC_STAR, BattleEmulator::HEAL,
             BattleEmulator::CRACK_ALLY, BattleEmulator::CRACK_ALLY, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::SPECIAL_ANTIDOTE, BattleEmulator::WOOSH_ALLY, BattleEmulator::ACROBATIC_STAR,
             BattleEmulator::CRACK_ALLY, BattleEmulator::DRAGON_SLASH, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::CRACK_ALLY, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::FLEE_ALLY, BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::CRACK_ALLY, BattleEmulator::ATTACK_ALLY},
            {BattleEmulator::DEFENCE, BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::DRAGON_SLASH,
             BattleEmulator::ACROBATIC_STAR, BattleEmulator::WOOSH_ALLY, BattleEmulator::SPECIAL_MEDICINE,
             BattleEmulator::DRAGON_SLASH, BattleEmulator::ATTACK_ALLY, BattleEmulator::FLEE_ALLY,
             BattleEmulator::CRACK_ALLY, BattleEmulator::ACROBATIC_STAR, BattleEmulator::DRAGON_SLASH,
             BattleEmulator::WOOSH_ALLY, BattleEmulator::CRACK_ALLY, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::DRAGON_SLASH, BattleEmulator::ATTACK_ALLY},
            {BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::SPECIAL_MEDICINE,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::HEAL, BattleEmulator::WOOSH_ALLY, BattleEmulator::ACROBATIC_STAR,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY, BattleEmulator::FLEE_ALLY,
             BattleEmulator::HEAL, BattleEmulator::CRACK_ALLY, BattleEmulator::FLEE_ALLY,
             BattleEmulator::CRACK_ALLY, BattleEmulator::ATTACK_ALLY, BattleEmulator::SPECIAL_ANTIDOTE,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY},
            {BattleEmulator::CRACK_ALLY, BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::SPECIAL_MEDICINE,
             BattleEmulator::ACROBATIC_STAR, BattleEmulator::CRACK_ALLY, BattleEmulator::CRACK_ALLY,
             BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::FLEE_ALLY, BattleEmulator::FLEE_ALLY,
             BattleEmulator::DRAGON_SLASH, BattleEmulator::DRAGON_SLASH, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::HEAL, BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY}
            ,
            {BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::CRACK_ALLY, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::DRAGON_SLASH, BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::ACROBATIC_STAR,
             BattleEmulator::DRAGON_SLASH, BattleEmulator::ATTACK_ALLY, BattleEmulator::CRACK_ALLY,
             BattleEmulator::DRAGON_SLASH, BattleEmulator::FLEE_ALLY, BattleEmulator::CRACK_ALLY,
             BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::CRACK_ALLY, BattleEmulator::CRACK_ALLY,
             BattleEmulator::CRACK_ALLY, BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::DRAGON_SLASH, BattleEmulator::ATTACK_ALLY},
            {BattleEmulator::ATTACK_ALLY, BattleEmulator::DRAGON_SLASH, BattleEmulator::SPECIAL_MEDICINE,
             BattleEmulator::ACROBATIC_STAR, BattleEmulator::DRAGON_SLASH, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::CRACK_ALLY, BattleEmulator::DRAGON_SLASH, BattleEmulator::SPECIAL_MEDICINE,
             BattleEmulator::ACROBATIC_STAR, BattleEmulator::CRACK_ALLY, BattleEmulator::CRACK_ALLY,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY, BattleEmulator::DRAGON_SLASH,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::CRACK_ALLY,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY},
            {BattleEmulator::ACROBATIC_STAR, BattleEmulator::HEAL, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::CRACK_ALLY, BattleEmulator::ATTACK_ALLY, BattleEmulator::DRAGON_SLASH,
             BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::ACROBATIC_STAR, BattleEmulator::CRACK_ALLY,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::DRAGON_SLASH, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::CRACK_ALLY, BattleEmulator::ATTACK_ALLY},
            {BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::HEAL,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::ACROBATIC_STAR, BattleEmulator::HEAL, BattleEmulator::HEAL,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::CRACK_ALLY, BattleEmulator::FLEE_ALLY,
             BattleEmulator::FLEE_ALLY, BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::ATTACK_ALLY},
            {BattleEmulator::CRACK_ALLY, BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::ACROBATIC_STAR,
             BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::CRACK_ALLY, BattleEmulator::CRACK_ALLY,
             BattleEmulator::HEAL, BattleEmulator::CRACK_ALLY, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::ACROBATIC_STAR, BattleEmulator::ATTACK_ALLY, BattleEmulator::DRAGON_SLASH,
             BattleEmulator::DRAGON_SLASH, BattleEmulator::DRAGON_SLASH, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::CRACK_ALLY, BattleEmulator::ATTACK_ALLY},
            {BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::DRAGON_SLASH, BattleEmulator::CRACK_ALLY,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::ACROBATIC_STAR, BattleEmulator::SPECIAL_MEDICINE,
             BattleEmulator::WOOSH_ALLY, BattleEmulator::FLEE_ALLY, BattleEmulator::FLEE_ALLY,
             BattleEmulator::CRACK_ALLY, BattleEmulator::FLEE_ALLY, BattleEmulator::DRAGON_SLASH,
             BattleEmulator::ACROBATIC_STAR, BattleEmulator::WOOSH_ALLY, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::CRACK_ALLY, BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::DRAGON_SLASH, BattleEmulator::ATTACK_ALLY},
            {BattleEmulator::WOOSH_ALLY, BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::ACROBATIC_STAR,
             BattleEmulator::CRACK_ALLY, BattleEmulator::HEAL, BattleEmulator::WOOSH_ALLY,
             BattleEmulator::CRACK_ALLY, BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::HEAL, BattleEmulator::ATTACK_ALLY, BattleEmulator::SPECIAL_MEDICINE,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::ACROBATIC_STAR, BattleEmulator::SPECIAL_ANTIDOTE,
             BattleEmulator::DRAGON_SLASH, BattleEmulator::DRAGON_SLASH, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY},
            {BattleEmulator::SPECIAL_ANTIDOTE, BattleEmulator::FLEE_ALLY, BattleEmulator::CRACK_ALLY,
             BattleEmulator::ACROBATIC_STAR, BattleEmulator::FLEE_ALLY, BattleEmulator::DRAGON_SLASH,
             BattleEmulator::CRACK_ALLY, BattleEmulator::CRACK_ALLY, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::FLEE_ALLY, BattleEmulator::FLEE_ALLY, BattleEmulator::DRAGON_SLASH,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::DRAGON_SLASH, BattleEmulator::ATTACK_ALLY, BattleEmulator::CRACK_ALLY,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY},
            {BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::HEAL, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::HEAL, BattleEmulator::ACROBATIC_STAR,
             BattleEmulator::CRACK_ALLY, BattleEmulator::DRAGON_SLASH, BattleEmulator::DRAGON_SLASH,
             BattleEmulator::CRACK_ALLY, BattleEmulator::DRAGON_SLASH, BattleEmulator::ACROBATIC_STAR,
             BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::WOOSH_ALLY, BattleEmulator::WOOSH_ALLY,
             BattleEmulator::DRAGON_SLASH, BattleEmulator::CRACK_ALLY, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY},
            {BattleEmulator::SPECIAL_ANTIDOTE, BattleEmulator::DRAGON_SLASH, BattleEmulator::SPECIAL_ANTIDOTE,
             BattleEmulator::CRACK_ALLY, BattleEmulator::ACROBATIC_STAR, BattleEmulator::DRAGON_SLASH,
             BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::DRAGON_SLASH, BattleEmulator::DRAGON_SLASH,
             BattleEmulator::CRACK_ALLY, BattleEmulator::ACROBATIC_STAR, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::WOOSH_ALLY, BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY, BattleEmulator::DRAGON_SLASH,
             BattleEmulator::ATTACK_ALLY, BattleEmulator::ATTACK_ALLY}
        }};

        for (const auto &pattern: patterns) {
            for (int offset = 0; offset < static_cast<int>(pattern.size()) &&
                               context.nodesVisited < context.nodeBudget; ++offset) {
                Genome currentGenome = initialGenome;
                for (int depth = 0; depth < BEAM_DEPTH && currentGenome.AllyPlayer.hp > 0 &&
                                    currentGenome.EnemyPlayer.hp > 0; ++depth) {
                    int action = pattern[(depth + offset) % static_cast<int>(pattern.size())];
                    if (!isLegalAction(currentGenome, action)) {
                        std::array<SearchCandidate, std::size(ACTION_TABLE)> candidates{};
                        const int candidateCount = collectCandidates(currentGenome, context, candidates);
                        context.nodesVisited += candidateCount;
                        if (candidateCount == 0) {
                            break;
                        }
                        currentGenome = candidates[0].genome;
                    } else {
                        Genome nextGenome{};
                        initializeGenomeActions(nextGenome);
                        if (!advanceGenome(currentGenome, action, context.seed, nextGenome)) {
                            break;
                        }
                        ++context.nodesVisited;
                        currentGenome = nextGenome;
                    }
                    updateBestGenome(context, currentGenome);
                    if (isSolution(currentGenome)) {
                        return;
                    }
                }
            }
        }
    }

    void runIddfs(SearchContext &context, const Genome &initialGenome, int maxTargetTurn) {
        const int currentActionCount = initialGenome.turn - 1;
        for (int targetTurn = currentActionCount + 1;
             targetTurn <= maxTargetTurn && context.nodesVisited < context.nodeBudget; ++targetTurn) {
            context.exhausted = false;
            if (depthFirstSearch(context, initialGenome, targetTurn - currentActionCount)) {
                return;
            }
            if (context.exhausted) {
                return;
            }
        }
    }
}

uint32_t ActionOptimizer::getNodesUsed() {
    return Node_Used;
}

Genome ActionOptimizer::RunAlgorithm(const Player players[2], uint64_t seed, int turns, int maxGenerations,
                                     int actions[350], int seedOffset) {
    (void) seedOffset;
    lcg::init(seed, true);
    Node_Used = 0;

    Player copiedPlayers[2] = {players[0], players[1]};
    int position = 1;
    uint64_t nowState = 0;

    BattleEmulator::Main(&position, turns, actions, copiedPlayers, nullptr, seed, nullptr, nullptr, -2, &nowState);

    Genome initialGenome{};
    initializeGenomeActions(initialGenome);
    initialGenome.EnemyPlayer = copiedPlayers[1];
    initialGenome.AllyPlayer = copiedPlayers[0];
    initialGenome.EActions[0] = -1;
    initialGenome.EActions[1] = -1;
    initialGenome.Aactions = -1;
    initialGenome.fitness = 0;
    initialGenome.turn = turns + 1;
    initialGenome.processed = turns;
    initialGenome.Initialized = false;
    initialGenome.Visited = 0;
    initialGenome.position = position;
    initialGenome.state = nowState;

    for (int i = 0; i < 350; ++i) {
        if (actions[i] == -1 || actions[i] == 0) {
            initialGenome.actions[i] = -1;
            break;
        }
        initialGenome.actions[i] = actions[i];
    }

    if (isSolution(initialGenome)) {
        return initialGenome;
    }

    SearchContext context{};
    context.seed = seed;
    context.nodeBudget = maxGenerations <= 0 ? 5'000 : std::max(5'000, maxGenerations);
    context.events = scanEvents(initialGenome.position);
    context.bestGenome = initialGenome;
    updateBestGenome(context, initialGenome);

    runPatternRollouts(context, initialGenome);
    runBeam(context, initialGenome);
    if (!context.bestIsSolution) {
        runDeterministicRollouts(context, initialGenome);
    }
    if (!context.bestIsSolution && context.nodesVisited < context.nodeBudget) {
        depthFirstSearch(context, initialGenome, BEAM_DEPTH);
    }

    const int currentActionCount = initialGenome.turn - 1;
    const int bestKnownTurn = context.bestIsSolution ? context.bestGenome.turn - 1 : currentActionCount + BEAM_DEPTH;
    if (context.bestIsSolution) {
        context.nodeBudget = std::min(context.nodeBudget, context.nodesVisited + 40'000);
    }
    runIddfs(context, initialGenome, bestKnownTurn - 1);

    Node_Used = static_cast<uint32_t>(context.nodesVisited);
    if (context.bestIsSolution) {
        return context.bestGenome;
    }

    context.bestGenome.turn = NO_SOLUTION_TURN;
    return context.bestGenome;
}

void ActionOptimizer::updateCompromiseScore(Genome &genome) {
    (void) genome;
}

std::pair<int, Genome> ActionOptimizer::RunAlgorithmAsync(const Player players[2], uint64_t seed, int turns,
                                                          int maxGenerations, int actions[350], int numThreads,
                                                          bool dropbug) {
    (void) numThreads;
    (void) dropbug;
    auto genome = RunAlgorithm(players, seed, turns, maxGenerations, actions, 0);
    return {0, genome};
}
