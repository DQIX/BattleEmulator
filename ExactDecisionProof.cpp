#include "ExactDecisionProof.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "BattleEmulator.h"
#include "lcg.h"

namespace {
    struct ExactSearchState {
        // The three words are the exact state itself, not a digest/hash.
        // Every packed field is injective over its proven emulator domain.
        uint64_t rngAndPresentation;
        uint64_t resources;
        uint64_t status;

        bool operator==(const ExactSearchState &) const = default;
    };

    static_assert(sizeof(int) == sizeof(uint32_t));
    static_assert(sizeof(ExactSearchState) == 3 * sizeof(uint64_t));

    constexpr uint64_t BitMask(unsigned bits) {
        return (uint64_t{1} << bits) - 1;
    }

    uint64_t CheckedUnsigned(int value, uint64_t maximum, const char *name) {
        if (value < 0 || static_cast<uint64_t>(value) > maximum) [[unlikely]] {
            throw std::logic_error(std::string("exact state field out of range: ") + name);
        }
        return static_cast<uint64_t>(value);
    }

    uint64_t CheckedBiased(int value, int bias, uint64_t maximum, const char *name) {
        const int64_t biased = static_cast<int64_t>(value) + bias;
        if (biased < 0 || static_cast<uint64_t>(biased) > maximum) [[unlikely]] {
            throw std::logic_error(std::string("exact state field out of range: ") + name);
        }
        return static_cast<uint64_t>(biased);
    }

    uint64_t Field(uint64_t word, unsigned shift, unsigned bits) {
        return (word >> shift) & BitMask(bits);
    }

    int Position(const ExactSearchState &state) {
        return static_cast<int>(Field(state.rngAndPresentation, 0, 32));
    }

    int CameraCounter(const ExactSearchState &state) {
        return static_cast<int>(Field(state.rngAndPresentation, 32, 4));
    }

    int HeroHp(const ExactSearchState &state) {
        return static_cast<int>(Field(state.resources, 0, 7));
    }

    int EnemyHp(const ExactSearchState &state) {
        return static_cast<int>(Field(state.resources, 7, 9));
    }

    int HeroMp(const ExactSearchState &state) {
        return static_cast<int>(Field(state.resources, 16, 5));
    }

    int HeroMedicinalHerbs(const ExactSearchState &state) {
        return static_cast<int>(Field(state.resources, 21, 3));
    }

    bool HeroSpecialCharge(const ExactSearchState &state) {
        return Field(state.status, 0, 3) != 0;
    }

    int HeroSpecialChargeTurn(const ExactSearchState &state) {
        const auto encoded = Field(state.status, 0, 3);
        return encoded == 0 ? 0 : static_cast<int>(encoded - 1);
    }

    bool HeroParalysis(const ExactSearchState &state) {
        return Field(state.status, 3, 1) != 0;
    }

    int HeroParalysisLevel(const ExactSearchState &state) {
        return static_cast<int>(Field(state.status, 4, 10));
    }

    int HeroParalysisTurns(const ExactSearchState &state) {
        return static_cast<int>(Field(state.status, 14, 4)) - 8;
    }

    bool HeroAcrobaticStar(const ExactSearchState &state) {
        return Field(state.status, 18, 3) != 0;
    }

    int HeroAcrobaticStarTurn(const ExactSearchState &state) {
        const auto encoded = Field(state.status, 18, 3);
        return encoded == 0 ? 0 : static_cast<int>(encoded);
    }

    bool HeroInactive(const ExactSearchState &state) {
        return Field(state.status, 21, 1) != 0;
    }

    bool EnemySpecialCharge(const ExactSearchState &state) {
        return Field(state.status, 22, 1) != 0;
    }

    bool EnemyRage(const ExactSearchState &state) {
        return Field(state.status, 23, 3) != 0;
    }

    int EnemyRageTurns(const ExactSearchState &state) {
        return static_cast<int>(Field(state.status, 23, 3));
    }

    struct ExactStateOrder {
        bool operator()(const ExactSearchState &left, const ExactSearchState &right) const {
            if (left.rngAndPresentation != right.rngAndPresentation) {
                return left.rngAndPresentation < right.rngAndPresentation;
            }
            if (left.resources != right.resources) {
                return left.resources < right.resources;
            }
            return left.status < right.status;
        }
    };

    void RadixPass16(std::vector<ExactSearchState> &states,
                     std::vector<ExactSearchState> &scratch,
                     uint64_t ExactSearchState::*member,
                     unsigned shift) {
        constexpr std::size_t kBucketCount = 1u << 16;
        std::array<uint32_t, kBucketCount> offsets{};

        for (const auto &state : states) {
            const auto digit = static_cast<uint16_t>((state.*member) >> shift);
            ++offsets[digit];
        }

        uint32_t total = 0;
        for (auto &offset : offsets) {
            const uint32_t count = offset;
            offset = total;
            total += count;
        }

        scratch.resize(states.size());
        for (const auto &state : states) {
            const auto digit = static_cast<uint16_t>((state.*member) >> shift);
            scratch[offsets[digit]++] = state;
        }
        states.swap(scratch);
    }

    void CanonicalizeRun(std::vector<ExactSearchState> &states) {
        if (states.size() > UINT32_MAX) [[unlikely]] {
            throw std::logic_error("exact radix run exceeds uint32_t indexing");
        }

        std::vector<ExactSearchState> scratch;
        scratch.reserve(states.size());

        // LSD radix order for the lexicographic key
        // (rngAndPresentation, resources, status).  Only the used bits are
        // visited: status=26, resources=24, rngAndPresentation=36.
        RadixPass16(states, scratch, &ExactSearchState::status, 0);
        RadixPass16(states, scratch, &ExactSearchState::status, 16);
        RadixPass16(states, scratch, &ExactSearchState::resources, 0);
        RadixPass16(states, scratch, &ExactSearchState::resources, 16);
        RadixPass16(states, scratch, &ExactSearchState::rngAndPresentation, 0);
        RadixPass16(states, scratch, &ExactSearchState::rngAndPresentation, 16);
        RadixPass16(states, scratch, &ExactSearchState::rngAndPresentation, 32);

        states.erase(std::unique(states.begin(), states.end()), states.end());
    }

    std::vector<ExactSearchState> MergeCanonicalRuns(const std::vector<ExactSearchState> &left,
                                                     const std::vector<ExactSearchState> &right) {
        std::vector<ExactSearchState> merged;
        merged.reserve(left.size() + right.size());

        const ExactStateOrder order;
        std::size_t leftIndex = 0;
        std::size_t rightIndex = 0;

        auto appendUnique = [&](const ExactSearchState &state) {
            if (merged.empty() || !(merged.back() == state)) {
                merged.push_back(state);
            }
        };

        while (leftIndex < left.size() || rightIndex < right.size()) {
            if (rightIndex == right.size() ||
                (leftIndex < left.size() && !order(right[rightIndex], left[leftIndex]))) {
                appendUnique(left[leftIndex++]);
            } else {
                appendUnique(right[rightIndex++]);
            }
        }
        return merged;
    }

    class CanonicalRunAccumulator {
    public:
        void Add(std::vector<ExactSearchState> run) {
            if (run.empty()) {
                return;
            }
            CanonicalizeRun(run);

            std::size_t level = 0;
            while (true) {
                if (level == levels_.size()) {
                    levels_.emplace_back();
                }
                if (levels_[level].empty()) {
                    levels_[level] = std::move(run);
                    return;
                }

                run = MergeCanonicalRuns(levels_[level], run);
                std::vector<ExactSearchState>().swap(levels_[level]);
                ++level;
            }
        }

        std::vector<ExactSearchState> Finish() {
            std::vector<ExactSearchState> result;
            for (auto &run : levels_) {
                if (run.empty()) {
                    continue;
                }
                if (result.empty()) {
                    result = std::move(run);
                } else {
                    result = MergeCanonicalRuns(result, run);
                }
            }
            return result;
        }

    private:
        std::vector<std::vector<ExactSearchState>> levels_;
    };

    ExactSearchState CaptureState(const Player players[2], int position, uint64_t nowState) {
        ExactSearchState state{};
        const auto packedPosition = CheckedUnsigned(position, UINT32_MAX, "position");
        const auto cameraCounter = (nowState >> 8) & 0x0f;
        state.rngAndPresentation = packedPosition | (cameraCounter << 32);

        const auto heroHp = CheckedUnsigned(players[0].hp, 65, "hero.hp");
        const auto enemyHp = CheckedUnsigned(players[1].hp, 456, "enemy.hp");
        const auto heroMp = CheckedUnsigned(players[0].mp, 22, "hero.mp");
        const auto herbs = CheckedUnsigned(players[0].medicinal_herbs_count, 7, "hero.medicinal_herbs_count");
        state.resources = heroHp | (enemyHp << 7) | (heroMp << 16) | (herbs << 21);

        uint64_t specialCode = 0;
        if (players[0].specialCharge) {
            specialCode = CheckedUnsigned(players[0].specialChargeTurn, 6, "hero.specialChargeTurn") + 1;
        }

        const auto paralysis = players[0].paralysis ? uint64_t{1} : uint64_t{0};
        const auto paralysisLevel = CheckedUnsigned(players[0].paralysisLevel, 1023, "hero.paralysisLevel");
        const auto paralysisTurns = CheckedBiased(players[0].paralysis ? players[0].paralysisTurns : -1,
                                                  8, 15, "hero.paralysisTurns");

        uint64_t acroCode = 0;
        if (players[0].acrobaticStar) {
            acroCode = CheckedUnsigned(players[0].acrobaticStarTurn, 6, "hero.acrobaticStarTurn");
            if (acroCode == 0) [[unlikely]] {
                throw std::logic_error("exact state field out of range: hero.acrobaticStarTurn");
            }
        }

        uint64_t rageCode = 0;
        if (players[1].rage) {
            rageCode = CheckedUnsigned(players[1].rageTurns, 4, "enemy.rageTurns");
            if (rageCode == 0) [[unlikely]] {
                throw std::logic_error("exact state field out of range: enemy.rageTurns");
            }
        }

        state.status = specialCode |
                       (paralysis << 3) |
                       (paralysisLevel << 4) |
                       (paralysisTurns << 14) |
                       (acroCode << 18) |
                       (static_cast<uint64_t>(players[0].inactive) << 21) |
                       (static_cast<uint64_t>(players[1].specialCharge) << 22) |
                       (rageCode << 23);
        return state;
    }

    void RestoreState(const ExactSearchState &state, Player players[2], int &position, uint64_t &nowState) {
        players[0].hp = HeroHp(state);
        players[0].mp = HeroMp(state);
        players[0].specialCharge = HeroSpecialCharge(state);
        players[0].specialChargeTurn = HeroSpecialChargeTurn(state);
        players[0].paralysis = HeroParalysis(state);
        players[0].paralysisLevel = HeroParalysisLevel(state);
        players[0].paralysisTurns = HeroParalysisTurns(state);
        players[0].acrobaticStar = HeroAcrobaticStar(state);
        players[0].acrobaticStarTurn = HeroAcrobaticStarTurn(state);
        players[0].medicinal_herbs_count = HeroMedicinalHerbs(state);
        players[0].inactive = HeroInactive(state);
        players[0].defence = 1.0;

        players[1].hp = EnemyHp(state);
        players[1].specialCharge = EnemySpecialCharge(state);
        // Enemy specialChargeTurn is only written together with specialCharge
        // in this battle and is never decremented/read by the transition rules.
        // Restore the exact reachable representative instead of carrying a
        // previous action's workspace value into the next edge evaluation.
        players[1].specialChargeTurn = players[1].specialCharge ? 6 : 0;
        players[1].rage = EnemyRage(state);
        players[1].rageTurns = EnemyRage(state) ? EnemyRageTurns(state) : -1;

        position = Position(state);
        nowState = static_cast<uint64_t>(CameraCounter(state)) << 8;
    }

    bool IsHeroCommandSelectable(const ExactSearchState &state, int action) {
        switch (action) {
            case BattleEmulator::ATTACK_ALLY:
            case BattleEmulator::DRAGON_SLASH:
            case BattleEmulator::DEFENCE:
            case BattleEmulator::FLEE_ALLY:
                return true;
            case BattleEmulator::MEDICINAL_HERBS:
                return HeroMedicinalHerbs(state) > 0;
            case BattleEmulator::HEAL:
                return HeroMp(state) >= 2;
            case BattleEmulator::CRACK_ALLY:
                return HeroMp(state) >= 3;
            case BattleEmulator::ACROBATIC_STAR:
                return HeroSpecialCharge(state) && HeroSpecialChargeTurn(state) != 0 &&
                       !HeroAcrobaticStar(state);
            default:
                return false;
        }
    }

    void RunOneSearchTurn(const ExactSearchState &source, Player players[2],
                          uint64_t seed, int forcedHeroAction, bool stopBeforePresentationTail,
                          int &position, uint64_t &nowState) {
        RestoreState(source, players, position, nowState);

        // logicalTurnStart=true and a zero turn-count NowState make this call
        // execute exactly one turn with genePosition == 0.  Main therefore
        // reads only Gene[0]; zeroing 350 entries on every sparse-matrix edge
        // is pure overhead.
        const int32_t gene[1] = {forcedHeroAction};

        BattleEmulator::Main(&position, 1, gene, players, nullptr, seed, nullptr, nullptr, -2,
                             &nowState, true, stopBeforePresentationTail);
    }

    ExactSearchState StepSearchState(const ExactSearchState &source, Player players[2],
                                     uint64_t seed, int forcedHeroAction) {
        int position = 1;
        uint64_t nowState = 0;
        RunOneSearchTurn(source, players, seed, forcedHeroAction, false, position, nowState);
        return CaptureState(players, position, nowState);
    }

    bool FinalLayerKills(const ExactSearchState &source, Player players[2],
                         uint64_t seed, int forcedHeroAction) {
        int position = 1;
        uint64_t nowState = 0;
        RunOneSearchTurn(source, players, seed, forcedHeroAction, true, position, nowState);
        return players[1].hp == 0;
    }

    constexpr std::array<int, 8> kHeroCommands = {
        BattleEmulator::ATTACK_ALLY,
        BattleEmulator::DRAGON_SLASH,
        BattleEmulator::DEFENCE,
        BattleEmulator::FLEE_ALLY,
        BattleEmulator::MEDICINAL_HERBS,
        BattleEmulator::HEAL,
        BattleEmulator::CRACK_ALLY,
        BattleEmulator::ACROBATIC_STAR,
    };

    constexpr std::size_t kStatePartitionCount = 8;
    using StatePartitions = std::array<std::vector<ExactSearchState>, kStatePartitionCount>;

    std::size_t StatePartition(const ExactSearchState &state) {
        // Partitioning only chooses storage ownership.  It is not state
        // identity: equality remains all three uint64_t words exactly equal.
        return static_cast<std::size_t>(Position(state)) & (kStatePartitionCount - 1);
    }

    std::size_t FrontierSize(const StatePartitions &frontier) {
        std::size_t size = 0;
        for (const auto &partition : frontier) {
            size += partition.size();
        }
        return size;
    }

    struct WorkerResult {
        StatePartitions next;
        std::size_t expandedStates = 0;
        std::size_t generatedTransitions = 0;
        bool reachable = false;
        std::exception_ptr error;
    };

    unsigned ProofThreadCount(std::size_t frontierSize) {
#if defined(__EMSCRIPTEN__)
        (void)frontierSize;
        return 1;
#else
        if (frontierSize < 4096) {
            return 1;
        }
        const unsigned hardwareThreads = std::max(1u, std::thread::hardware_concurrency());
        return static_cast<unsigned>(std::min<std::size_t>(hardwareThreads, frontierSize));
#endif
    }

    unsigned PartitionThreadCount() {
#if defined(__EMSCRIPTEN__)
        return 1;
#else
        return std::min<unsigned>(std::max(1u, std::thread::hardware_concurrency()),
                                  static_cast<unsigned>(kStatePartitionCount));
#endif
    }

    void ExpandRange(const std::vector<ExactSearchState> &frontier,
                     std::size_t begin, std::size_t end,
                     const Player initialPlayers[2], uint64_t seed,
                     bool finalLayer, WorkerResult &worker) {
        try {
            if (!finalLayer) {
                const std::size_t sourceCount = end - begin;
                for (auto &partition : worker.next) {
                    partition.reserve(sourceCount);
                }
            }
            Player players[2] = {initialPlayers[0], initialPlayers[1]};

            for (std::size_t index = begin; index < end; ++index) {
                const auto &source = frontier[index];
                ++worker.expandedStates;
                for (const int action : kHeroCommands) {
                    if (!IsHeroCommandSelectable(source, action)) {
                        continue;
                    }

                    ++worker.generatedTransitions;

                    if (finalLayer) {
                        if (FinalLayerKills(source, players, seed, action)) {
                            worker.reachable = true;
                        }
                        continue;
                    }

                    const auto destination = StepSearchState(source, players, seed, action);

                    if (EnemyHp(destination) == 0) {
                        worker.reachable = true;
                        continue;
                    }
                    if (HeroHp(destination) == 0) {
                        continue;
                    }

                    worker.next[StatePartition(destination)].push_back(destination);
                }
            }
        } catch (...) {
            worker.error = std::current_exception();
        }
    }
}

ExactDecisionProofResult ExactDecisionProof::Run(const Player initialPlayers[2], uint64_t seed, int horizon) {
    if (initialPlayers == nullptr) {
        throw std::invalid_argument("initialPlayers is null");
    }
    if (seed == 0) {
        throw std::invalid_argument("seed must be non-zero");
    }
    if (horizon < 0 || horizon >= 350) {
        throw std::invalid_argument("horizon must be in [0, 349]");
    }

    const auto startedAt = std::chrono::steady_clock::now();
    ExactDecisionProofResult result;
    result.frontierSizes.reserve(static_cast<std::size_t>(horizon) + 1);

    // Native workers share the LCG tape read-only.  Precompute it before any
    // worker starts so concurrent BattleEmulator::Main calls never mutate the
    // generator cache.
    lcg::init(seed, true);

    StatePartitions frontier;
    const auto initialState = CaptureState(initialPlayers, 1, 0);
    frontier[StatePartition(initialState)].push_back(initialState);
    result.frontierSizes.push_back(1);

    if (initialPlayers[1].hp == 0) {
        result.reachable = true;
    }

    for (int depth = 0; depth < horizon && !result.reachable; ++depth) {
        const bool finalLayer = (depth + 1 == horizon);
        std::array<CanonicalRunAccumulator, kStatePartitionCount> nextAccumulators;
        constexpr std::size_t kSourceChunkStates = 1u << 18;

        for (auto &sourcePartition : frontier) {
            for (std::size_t chunkBegin = 0;
                 chunkBegin < sourcePartition.size() && !result.reachable;
                 chunkBegin += kSourceChunkStates) {
                const std::size_t chunkEnd = std::min(sourcePartition.size(), chunkBegin + kSourceChunkStates);
                const std::size_t chunkSize = chunkEnd - chunkBegin;
                const unsigned threadCount = ProofThreadCount(chunkSize);
                std::vector<WorkerResult> workers(threadCount);
                std::vector<std::thread> threads;
                threads.reserve(threadCount > 1 ? threadCount : 0);

                for (unsigned threadIndex = 0; threadIndex < threadCount; ++threadIndex) {
                    const std::size_t begin = chunkBegin + chunkSize * threadIndex / threadCount;
                    const std::size_t end = chunkBegin + chunkSize * (threadIndex + 1) / threadCount;
                    if (threadCount == 1) {
                        ExpandRange(sourcePartition, begin, end, initialPlayers, seed,
                                    finalLayer, workers[threadIndex]);
                    } else {
                        threads.emplace_back(ExpandRange, std::cref(sourcePartition), begin, end,
                                             initialPlayers, seed, finalLayer, std::ref(workers[threadIndex]));
                    }
                }
                for (auto &thread : threads) {
                    thread.join();
                }

                for (auto &worker : workers) {
                    if (worker.error) {
                        std::rethrow_exception(worker.error);
                    }
                    result.expandedStates += worker.expandedStates;
                    result.generatedTransitions += worker.generatedTransitions;
                    result.reachable = result.reachable || worker.reachable;
                }

                if (!finalLayer && !result.reachable) {
                    StatePartitions runs;
                    for (std::size_t partitionIndex = 0;
                         partitionIndex < kStatePartitionCount;
                         ++partitionIndex) {
                        std::size_t runSize = 0;
                        for (const auto &worker : workers) {
                            runSize += worker.next[partitionIndex].size();
                        }
                        if (runSize == 0) {
                            continue;
                        }

                        auto &run = runs[partitionIndex];
                        run.reserve(runSize);
                        for (auto &worker : workers) {
                            auto &states = worker.next[partitionIndex];
                            run.insert(run.end(), states.begin(), states.end());
                            std::vector<ExactSearchState>().swap(states);
                        }
                    }

                    const unsigned partitionThreads = PartitionThreadCount();
                    std::vector<std::thread> dedupThreads;
                    dedupThreads.reserve(partitionThreads > 1 ? partitionThreads : 0);
                    auto addPartitions = [&](unsigned threadIndex) {
                        for (std::size_t partitionIndex = threadIndex;
                             partitionIndex < kStatePartitionCount;
                             partitionIndex += partitionThreads) {
                            if (!runs[partitionIndex].empty()) {
                                nextAccumulators[partitionIndex].Add(std::move(runs[partitionIndex]));
                            }
                        }
                    };

                    if (partitionThreads == 1) {
                        addPartitions(0);
                    } else {
                        for (unsigned threadIndex = 0; threadIndex < partitionThreads; ++threadIndex) {
                            dedupThreads.emplace_back(addPartitions, threadIndex);
                        }
                        for (auto &thread : dedupThreads) {
                            thread.join();
                        }
                    }
                }
            }

            // Every source in this partition has now contributed all legal
            // outgoing edges to the next exact set.  The old layer is never
            // read again, so release it before constructing more partitions.
            std::vector<ExactSearchState>().swap(sourcePartition);
        }

        if (!finalLayer && !result.reachable) {
            StatePartitions nextFrontier;
            std::size_t nextSize = 0;
            for (std::size_t partitionIndex = 0;
                 partitionIndex < kStatePartitionCount;
                 ++partitionIndex) {
                nextFrontier[partitionIndex] = nextAccumulators[partitionIndex].Finish();
                nextSize += nextFrontier[partitionIndex].size();
            }
            frontier = std::move(nextFrontier);
            result.frontierSizes.push_back(nextSize);
            if (nextSize == 0) {
                break;
            }
        }
    }

    const auto finishedAt = std::chrono::steady_clock::now();
    result.elapsedMilliseconds = std::chrono::duration<double, std::milli>(finishedAt - startedAt).count();
    return result;
}