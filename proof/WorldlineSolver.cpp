#include "WorldlineSolver.h"

#include <algorithm>
#include <chrono>
#include <iostream>


#include <limits>
#include <map>
#include <memory>
#include <set>
#include <tuple>

namespace d20proof {
    namespace {
        using Clock = std::chrono::steady_clock;
        constexpr int kInfiniteDistance = std::numeric_limits<int>::max() / 4;

        ProofBudget effectiveProofBudget(
            const ProofBudget &requested,
            Clock::time_point start) {
            ProofBudget result = requested;
            auto clamp = [](auto &value, auto maximum) {
                value = std::min(value, static_cast<decltype(value)>(maximum));
            };

            if (requested.totalTimeMs <= 300) {
                clamp(result.maxLeavesPerPosition, 2u);
                clamp(result.maxDetailedTermsPerAction, 2u);
                clamp(result.maxCompletionTermsPerAction, 2u);
                clamp(result.maxPriceEvaluations, 2u);
                clamp(result.maxWork, 500'000ull);
                clamp(result.maxBytes, 16ull * 1024ull * 1024ull);
                clamp(result.maxModelTerms, 65'000u);
            } else if (requested.totalTimeMs <= 2000) {
                clamp(result.maxLeavesPerPosition, 4u);
                clamp(result.maxDetailedTermsPerAction, 4u);
                clamp(result.maxCompletionTermsPerAction, 3u);
                clamp(result.maxPriceEvaluations, 4u);
                clamp(result.maxWork, 2'000'000ull);
                clamp(result.maxBytes, 64ull * 1024ull * 1024ull);
                clamp(result.maxModelTerms, 150'000u);
            } else {
                clamp(result.maxLeavesPerPosition, 37u);
                clamp(result.maxDetailedTermsPerAction, 20u);
                clamp(result.maxCompletionTermsPerAction, 3u);
                clamp(result.maxPriceEvaluations, 8u);
                // The specification labels 8M as the PoC's initial tuning
                // cutoff, not a semantic proof bound.  Measured production
                // runs exhaust it in well under one second while the shared
                // 15s deadline remains the actual wall-clock limit.  Keep all
                // structural/certificate caps unchanged and allow enough V for
                // several checked repair generations under that same deadline.
                clamp(result.maxWork, 1'500'000'000ull);
                clamp(result.maxBytes, 192ull * 1024ull * 1024ull);
                clamp(result.maxModelTerms, 250'000u);
            }
            result.hasDeadline = true;
            result.deadline = start + std::chrono::milliseconds(result.totalTimeMs);
            return result;
        }

        void stampElapsed(BudgetReport &report, Clock::time_point start) {
            report.elapsedMs = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
        }

        void stampFirstWin(BudgetReport &report, Clock::time_point start) {
            if (report.firstWinMs >= 0) {
                return;
            }
            stampElapsed(report, start);
            report.firstWinMs = report.elapsedMs;
        }

        bool isBudgetFailure(const std::string &reason) {
            return reason.find("budget") != std::string::npos ||
                   reason.find("proof work") != std::string::npos ||
                   reason.find("proof bytes") != std::string::npos ||
                   reason.find("limit exceeded") != std::string::npos ||
                   reason.find("limit reached") != std::string::npos ||
                   reason.find("total_time") != std::string::npos ||
                   reason.find("deadline") != std::string::npos;
        }

        bool chargeSearchBudget(
            BudgetReport &report,
            const ProofBudget &limits,
            std::uint64_t work,
            std::uint64_t bytes) {
            if (limits.hasDeadline && Clock::now() >= limits.deadline) {
                const auto start = limits.deadline - std::chrono::milliseconds(limits.totalTimeMs);
                stampElapsed(report, start);
                return false;
            }
            if (work > limits.maxWork - std::min(report.work, limits.maxWork)) {
                return false;
            }
            if (bytes > limits.maxBytes - std::min(report.bytes, limits.maxBytes)) {
                return false;
            }
            report.work += work;
            report.bytes += bytes;
            return true;
        }

        void releaseSearchBytes(BudgetReport &report, std::uint64_t bytes) {
            report.bytes = bytes >= report.bytes ? 0 : report.bytes - bytes;
        }

        void releaseReplayBytes(BudgetReport &report, ReplayResult &replay) {
            releaseSearchBytes(report, replay.accountedBytes);
            replay.accountedBytes = 0;
        }

        void releasePrefixReceiptBytes(BudgetReport &report, PrefixReceipt &receipt) {
            releaseReplayBytes(report, receipt.replay);
            releaseSearchBytes(report, receipt.accountedBytes);
            receipt.accountedBytes = 0;
        }

        std::uint64_t accountedProblemDynamicBytes(const Problem &problem) noexcept {
            std::uint64_t bytes = problem.ruleId.registryNamespace.size();
            auto add = [&](std::uint64_t amount) {
                if (amount > std::numeric_limits<std::uint64_t>::max() - bytes) {
                    bytes = std::numeric_limits<std::uint64_t>::max();
                } else {
                    bytes += amount;
                }
            };
            add(problem.constraints.actions.size() * sizeof(ActionConstraint));
            for (const ActionConstraint &constraint: problem.constraints.actions) {
                add(constraint.allowedCommands.size() * sizeof(int));
            }
            add(problem.constraints.observations.size() * sizeof(BoundaryObservation));
            return bytes;
        }

        std::uint64_t accountedPrefixReceiptBytes(const PrefixReceipt &receipt) noexcept {
            std::uint64_t bytes = sizeof(PrefixReceipt);
            auto add = [&](std::uint64_t amount) {
                if (amount > std::numeric_limits<std::uint64_t>::max() - bytes) {
                    bytes = std::numeric_limits<std::uint64_t>::max();
                } else {
                    bytes += amount;
                }
            };
            add(receipt.reason.size());
            add(accountedProblemDynamicBytes(receipt.initialProblem));
            add(receipt.prefix.size() * sizeof(int));
            // receipt.replay's dynamically retained turn/command records are
            // already charged by ExactReplay::chargeReplayBudget.
            return bytes;
        }

        std::uint64_t accountedPartitionFamilyBytes(const PartitionFamily &family) {
            std::uint64_t bytes = 0;
            auto add = [&](std::uint64_t amount) {
                if (amount > std::numeric_limits<std::uint64_t>::max() - bytes) {
                    bytes = std::numeric_limits<std::uint64_t>::max();
                } else {
                    bytes += amount;
                }
            };
            add(family.trees.size() * sizeof(PredicatePartition));
            for (const PredicatePartition &tree: family.trees) {
                add(tree.nodes.size() * sizeof(PartitionNode));
            }
            return bytes;
        }

        std::uint64_t accountedSymbolicFrameBytes(const SymbolicFrame &frame) {
            std::uint64_t bytes = sizeof(SymbolicFrame);
            auto add = [&](std::uint64_t amount) {
                if (amount > std::numeric_limits<std::uint64_t>::max() - bytes) {
                    bytes = std::numeric_limits<std::uint64_t>::max();
                } else {
                    bytes += amount;
                }
            };
            add(frame.routineId.size());
            add(frame.callStack.size() * sizeof(ReturnAddress));
            for (const ReturnAddress &address: frame.callStack) {
                add(address.routineId.size());
            }
            return bytes;
        }

        std::uint64_t accountedDetailedProofNodeBytes(const DetailedProofNode &node) {
            std::uint64_t bytes = sizeof(DetailedProofNode);
            auto add = [&](std::uint64_t amount) {
                if (amount > std::numeric_limits<std::uint64_t>::max() - bytes) {
                    bytes = std::numeric_limits<std::uint64_t>::max();
                } else {
                    bytes += amount;
                }
            };
            const std::uint64_t frameBytes = accountedSymbolicFrameBytes(node.claimedFrame);
            if (frameBytes >= sizeof(SymbolicFrame)) {
                add(frameBytes - sizeof(SymbolicFrame));
            }
            add(node.expectedPc.routineId.size());
            add(node.children.size() * sizeof(std::uint32_t));
            return bytes;
        }

        std::uint64_t accountedRootProofRecordBytes(const RootProofRecord &proof) {
            std::uint64_t bytes = sizeof(RootProofRecord);
            auto add = [&](std::uint64_t amount) {
                if (amount > std::numeric_limits<std::uint64_t>::max() - bytes) {
                    bytes = std::numeric_limits<std::uint64_t>::max();
                } else {
                    bytes += amount;
                }
            };
            add(proof.expectedPc.routineId.size());
            add(proof.completionCases.size() * sizeof(CompletionProofCase));
            for (const DetailedProofNode &node: proof.detailedNodes) {
                add(accountedDetailedProofNodeBytes(node));
            }
            return bytes;
        }

        std::uint64_t accountedCheckedRootRecordBytes(const CheckedRootRecord &record) {
            std::uint64_t bytes = sizeof(CheckedRootRecord);
            if (record.verifiedPc.routineId.size() > std::numeric_limits<std::uint64_t>::max() - bytes) {
                return std::numeric_limits<std::uint64_t>::max();
            }
            return bytes + record.verifiedPc.routineId.size();
        }

        std::uint64_t accountedCompletionCheckpointBytes(const CompletionCheckpoint &checkpoint) {
            std::uint64_t bytes = sizeof(CompletionCheckpoint);
            const std::uint64_t frameBytes = accountedSymbolicFrameBytes(checkpoint.frame);
            if (frameBytes < sizeof(SymbolicFrame)) {
                return std::numeric_limits<std::uint64_t>::max();
            }
            const std::uint64_t dynamicFrameBytes = frameBytes - sizeof(SymbolicFrame);
            if (dynamicFrameBytes > std::numeric_limits<std::uint64_t>::max() - bytes) {
                return std::numeric_limits<std::uint64_t>::max();
            }
            return bytes + dynamicFrameBytes;
        }

        std::uint64_t accountedSnapshotBytes(const CheckedSnapshot &snapshot) {
            std::uint64_t bytes = accountedPartitionFamilyBytes(snapshot.partitions);
            auto add = [&](std::uint64_t amount) {
                if (amount > std::numeric_limits<std::uint64_t>::max() - bytes) {
                    bytes = std::numeric_limits<std::uint64_t>::max();
                } else {
                    bytes += amount;
                }
            };
            for (const RootProofRecord &proof: snapshot.proofs) {
                add(accountedRootProofRecordBytes(proof));
            }
            for (const CheckedRootRecord &record: snapshot.coverage) {
                add(accountedCheckedRootRecordBytes(record));
            }
            for (const CompletionCheckpoint &checkpoint: snapshot.completionCheckpoints) {
                add(accountedCompletionCheckpointBytes(checkpoint));
            }
            for (const auto &layer: snapshot.edgesByElapsedTurn) {
                add(layer.size() * sizeof(CheckedEdge));
                for (const CheckedEdge &edge: layer) {
                    if (edge.continuingOutput) {
                        add(sizeof(Box));
                    }
                    add(edge.weightTerms.size() * sizeof(CompletionWeightTerm));
                    add(edge.completionMembers.size() * sizeof(CompletionModelMember));
                    add(edge.targets.size() * sizeof(CellKey));
                }
            }
            for (const auto &layer: snapshot.support) {
                add(layer.size() * sizeof(CellKey));
            }
            return bytes;
        }

        void releaseSearchSnapshotCertificateRecords(BudgetReport &report, CheckedSnapshot &snapshot) {
            std::uint64_t releasedBytes = 0;
            auto addReleasedBytes = [&](std::uint64_t bytes) {
                if (bytes > std::numeric_limits<std::uint64_t>::max() - releasedBytes) {
                    releasedBytes = std::numeric_limits<std::uint64_t>::max();
                } else {
                    releasedBytes += bytes;
                }
            };
            for (const RootProofRecord &proof: snapshot.proofs) {
                addReleasedBytes(accountedRootProofRecordBytes(proof));
            }
            for (const CheckedRootRecord &record: snapshot.coverage) {
                addReleasedBytes(accountedCheckedRootRecordBytes(record));
            }
            releaseSearchBytes(report, releasedBytes);
            std::vector<RootProofRecord>().swap(snapshot.proofs);
            std::vector<CheckedRootRecord>().swap(snapshot.coverage);
        }

        void absorbDiscardedTrial(
            BudgetReport &live,
            const BudgetReport &trial,
            std::uint64_t retainedBytes) {
            const std::uint64_t supportCells = live.supportCells;
            const std::uint64_t detailedEdges = live.detailedEdges;
            const std::uint64_t completionEdges = live.completionEdges;
            const std::uint64_t proofRoots = live.proofRoots;
            const std::uint64_t completionCases = live.completionCases;
            live = trial;
            live.bytes = retainedBytes;
            live.supportCells = supportCells;
            live.detailedEdges = detailedEdges;
            live.completionEdges = completionEdges;
            live.proofRoots = proofRoots;
            live.completionCases = completionCases;
        }

        bool deadlineReached(Clock::time_point start, const ProofBudget &limits, BudgetReport &report) {
            stampElapsed(report, start);
            return report.elapsedMs >= limits.totalTimeMs;
        }

        std::optional<std::size_t> cellIndex(const std::vector<CellKey> &cells, const CellKey &key) {
            const auto found = std::lower_bound(cells.begin(), cells.end(), key);
            if (found == cells.end() || !(*found == key)) {
                return std::nullopt;
            }
            return static_cast<std::size_t>(found - cells.begin());
        }

        bool edgeContainsCandidateTarget(const CheckedEdge &edge, const CellKey &target) {
            if (!edge.hasContinuingOutput) {
                return false;
            }
            if (edge.kind == CheckedEdgeKind::Completion) {
                return target.rngPosition >= edge.firstOutputPosition &&
                       target.rngPosition <= edge.lastOutputPosition;
            }
            return std::binary_search(edge.targets.begin(), edge.targets.end(), target);
        }

        struct GoalDistances {
            bool accepted = false;
            std::string reason;
            std::vector<std::vector<int>> values;
            std::vector<std::vector<std::uint8_t>> finiteEdges;
            std::uint64_t chargedBytes = 0;
        };

        GoalDistances buildGoalDistances(
            const CheckedSnapshot &snapshot,
            int horizon,
            const ProofBudget &limits,
            BudgetReport &report) {
            GoalDistances result;
            if (!snapshot.check.accepted ||
                snapshot.support.size() != static_cast<std::size_t>(horizon) + 1 ||
                snapshot.edgesByElapsedTurn.size() != static_cast<std::size_t>(horizon)) {
                result.reason = "goal-distance input snapshot has inconsistent layers";
                return result;
            }

            struct TemporarySearchBytes {
                BudgetReport &report;
                std::uint64_t bytes = 0;
                ~TemporarySearchBytes() {
                    releaseSearchBytes(report, bytes);
                }
            } partitionIndexBytes{report};
            const int firstHotelPosition = snapshot.root.rngPosition;
            const std::size_t hotelCount = snapshot.partitions.trees.size();
            const std::uint64_t hotelIndexStorage =
                hotelCount * (sizeof(const PredicatePartition *) + sizeof(std::size_t));
            if (!chargeSearchBudget(
                    report,
                    limits,
                    hotelCount,
                    hotelIndexStorage)) {
                result.reason = "goal-distance hotel partition index exceeded proof budget";
                return result;
            }
            partitionIndexBytes.bytes += hotelIndexStorage;
            std::vector<const PredicatePartition *> partitionByHotel(hotelCount, nullptr);
            std::vector<std::size_t> partitionLeafCountByHotel(hotelCount, 0);
            std::uint64_t partitionLeafIndexWork = 0;
            for (const PredicatePartition &partition: snapshot.partitions.trees) {
                if (partition.rngPosition < firstHotelPosition) {
                    result.reason = "goal-distance partition precedes the first hotel";
                    return result;
                }
                const std::uint64_t offset = static_cast<std::uint64_t>(
                    partition.rngPosition - firstHotelPosition);
                if (offset >= partitionByHotel.size() || partitionByHotel[offset] != nullptr) {
                    result.reason = "goal-distance partition family is not a unique contiguous hotel index";
                    return result;
                }
                partitionByHotel[static_cast<std::size_t>(offset)] = &partition;
                std::size_t leafCount = 0;
                for (const PartitionNode &node: partition.nodes) {
                    ++partitionLeafIndexWork;
                    if (node.leaf) {
                        ++leafCount;
                    }
                }
                partitionLeafCountByHotel[static_cast<std::size_t>(offset)] = leafCount;
            }
            if (partitionLeafIndexWork != 0 &&
                !chargeSearchBudget(report, limits, partitionLeafIndexWork, 0)) {
                result.reason = "goal-distance partition leaf-count index exceeded proof budget";
                return result;
            }
            if (std::any_of(
                    partitionByHotel.begin(),
                    partitionByHotel.end(),
                    [](const PredicatePartition *partition) { return partition == nullptr; })) {
                result.reason = "goal-distance partition family has a missing hotel";
                return result;
            }

            result.values.resize(static_cast<std::size_t>(horizon) + 1);
            result.finiteEdges.resize(static_cast<std::size_t>(horizon));
            result.values[horizon].assign(snapshot.support[horizon].size(), kInfiniteDistance);
            if (!chargeSearchBudget(
                    report,
                    limits,
                    result.values[horizon].size(),
                    result.values[horizon].size() * sizeof(int))) {
                result.reason = "goal-distance terminal layer exceeded proof budget";
                return result;
            }
            result.chargedBytes += result.values[horizon].size() * sizeof(int);

            for (int elapsedTurn = horizon - 1; elapsedTurn >= 0; --elapsedTurn) {
                const std::vector<CellKey> &sources = snapshot.support[elapsedTurn];
                const std::vector<CellKey> &next = snapshot.support[elapsedTurn + 1];
                const std::vector<int> &nextDistances = result.values[elapsedTurn + 1];
                if (nextDistances.size() != next.size()) {
                    result.reason = "goal-distance next layer does not match Support";
                    return result;
                }

                struct PositionMinimum {
                    int rngPosition = 0;
                    int distance = kInfiniteDistance;
                    bool complete = false;
                };
                TemporarySearchBytes temporaryBytes{report};

                // COMPLETE destinations contain every checked CellKey for a
                // contiguous RNG-position interval.  Collapse each position
                // to its exact minimum once, then answer each completion edge
                // with a range minimum instead of rescanning the same layer.
                const std::uint64_t positionBytes = next.size() * sizeof(PositionMinimum);
                if (positionBytes != 0 &&
                    !chargeSearchBudget(report, limits, 0, positionBytes)) {
                    result.reason = "goal-distance per-position table exceeded proof budget";
                    return result;
                }
                temporaryBytes.bytes += positionBytes;
                std::vector<PositionMinimum> positionMinimums;
                positionMinimums.reserve(next.size());
                for (std::size_t begin = 0; begin < next.size();) {
                    const int position = next[begin].rngPosition;
                    std::size_t end = begin + 1;
                    int minimum = nextDistances[begin];
                    while (end < next.size() && next[end].rngPosition == position) {
                        minimum = std::min(minimum, nextDistances[end]);
                        ++end;
                    }
                    const PredicatePartition *partition = nullptr;
                    std::size_t leafCount = 0;
                    if (position >= firstHotelPosition) {
                        const std::uint64_t offset = static_cast<std::uint64_t>(
                            position - firstHotelPosition);
                        if (offset < partitionByHotel.size()) {
                            partition = partitionByHotel[static_cast<std::size_t>(offset)];
                            leafCount = partitionLeafCountByHotel[static_cast<std::size_t>(offset)];
                        }
                    }
                    if (partition == nullptr) {
                        result.reason = "goal-distance per-position minimum references a missing partition";
                        return result;
                    }
                    if (!chargeSearchBudget(report, limits, end - begin, 0)) {
                        result.reason = "goal-distance per-position minimum construction exceeded proof budget";
                        return result;
                    }
                    positionMinimums.push_back({position, minimum, leafCount == end - begin});
                    begin = end;
                }

                const std::size_t positionSpan = positionMinimums.empty()
                    ? 0
                    : static_cast<std::size_t>(
                        positionMinimums.back().rngPosition - positionMinimums.front().rngPosition + 1);
                const std::uint64_t rangeIndexBytes =
                    positionSpan * sizeof(int) +
                    positionSpan * sizeof(std::uint8_t) +
                    (positionSpan + 1) * sizeof(std::uint32_t) +
                    (positionSpan + 1) * sizeof(int) +
                    positionSpan * sizeof(std::size_t);
                if (rangeIndexBytes != 0 &&
                    !chargeSearchBudget(report, limits, 0, rangeIndexBytes)) {
                    result.reason = "goal-distance sliding range-minimum index exceeded proof budget";
                    return result;
                }
                temporaryBytes.bytes += rangeIndexBytes;
                std::vector<int> densePositionMinimums(positionSpan, kInfiniteDistance);
                std::vector<std::uint8_t> completePosition(positionSpan, 0);
                std::vector<std::uint32_t> incompletePrefix(positionSpan + 1, 0);
                std::vector<int> windowTableIndexByLength(positionSpan + 1, -1);
                std::vector<std::size_t> monotoneQueue(positionSpan, 0);
                std::uint64_t rangeBuildWork = 0;
                if (!positionMinimums.empty()) {
                    const int basePosition = positionMinimums.front().rngPosition;
                    for (const PositionMinimum &entry: positionMinimums) {
                        const std::size_t offset = static_cast<std::size_t>(
                            entry.rngPosition - basePosition);
                        if (offset >= positionSpan || completePosition[offset] != 0) {
                            result.reason = "goal-distance per-position minimum index is not unique";
                            return result;
                        }
                        densePositionMinimums[offset] = entry.distance;
                        completePosition[offset] = entry.complete ? 1u : 0u;
                        ++rangeBuildWork;
                    }
                    for (std::size_t offset = 0; offset < positionSpan; ++offset) {
                        incompletePrefix[offset + 1] =
                            incompletePrefix[offset] + (completePosition[offset] == 0 ? 1u : 0u);
                        ++rangeBuildWork;
                    }
                }
                if (rangeBuildWork != 0 &&
                    !chargeSearchBudget(report, limits, rangeBuildWork, 0)) {
                    result.reason = "goal-distance sliding range-minimum index construction exceeded proof budget";
                    return result;
                }

                struct WindowMinimumTable {
                    std::size_t length = 0;
                    std::vector<int> minimumByStart;
                };
                std::vector<WindowMinimumTable> windowMinimumTables;

                auto completionRangeMinimum = [&](int firstPosition,
                                                  int lastPosition,
                                                  int &minimum) -> bool {
                    minimum = kInfiniteDistance;
                    if (firstPosition > lastPosition || positionMinimums.empty()) {
                        result.reason = "goal-distance COMPLETE range has no next-layer Support target";
                        return false;
                    }
                    const int basePosition = positionMinimums.front().rngPosition;
                    if (firstPosition < basePosition || lastPosition < basePosition ||
                        static_cast<std::uint64_t>(lastPosition - basePosition) >= positionSpan) {
                        result.reason = "goal-distance COMPLETE range is missing a next-layer RNG position";
                        return false;
                    }
                    const std::size_t left = static_cast<std::size_t>(firstPosition - basePosition);
                    const std::size_t right = static_cast<std::size_t>(lastPosition - basePosition);
                    const std::size_t count = static_cast<std::size_t>(lastPosition - firstPosition + 1);
                    if (left >= positionSpan || right >= positionSpan || right < left ||
                        right - left + 1 != count || count == 0 || count > positionSpan) {
                        result.reason = "goal-distance COMPLETE range is missing a next-layer RNG position";
                        return false;
                    }
                    if (incompletePrefix[right + 1] != incompletePrefix[left]) {
                        result.reason = "goal-distance COMPLETE range does not contain every partition leaf";
                        return false;
                    }
                    int tableIndex = windowTableIndexByLength[count];
                    if (tableIndex < 0) {
                        const std::size_t startCount = positionSpan - count + 1;
                        const std::uint64_t tableBytes =
                            sizeof(WindowMinimumTable) + startCount * sizeof(int);
                        if (!chargeSearchBudget(report, limits, 0, tableBytes)) {
                            result.reason = "goal-distance sliding range-minimum table exceeded proof budget";
                            return false;
                        }
                        temporaryBytes.bytes += tableBytes;
                        WindowMinimumTable table;
                        table.length = count;
                        table.minimumByStart.assign(startCount, kInfiniteDistance);

                        std::size_t head = 0;
                        std::size_t tail = 0;
                        std::uint64_t windowWork = 0;
                        for (std::size_t index = 0; index < positionSpan; ++index) {
                            while (head < tail && monotoneQueue[head] + count <= index) {
                                ++head;
                                ++windowWork;
                            }
                            while (head < tail) {
                                ++windowWork;
                                const std::size_t back = monotoneQueue[tail - 1];
                                if (densePositionMinimums[back] < densePositionMinimums[index]) {
                                    break;
                                }
                                --tail;
                            }
                            monotoneQueue[tail++] = index;
                            ++windowWork;
                            if (index + 1 >= count) {
                                table.minimumByStart[index + 1 - count] =
                                    densePositionMinimums[monotoneQueue[head]];
                                ++windowWork;
                            }
                        }
                        if (!chargeSearchBudget(report, limits, windowWork, 0)) {
                            result.reason = "goal-distance sliding range-minimum construction exceeded proof budget";
                            return false;
                        }
                        windowMinimumTables.push_back(std::move(table));
                        tableIndex = static_cast<int>(windowMinimumTables.size() - 1);
                        windowTableIndexByLength[count] = tableIndex;
                    }
                    const WindowMinimumTable &table =
                        windowMinimumTables[static_cast<std::size_t>(tableIndex)];
                    if (table.length != count || left >= table.minimumByStart.size()) {
                        result.reason = "goal-distance sliding range-minimum table lookup is inconsistent";
                        return false;
                    }
                    minimum = table.minimumByStart[left];
                    if (!chargeSearchBudget(report, limits, 1, 0)) {
                        result.reason = "goal-distance COMPLETE range query exceeded proof budget";
                        return false;
                    }
                    return true;
                };
                std::vector<int> current(sources.size(), kInfiniteDistance);
                const std::vector<CheckedEdge> &layerEdges =
                    snapshot.edgesByElapsedTurn[elapsedTurn];
                std::vector<std::uint8_t> &finiteEdges = result.finiteEdges[elapsedTurn];
                const std::uint64_t finiteEdgeBytes =
                    layerEdges.size() * sizeof(std::uint8_t);
                if (finiteEdgeBytes != 0 &&
                    !chargeSearchBudget(report, limits, 0, finiteEdgeBytes)) {
                    result.reason = "goal-distance finite-edge index exceeded proof budget";
                    return result;
                }
                result.chargedBytes += finiteEdgeBytes;
                finiteEdges.assign(layerEdges.size(), 0);

                std::size_t edgeLo = 0;
                for (std::size_t sourceIndex = 0; sourceIndex < sources.size(); ++sourceIndex) {
                    int best = kInfiniteDistance;
                    const CellKey &source = sources[sourceIndex];
                    while (edgeLo < layerEdges.size() && layerEdges[edgeLo].source < source) {
                        ++edgeLo;
                    }
                    for (std::size_t edgeIndex = edgeLo;
                         edgeIndex < layerEdges.size() && layerEdges[edgeIndex].source == source;
                         ++edgeIndex) {
                        const CheckedEdge &edge = layerEdges[edgeIndex];
                        bool finiteChoice = false;
                        if (edge.mayReachGoal) {
                            best = 1;
                            finiteChoice = true;
                        }
                        if (edge.hasContinuingOutput && edge.kind == CheckedEdgeKind::Completion) {
                            if (!edge.targets.empty() ||
                                edge.firstOutputPosition > edge.lastOutputPosition) {
                                result.reason = "goal-distance saw an invalid COMPLETE virtual target range";
                                return result;
                            }
                            int destinationDistance = kInfiniteDistance;
                            if (!completionRangeMinimum(
                                    edge.firstOutputPosition,
                                    edge.lastOutputPosition,
                                    destinationDistance)) {
                                return result;
                            }
                            if (destinationDistance < kInfiniteDistance - 1) {
                                best = std::min(best, destinationDistance + 1);
                                finiteChoice = true;
                            }
                        } else {
                            for (const CellKey &target: edge.targets) {
                                const std::optional<std::size_t> targetIndex = cellIndex(next, target);
                                if (!targetIndex || *targetIndex >= nextDistances.size()) {
                                    result.reason = "goal-distance edge target is missing from Support";
                                    return result;
                                }
                                const int destinationDistance = nextDistances[*targetIndex];
                                if (destinationDistance < kInfiniteDistance - 1) {
                                    best = std::min(best, destinationDistance + 1);
                                    finiteChoice = true;
                                }
                                if (!chargeSearchBudget(report, limits, 1, 0)) {
                                    result.reason = "goal-distance target scan exceeded proof budget";
                                    return result;
                                }
                            }
                        }
                        finiteEdges[edgeIndex] = finiteChoice ? 1 : 0;
                    }
                    current[sourceIndex] = best;
                }

                if (!chargeSearchBudget(
                        report,
                        limits,
                        current.size(),
                        current.size() * sizeof(int))) {
                    result.reason = "goal-distance layer exceeded proof budget";
                    return result;
                }
                result.chargedBytes += current.size() * sizeof(int);
                result.values[elapsedTurn] = std::move(current);
            }

            result.accepted = true;
            return result;
        }

        int commandOrder(const RuleBundle &bundle, int command) {
            const auto found = std::find(bundle.profile.heroCommands.begin(), bundle.profile.heroCommands.end(), command);
            if (found == bundle.profile.heroCommands.end()) {
                return std::numeric_limits<int>::max();
            }
            return static_cast<int>(found - bundle.profile.heroCommands.begin());
        }

        struct CandidateChoice {
            int remainingDistance = kInfiniteDistance;
            bool completion = true;
            int selectedCommand = 0;
            int commandOrder = 0;
            std::size_t modelTermNumber = 0;
            bool goal = false;
            CellKey target;
            std::size_t targetSupportIndex = 0;
            std::size_t commandSequenceId = 0;
        };

        bool candidateChoiceLess(const CandidateChoice &a, const CandidateChoice &b) {
            const auto destinationKey = [](const CandidateChoice &choice) {
                if (choice.goal) {
                    return std::tuple<int, int, std::uint32_t>{0, 0, 0};
                }
                return std::tuple<int, int, std::uint32_t>{
                    1,
                    choice.target.rngPosition,
                    choice.target.localCellId};
            };
            return std::tuple{
                       a.remainingDistance,
                       a.completion ? 1 : 0,
                       a.commandOrder,
                       a.modelTermNumber,
                       destinationKey(a)} <
                   std::tuple{
                       b.remainingDistance,
                       b.completion ? 1 : 0,
                       b.commandOrder,
                       b.modelTermNumber,
                       destinationKey(b)};
        }

        enum class CandidateFailureKind {
            IllegalCommand,
            Lost,
            NotWon,
            AbstractMismatch,
        };

        struct CandidateStep {
            int elapsedTurn = 0;
            CellKey source;
            int selectedCommand = 0;
            std::size_t modelTermNumber = 0;
            CheckedEdgeKind edgeKind = CheckedEdgeKind::Completion;
            CompletionCutId completionCut = CompletionCutId::TurnEntry;
            bool goal = false;
            CellKey target;
        };

        struct CandidatePath {
            std::vector<int> commands;
            std::vector<CandidateStep> steps;
            std::size_t commandSequenceId = 0;
        };

        enum class IteratorStatus {
            Path,
            Exhausted,
            BudgetExceeded,
            ModelError,
        };

        struct OrderedDestination {
            int distance = kInfiniteDistance;
            CellKey key;
            std::size_t supportIndex = 0;
        };

        struct OrderedDestinationRun {
            int distance = kInfiniteDistance;
            int firstPosition = 0;
            int lastPosition = 0;
            std::size_t cellsPerPosition = 0;
            std::size_t beginIndex = 0;
        };

        class CommandSequenceTrie {
        public:
            CommandSequenceTrie(
                std::size_t commandCount,
                const ProofBudget &limits,
                BudgetReport &report)
                : commandCount_(commandCount), limits_(limits), report_(report) {
                if (commandCount_ == 0) {
                    error_ = "command-sequence trie requires a nonempty command alphabet";
                    return;
                }
                if (!appendNode()) {
                    if (error_.empty()) {
                        error_ = "command-sequence trie root exceeded proof budget";
                    }
                    return;
                }
                ready_ = true;
            }

            ~CommandSequenceTrie() {
                releaseSearchBytes(report_, ownedBytes_);
            }

            bool ready() const noexcept {
                return ready_;
            }

            const std::string &error() const noexcept {
                return error_;
            }

            std::size_t diagnosticNodeCount() const noexcept {
                return nodes_.size();
            }

            bool extend(
                std::size_t parentId,
                int commandSlot,
                std::size_t &childId) {
                if (parentId >= nodes_.size() || commandSlot < 0 ||
                    static_cast<std::size_t>(commandSlot) >= commandCount_) {
                    error_ = "command-sequence trie received an invalid parent or command slot";
                    return false;
                }
                const std::size_t slot = static_cast<std::size_t>(commandSlot);
                childId = nodes_[parentId].children[slot];
                if (childId != kNoNode) {
                    return true;
                }
                // The selected abstract choice has already been charged by the
                // candidate iterator.  Re-reading its dense (prefix, command)
                // child slot is bookkeeping for an exact FailedCommands id,
                // not a second abstract-path visit.  Only creation of a new
                // exact prefix record adds extra proof-search work.
                if (!chargeSearchBudget(report_, limits_, 1, 0)) {
                    error_ = "command-sequence trie growth exceeded proof budget";
                    return false;
                }
                const std::size_t stableParent = parentId;
                if (!appendNode()) {
                    if (error_.empty()) {
                        error_ = "command-sequence trie growth exceeded proof budget";
                    }
                    return false;
                }
                childId = nodes_.size() - 1;
                nodes_[stableParent].children[slot] = childId;
                return true;
            }

            bool failed(std::size_t sequenceId, bool &isFailed) {
                if (sequenceId >= nodes_.size()) {
                    error_ = "FailedCommands sequence id is outside the exact command trie";
                    return false;
                }
                // The iterator has already charged every selected abstract choice
                // used to derive this canonical exact-command trie id.  Looking up
                // the failure bit by that id performs no command comparison and is
                // not another region/path visit, so it must not consume V again.
                isFailed = nodes_[sequenceId].failed;
                return true;
            }

            bool rememberFailure(
                std::size_t sequenceId,
                const std::vector<int> &commands,
                CandidateFailureKind failure) {
                if (sequenceId >= nodes_.size()) {
                    error_ = "FailedCommands sequence id is outside the exact command trie";
                    return false;
                }
                Node &node = nodes_[sequenceId];
                if (node.failed) {
                    return true;
                }
                const std::uint64_t commandBytes = commands.size() * sizeof(int);
                if (!chargeSearchBudget(report_, limits_, 1, commandBytes)) {
                    error_ = "FailedCommands exact sequence record exceeded proof budget";
                    return false;
                }
                ownedBytes_ += commandBytes;
                node.failed = true;
                node.failure = failure;
                node.completeCommands = commands;
                return true;
            }

        private:
            static constexpr std::size_t kNoNode = std::numeric_limits<std::size_t>::max();
            struct Node {
                std::vector<std::size_t> children;
                bool failed = false;
                CandidateFailureKind failure = CandidateFailureKind::NotWon;
                std::vector<int> completeCommands;
            };


            bool appendNode() {
                const std::uint64_t bytes =
                    sizeof(Node) + commandCount_ * sizeof(std::size_t);
                if (!chargeSearchBudget(report_, limits_, 0, bytes)) {
                    return false;
                }
                ownedBytes_ += bytes;
                Node node;
                node.children.assign(commandCount_, kNoNode);
                nodes_.push_back(std::move(node));
                return true;
            }

            std::size_t commandCount_ = 0;
            const ProofBudget &limits_;
            BudgetReport &report_;
            std::vector<Node> nodes_;
            std::uint64_t ownedBytes_ = 0;
            std::string error_;
            bool ready_ = false;
        };

        class GoalCandidateIterator {
        public:
            GoalCandidateIterator(
                const RuleBundle &bundle,
                const CheckedSnapshot &snapshot,
                const GoalDistances &distances,
                CommandSequenceTrie &commandSequences,
                int horizon,
                const ProofBudget &limits,
                BudgetReport &report)
                : bundle_(bundle),
                  snapshot_(snapshot),
                  distances_(distances),
                  commandSequences_(commandSequences),
                  horizon_(horizon),
                  limits_(limits),
                  report_(report) {
                if (!buildDestinationOrders()) {
                    return;
                }
                if (!buildCandidateEdgeIndex()) {
                    return;
                }
                Frame root;
                root.elapsedTurn = 0;
                root.source = snapshot.root;
                root.sourceIndex = 0;
                if (snapshot_.support.empty() || snapshot_.support[0].size() != 1 ||
                    !(snapshot_.support[0][0] == snapshot_.root)) {
                    error_ = "candidate iterator root is not the unique dense Support room";
                    return;
                }
                root.commandSequenceId = 0;
                if (!buildFrame(root)) {
                    return;
                }
                stack_.push_back(std::move(root));
                ready_ = true;
            }

            ~GoalCandidateIterator() {
                releaseSearchBytes(report_, ownedBytes_);
            }

            void releaseStaticIndexesForRepair() {
                if (!staticIndexesReady_) {
                    return;
                }
                const std::uint64_t bytes = staticIndexBytes();
                releaseOwnedBytes(bytes);
                std::vector<std::vector<OrderedDestination>>().swap(orderedDestinations_);
                std::vector<std::vector<OrderedDestinationRun>>().swap(orderedDestinationRuns_);
                std::vector<std::vector<std::size_t>>().swap(orderedDestinationRunDistanceOffsets_);
                std::vector<std::vector<CandidateEdgeRef>>().swap(candidateEdgeRefs_);
                std::vector<std::vector<CandidateRoomEdgeRange>>().swap(candidateRoomEdgeRanges_);
                staticIndexesReady_ = false;
            }

            bool restoreStaticIndexesAfterRepairFailure(std::string &reason) {
                if (staticIndexesReady_) {
                    return true;
                }
                error_.clear();
                if (!buildDestinationOrders() || !buildCandidateEdgeIndex()) {
                    reason = error_;
                    return false;
                }
                staticIndexesReady_ = true;
                return true;
            }

            std::uint64_t diagnosticFrameBuilds() const noexcept {
                return diagnosticFrameBuilds_;
            }

            std::uint64_t diagnosticFrameCursorTerms() const noexcept {
                return diagnosticFrameCursorTerms_;
            }

            std::uint64_t diagnosticRangeLookupWork() const noexcept {
                return diagnosticRangeLookupWork_;
            }

            std::uint64_t diagnosticRangeScanWork() const noexcept {
                return diagnosticRangeScanWork_;
            }

            std::uint64_t diagnosticDuplicateGoalWork() const noexcept {
                return diagnosticDuplicateGoalWork_;
            }

            IteratorStatus next(CandidatePath &path, std::string &reason) {
                path = {};
                if (!ready_) {
                    reason = error_;
                    if (error_.empty()) {
                        return IteratorStatus::Exhausted;
                    }
                    return isBudgetFailure(error_) ? IteratorStatus::BudgetExceeded : IteratorStatus::ModelError;
                }

                while (!stack_.empty()) {
                    if (report_.candidateScans >= limits_.maxCandidateScans) {
                        reason = "candidate choice scan limit reached";
                        return IteratorStatus::BudgetExceeded;
                    }

                    Frame &frame = stack_.back();
                    CandidateChoice choice;
                    const ChoiceStatus choiceStatus = nextChoice(frame, choice);
                    if (choiceStatus == ChoiceStatus::BudgetExceeded) {
                        reason = error_;
                        return IteratorStatus::BudgetExceeded;
                    }
                    if (choiceStatus == ChoiceStatus::ModelError) {
                        reason = error_;
                        return IteratorStatus::ModelError;
                    }
                    if (choiceStatus == ChoiceStatus::Exhausted) {
                        const bool hadIncomingStep = stack_.size() > 1;
                        const std::uint64_t frameBytes =
                            frame.edgeCursors.size() * sizeof(EdgeCursor);
                        stack_.pop_back();
                        releaseOwnedBytes(frameBytes);
                        if (hadIncomingStep) {
                            commandPath_.pop_back();
                            stepPath_.pop_back();
                        }
                        continue;
                    }

                    CandidateStep step;
                    step.elapsedTurn = frame.elapsedTurn;
                    step.source = frame.source;
                    step.selectedCommand = choice.selectedCommand;
                    step.modelTermNumber = choice.modelTermNumber;
                    const CheckedEdge &selectedEdge =
                        snapshot_.edgesByElapsedTurn[frame.elapsedTurn][choice.modelTermNumber];
                    step.edgeKind = selectedEdge.kind;
                    step.completionCut = selectedEdge.completionCut;
                    step.goal = choice.goal;
                    step.target = choice.target;
                    commandPath_.push_back(choice.selectedCommand);
                    stepPath_.push_back(step);
                    const std::size_t childCommandSequenceId = choice.commandSequenceId;

                    if (choice.goal) {
                        path.commands = commandPath_;
                        path.steps = stepPath_;
                        path.commandSequenceId = childCommandSequenceId;
                        commandPath_.pop_back();
                        stepPath_.pop_back();
                        return IteratorStatus::Path;
                    }

                    if (frame.elapsedTurn + 1 >= horizon_) {
                        commandPath_.pop_back();
                        stepPath_.pop_back();
                        continue;
                    }

                    Frame child;
                    child.elapsedTurn = frame.elapsedTurn + 1;
                    child.source = choice.target;
                    child.sourceIndex = choice.targetSupportIndex;
                    child.commandSequenceId = childCommandSequenceId;
                    if (!buildFrame(child)) {
                        reason = error_;
                        commandPath_.pop_back();
                        stepPath_.pop_back();
                        return isBudgetFailure(error_)
                            ? IteratorStatus::BudgetExceeded
                            : IteratorStatus::ModelError;
                    }
                    stack_.push_back(std::move(child));
                }

                return IteratorStatus::Exhausted;
            }

        private:
            enum class ChoiceStatus {
                Choice,
                Exhausted,
                BudgetExceeded,
                ModelError,
            };

            struct EdgeCursor {
                std::size_t destinationCursor = 0;
                std::optional<CandidateChoice> targetHead;
                std::optional<std::size_t> childCommandSequenceId;
                int completionDistance = 1;
                std::size_t completionRunCursor = 0;
                std::size_t completionRunEnd = 0;
                bool completionRunActive = false;
                bool completionDistanceInitialized = false;
                bool goalConsumed = false;
                bool exhausted = false;
            };

            struct Frame {
                int elapsedTurn = 0;
                CellKey source;
                std::size_t sourceIndex = 0;
                std::size_t commandSequenceId = 0;
                std::vector<EdgeCursor> edgeCursors;
            };

            struct CandidateEdgeRef {
                std::size_t edgeIndex = 0;
                bool mayReachGoal = false;
            };

            struct CandidateRoomEdgeRange {
                std::size_t begin = 0;
                std::size_t end = 0;
            };

            std::uint64_t staticIndexBytes() const noexcept {
                std::uint64_t bytes = 0;
                auto add = [&](std::uint64_t amount) {
                    if (amount > std::numeric_limits<std::uint64_t>::max() - bytes) {
                        bytes = std::numeric_limits<std::uint64_t>::max();
                    } else {
                        bytes += amount;
                    }
                };
                for (const auto &values: orderedDestinations_) {
                    add(values.size() * sizeof(OrderedDestination));
                }
                for (const auto &values: orderedDestinationRuns_) {
                    add(values.size() * sizeof(OrderedDestinationRun));
                }
                for (const auto &values: orderedDestinationRunDistanceOffsets_) {
                    add(values.size() * sizeof(std::size_t));
                }
                for (const auto &values: candidateEdgeRefs_) {
                    add(values.size() * sizeof(CandidateEdgeRef));
                }
                for (const auto &values: candidateRoomEdgeRanges_) {
                    add(values.size() * sizeof(CandidateRoomEdgeRange));
                }
                return bytes;
            }

            bool buildDestinationOrders() {
                orderedDestinations_.resize(static_cast<std::size_t>(horizon_) + 1);
                orderedDestinationRuns_.resize(static_cast<std::size_t>(horizon_) + 1);
                orderedDestinationRunDistanceOffsets_.resize(static_cast<std::size_t>(horizon_) + 1);
                for (int elapsedTurn = 1; elapsedTurn <= horizon_; ++elapsedTurn) {
                    const std::vector<CellKey> &support = snapshot_.support[elapsedTurn];
                    const std::vector<int> &layerDistances = distances_.values[elapsedTurn];
                    if (support.size() != layerDistances.size()) {
                        error_ = "candidate destination index does not match goal-distance layer";
                        return false;
                    }

                    std::vector<OrderedDestination> &ordered = orderedDestinations_[elapsedTurn];
                    const std::size_t bucketCount = static_cast<std::size_t>(horizon_) + 1;
                    const std::uint64_t bucketBytes =
                        bucketCount * 2 * sizeof(std::size_t);
                    if (!chargeSearchBudget(report_, limits_, 0, bucketBytes)) {
                        error_ = "candidate destination bucket index exceeded proof budget";
                        return false;
                    }
                    std::vector<std::size_t> bucketOffsets(bucketCount, 0);
                    std::vector<std::size_t> bucketCursors(bucketCount, 0);
                    std::size_t finiteCount = 0;
                    for (std::size_t index = 0; index < support.size(); ++index) {
                        const int distance = layerDistances[index];
                        if (distance >= kInfiniteDistance) {
                            continue;
                        }
                        if (distance <= 0 || distance > horizon_) {
                            releaseSearchBytes(report_, bucketBytes);
                            error_ = "candidate destination has an invalid finite goal distance";
                            return false;
                        }
                        ++bucketOffsets[static_cast<std::size_t>(distance)];
                        ++finiteCount;
                    }
                    std::size_t offset = 0;
                    for (std::size_t distance = 0; distance < bucketCount; ++distance) {
                        const std::size_t count = bucketOffsets[distance];
                        bucketOffsets[distance] = offset;
                        bucketCursors[distance] = offset;
                        offset += count;
                    }
                    ordered.resize(finiteCount);
                    for (std::size_t index = 0; index < support.size(); ++index) {
                        const int distance = layerDistances[index];
                        if (distance >= kInfiniteDistance) {
                            continue;
                        }
                        const std::size_t bucket = static_cast<std::size_t>(distance);
                        ordered[bucketCursors[bucket]++] = {distance, support[index], index};
                    }
                    if (!chargeSearchBudget(
                            report_,
                            limits_,
                            support.size() + bucketCount,
                            ordered.size() * sizeof(OrderedDestination))) {
                        releaseSearchBytes(report_, bucketBytes);
                        error_ = "candidate destination index exceeded proof budget";
                        return false;
                    }
                    ownedBytes_ += ordered.size() * sizeof(OrderedDestination);
                    releaseSearchBytes(report_, bucketBytes);

                    std::vector<OrderedDestinationRun> &runs = orderedDestinationRuns_[elapsedTurn];
                    for (std::size_t begin = 0; begin < ordered.size();) {
                        const int distance = ordered[begin].distance;
                        const int position = ordered[begin].key.rngPosition;
                        std::size_t end = begin + 1;
                        while (end < ordered.size() &&
                               ordered[end].distance == distance &&
                               ordered[end].key.rngPosition == position) {
                            ++end;
                        }
                        const std::size_t cells = end - begin;
                        if (!runs.empty() &&
                            runs.back().distance == distance &&
                            runs.back().lastPosition != std::numeric_limits<int>::max() &&
                            runs.back().lastPosition + 1 == position &&
                            runs.back().cellsPerPosition == cells) {
                            runs.back().lastPosition = position;
                        } else {
                            runs.push_back({distance, position, position, cells, begin});
                        }
                        begin = end;
                    }
                    const std::uint64_t runBytes =
                        runs.size() * sizeof(OrderedDestinationRun);
                    if (!chargeSearchBudget(
                            report_,
                            limits_,
                            ordered.size(),
                            runBytes)) {
                        error_ = "candidate destination range index exceeded proof budget";
                        return false;
                    }
                    ownedBytes_ += runBytes;

                    // Runs are already ordered by (distance, position).  Keep
                    // the exact run interval for each finite L value so a
                    // COMPLETE edge can jump directly to the first run whose
                    // RNG range can intersect its virtual p interval.  This
                    // preserves the required (L,p,cell) candidate order while
                    // avoiding a fresh scan from runs[0] for every edge.
                    std::vector<std::size_t> &distanceOffsets =
                        orderedDestinationRunDistanceOffsets_[elapsedTurn];
                    distanceOffsets.resize(static_cast<std::size_t>(horizon_) + 2, runs.size());
                    std::size_t runCursor = 0;
                    std::uint64_t offsetWork = 0;
                    for (int distance = 0; distance <= horizon_ + 1; ++distance) {
                        while (runCursor < runs.size() && runs[runCursor].distance < distance) {
                            ++runCursor;
                            ++offsetWork;
                        }
                        distanceOffsets[static_cast<std::size_t>(distance)] = runCursor;
                        ++offsetWork;
                    }
                    const std::uint64_t offsetBytes =
                        distanceOffsets.size() * sizeof(std::size_t);
                    if (!chargeSearchBudget(report_, limits_, offsetWork, offsetBytes)) {
                        error_ = "candidate destination distance-run index exceeded proof budget";
                        return false;
                    }
                    ownedBytes_ += offsetBytes;
                }
                return true;
            }

            bool buildCandidateEdgeIndex() {
                if (distances_.finiteEdges.size() != static_cast<std::size_t>(horizon_)) {
                    error_ = "candidate finite-edge index does not match horizon";
                    return false;
                }
                candidateEdgeRefs_.resize(static_cast<std::size_t>(horizon_));
                candidateRoomEdgeRanges_.resize(static_cast<std::size_t>(horizon_));
                for (int elapsedTurn = 0; elapsedTurn < horizon_; ++elapsedTurn) {
                    const std::vector<CheckedEdge> &edges = snapshot_.edgesByElapsedTurn[elapsedTurn];
                    if (distances_.finiteEdges[elapsedTurn].size() != edges.size()) {
                        error_ = "candidate finite-edge index does not match checked edge layer";
                        return false;
                    }
                    const std::vector<CellKey> &support = snapshot_.support[elapsedTurn];
                    std::vector<CandidateEdgeRef> &refs = candidateEdgeRefs_[elapsedTurn];
                    std::vector<CandidateRoomEdgeRange> &ranges = candidateRoomEdgeRanges_[elapsedTurn];
                    ranges.resize(support.size());
                    std::size_t edgeIndex = 0;
                    std::uint64_t indexWork = 0;
                    for (std::size_t sourceIndex = 0; sourceIndex < support.size(); ++sourceIndex) {
                        const CellKey &source = support[sourceIndex];
                        const std::size_t begin = refs.size();
                        while (edgeIndex < edges.size() && edges[edgeIndex].source < source) {
                            error_ = "candidate edge index contains a source outside Support order";
                            return false;
                        }
                        while (edgeIndex < edges.size() && edges[edgeIndex].source == source) {
                            ++indexWork;
                            const CheckedEdge &edge = edges[edgeIndex];
                            if (distances_.finiteEdges[elapsedTurn][edgeIndex] != 0) {
                                if (edge.kind == CheckedEdgeKind::Completion &&
                                    (!edge.targets.empty() ||
                                     (edge.hasContinuingOutput &&
                                      edge.firstOutputPosition > edge.lastOutputPosition))) {
                                    error_ = "checked COMPLETE edge uses an invalid explicit/virtual target representation";
                                    return false;
                                }
                                if (commandOrder(bundle_, edge.selectedCommand) ==
                                    std::numeric_limits<int>::max()) {
                                    error_ = "checked model contains a command outside registered profile";
                                    return false;
                                }
                                refs.push_back({edgeIndex, edge.mayReachGoal});
                            }
                            ++edgeIndex;
                        }
                        ranges[sourceIndex] = {begin, refs.size()};
                        ++indexWork;
                    }
                    if (edgeIndex != edges.size()) {
                        error_ = "candidate edge index ends with a source outside Support";
                        return false;
                    }
                    const std::uint64_t bytes =
                        refs.size() * sizeof(CandidateEdgeRef) +
                        ranges.size() * sizeof(CandidateRoomEdgeRange);
                    if (!chargeSearchBudget(report_, limits_, indexWork, bytes)) {
                        error_ = "candidate room-edge index exceeded proof budget";
                        return false;
                    }
                    ownedBytes_ += bytes;
                }
                return true;
            }

            bool buildFrame(Frame &frame) {
                ++diagnosticFrameBuilds_;
                if (frame.elapsedTurn < 0 || frame.elapsedTurn >= horizon_) {
                    error_ = "candidate iterator requested choices outside horizon";
                    return false;
                }
                if (frame.sourceIndex >= snapshot_.support[frame.elapsedTurn].size() ||
                    !(snapshot_.support[frame.elapsedTurn][frame.sourceIndex] == frame.source) ||
                    frame.sourceIndex >= candidateRoomEdgeRanges_[frame.elapsedTurn].size()) {
                    error_ = "candidate frame source index does not match Support";
                    return false;
                }
                const CandidateRoomEdgeRange range =
                    candidateRoomEdgeRanges_[frame.elapsedTurn][frame.sourceIndex];
                if (range.begin > range.end ||
                    range.end > candidateEdgeRefs_[frame.elapsedTurn].size()) {
                    error_ = "candidate room-edge range is invalid";
                    return false;
                }
                frame.edgeCursors.resize(range.end - range.begin);
                if (!chargeSearchBudget(
                        report_,
                        limits_,
                        0,
                        frame.edgeCursors.size() * sizeof(EdgeCursor))) {
                    error_ = "candidate frame construction exceeded proof budget";
                    return false;
                }
                ownedBytes_ += frame.edgeCursors.size() * sizeof(EdgeCursor);
                diagnosticFrameCursorTerms_ += frame.edgeCursors.size();
                return true;
            }

            const CandidateEdgeRef *edgeRef(const Frame &frame, std::size_t cursorIndex) const noexcept {
                if (frame.elapsedTurn < 0 || frame.elapsedTurn >= horizon_ ||
                    frame.sourceIndex >= candidateRoomEdgeRanges_[frame.elapsedTurn].size()) {
                    return nullptr;
                }
                const CandidateRoomEdgeRange range =
                    candidateRoomEdgeRanges_[frame.elapsedTurn][frame.sourceIndex];
                if (cursorIndex >= range.end - range.begin) {
                    return nullptr;
                }
                return &candidateEdgeRefs_[frame.elapsedTurn][range.begin + cursorIndex];
            }


            void releaseOwnedBytes(std::uint64_t bytes) {
                const std::uint64_t released = std::min(bytes, ownedBytes_);
                releaseSearchBytes(report_, released);
                ownedBytes_ -= released;
            }

            ChoiceStatus ensureTargetHead(
                Frame &frame,
                std::size_t cursorIndex,
                EdgeCursor &cursor) {
                if (cursor.exhausted) {
                    return ChoiceStatus::Exhausted;
                }
                if (cursor.targetHead) {
                    return ChoiceStatus::Choice;
                }

                const CandidateEdgeRef *ref = edgeRef(frame, cursorIndex);
                if (ref == nullptr) {
                    error_ = "candidate edge cursor is outside its room-edge index";
                    return ChoiceStatus::ModelError;
                }
                const CheckedEdge &edge = snapshot_.edgesByElapsedTurn[frame.elapsedTurn][ref->edgeIndex];
                const int profileOrder = commandOrder(bundle_, edge.selectedCommand);
                const std::vector<OrderedDestination> &ordered = orderedDestinations_[frame.elapsedTurn + 1];
                if (edge.kind == CheckedEdgeKind::Completion) {
                    if (!edge.hasContinuingOutput || ordered.empty()) {
                        return ChoiceStatus::Exhausted;
                    }
                    const std::vector<OrderedDestinationRun> &runs =
                        orderedDestinationRuns_[frame.elapsedTurn + 1];
                    const std::vector<std::size_t> &distanceOffsets =
                        orderedDestinationRunDistanceOffsets_[frame.elapsedTurn + 1];
                    if (distanceOffsets.size() != static_cast<std::size_t>(horizon_) + 2) {
                        error_ = "candidate COMPLETE distance-run index has invalid dimensions";
                        return ChoiceStatus::ModelError;
                    }
                    while (true) {
                        if (cursor.completionRunActive &&
                            cursor.destinationCursor < cursor.completionRunEnd) {
                            if (report_.candidateScans >= limits_.maxCandidateScans) {
                                error_ = "candidate choice scan limit reached";
                                return ChoiceStatus::BudgetExceeded;
                            }
                            const OrderedDestination destination = ordered[cursor.destinationCursor++];
                            ++report_.candidateScans;
                            if (!chargeSearchBudget(report_, limits_, 1, 0)) {
                                error_ = "candidate target scan exceeded proof budget";
                                return ChoiceStatus::BudgetExceeded;
                            }
                            cursor.targetHead = CandidateChoice{
                                destination.distance + 1,
                                true,
                                edge.selectedCommand,
                                profileOrder,
                                ref->edgeIndex,
                                false,
                                destination.key,
                                destination.supportIndex};
                            return ChoiceStatus::Choice;
                        }
                        if (cursor.completionRunActive) {
                            cursor.completionRunActive = false;
                            ++cursor.completionRunCursor;
                        }
                        while (cursor.completionDistance <= horizon_) {
                            const std::size_t distanceIndex =
                                static_cast<std::size_t>(cursor.completionDistance);
                            const std::size_t runBegin = distanceOffsets[distanceIndex];
                            const std::size_t runEnd = distanceOffsets[distanceIndex + 1];
                            if (!cursor.completionDistanceInitialized) {
                                std::size_t lo = runBegin;
                                std::size_t hi = runEnd;
                                std::uint64_t comparisons = 0;
                                while (lo < hi) {
                                    ++comparisons;
                                    const std::size_t mid = lo + (hi - lo) / 2;
                                    if (runs[mid].lastPosition < edge.firstOutputPosition) {
                                        lo = mid + 1;
                                    } else {
                                        hi = mid;
                                    }
                                }
                                if (comparisons != 0 &&
                                    !chargeSearchBudget(report_, limits_, comparisons, 0)) {
                                    error_ = "candidate COMPLETE range-index lookup exceeded proof budget";
                                    return ChoiceStatus::BudgetExceeded;
                                }
                                diagnosticRangeLookupWork_ += comparisons;
                                cursor.completionRunCursor = lo;
                                cursor.completionDistanceInitialized = true;
                            }
                            if (cursor.completionRunCursor >= runEnd ||
                                runs[cursor.completionRunCursor].firstPosition > edge.lastOutputPosition) {
                                ++cursor.completionDistance;
                                cursor.completionDistanceInitialized = false;
                                continue;
                            }
                            if (!chargeSearchBudget(report_, limits_, 1, 0)) {
                                error_ = "candidate COMPLETE range-index scan exceeded proof budget";
                                return ChoiceStatus::BudgetExceeded;
                            }
                            ++diagnosticRangeScanWork_;
                            const OrderedDestinationRun &run = runs[cursor.completionRunCursor];
                            const int firstPosition =
                                std::max(run.firstPosition, edge.firstOutputPosition);
                            const int lastPosition =
                                std::min(run.lastPosition, edge.lastOutputPosition);
                            const std::size_t firstOffset =
                                static_cast<std::size_t>(firstPosition - run.firstPosition) *
                                run.cellsPerPosition;
                            const std::size_t positionCount =
                                static_cast<std::size_t>(lastPosition - firstPosition) + 1;
                            cursor.destinationCursor = run.beginIndex + firstOffset;
                            cursor.completionRunEnd =
                                cursor.destinationCursor + positionCount * run.cellsPerPosition;
                            if (cursor.completionRunEnd > ordered.size()) {
                                error_ = "candidate COMPLETE range index escaped the ordered destination layer";
                                return ChoiceStatus::ModelError;
                            }
                            cursor.completionRunActive = true;
                            break;
                        }
                        if (!cursor.completionRunActive) {
                            return ChoiceStatus::Exhausted;
                        }
                    }
                }

                while (cursor.destinationCursor < ordered.size()) {
                    if (report_.candidateScans >= limits_.maxCandidateScans) {
                        error_ = "candidate choice scan limit reached";
                        return ChoiceStatus::BudgetExceeded;
                    }
                    const OrderedDestination destination = ordered[cursor.destinationCursor++];
                    ++report_.candidateScans;
                    if (!chargeSearchBudget(report_, limits_, 1, 0)) {
                        error_ = "candidate target scan exceeded proof budget";
                        return ChoiceStatus::BudgetExceeded;
                    }
                    if (!edgeContainsCandidateTarget(edge, destination.key)) {
                        continue;
                    }
                    cursor.targetHead = CandidateChoice{
                        destination.distance + 1,
                        edge.kind == CheckedEdgeKind::Completion,
                        edge.selectedCommand,
                        profileOrder,
                        ref->edgeIndex,
                        false,
                        destination.key,
                        destination.supportIndex};
                    return ChoiceStatus::Choice;
                }
                return ChoiceStatus::Exhausted;
            }

            ChoiceStatus nextChoice(Frame &frame, CandidateChoice &selected) {
                bool haveChoice = false;
                std::size_t selectedCursor = 0;
                bool selectedGoal = false;

                for (std::size_t cursorIndex = 0; cursorIndex < frame.edgeCursors.size(); ++cursorIndex) {
                    EdgeCursor &cursor = frame.edgeCursors[cursorIndex];
                    if (cursor.exhausted) {
                        continue;
                    }
                    const CandidateEdgeRef *ref = edgeRef(frame, cursorIndex);
                    if (ref == nullptr) {
                        error_ = "candidate edge cursor is outside its room-edge index";
                        return ChoiceStatus::ModelError;
                    }
                    const CheckedEdge &edge = snapshot_.edgesByElapsedTurn[frame.elapsedTurn][ref->edgeIndex];
                    const int profileOrder = commandOrder(bundle_, edge.selectedCommand);
                    bool goalPending = ref->mayReachGoal && !cursor.goalConsumed;

                    if (goalPending) {
                        const CandidateChoice goalChoice{
                            1,
                            edge.kind == CheckedEdgeKind::Completion,
                            edge.selectedCommand,
                            profileOrder,
                            ref->edgeIndex,
                            true,
                            {}};
                        if (!haveChoice || candidateChoiceLess(goalChoice, selected)) {
                            selected = goalChoice;
                            selectedCursor = cursorIndex;
                            selectedGoal = true;
                            haveChoice = true;
                        }
                    }

                    const ChoiceStatus targetStatus = ensureTargetHead(frame, cursorIndex, cursor);
                    if (targetStatus == ChoiceStatus::BudgetExceeded || targetStatus == ChoiceStatus::ModelError) {
                        return targetStatus;
                    }
                    if (targetStatus == ChoiceStatus::Choice &&
                        (!haveChoice || candidateChoiceLess(*cursor.targetHead, selected))) {
                        selected = *cursor.targetHead;
                        selectedCursor = cursorIndex;
                        selectedGoal = false;
                        haveChoice = true;
                    }
                }

                if (!haveChoice) {
                    return ChoiceStatus::Exhausted;
                }

                EdgeCursor &cursor = frame.edgeCursors[selectedCursor];
                if (!cursor.childCommandSequenceId.has_value()) {
                    std::size_t childId = 0;
                    if (!commandSequences_.extend(
                            frame.commandSequenceId,
                            selected.commandOrder,
                            childId)) {
                        error_ = commandSequences_.error();
                        return isBudgetFailure(error_)
                            ? ChoiceStatus::BudgetExceeded
                            : ChoiceStatus::ModelError;
                    }
                    cursor.childCommandSequenceId = childId;
                }
                selected.commandSequenceId = *cursor.childCommandSequenceId;
                if (selectedGoal) {
                    cursor.goalConsumed = true;
                    if (report_.candidateScans >= limits_.maxCandidateScans) {
                        error_ = "candidate choice scan limit reached";
                        return ChoiceStatus::BudgetExceeded;
                    }
                    ++report_.candidateScans;
                    if (!chargeSearchBudget(report_, limits_, 1, 0)) {
                        error_ = "candidate goal scan exceeded proof budget";
                        return ChoiceStatus::BudgetExceeded;
                    }
                } else {
                    cursor.targetHead.reset();
                }
                return ChoiceStatus::Choice;
            }

            const RuleBundle &bundle_;
            const CheckedSnapshot &snapshot_;
            const GoalDistances &distances_;
            CommandSequenceTrie &commandSequences_;
            int horizon_ = 0;
            const ProofBudget &limits_;
            BudgetReport &report_;
            std::vector<std::vector<OrderedDestination>> orderedDestinations_;
            std::vector<std::vector<OrderedDestinationRun>> orderedDestinationRuns_;
            std::vector<std::vector<std::size_t>> orderedDestinationRunDistanceOffsets_;
            std::vector<std::vector<CandidateEdgeRef>> candidateEdgeRefs_;
            std::vector<std::vector<CandidateRoomEdgeRange>> candidateRoomEdgeRanges_;

            std::vector<Frame> stack_;
            std::vector<int> commandPath_;
            std::vector<CandidateStep> stepPath_;
            std::string error_;
            bool ready_ = false;
            bool staticIndexesReady_ = true;
            std::uint64_t ownedBytes_ = 0;
            std::uint64_t diagnosticFrameBuilds_ = 0;
            std::uint64_t diagnosticFrameCursorTerms_ = 0;
            std::uint64_t diagnosticRangeLookupWork_ = 0;
            std::uint64_t diagnosticRangeScanWork_ = 0;
            std::uint64_t diagnosticDuplicateGoalWork_ = 0;
        };

        enum class CandidateMismatchKind {
            Completion,
            Guard,
            DetailedModelError,
        };

        struct CandidateMismatch {
            CandidateMismatchKind kind = CandidateMismatchKind::Completion;
            int elapsedTurn = 0;
            std::uint32_t trialOrder = 0;
            std::size_t stepIndex = 0;
            std::size_t edgeIndex = 0;
            CandidateStep step;
            RawState stateBefore;
            RawState stateAfter;
            bool hasStateAfter = false;
            std::string reason;
            std::uint64_t partitionVersion = 0;
            std::uint64_t coverageVersion = 0;
        };

        const PredicatePartition *partitionAt(const PartitionFamily &partitions, int position) {
            for (const PredicatePartition &partition: partitions.trees) {
                if (partition.rngPosition == position) {
                    return &partition;
                }
            }
            return nullptr;
        }

        const CheckedEdge *edgeForStep(
            const CheckedSnapshot &snapshot,
            const CandidateStep &step) {
            if (step.elapsedTurn < 0 ||
                step.elapsedTurn >= static_cast<int>(snapshot.edgesByElapsedTurn.size())) {
                return nullptr;
            }
            const std::vector<CheckedEdge> &edges = snapshot.edgesByElapsedTurn[step.elapsedTurn];
            if (step.modelTermNumber >= edges.size()) {
                return nullptr;
            }
            const CheckedEdge &edge = edges[step.modelTermNumber];
            if (!(edge.source == step.source) || edge.selectedCommand != step.selectedCommand) {
                return nullptr;
            }
            return &edge;
        }

        std::optional<CandidateMismatch> firstCandidateMismatch(
            const RuleBundle &bundle,
            const CheckedSnapshot &snapshot,
            const CandidatePath &path,
            const ReplayResult &replay,
            std::uint32_t trialOrder) {
            const std::size_t comparedTurns = std::min(path.steps.size(), replay.turns.size());
            for (std::size_t index = 0; index < comparedTurns; ++index) {
                const CandidateStep &step = path.steps[index];
                const CheckedEdge *edge = edgeForStep(snapshot, step);
                if (edge == nullptr) {
                    return CandidateMismatch{
                        CandidateMismatchKind::DetailedModelError,
                        step.elapsedTurn,
                        trialOrder,
                        index,
                        step.modelTermNumber,
                        step,
                        replay.turns[index].stateBefore,
                        replay.turns[index].stateAfter,
                        true,
                        "candidate references a missing or different checked edge"};
                }

                const ReplayTurn &turn = replay.turns[index];
                if (turn.stateBefore.position != step.source.rngPosition) {
                    return CandidateMismatch{
                        CandidateMismatchKind::DetailedModelError,
                        step.elapsedTurn,
                        trialOrder,
                        index,
                        step.modelTermNumber,
                        step,
                        turn.stateBefore,
                        turn.stateAfter,
                        true,
                        "concrete turn-entry RNG position differs from candidate source"};
                }
                const PredicatePartition *sourcePartition = partitionAt(
                    snapshot.partitions,
                    turn.stateBefore.position);
                if (sourcePartition == nullptr) {
                    return CandidateMismatch{
                        CandidateMismatchKind::DetailedModelError,
                        step.elapsedTurn,
                        trialOrder,
                        index,
                        step.modelTermNumber,
                        step,
                        turn.stateBefore,
                        turn.stateAfter,
                        true,
                        "candidate source partition is missing"};
                }
                std::string stateError;
                const std::optional<std::uint32_t> sourceLeaf = ProofKernel::project(
                    turn.stateBefore,
                    *sourcePartition,
                    stateError);
                if (!sourceLeaf || *sourceLeaf != step.source.localCellId) {
                    return CandidateMismatch{
                        CandidateMismatchKind::DetailedModelError,
                        step.elapsedTurn,
                        trialOrder,
                        index,
                        step.modelTermNumber,
                        step,
                        turn.stateBefore,
                        turn.stateAfter,
                        true,
                        "concrete turn-entry cell differs from candidate source"};
                }

                const std::optional<Box> before = ProofKernel::alphaSingleton(turn.stateBefore, stateError);
                if (!before) {
                    return CandidateMismatch{
                        CandidateMismatchKind::DetailedModelError,
                        step.elapsedTurn,
                        trialOrder,
                        index,
                        step.modelTermNumber,
                        step,
                        turn.stateBefore,
                        turn.stateAfter,
                        true,
                        "concrete turn-entry state cannot be abstracted: " + stateError};
                }
                if (!contains(edge->rootDomain, *before)) {
                    return CandidateMismatch{
                        CandidateMismatchKind::Guard,
                        step.elapsedTurn,
                        trialOrder,
                        index,
                        step.modelTermNumber,
                        step,
                        turn.stateBefore,
                        turn.stateAfter,
                        true,
                        "concrete input is outside the selected checked guard/selectable domain"};
                }

                const bool concreteGoal = turn.stateAfter.players[1].hp == 0;
                const bool concreteFailure =
                    !concreteGoal && turn.stateAfter.players[0].hp == 0;
                auto outputMismatch = [&](std::string reason) {
                    return CandidateMismatch{
                        edge->kind == CheckedEdgeKind::Completion
                            ? CandidateMismatchKind::Completion
                            : CandidateMismatchKind::DetailedModelError,
                        step.elapsedTurn,
                        trialOrder,
                        index,
                        step.modelTermNumber,
                        step,
                        turn.stateBefore,
                        turn.stateAfter,
                        true,
                        std::move(reason)};
                };

                if (step.goal) {
                    if (!concreteGoal) {
                        return outputMismatch("candidate selected GOAL but exact replay did not defeat the enemy");
                    }
                    continue;
                }
                if (concreteGoal || concreteFailure) {
                    return outputMismatch("candidate selected a continuing destination but exact replay terminated");
                }
                if (!edge->hasContinuingOutput) {
                    return outputMismatch("checked edge has no continuing output for an exact continuing turn");
                }

                const std::optional<Box> after = ProofKernel::alphaSingleton(turn.stateAfter, stateError);
                if (!after) {
                    return CandidateMismatch{
                        CandidateMismatchKind::DetailedModelError,
                        step.elapsedTurn,
                        trialOrder,
                        index,
                        step.modelTermNumber,
                        step,
                        turn.stateBefore,
                        turn.stateAfter,
                        true,
                        "concrete output state cannot be abstracted: " + stateError};
                }
                const Box *checkedContinuingOutput = edge->continuingOutput.get();
                std::optional<Box> reconstructedTurnEntryOutput;
                if (checkedContinuingOutput == nullptr &&
                    edge->kind == CheckedEdgeKind::Completion &&
                    edge->completionCut == CompletionCutId::TurnEntry) {
                    std::string envelopeError;
                    reconstructedTurnEntryOutput = ProofKernel::turnEntryCompletionContinuingEnvelope(
                        bundle,
                        *edge,
                        envelopeError);
                    if (!reconstructedTurnEntryOutput.has_value()) {
                        return CandidateMismatch{
                            CandidateMismatchKind::DetailedModelError,
                            step.elapsedTurn,
                            trialOrder,
                            index,
                            step.modelTermNumber,
                            step,
                            turn.stateBefore,
                            turn.stateAfter,
                            true,
                            "TurnEntry COMPLETE envelope reconstruction failed: " + envelopeError};
                    }
                    checkedContinuingOutput = &*reconstructedTurnEntryOutput;
                }
                if (checkedContinuingOutput == nullptr) {
                    return CandidateMismatch{
                        CandidateMismatchKind::DetailedModelError,
                        step.elapsedTurn,
                        trialOrder,
                        index,
                        step.modelTermNumber,
                        step,
                        turn.stateBefore,
                        turn.stateAfter,
                        true,
                        "checked continuing edge is missing its checked output image"};
                }
                if (!contains(*checkedContinuingOutput, *after)) {
                    return outputMismatch(
                        edge->kind == CheckedEdgeKind::Completion
                            ? "exact output escapes the selected COMPLETE envelope"
                            : "exact output escapes a detailed checked image");
                }
                if (turn.stateAfter.position != step.target.rngPosition) {
                    return outputMismatch("exact output RNG position differs from selected abstract destination");
                }
                const PredicatePartition *targetPartition = partitionAt(
                    snapshot.partitions,
                    turn.stateAfter.position);
                if (targetPartition == nullptr) {
                    return CandidateMismatch{
                        CandidateMismatchKind::DetailedModelError,
                        step.elapsedTurn,
                        trialOrder,
                        index,
                        step.modelTermNumber,
                        step,
                        turn.stateBefore,
                        turn.stateAfter,
                        true,
                        "candidate target partition is missing"};
                }
                stateError.clear();
                const std::optional<std::uint32_t> targetLeaf = ProofKernel::project(
                    turn.stateAfter,
                    *targetPartition,
                    stateError);
                if (!targetLeaf) {
                    return CandidateMismatch{
                        CandidateMismatchKind::DetailedModelError,
                        step.elapsedTurn,
                        trialOrder,
                        index,
                        step.modelTermNumber,
                        step,
                        turn.stateBefore,
                        turn.stateAfter,
                        true,
                        "exact output cannot be projected: " + stateError};
                }
                if (*targetLeaf != step.target.localCellId) {
                    return outputMismatch("exact output belongs to a different destination leaf");
                }
            }

            if (replay.turns.size() < path.steps.size()) {
                const std::size_t index = replay.turns.size();
                const CandidateStep &step = path.steps[index];
                const CheckedEdge *edge = edgeForStep(snapshot, step);
                if (edge == nullptr) {
                    return CandidateMismatch{
                        CandidateMismatchKind::DetailedModelError,
                        step.elapsedTurn,
                        trialOrder,
                        index,
                        step.modelTermNumber,
                        step,
                        replay.finalState,
                        {},
                        false,
                        "candidate stopped before a missing checked edge"};
                }
                std::string stateError;
                const std::optional<Box> before = ProofKernel::alphaSingleton(replay.finalState, stateError);
                if (before && !contains(edge->rootDomain, *before)) {
                    return CandidateMismatch{
                        CandidateMismatchKind::Guard,
                        step.elapsedTurn,
                        trialOrder,
                        index,
                        step.modelTermNumber,
                        step,
                        replay.finalState,
                        {},
                        false,
                        "exact replay stopped before a command outside the selected guard/selectable domain"};
                }
                if (!ExactReplay::isSelectable(bundle, replay.finalState, step.selectedCommand)) {
                    return CandidateMismatch{
                        CandidateMismatchKind::Guard,
                        step.elapsedTurn,
                        trialOrder,
                        index,
                        step.modelTermNumber,
                        step,
                        replay.finalState,
                        {},
                        false,
                        "abstract candidate selected a command that is not selectable in the concrete state"};
                }
                return CandidateMismatch{
                    edge->kind == CheckedEdgeKind::Completion
                        ? CandidateMismatchKind::Completion
                        : CandidateMismatchKind::DetailedModelError,
                    step.elapsedTurn,
                    trialOrder,
                    index,
                    step.modelTermNumber,
                    step,
                    replay.finalState,
                    {},
                    false,
                    "exact replay ended before the abstract goal path"};
            }
            return std::nullopt;
        }

        bool firstSeparatingPredicate(
            const Box &cell,
            const Box &guard,
            const RawState &state,
            Predicate &predicate,
            std::string &error) {
            const std::optional<Box> singleton = ProofKernel::alphaSingleton(state, error);
            if (!singleton) {
                return false;
            }

            constexpr ResourceAxis resources[] = {
                ResourceAxis::EnemyHp,
                ResourceAxis::HeroHp,
                ResourceAxis::Mp,
                ResourceAxis::Herb,
            };
            for (ResourceAxis axis: resources) {
                const Interval &whole = intervalOf(cell, axis);
                const Interval &allowed = intervalOf(guard, axis);
                const std::int64_t value = intervalOf(*singleton, axis).lo;
                if (value < allowed.lo && allowed.lo > whole.lo) {
                    if (allowed.lo == std::numeric_limits<std::int64_t>::min()) {
                        continue;
                    }
                    predicate = {};
                    predicate.kind = PredicateKind::ResourceLe;
                    predicate.resource = axis;
                    predicate.threshold = allowed.lo - 1;
                    return true;
                }
                if (value > allowed.hi && allowed.hi < whole.hi) {
                    predicate = {};
                    predicate.kind = PredicateKind::ResourceLe;
                    predicate.resource = axis;
                    predicate.threshold = allowed.hi;
                    return true;
                }
            }

            constexpr ModeAxis modes[] = {
                ModeAxis::Charge,
                ModeAxis::Paralysis,
                ModeAxis::Acro,
                ModeAxis::Rage,
                ModeAxis::Inactive,
                ModeAxis::Camera,
            };
            for (ModeAxis axis: modes) {
                const std::uint16_t whole = modeMaskOf(cell, axis);
                const std::uint16_t allowed = modeMaskOf(guard, axis);
                const std::uint16_t actual = modeMaskOf(*singleton, axis);
                if ((actual & allowed) == 0 && allowed != 0 && allowed != whole) {
                    predicate = {};
                    predicate.kind = PredicateKind::ModeInMask;
                    predicate.mode = axis;
                    predicate.mask = allowed;
                    return true;
                }
            }
            error = "no allowed separating atom distinguishes the concrete state from the checked guard";
            return false;
        }

        CandidateFailureKind classifyCandidateFailure(
            const CheckedSnapshot &snapshot,
            const CandidatePath &path,
            const ReplayResult &replay) {
            if (!replay.valid && !replay.reason.empty()) {
                return CandidateFailureKind::IllegalCommand;
            }
            const std::size_t comparedTurns = std::min(path.steps.size(), replay.turns.size());
            for (std::size_t index = 0; index < comparedTurns; ++index) {
                const CandidateStep &step = path.steps[index];
                const ReplayTurn &turn = replay.turns[index];
                if (turn.stateBefore.position != step.source.rngPosition) {
                    return CandidateFailureKind::AbstractMismatch;
                }

                const PredicatePartition *sourcePartition = partitionAt(
                    snapshot.partitions,
                    turn.stateBefore.position);
                if (sourcePartition == nullptr) {
                    return CandidateFailureKind::AbstractMismatch;
                }
                std::string projectError;
                const std::optional<std::uint32_t> sourceLeaf = ProofKernel::project(
                    turn.stateBefore,
                    *sourcePartition,
                    projectError);
                if (!sourceLeaf || *sourceLeaf != step.source.localCellId) {
                    return CandidateFailureKind::AbstractMismatch;
                }

                if (step.goal) {
                    if (turn.stateAfter.players[1].hp != 0) {
                        return CandidateFailureKind::AbstractMismatch;
                    }
                    continue;
                }

                if (turn.stateAfter.players[0].hp == 0 || turn.stateAfter.players[1].hp == 0) {
                    return CandidateFailureKind::AbstractMismatch;
                }
                if (turn.stateAfter.position != step.target.rngPosition) {
                    return CandidateFailureKind::AbstractMismatch;
                }
                const PredicatePartition *targetPartition = partitionAt(
                    snapshot.partitions,
                    turn.stateAfter.position);
                if (targetPartition == nullptr) {
                    return CandidateFailureKind::AbstractMismatch;
                }
                projectError.clear();
                const std::optional<std::uint32_t> targetLeaf = ProofKernel::project(
                    turn.stateAfter,
                    *targetPartition,
                    projectError);
                if (!targetLeaf || *targetLeaf != step.target.localCellId) {
                    return CandidateFailureKind::AbstractMismatch;
                }
            }

            if (replay.lost) {
                return CandidateFailureKind::Lost;
            }
            return CandidateFailureKind::NotWon;
        }

        SolveResult makeFailure(
            SolveKind kind,
            int horizon,
            std::string reason,
            Clock::time_point start) {
            SolveResult result;
            result.kind = kind;
            result.horizon = horizon;
            result.reason = std::move(reason);
            stampElapsed(result.budget, start);
            return result;
        }

        void bindProblemKey(SolveResult &result, const Problem &problem) {
            result.hasProblemKey = true;
            result.problemKey = problem;
        }

        SolveResult attachCheckedPrefix(
            PrefixReceipt receipt,
            const Problem &suffixProblem,
            SolveResult result,
            const ProofBudget &limits,
            Clock::time_point start) {
            if (!receipt.valid || !receipt.observationsChecked) {
                releasePrefixReceiptBytes(result.budget, receipt);
                result.kind = SolveKind::ModelError;
                result.reason = "prefix attachment received an unchecked PrefixReceipt";
                stampElapsed(result.budget, start);
                return result;
            }

            const Problem expectedSuffix = ExactReplay::bindSuffixProblem(
                receipt.initialProblem,
                receipt);
            if (!sameProblemKey(expectedSuffix, suffixProblem) ||
                !sameRawState(receipt.terminalState, suffixProblem.s0) ||
                receipt.terminalTurn != suffixProblem.startTurn ||
                receipt.initialProblem.ruleId != suffixProblem.ruleId ||
                receipt.initialProblem.seed != suffixProblem.seed) {
                releasePrefixReceiptBytes(result.budget, receipt);
                result.kind = SolveKind::ModelError;
                result.reason = "prefix/suffix raw-state, turn, rule, seed, or future-constraint connection mismatch";
                stampElapsed(result.budget, start);
                return result;
            }
            if (result.hasProblemKey && !sameProblemKey(result.problemKey, suffixProblem)) {
                releasePrefixReceiptBytes(result.budget, receipt);
                result.kind = SolveKind::ModelError;
                result.reason = "suffix result belongs to a different problem_key";
                stampElapsed(result.budget, start);
                return result;
            }
            const std::uint64_t joinedPrefixBytes =
                result.kind == SolveKind::Win ? receipt.prefix.size() * sizeof(int) : 0;
            if (!chargeSearchBudget(result.budget, limits, 1, joinedPrefixBytes)) {
                releasePrefixReceiptBytes(result.budget, receipt);
                result.kind = SolveKind::Unknown;
                result.reason = "prefix attachment exceeded the shared proof budget";
                result.prefixConnected = false;
                stampElapsed(result.budget, start);
                return result;
            }

            bindProblemKey(result, suffixProblem);
            result.prefixConnected = true;
            if (result.kind == SolveKind::Win) {
                std::vector<int> joined = receipt.prefix;
                joined.insert(joined.end(), result.commands.begin(), result.commands.end());
                result.commands = std::move(joined);
            }
            result.prefixReceipt = std::move(receipt);
            stampElapsed(result.budget, start);
            return result;
        }
    } // namespace

    SolveResult WorldlineSolver::solveSuffix(
        const Problem &problem,
        int horizon,
        const ProofBudget &budget,
        const std::vector<int> &candidateHint) const {
        const Clock::time_point start = Clock::now();
        const ProofBudget limits = effectiveProofBudget(budget, start);
        CheckedKernelCache checkedCache;
        if (const CheckResult cacheCheck = ProofKernel::bindCheckedCache(checkedCache, problem);
            !cacheCheck.accepted) {
            return makeFailure(
                SolveKind::ModelError,
                horizon,
                cacheCheck.reason,
                start);
        }
        return solveSuffixWithBudget(
            problem,
            horizon,
            limits,
            candidateHint,
            start,
            {},
            checkedCache);
    }

    SolveResult WorldlineSolver::proveMinimum(
        const Problem &problem,
        int horizonLimit,
        const ProofBudget &budget,
        const std::vector<int> &candidateHint) const {
        const Clock::time_point start = Clock::now();
        const ProofBudget limits = effectiveProofBudget(budget, start);
        BudgetReport report;
        CheckedKernelCache checkedCache;
        if (const CheckResult cacheCheck = ProofKernel::bindCheckedCache(checkedCache, problem);
            !cacheCheck.accepted) {
            return makeFailure(
                SolveKind::ModelError,
                horizonLimit,
                cacheCheck.reason,
                start);
        }

        if (!bundle_.registered || problem.ruleId != bundle_.id) {
            return makeFailure(
                SolveKind::UnsupportedInput,
                horizonLimit,
                "problem rule_id is not this registered bundle",
                start);
        }
        if (limits.totalTimeMs <= 0 || limits.totalTimeMs > 15000) {
            return makeFailure(
                SolveKind::InvalidInput,
                horizonLimit,
                "total_time must be 1..15000 ms",
                start);
        }
        if (horizonLimit < 0) {
            return makeFailure(
                SolveKind::InvalidInput,
                horizonLimit,
                "negative H_limit",
                start);
        }
        const std::string validationError = ExactReplay::validateProblem(
            bundle_, problem, horizonLimit);
        if (!validationError.empty()) {
            return makeFailure(
                SolveKind::UnsupportedInput,
                horizonLimit,
                validationError,
                start);
        }
        if (static_cast<int>(candidateHint.size()) > horizonLimit) {
            return makeFailure(
                SolveKind::InvalidInput,
                horizonLimit,
                "candidate hint exceeds H_limit",
                start);
        }

        if (problem.s0.players[1].hp == 0) {
            SolveResult result;
            result.kind = SolveKind::Win;
            result.minimality = MinimalityKind::Optimal;
            result.horizon = horizonLimit;
            result.firstWinningTurn = 0;
            result.minimumTurnLowerBound = 0;
            result.minimumTurnUpperBound = 0;
            result.replay = ExactReplay::replay(bundle_, problem, {}, true, &limits, &report);
            stampFirstWin(report, start);
            result.budget = report;
            bindProblemKey(result, problem);
            stampElapsed(result.budget, start);
            return result;
        }

        SolveResult witness = solveSuffixWithBudget(
            problem,
            horizonLimit,
            limits,
            candidateHint,
            start,
            report,
            checkedCache);
        if (witness.kind != SolveKind::Win) {
            return witness;
        }

        return tightenMinimumWithBudget(
            problem,
            std::move(witness),
            limits,
            start,
            checkedCache);
    }

    SolveResult WorldlineSolver::tightenMinimumWithBudget(
        const Problem &problem,
        SolveResult witness,
        const ProofBudget &budget,
        Clock::time_point start,
        CheckedKernelCache &checkedCache) const {
        if (witness.kind != SolveKind::Win || witness.firstWinningTurn < 0 ||
            !witness.hasProblemKey || !sameProblemKey(witness.problemKey, problem)) {
            SolveResult error = makeFailure(
                SolveKind::ModelError,
                witness.horizon,
                "tighten_minimum received a witness that is not exactly checked",
                start);
            error.commands = witness.commands;
            error.replay = witness.replay;
            error.budget = witness.budget;
            stampElapsed(error.budget, start);
            return error;
        }

        ReplayResult freshWitness = ExactReplay::replay(
            bundle_,
            problem,
            witness.commands,
            true,
            &budget,
            &witness.budget);
        if (freshWitness.interrupted) {
            releaseReplayBytes(witness.budget, freshWitness);
            witness.minimality = MinimalityKind::Unknown;
            witness.reason = "exact witness retained; fresh TRUE replay exhausted the shared deadline";
            stampElapsed(witness.budget, start);
            return witness;
        }
        if (!freshWitness.supported || !freshWitness.valid || !freshWitness.won ||
            freshWitness.firstWinningTurn != witness.firstWinningTurn) {
            SolveResult error = makeFailure(
                SolveKind::ModelError,
                witness.horizon,
                "fresh exact replay rejected the TRUE witness used by tighten_minimum",
                start);
            error.commands = witness.commands;
            error.replay = std::move(freshWitness);
            error.budget = witness.budget;
            releaseReplayBytes(error.budget, witness.replay);
            bindProblemKey(error, problem);
            stampElapsed(error.budget, start);
            return error;
        }
        releaseReplayBytes(witness.budget, witness.replay);
        witness.replay = std::move(freshWitness);

        int winningTurns = witness.firstWinningTurn;
        if (winningTurns == 0) {
            witness.minimality = MinimalityKind::Optimal;
            witness.minimumTurnLowerBound = 0;
            witness.minimumTurnUpperBound = 0;
            witness.reason = "exact zero-turn success is optimal";
            stampElapsed(witness.budget, start);
            return witness;
        }

        int provenLowerBound = 1;
        while (!deadlineReached(start, budget, witness.budget)) {
            const int lowerHorizon = winningTurns - 1;
            SolveResult bound = solveSuffixWithBudget(
                problem,
                lowerHorizon,
                budget,
                {},
                start,
                witness.budget,
                checkedCache);

            if (bound.kind == SolveKind::ProvedFalse) {
                if (!bound.hasProblemKey || !sameProblemKey(bound.problemKey, problem) ||
                    bound.horizon != winningTurns - 1) {
                    SolveResult error = bound;
                    error.kind = SolveKind::ModelError;
                    error.reason = "D(N-1)=false result does not match the fresh TRUE problem_key or horizon";
                    error.commands = witness.commands;
                    error.replay = witness.replay;
                    error.minimality = MinimalityKind::Unknown;
                    bindProblemKey(error, problem);
                    return error;
                }
                witness.minimality = MinimalityKind::Optimal;
                witness.minimumTurnLowerBound = winningTurns;
                witness.minimumTurnUpperBound = winningTurns;
                witness.reason = "exact witness paired with independently verified D(N-1)=false";
                witness.provedFalseRootBound = bound.provedFalseRootBound;
                witness.provedFalseNoAbstractSuccessPath = bound.provedFalseNoAbstractSuccessPath;
                witness.provedFalseDelta = bound.provedFalseDelta;
                witness.partitionVersion = bound.partitionVersion;
                witness.coverageVersion = bound.coverageVersion;
                witness.budget = bound.budget;
                stampElapsed(witness.budget, start);
                return witness;
            }

            if (bound.kind == SolveKind::Win) {
                if (bound.firstWinningTurn < 0 || bound.firstWinningTurn >= winningTurns ||
                    !bound.replay.won || !bound.hasProblemKey ||
                    !sameProblemKey(bound.problemKey, problem) ||
                    bound.horizon != winningTurns - 1) {
                    SolveResult error = bound;
                    error.kind = SolveKind::ModelError;
                    error.reason = "tighten_minimum received a non-shortening or disconnected checked witness";
                    error.commands = witness.commands;
                    error.replay = witness.replay;
                    error.minimality = MinimalityKind::Unknown;
                    error.minimumTurnLowerBound = provenLowerBound;
                    error.minimumTurnUpperBound = winningTurns;
                    stampElapsed(error.budget, start);
                    return error;
                }
                witness = std::move(bound);
                winningTurns = witness.firstWinningTurn;
                if (winningTurns == 0) {
                    witness.minimality = MinimalityKind::Optimal;
                    witness.minimumTurnLowerBound = 0;
                    witness.minimumTurnUpperBound = 0;
                    witness.reason = "tightening found exact zero-turn success";
                    stampElapsed(witness.budget, start);
                    return witness;
                }
                continue;
            }

            if (bound.kind == SolveKind::ModelError ||
                bound.kind == SolveKind::InvalidInput ||
                bound.kind == SolveKind::UnsupportedInput) {
                bound.commands = witness.commands;
                bound.replay = witness.replay;
                bound.firstWinningTurn = witness.firstWinningTurn;
                bound.minimality = MinimalityKind::Unknown;
                bound.minimumTurnLowerBound = provenLowerBound;
                bound.minimumTurnUpperBound = winningTurns;
                bound.reason += "; exact witness remains available";
                stampElapsed(bound.budget, start);
                return bound;
            }

            witness.minimality = MinimalityKind::Unknown;
            witness.minimumTurnLowerBound = provenLowerBound;
            witness.minimumTurnUpperBound = winningTurns;
            witness.reason = "exact witness retained; minimality proof is UNKNOWN within the shared budget";
            witness.budget = bound.budget;
            stampElapsed(witness.budget, start);
            return witness;
        }

        witness.minimality = MinimalityKind::Unknown;
        witness.minimumTurnLowerBound = provenLowerBound;
        witness.minimumTurnUpperBound = winningTurns;
        witness.reason = "exact witness retained; shared deadline expired before minimality was proved";
        stampElapsed(witness.budget, start);
        return witness;
    }

    SolveResult WorldlineSolver::solveSuffixWithBudget(
        const Problem &problem,
        int horizon,
        const ProofBudget &budget,
        const std::vector<int> &candidateHint,
        Clock::time_point start,
        BudgetReport report,
        CheckedKernelCache &checkedCache) const {

        if (const CheckResult cacheCheck = ProofKernel::bindCheckedCache(checkedCache, problem);
            !cacheCheck.accepted) {
            SolveResult result = makeFailure(
                SolveKind::ModelError,
                horizon,
                cacheCheck.reason,
                start);
            result.budget = report;
            return result;
        }

        if (!bundle_.registered || problem.ruleId != bundle_.id) {
            return makeFailure(
                SolveKind::UnsupportedInput,
                horizon,
                "problem rule_id is not this registered bundle",
                start);
        }
        if (budget.totalTimeMs <= 0 || budget.totalTimeMs > 15000) {
            return makeFailure(
                SolveKind::InvalidInput,
                horizon,
                "total_time must be 1..15000 ms",
                start);
        }
        if (horizon < 0) {
            return makeFailure(
                SolveKind::InvalidInput,
                horizon,
                "negative H_limit",
                start);
        }

        const std::string validationError = ExactReplay::validateProblem(bundle_, problem, horizon);
        if (!validationError.empty()) {
            return makeFailure(SolveKind::UnsupportedInput, horizon, validationError, start);
        }

        if (problem.s0.players[1].hp == 0) {
            SolveResult result;
            result.kind = SolveKind::Win;
            result.horizon = horizon;
            result.firstWinningTurn = 0;
            result.replay = ExactReplay::replay(bundle_, problem, {}, true, &budget, &report);
            stampFirstWin(report, start);
            result.budget = report;
            bindProblemKey(result, problem);
            stampElapsed(result.budget, start);
            return result;
        }

        bool trivialFalse = false;
        const CheckResult trivialCheck = ProofKernel::checkTrivialFalse(
            bundle_,
            problem,
            horizon,
            trivialFalse);
        if (!trivialCheck.accepted) {
            return makeFailure(SolveKind::UnsupportedInput, horizon, trivialCheck.reason, start);
        }
        if (trivialFalse) {
            SolveResult result = makeFailure(
                SolveKind::ProvedFalse,
                horizon,
                problem.s0.players[0].hp == 0
                    ? "kernel checked live enemy with dead hero"
                    : "kernel checked live enemy with zero remaining turns",
                start);
            result.budget = report;
            bindProblemKey(result, problem);
            stampElapsed(result.budget, start);
            return result;
        }

        if (static_cast<int>(candidateHint.size()) > horizon) {
            return makeFailure(
                SolveKind::InvalidInput,
                horizon,
                "candidate hint exceeds H_limit",
                start);
        }
        std::optional<std::pair<std::vector<int>, CandidateFailureKind>> rejectedHint;
        if (!candidateHint.empty()) {
            if (report.candidates >= budget.maxCandidates) {
                SolveResult result = makeFailure(
                    SolveKind::Unknown,
                    horizon,
                    "candidate limit reached before candidate hint replay",
                    start);
                result.budget = report;
                stampElapsed(result.budget, start);
                return result;
            }
            ReplayResult candidateReplay = ExactReplay::replay(
                bundle_, problem, candidateHint, false, &budget, &report);
            ++report.candidates;
            if (candidateReplay.interrupted) {
                releaseReplayBytes(report, candidateReplay);
                SolveResult result = makeFailure(
                    SolveKind::Unknown,
                    horizon,
                    candidateReplay.reason,
                    start);
                result.budget = report;
                stampElapsed(result.budget, start);
                return result;
            }
            if (!candidateReplay.supported) {
                releaseReplayBytes(report, candidateReplay);
                SolveResult result = makeFailure(SolveKind::ModelError, horizon, candidateReplay.reason, start);
                result.budget = report;
                stampElapsed(result.budget, start);
                return result;
            }

            if (candidateReplay.valid && candidateReplay.won && candidateReplay.firstWinningTurn <= horizon) {
                SolveResult result;
                result.kind = SolveKind::Win;
                result.horizon = horizon;
                result.firstWinningTurn = candidateReplay.firstWinningTurn;
                result.commands.assign(
                    candidateHint.begin(),
                    candidateHint.begin() + candidateReplay.firstWinningTurn);
                result.replay = std::move(candidateReplay);
                stampFirstWin(report, start);
                result.budget = report;
                bindProblemKey(result, problem);
                stampElapsed(result.budget, start);
                return result;
            }
            CandidateFailureKind hintFailure = CandidateFailureKind::NotWon;
            if (!candidateReplay.valid) {
                hintFailure = CandidateFailureKind::IllegalCommand;
            } else if (candidateReplay.lost) {
                hintFailure = CandidateFailureKind::Lost;
            }
            rejectedHint = std::pair{candidateHint, hintFailure};
            releaseReplayBytes(report, candidateReplay);
            if (deadlineReached(start, budget, report)) {
                SolveResult result = makeFailure(
                    SolveKind::Unknown,
                    horizon,
                    "candidate hint replay exhausted total_time",
                    start);
                result.budget = report;
                stampElapsed(result.budget, start);
                return result;
            }
        }

        std::string familyError;
        PartitionFamily family = ProofKernel::makeInitialFamily(
            bundle_,
            problem.s0,
            horizon,
            budget.maxLeavesPerPosition,
            familyError);
        if (!familyError.empty()) {
            return makeFailure(SolveKind::UnsupportedInput, horizon, familyError, start);
        }

        std::vector<ProofTemplate> &proofTemplates = checkedCache.templates;
        std::uint64_t coverageVersion = 1;

        FalseCheckResult falseCheck = ProofKernel::tryFalseZeroPrice(
            bundle_,
            problem,
            horizon,
            family,
            budget,
            report,
            &proofTemplates,
            coverageVersion);
        if (deadlineReached(start, budget, report)) {
            SolveResult result = makeFailure(
                SolveKind::Unknown,
                horizon,
                "checked snapshot / zero-price verification exhausted total_time",
                start);
            result.budget = report;
            stampElapsed(result.budget, start);
            return result;
        }
        if (!falseCheck.check.accepted) {
            SolveResult result;
            result.kind = isBudgetFailure(falseCheck.check.reason)
                ? SolveKind::Unknown
                : SolveKind::ModelError;
            result.horizon = horizon;
            result.reason = falseCheck.check.reason;
            result.budget = report;
            stampElapsed(result.budget, start);
            return result;
        }
        if (falseCheck.provedFalse) {
            SolveResult result;
            result.kind = SolveKind::ProvedFalse;
            result.horizon = horizon;
            result.reason = "independently verified max-plus FALSE certificate";
            result.provedFalseNoAbstractSuccessPath =
                falseCheck.certificate.rootBound.isNegativeInfinity();
            result.provedFalseRootBound = falseCheck.certificate.rootBound.finite;
            result.provedFalseDelta = falseCheck.certificate.delta;
            result.partitionVersion = falseCheck.certificate.partitions.partitionVersion;
            result.coverageVersion = falseCheck.certificate.coverageVersion;
            result.budget = report;
            bindProblemKey(result, problem);
            stampElapsed(result.budget, start);
            return result;
        }

        // The checked snapshot is the owner of the current partition family.
        // The initial builder input is no longer needed after rebuildSupport
        // has produced that snapshot; keeping both deep copies would make
        // transactional repair peaks count memory that is not part of the
        // snapshot model itself.
        family = {};

        if (const CheckResult cacheUpdate = ProofKernel::rememberCheckedSnapshot(
                checkedCache,
                problem,
                falseCheck.snapshot,
                budget,
                report);
            !cacheUpdate.accepted) {
            SolveResult result = makeFailure(
                isBudgetFailure(cacheUpdate.reason) ? SolveKind::Unknown : SolveKind::ModelError,
                horizon,
                cacheUpdate.reason,
                start);
            result.budget = report;
            return result;
        }
        // This snapshot has already had its one zero-price FALSE attempt.  If
        // it was not FALSE, candidate generation and mismatch repair use the
        // checked coverage/model, not the submitted proof-record copies.
        // Stronger reusable proofs were just retained in checkedCache, while
        // default TurnEntry proofs are reconstructible from the RuleBundle.
        releaseSearchSnapshotCertificateRecords(report, falseCheck.snapshot);
        std::cerr << "TRACE_WORK snapshot=" << report.work
                  << " bytes=" << report.bytes
                  << " snapshot_bytes=" << accountedSnapshotBytes(falseCheck.snapshot)
                  << '\n';
        GoalDistances distances = buildGoalDistances(falseCheck.snapshot, horizon, budget, report);
        if (!distances.accepted) {
            releaseSearchBytes(report, distances.chargedBytes);
            SolveResult result;
            result.kind = isBudgetFailure(distances.reason) ? SolveKind::Unknown : SolveKind::ModelError;
            result.horizon = horizon;
            result.reason = distances.reason;
            result.partitionVersion = falseCheck.snapshot.partitions.partitionVersion;
            result.coverageVersion = falseCheck.snapshot.coverageVersion;
            result.budget = report;
            stampElapsed(result.budget, start);
            return result;
        }
        if (deadlineReached(start, budget, report)) {
            SolveResult result = makeFailure(
                SolveKind::Unknown,
                horizon,
                "goal-distance construction exhausted total_time",
                start);
            result.partitionVersion = falseCheck.snapshot.partitions.partitionVersion;
            result.coverageVersion = falseCheck.snapshot.coverageVersion;
            result.budget = report;
            stampElapsed(result.budget, start);
            return result;
        }
        std::cerr << "TRACE_WORK distances=" << report.work
                  << " bytes=" << report.bytes
                  << " charged=" << distances.chargedBytes
                  << '\n';

        CommandSequenceTrie commandSequences(
            bundle_.profile.heroCommands.size(),
            budget,
            report);
        if (!commandSequences.ready()) {
            return makeFailure(
                isBudgetFailure(commandSequences.error()) ? SolveKind::Unknown : SolveKind::ModelError,
                horizon,
                commandSequences.error(),
                start);
        }
        auto iterator = std::make_unique<GoalCandidateIterator>(
            bundle_, falseCheck.snapshot, distances, commandSequences, horizon, budget, report);
        std::cerr << "TRACE_WORK iterator=" << report.work
                  << " bytes=" << report.bytes
                  << '\n';
        if (rejectedHint.has_value()) {
            std::size_t sequenceId = 0;
            bool representable = true;
            for (int command: rejectedHint->first) {
                const int slot = commandOrder(bundle_, command);
                if (slot == std::numeric_limits<int>::max()) {
                    representable = false;
                    break;
                }
                std::size_t childId = 0;
                if (!commandSequences.extend(sequenceId, slot, childId)) {
                    return makeFailure(
                        isBudgetFailure(commandSequences.error()) ? SolveKind::Unknown : SolveKind::ModelError,
                        horizon,
                        commandSequences.error(),
                        start);
                }
                sequenceId = childId;
            }
            if (representable &&
                !commandSequences.rememberFailure(
                    sequenceId,
                    rejectedHint->first,
                    rejectedHint->second)) {
                return makeFailure(
                    isBudgetFailure(commandSequences.error()) ? SolveKind::Unknown : SolveKind::ModelError,
                    horizon,
                    commandSequences.error(),
                    start);
            }
        }
        std::vector<CandidateMismatch> failures;
        std::uint64_t failureBatchBytes = 0;
        std::uint32_t trialOrder = 0;
        std::uint32_t failedCandidatesInBatch = rejectedHint.has_value() ? 1u : 0u;



        auto makeCurrentFailure = [&](SolveKind kind, std::string reason) {
            SolveResult result = makeFailure(kind, horizon, std::move(reason), start);
            result.partitionVersion = falseCheck.snapshot.partitions.partitionVersion;
            result.coverageVersion = falseCheck.snapshot.coverageVersion;
            result.budget = report;
            stampElapsed(result.budget, start);
            return result;
        };

        auto checkedCellDomain = [&](const CellKey &source, Box &domain, std::string &error) -> bool {
            const PredicatePartition *partition = partitionAt(falseCheck.snapshot.partitions, source.rngPosition);
            if (partition == nullptr) {
                error = "repair source partition is missing";
                return false;
            }
            const CheckedPartition checked = ProofKernel::validatePartition(
                *partition,
                ProofKernel::baseBox(bundle_, problem.s0),
                budget.maxLeavesPerPosition);
            if (!checked.check.accepted) {
                error = "repair source partition is invalid: " + checked.check.reason;
                return false;
            }
            for (const auto &[leafId, leafDomain]: checked.leaves) {
                if (leafId == source.localCellId) {
                    domain = leafDomain;
                    return true;
                }
            }
            error = "repair source local cell is missing";
            return false;
        };

        auto templateRank = [](const ProofTemplate &value) {
            if (value.kind == RootProofKind::FullyDetailed) {
                return std::uint64_t{1} << 62;
            }
            if (value.kind == RootProofKind::Refined) {
                return (std::uint64_t{1} << 61) + value.expandedCompletions.size();
            }
            switch (value.cut) {
                case CompletionCutId::TurnEntry:
                    return std::uint64_t{0};
                case CompletionCutId::AllyDoneEnemyPending:
                case CompletionCutId::EnemyDoneAllyPending:
                    return std::uint64_t{10};
                case CompletionCutId::ActionsDone:
                    return std::uint64_t{20};
            }
            return std::uint64_t{0};
        };

        auto repairProposalTooLarge = [](const std::string &reason) {
            return reason.find("detailed term limit J exceeded") != std::string::npos ||
                   reason.find("completion term limit C exceeded") != std::string::npos ||
                   reason.find("partition leaf limit exceeded") != std::string::npos;
        };

        bool repairSnapshotPrepared = false;
        struct RejectedCompletionRepair {
            std::uint64_t partitionVersion = 0;
            std::uint64_t coverageVersion = 0;
            int elapsedTurn = 0;
            std::size_t modelTermNumber = 0;
        };
        std::vector<RejectedCompletionRepair> rejectedCompletionRepairs;
        std::uint64_t rejectedCompletionRepairBytes = 0;
        std::uint64_t diagnosticNoCheckpoint = 0;
        std::uint64_t diagnosticAdvanceRejected = 0;
        std::uint64_t diagnosticNoStrongerRank = 0;
        std::uint64_t diagnosticProposalTooLarge = 0;
        std::uint64_t diagnosticAlreadyRejectedCompletion = 0;
        std::uint64_t diagnosticCacheUnchanged = 0;
        std::uint64_t diagnosticRepairBudgetFailure = 0;
        std::uint64_t diagnosticGuardNoPredicate = 0;
        std::uint64_t diagnosticRefinementRejected = 0;
        std::uint64_t diagnosticGuardProposalTooLarge = 0;
        std::uint64_t diagnosticNoMismatch = 0;
        std::string diagnosticLastAdvanceReason;
        std::string diagnosticLastProposalTooLargeReason;
        std::string diagnosticLastGuardReason;
        std::string diagnosticLastRepairBudgetReason;
        std::string diagnosticLastRepairBudgetIdentity;

        auto completionRepairWasRejected = [&](const CandidateMismatch &failure) -> std::optional<bool> {
            for (const RejectedCompletionRepair &rejected: rejectedCompletionRepairs) {
                if (!chargeSearchBudget(report, budget, 1, 0)) {
                    return std::nullopt;
                }
                if (rejected.partitionVersion == failure.partitionVersion &&
                    rejected.coverageVersion == failure.coverageVersion &&
                    rejected.elapsedTurn == failure.elapsedTurn &&
                    rejected.modelTermNumber == failure.step.modelTermNumber) {
                    return true;
                }
            }
            if (report.candidates <= 96) {
                std::cerr << "TRACE_REPAIR_NONE candidates=" << report.candidates
                          << " failures=" << failures.size()
                          << " no_checkpoint=" << diagnosticNoCheckpoint
                          << " advance_rejected=" << diagnosticAdvanceRejected
                          << " no_stronger_rank=" << diagnosticNoStrongerRank
                          << " proposal_too_large=" << diagnosticProposalTooLarge
                          << " already_rejected=" << diagnosticAlreadyRejectedCompletion
                          << " cache_unchanged=" << diagnosticCacheUnchanged
                          << " repair_budget=" << diagnosticRepairBudgetFailure
                          << " guard_no_pred=" << diagnosticGuardNoPredicate
                          << " refine_rejected=" << diagnosticRefinementRejected
                          << " guard_too_large=" << diagnosticGuardProposalTooLarge
                          << " no_mismatch=" << diagnosticNoMismatch
                          << " last_proposal=" << diagnosticLastProposalTooLargeReason
                          << " last_advance=" << diagnosticLastAdvanceReason
                          << " last_repair_budget=" << diagnosticLastRepairBudgetReason
                          << " last_repair_identity=" << diagnosticLastRepairBudgetIdentity
                          << '\n';
            }
            return false;
        };

        auto rememberRejectedCompletionRepair = [&](const CandidateMismatch &failure) -> bool {
            if (!chargeSearchBudget(
                    report,
                    budget,
                    1,
                    sizeof(RejectedCompletionRepair))) {
                return false;
            }
            rejectedCompletionRepairs.push_back({
                failure.partitionVersion,
                failure.coverageVersion,
                failure.elapsedTurn,
                failure.step.modelTermNumber,
            });
            rejectedCompletionRepairBytes += sizeof(RejectedCompletionRepair);
            return true;
        };

        auto tryOneRepair = [&]() -> std::optional<bool> {
            if (failures.empty() || report.repairs >= budget.maxRepairs) {
                return false;
            }
            std::stable_sort(
                failures.begin(),
                failures.end(),
                [](const CandidateMismatch &a, const CandidateMismatch &b) {
                    return std::tuple{a.elapsedTurn, a.trialOrder} <
                           std::tuple{b.elapsedTurn, b.trialOrder};
                });

            for (const CandidateMismatch &failure: failures) {
                if (failure.partitionVersion != falseCheck.snapshot.partitions.partitionVersion ||
                    failure.coverageVersion != falseCheck.snapshot.coverageVersion) {
                    return std::nullopt;
                }
                const CheckedEdge *edge = edgeForStep(falseCheck.snapshot, failure.step);
                if (edge == nullptr) {
                    return std::nullopt;
                }


                if (failure.kind == CandidateMismatchKind::Completion) {
                    const std::optional<bool> alreadyRejected = completionRepairWasRejected(failure);
                    if (!alreadyRejected.has_value()) {
                        return false;
                    }
                    if (*alreadyRejected) {
                        ++diagnosticAlreadyRejectedCompletion;
                        continue;
                    }
                    const CompletionCheckpoint *checkpoint = nullptr;
                    std::optional<CompletionCheckpoint> reconstructedTurnEntry;
                    if (edge->completionCut == CompletionCutId::TurnEntry &&
                        edge->completionMembers.empty()) {
                        SymbolicStepper stepper(bundle_, problem);
                        reconstructedTurnEntry = CompletionCheckpoint{
                            failure.elapsedTurn,
                            failure.step.source,
                            failure.step.selectedCommand,
                            CompletionCutId::TurnEntry,
                            edge->rootDomain,
                            stepper.makeRootFrame(
                                failure.elapsedTurn,
                                failure.step.selectedCommand,
                                failure.step.source.rngPosition,
                                edge->rootDomain),
                        };
                        checkpoint = &*reconstructedTurnEntry;
                    } else {
                        const CompletionModelMember *member = nullptr;
                        for (const CompletionModelMember &candidateMember: edge->completionMembers) {
                            const bool suppliesChoice = failure.step.goal
                                ? candidateMember.mayReachGoal
                                : candidateMember.hasContinuingOutput &&
                                  failure.step.target.rngPosition >= candidateMember.firstOutputPosition &&
                                  failure.step.target.rngPosition <= candidateMember.lastOutputPosition;
                            if (suppliesChoice) {
                                member = &candidateMember;
                                break;
                            }
                        }
                        if (member == nullptr) {
                            ++diagnosticNoCheckpoint;
                            continue;
                        }
                        std::size_t matchingCheckpoints = 0;
                        for (const CompletionCheckpoint &candidate: falseCheck.snapshot.completionCheckpoints) {
                            if (candidate.elapsedTurn != failure.elapsedTurn ||
                                !(candidate.source == failure.step.source) ||
                                candidate.selectedCommand != failure.step.selectedCommand ||
                                candidate.cut != member->cut ||
                                !(candidate.frame.inputDomain == member->inputDomain)) {
                                continue;
                            }
                            checkpoint = &candidate;
                            ++matchingCheckpoints;
                        }
                        if (matchingCheckpoints > 1) {
                            return std::nullopt;
                        }
                    }
                    if (checkpoint == nullptr) {
                        ++diagnosticNoCheckpoint;
                        continue;
                    }

                    ProofTemplate advanced;
                    const CheckResult advance = ProofKernel::advanceCompletionCheckpoint(
                        bundle_, problem, *checkpoint, budget, report, advanced);
                    if (!advance.accepted) {
                        if (isBudgetFailure(advance.reason)) {
                            ++diagnosticRepairBudgetFailure;
                            diagnosticLastRepairBudgetReason = advance.reason;
                            return false;
                        }
                        ++diagnosticAdvanceRejected;
                        diagnosticLastAdvanceReason = advance.reason;
                        continue;
                    }
                    ProofTemplate current;
                    current.elapsedTurn = failure.elapsedTurn;
                    current.rngPosition = failure.step.source.rngPosition;
                    current.selectedCommand = failure.step.selectedCommand;
                    current.coveredDomain = checkpoint->rootDomain;
                    current.kind = RootProofKind::Completion;
                    current.cut = checkpoint->cut;
                    if (templateRank(advanced) <= templateRank(current)) {
                        ++diagnosticNoStrongerRank;
                        continue;
                    }

                    if (coverageVersion == std::numeric_limits<std::uint64_t>::max()) {
                        return std::nullopt;
                    }
                    const std::uint64_t trialCoverageVersion = coverageVersion + 1;
                    const std::uint64_t retainedBytesBeforeTrial = report.bytes;
                    const std::uint64_t oldSnapshotBytes = accountedSnapshotBytes(falseCheck.snapshot);
                    BudgetReport trialBudget = report;

                    // A completion repair strengthens one checked template for
                    // the same (turn, RNG position, command, covered domain).
                    // Do not clone the entire checked cache just to make this
                    // one transactional edit: retain the exact old template's
                    // slot (or the insertion slot) and roll that single edit
                    // back if rebuilding Support fails.
                    const auto templateKey = [](const ProofTemplate &value) {
                        return std::tuple{
                            value.elapsedTurn,
                            value.rngPosition,
                            value.selectedCommand};
                    };
                    const auto currentKey = templateKey(current);
                    std::size_t lo = 0;
                    std::size_t hi = checkedCache.templates.size();
                    std::uint64_t rollbackLookupWork = 0;
                    while (lo < hi) {
                        ++rollbackLookupWork;
                        const std::size_t mid = lo + (hi - lo) / 2;
                        if (templateKey(checkedCache.templates[mid]) < currentKey) {
                            lo = mid + 1;
                        } else {
                            hi = mid;
                        }
                    }
                    std::size_t insertAt = lo;
                    std::optional<std::size_t> replacedIndex;
                    std::optional<ProofTemplate> replacedTemplateBefore;
                    while (insertAt < checkedCache.templates.size() &&
                           templateKey(checkedCache.templates[insertAt]) == currentKey) {
                        ++rollbackLookupWork;
                        const ProofTemplate &existingTemplate = checkedCache.templates[insertAt];
                        const bool exactDomainReplacement =
                            existingTemplate.coveredDomain == current.coveredDomain;
                        const bool refinedContainingReplacement =
                            advanced.kind == RootProofKind::Refined &&
                            existingTemplate.kind == RootProofKind::Refined &&
                            contains(existingTemplate.coveredDomain, advanced.coveredDomain);
                        if (!replacedIndex.has_value() &&
                            (exactDomainReplacement || refinedContainingReplacement)) {
                            replacedIndex = insertAt;
                            replacedTemplateBefore = existingTemplate;
                        }
                        ++insertAt;
                    }
                    if (!chargeSearchBudget(trialBudget, budget, rollbackLookupWork, 0)) {
                        absorbDiscardedTrial(report, trialBudget, retainedBytesBeforeTrial);
                        ++diagnosticRepairBudgetFailure;
                        diagnosticLastRepairBudgetReason = "completion rollback lookup budget";
                        return false;
                    }
                    const std::size_t cacheSizeBefore = checkedCache.templates.size();
                    bool cacheChanged = false;
                    const CheckResult cacheUpdate = ProofKernel::rememberCheckedTemplate(
                        checkedCache,
                        problem,
                        advanced,
                        budget,
                        trialBudget,
                        cacheChanged);
                    if (!cacheUpdate.accepted) {
                        absorbDiscardedTrial(report, trialBudget, retainedBytesBeforeTrial);
                        if (isBudgetFailure(cacheUpdate.reason)) {
                            ++diagnosticRepairBudgetFailure;
                            diagnosticLastRepairBudgetReason = cacheUpdate.reason;
                            return false;
                        }
                        return std::nullopt;
                    }
                    if (!cacheChanged) {
                        absorbDiscardedTrial(report, trialBudget, retainedBytesBeforeTrial);
                        ++diagnosticCacheUnchanged;
                        continue;
                    }

                    const bool insertedTemplate = checkedCache.templates.size() == cacheSizeBefore + 1;
                    const bool replacedTemplate = checkedCache.templates.size() == cacheSizeBefore && replacedIndex.has_value();
                    if (!insertedTemplate && !replacedTemplate) {
                        absorbDiscardedTrial(report, trialBudget, retainedBytesBeforeTrial);
                        return std::nullopt;
                    }

                    auto rollbackTemplate = [&]() {
                        if (insertedTemplate) {
                            if (insertAt < checkedCache.templates.size()) {
                                checkedCache.templates.erase(
                                    checkedCache.templates.begin() + static_cast<std::ptrdiff_t>(insertAt));
                            }
                        } else if (replacedIndex.has_value() && replacedTemplateBefore.has_value() &&
                                   *replacedIndex < checkedCache.templates.size()) {
                            checkedCache.templates[*replacedIndex] = *replacedTemplateBefore;
                        }
                    };

                    CheckedSnapshot trialSnapshot = ProofKernel::rebuildSupport(
                        bundle_,
                        problem,
                        horizon,
                        falseCheck.snapshot.partitions,
                        budget,
                        trialBudget,
                        nullptr,
                        &checkedCache.templates,
                        trialCoverageVersion,
                        false,
                        false,
                        &falseCheck.snapshot);
                    if (!trialSnapshot.check.accepted) {
                        rollbackTemplate();
                        absorbDiscardedTrial(report, trialBudget, retainedBytesBeforeTrial);
                        if (repairProposalTooLarge(trialSnapshot.check.reason)) {
                            ++diagnosticProposalTooLarge;
                            diagnosticLastProposalTooLargeReason =
                                trialSnapshot.check.reason +
                                "; turn=" + std::to_string(failure.elapsedTurn) +
                                ", command=" + std::to_string(failure.step.selectedCommand) +
                                ", cut=" + std::to_string(static_cast<int>(edge->completionCut)) +
                                ", term=" + std::to_string(failure.step.modelTermNumber);
                            if (!rememberRejectedCompletionRepair(failure)) {
                                return false;
                            }
                            continue;
                        }
                        if (isBudgetFailure(trialSnapshot.check.reason)) {
                            ++diagnosticRepairBudgetFailure;
                            diagnosticLastRepairBudgetReason = trialSnapshot.check.reason;
                            diagnosticLastRepairBudgetIdentity =
                                "completion turn=" + std::to_string(failure.elapsedTurn) +
                                ",p=" + std::to_string(failure.step.source.rngPosition) +
                                ",cell=" + std::to_string(failure.step.source.localCellId) +
                                ",command=" + std::to_string(failure.step.selectedCommand) +
                                ",term=" + std::to_string(failure.step.modelTermNumber);
                            if (trialSnapshot.check.reason ==
                                "Support materialization exceeded proof budget") {
                                if (!rememberRejectedCompletionRepair(failure)) {
                                    return false;
                                }
                                continue;
                            }
                            return false;
                        }
                        return std::nullopt;
                    }

                    const std::uint64_t retainedPartitionBytes =
                        accountedPartitionFamilyBytes(falseCheck.snapshot.partitions);
                    trialSnapshot.partitions = std::move(falseCheck.snapshot.partitions);
                    report = trialBudget;
                    releaseSearchBytes(report, oldSnapshotBytes - retainedPartitionBytes);
                    falseCheck = {};
                    falseCheck.check.accepted = true;
                    falseCheck.snapshot = std::move(trialSnapshot);
                    coverageVersion = trialCoverageVersion;
                    repairSnapshotPrepared = true;
                    releaseSearchBytes(report, rejectedCompletionRepairBytes);
                    rejectedCompletionRepairs.clear();
                    rejectedCompletionRepairBytes = 0;
                    ++report.completionResumes;
                    ++report.repairs;
                    return true;
                }

                if (failure.kind == CandidateMismatchKind::Guard) {
                    Box cell;
                    std::string repairError;
                    if (!checkedCellDomain(failure.step.source, cell, repairError)) {
                        return std::nullopt;
                    }
                    Predicate predicate;
                    if (!firstSeparatingPredicate(
                            cell,
                            edge->rootDomain,
                            failure.stateBefore,
                            predicate,
                            repairError)) {
                        ++diagnosticGuardNoPredicate;
                        diagnosticLastGuardReason = repairError;
                        continue;
                    }
                    const std::uint64_t retainedBytesBeforeTrial = report.bytes;
                    const std::uint64_t refinedWorkingBytes =
                        accountedPartitionFamilyBytes(falseCheck.snapshot.partitions) + 2 * sizeof(PartitionNode);
                    std::uint64_t refinementWork = falseCheck.snapshot.partitions.trees.size() + 1;
                    for (const PredicatePartition &tree: falseCheck.snapshot.partitions.trees) {
                        refinementWork += tree.nodes.size();
                    }
                    if (!chargeSearchBudget(
                            report,
                            budget,
                            refinementWork,
                            refinedWorkingBytes)) {
                        ++diagnosticRepairBudgetFailure;
                        diagnosticLastRepairBudgetReason = "guard refinement working budget";
                        return false;
                    }
                    PartitionFamily refined;
                    const CheckResult refinement = ProofKernel::refineLeaf(
                        falseCheck.snapshot.partitions,
                        ProofKernel::baseBox(bundle_, problem.s0),
                        failure.step.source.rngPosition,
                        failure.step.source.localCellId,
                        predicate,
                        budget.maxLeavesPerPosition,
                        refined);
                    if (!refinement.accepted) {
                        releaseSearchBytes(report, refinedWorkingBytes);
                        ++diagnosticRefinementRejected;
                        diagnosticLastGuardReason = refinement.reason;
                        continue;
                    }

                    if (coverageVersion == std::numeric_limits<std::uint64_t>::max()) {
                        releaseSearchBytes(report, refinedWorkingBytes);
                        return std::nullopt;
                    }
                    const std::uint64_t trialCoverageVersion = coverageVersion + 1;
                    const std::uint64_t oldSnapshotBytes = accountedSnapshotBytes(falseCheck.snapshot);
                    BudgetReport trialBudget = report;
                    CheckedSnapshot trialSnapshot = ProofKernel::rebuildSupport(
                        bundle_,
                        problem,
                        horizon,
                        refined,
                        budget,
                        trialBudget,
                        nullptr,
                        &checkedCache.templates,
                        trialCoverageVersion,
                        false,
                        false,
                        &falseCheck.snapshot);
                    if (!trialSnapshot.check.accepted) {
                        absorbDiscardedTrial(report, trialBudget, retainedBytesBeforeTrial);
                        if (repairProposalTooLarge(trialSnapshot.check.reason)) {
                            ++diagnosticGuardProposalTooLarge;
                            diagnosticLastGuardReason = trialSnapshot.check.reason;
                            continue;
                        }
                        if (isBudgetFailure(trialSnapshot.check.reason)) {
                            ++diagnosticRepairBudgetFailure;
                            diagnosticLastRepairBudgetReason = trialSnapshot.check.reason;
                            diagnosticLastRepairBudgetIdentity =
                                "guard turn=" + std::to_string(failure.elapsedTurn) +
                                ",p=" + std::to_string(failure.step.source.rngPosition) +
                                ",cell=" + std::to_string(failure.step.source.localCellId) +
                                ",command=" + std::to_string(failure.step.selectedCommand) +
                                ",term=" + std::to_string(failure.step.modelTermNumber);
                            return false;
                        }
                        return std::nullopt;
                    }

                    trialSnapshot.partitions = std::move(refined);
                    report = trialBudget;
                    releaseSearchBytes(report, oldSnapshotBytes);
                    falseCheck = {};
                    falseCheck.check.accepted = true;
                    falseCheck.snapshot = std::move(trialSnapshot);
                    coverageVersion = trialCoverageVersion;
                    repairSnapshotPrepared = true;
                    releaseSearchBytes(report, rejectedCompletionRepairBytes);
                    rejectedCompletionRepairs.clear();
                    rejectedCompletionRepairBytes = 0;
                    ++report.addedPredicates;
                    ++report.repairs;
                    return true;
                }
            }
            return false;
        };

        auto rebuildAfterRepair = [&]() -> std::optional<SolveResult> {
            if (!repairSnapshotPrepared) {
                if (coverageVersion == std::numeric_limits<std::uint64_t>::max()) {
                    return makeCurrentFailure(SolveKind::ModelError, "coverage_version overflow");
                }
                ++coverageVersion;
                falseCheck = ProofKernel::tryFalseZeroPrice(
                    bundle_,
                    problem,
                    horizon,
                    falseCheck.snapshot.partitions,
                    budget,
                    report,
                    &proofTemplates,
                    coverageVersion);
                if (deadlineReached(start, budget, report)) {
                    return makeCurrentFailure(
                        SolveKind::Unknown,
                        "repair rebuild exhausted total_time");
                }
                if (!falseCheck.check.accepted) {
                    return makeCurrentFailure(
                        isBudgetFailure(falseCheck.check.reason) ? SolveKind::Unknown : SolveKind::ModelError,
                        falseCheck.check.reason);
                }
            } else {
                // A successful repair has already rebuilt Support/model for a
                // fresh immutable snapshot.  Section 13 then performs that
                // snapshot's at-most-once Q=256, zero-resource-price FALSE
                // attempt before rebuilding L and the candidate iterator.
                falseCheck = ProofKernel::tryFalseZeroPriceOnSnapshot(
                    bundle_,
                    problem,
                    horizon,
                    std::move(falseCheck.snapshot),
                    budget,
                    report);
                if (!falseCheck.check.accepted) {
                    return makeCurrentFailure(
                        isBudgetFailure(falseCheck.check.reason) ? SolveKind::Unknown : SolveKind::ModelError,
                        falseCheck.check.reason);
                }
            }
            repairSnapshotPrepared = false;
            if (falseCheck.provedFalse) {
                SolveResult result;
                result.kind = SolveKind::ProvedFalse;
                result.horizon = horizon;
                result.reason = "independently verified max-plus FALSE certificate after repair";
                result.provedFalseNoAbstractSuccessPath =
                    falseCheck.certificate.rootBound.isNegativeInfinity();
                result.provedFalseRootBound = falseCheck.certificate.rootBound.finite;
                result.provedFalseDelta = falseCheck.certificate.delta;
                result.partitionVersion = falseCheck.certificate.partitions.partitionVersion;
                result.coverageVersion = falseCheck.certificate.coverageVersion;
                result.budget = report;
                bindProblemKey(result, problem);
                stampElapsed(result.budget, start);
                return result;
            }
            if (const CheckResult cacheUpdate = ProofKernel::rememberCheckedSnapshot(
                    checkedCache,
                    problem,
                    falseCheck.snapshot,
                    budget,
                    report);
                !cacheUpdate.accepted) {
                return makeCurrentFailure(
                    isBudgetFailure(cacheUpdate.reason) ? SolveKind::Unknown : SolveKind::ModelError,
                    cacheUpdate.reason);
            }
            releaseSearchSnapshotCertificateRecords(report, falseCheck.snapshot);

            GoalDistances rebuiltDistances = buildGoalDistances(
                falseCheck.snapshot,
                horizon,
                budget,
                report);
            if (!rebuiltDistances.accepted) {
                releaseSearchBytes(report, rebuiltDistances.chargedBytes);
                return makeCurrentFailure(
                    isBudgetFailure(rebuiltDistances.reason) ? SolveKind::Unknown : SolveKind::ModelError,
                    rebuiltDistances.reason);
            }
            if (deadlineReached(start, budget, report)) {
                return makeCurrentFailure(
                    SolveKind::Unknown,
                    "repair goal-distance rebuild exhausted total_time");
            }
            iterator.reset();
            releaseSearchBytes(report, distances.chargedBytes);
            distances = std::move(rebuiltDistances);
            iterator = std::make_unique<GoalCandidateIterator>(
                bundle_, falseCheck.snapshot, distances, commandSequences, horizon, budget, report);
            failures.clear();
            releaseSearchBytes(report, failureBatchBytes);
            failureBatchBytes = 0;
            failedCandidatesInBatch = 0;
            return std::nullopt;
        };

        while (true) {
            if (deadlineReached(start, budget, report)) {
                SolveResult result = makeFailure(
                    SolveKind::Unknown,
                    horizon,
                    "candidate search exhausted total_time",
                    start);
                result.partitionVersion = falseCheck.snapshot.partitions.partitionVersion;
                result.coverageVersion = falseCheck.snapshot.coverageVersion;
                result.budget = report;
                stampElapsed(result.budget, start);
                return result;
            }
            if (report.candidates >= budget.maxCandidates) {
                SolveResult result = makeFailure(
                    SolveKind::Unknown,
                    horizon,
                    "candidate limit reached without checked witness" +
                        std::string("; repair_diag no_checkpoint=") +
                        std::to_string(diagnosticNoCheckpoint) +
                        " advance_rejected=" + std::to_string(diagnosticAdvanceRejected) +
                        " no_stronger_rank=" + std::to_string(diagnosticNoStrongerRank) +
                        " proposal_too_large=" + std::to_string(diagnosticProposalTooLarge) +
                        " already_rejected_completion=" + std::to_string(diagnosticAlreadyRejectedCompletion) +
                        " cache_unchanged=" + std::to_string(diagnosticCacheUnchanged) +
                        " repair_budget_failure=" + std::to_string(diagnosticRepairBudgetFailure) +
                        " guard_no_predicate=" + std::to_string(diagnosticGuardNoPredicate) +
                        " refinement_rejected=" + std::to_string(diagnosticRefinementRejected) +
                        " guard_proposal_too_large=" + std::to_string(diagnosticGuardProposalTooLarge) +
                        " no_mismatch=" + std::to_string(diagnosticNoMismatch) +
                        (diagnosticLastAdvanceReason.empty()
                             ? std::string{}
                             : "; last_advance_reason=" + diagnosticLastAdvanceReason) +
                        (diagnosticLastProposalTooLargeReason.empty()
                             ? std::string{}
                             : "; last_proposal_too_large=" + diagnosticLastProposalTooLargeReason) +
                        (diagnosticLastGuardReason.empty()
                             ? std::string{}
                             : "; last_guard_reason=" + diagnosticLastGuardReason) +
                        (diagnosticLastRepairBudgetReason.empty()
                             ? std::string{}
                             : "; last_repair_budget_reason=" + diagnosticLastRepairBudgetReason) +
                        (diagnosticLastRepairBudgetIdentity.empty()
                             ? std::string{}
                             : "; last_repair_budget_identity=" + diagnosticLastRepairBudgetIdentity),
                    start);
                result.partitionVersion = falseCheck.snapshot.partitions.partitionVersion;
                result.coverageVersion = falseCheck.snapshot.coverageVersion;
                result.budget = report;
                stampElapsed(result.budget, start);
                return result;
            }

            CandidatePath path;
            std::string iteratorReason;
            const IteratorStatus iteratorStatus = iterator->next(path, iteratorReason);
            if (iteratorStatus == IteratorStatus::Exhausted) {
                iterator->releaseStaticIndexesForRepair();
                const std::optional<bool> repaired = tryOneRepair();
                if (!repaired.has_value()) {
                    return makeCurrentFailure(
                        SolveKind::ModelError,
                        "repair inspection found an invalid checked edge");
                }
                if (*repaired) {
                    std::cerr << "TRACE_GENERATION repair=" << report.repairs
                              << " phase=snapshot work=" << report.work
                              << " bytes=" << report.bytes << '\n';
                    if (std::optional<SolveResult> result = rebuildAfterRepair()) {
                        return *result;
                    }
                    std::cerr << "TRACE_GENERATION repair=" << report.repairs
                              << " phase=iterator work=" << report.work
                              << " bytes=" << report.bytes << '\n';
                    continue;
                }
                const std::uint64_t exhaustedPartitionVersion =
                    falseCheck.snapshot.partitions.partitionVersion;
                const std::uint64_t exhaustedCoverageVersion = falseCheck.snapshot.coverageVersion;
                iterator.reset();
                releaseSearchBytes(report, distances.chargedBytes);
                distances = {};
                falseCheck = ProofKernel::tryFalseZeroPriceOnSnapshot(
                    bundle_,
                    problem,
                    horizon,
                    std::move(falseCheck.snapshot),
                    budget,
                    report,
                    true);
                if (!falseCheck.check.accepted) {
                    SolveResult result = makeFailure(
                        isBudgetFailure(falseCheck.check.reason) ? SolveKind::Unknown : SolveKind::ModelError,
                        horizon,
                        falseCheck.check.reason,
                        start);
                    result.partitionVersion = exhaustedPartitionVersion;
                    result.coverageVersion = exhaustedCoverageVersion;
                    result.budget = report;
                    stampElapsed(result.budget, start);
                    return result;
                }
                if (falseCheck.provedFalse) {
                    SolveResult result;
                    result.kind = SolveKind::ProvedFalse;
                    result.horizon = horizon;
                    result.reason = "independently verified no-abstract-success-path FALSE certificate";
                    result.provedFalseNoAbstractSuccessPath = true;
                    result.provedFalseRootBound = falseCheck.certificate.rootBound.finite;
                    result.provedFalseDelta = falseCheck.certificate.delta;
                    result.partitionVersion = falseCheck.certificate.partitions.partitionVersion;
                    result.coverageVersion = falseCheck.certificate.coverageVersion;
                    result.budget = report;
                    bindProblemKey(result, problem);
                    stampElapsed(result.budget, start);
                    return result;
                }
                return makeCurrentFailure(
                    SolveKind::Unknown,
                    "candidate iterator exhausted; search exhaustion is not a FALSE proof; repair_diag no_checkpoint=" +
                        std::to_string(diagnosticNoCheckpoint) +
                        " advance_rejected=" + std::to_string(diagnosticAdvanceRejected) +
                        " no_stronger_rank=" + std::to_string(diagnosticNoStrongerRank) +
                        " proposal_too_large=" + std::to_string(diagnosticProposalTooLarge) +
                        " already_rejected_completion=" + std::to_string(diagnosticAlreadyRejectedCompletion) +
                        " cache_unchanged=" + std::to_string(diagnosticCacheUnchanged) +
                        " repair_budget_failure=" + std::to_string(diagnosticRepairBudgetFailure) +
                        " guard_no_predicate=" + std::to_string(diagnosticGuardNoPredicate) +
                        " refinement_rejected=" + std::to_string(diagnosticRefinementRejected) +
                        " guard_proposal_too_large=" + std::to_string(diagnosticGuardProposalTooLarge) +
                        " no_mismatch=" + std::to_string(diagnosticNoMismatch) +
                        (diagnosticLastAdvanceReason.empty()
                             ? std::string{}
                             : "; last_advance_reason=" + diagnosticLastAdvanceReason) +
                        (diagnosticLastProposalTooLargeReason.empty()
                             ? std::string{}
                             : "; last_proposal_too_large=" + diagnosticLastProposalTooLargeReason) +
                        (diagnosticLastGuardReason.empty()
                             ? std::string{}
                             : "; last_guard_reason=" + diagnosticLastGuardReason) +
                        (diagnosticLastRepairBudgetReason.empty()
                             ? std::string{}
                             : "; last_repair_budget_reason=" + diagnosticLastRepairBudgetReason) +
                        (diagnosticLastRepairBudgetIdentity.empty()
                             ? std::string{}
                             : "; last_repair_budget_identity=" + diagnosticLastRepairBudgetIdentity));
            }
            if (iteratorStatus == IteratorStatus::BudgetExceeded) {
                SolveResult result = makeFailure(
                    SolveKind::Unknown,
                    horizon,
                    iteratorReason,
                    start);
                result.partitionVersion = falseCheck.snapshot.partitions.partitionVersion;
                result.coverageVersion = falseCheck.snapshot.coverageVersion;
                result.budget = report;
                stampElapsed(result.budget, start);
                return result;
            }
            if (iteratorStatus == IteratorStatus::ModelError) {
                SolveResult result = makeFailure(
                    SolveKind::ModelError,
                    horizon,
                    iteratorReason,
                    start);
                result.partitionVersion = falseCheck.snapshot.partitions.partitionVersion;
                result.coverageVersion = falseCheck.snapshot.coverageVersion;
                result.budget = report;
                stampElapsed(result.budget, start);
                return result;
            }

            bool duplicateCommand = false;
            if (!commandSequences.failed(path.commandSequenceId, duplicateCommand)) {
                return makeCurrentFailure(
                    isBudgetFailure(commandSequences.error()) ? SolveKind::Unknown : SolveKind::ModelError,
                    commandSequences.error());
            }
            if (duplicateCommand) {
                ++report.duplicateCandidateSkips;
                continue;
            }

            ReplayResult replay = ExactReplay::replay(
                bundle_, problem, path.commands, false, &budget, &report);
            ++report.candidates;
            if (replay.interrupted) {
                releaseReplayBytes(report, replay);
                SolveResult result = makeFailure(
                    SolveKind::Unknown,
                    horizon,
                    replay.reason,
                    start);
                result.partitionVersion = falseCheck.snapshot.partitions.partitionVersion;
                result.coverageVersion = falseCheck.snapshot.coverageVersion;
                result.budget = report;
                stampElapsed(result.budget, start);
                return result;
            }
            if (!replay.supported) {
                releaseReplayBytes(report, replay);
                SolveResult result = makeFailure(
                    SolveKind::ModelError,
                    horizon,
                    replay.reason,
                    start);
                result.partitionVersion = falseCheck.snapshot.partitions.partitionVersion;
                result.coverageVersion = falseCheck.snapshot.coverageVersion;
                result.budget = report;
                stampElapsed(result.budget, start);
                return result;
            }
            if (replay.valid && replay.won && replay.firstWinningTurn <= horizon) {
                SolveResult result;
                result.kind = SolveKind::Win;
                result.horizon = horizon;
                result.firstWinningTurn = replay.firstWinningTurn;
                result.commands.assign(
                    path.commands.begin(),
                    path.commands.begin() + replay.firstWinningTurn);
                replay.checkedCommands = result.commands;
                result.replay = std::move(replay);
                result.partitionVersion = falseCheck.snapshot.partitions.partitionVersion;
                result.coverageVersion = falseCheck.snapshot.coverageVersion;
                stampFirstWin(report, start);
                result.budget = report;
                bindProblemKey(result, problem);
                stampElapsed(result.budget, start);
                return result;
            }

            const CandidateFailureKind failure = classifyCandidateFailure(
                falseCheck.snapshot,
                path,
                replay);
            if (!commandSequences.rememberFailure(
                    path.commandSequenceId,
                    path.commands,
                    failure)) {
                releaseReplayBytes(report, replay);
                SolveResult result = makeFailure(
                    isBudgetFailure(commandSequences.error()) ? SolveKind::Unknown : SolveKind::ModelError,
                    horizon,
                    commandSequences.error(),
                    start);
                result.partitionVersion = falseCheck.snapshot.partitions.partitionVersion;
                result.coverageVersion = falseCheck.snapshot.coverageVersion;
                result.budget = report;
                stampElapsed(result.budget, start);
                return result;
            }
            ++failedCandidatesInBatch;

            const std::optional<CandidateMismatch> mismatch = firstCandidateMismatch(
                bundle_, falseCheck.snapshot, path, replay, trialOrder++);
            if (mismatch.has_value()) {
                if (mismatch->kind == CandidateMismatchKind::DetailedModelError) {
                    std::string detail;
                    if (const CheckedEdge *mismatchEdge = edgeForStep(falseCheck.snapshot, mismatch->step)) {
                        detail =
                            "; turn=" + std::to_string(mismatch->elapsedTurn) +
                            ", command=" + std::to_string(mismatch->step.selectedCommand) +
                            ", cut=" + std::to_string(static_cast<int>(mismatchEdge->completionCut)) +
                            ", source_p=" + std::to_string(mismatch->stateBefore.position) +
                            ", exact_p=" + std::to_string(mismatch->stateAfter.position) +
                            ", edge_p=[" + std::to_string(mismatchEdge->firstOutputPosition) +
                            "," + std::to_string(mismatchEdge->lastOutputPosition) + "]" +
                            ", exact_enemy_hp=" + std::to_string(mismatch->stateAfter.players[1].hp) +
                            ", exact_hero_hp=" + std::to_string(mismatch->stateAfter.players[0].hp) +
                            ", exact_mp=" + std::to_string(mismatch->stateAfter.players[0].mp) +
                            ", exact_herb=" +
                            std::to_string(mismatch->stateAfter.players[0].medicinal_herbs_count);
                        if (mismatchEdge->continuingOutput) {
                            const Box &envelope = *mismatchEdge->continuingOutput;
                            detail +=
                                ", envelope_enemy_hp=[" + std::to_string(envelope.enemyHp.lo) + "," +
                                std::to_string(envelope.enemyHp.hi) + "]" +
                                ", envelope_hero_hp=[" + std::to_string(envelope.heroHp.lo) + "," +
                                std::to_string(envelope.heroHp.hi) + "]" +
                                ", envelope_mp=[" + std::to_string(envelope.mp.lo) + "," +
                                std::to_string(envelope.mp.hi) + "]" +
                                ", envelope_herb=[" + std::to_string(envelope.herb.lo) + "," +
                                std::to_string(envelope.herb.hi) + "]";
                        } else {
                            detail += ", envelope=registered_completion_lemma";
                        }
                    }
                    releaseReplayBytes(report, replay);
                    return makeCurrentFailure(
                        SolveKind::ModelError,
                        "detailed checked edge disagrees with exact replay: " + mismatch->reason + detail);
                }
                if (!chargeSearchBudget(report, budget, 1, sizeof(CandidateMismatch))) {
                    releaseReplayBytes(report, replay);
                    return makeCurrentFailure(
                        SolveKind::Unknown,
                        "candidate mismatch batch exceeded proof budget");
                }
                failureBatchBytes += sizeof(CandidateMismatch);
                CandidateMismatch recorded = *mismatch;
                recorded.partitionVersion = falseCheck.snapshot.partitions.partitionVersion;
                recorded.coverageVersion = falseCheck.snapshot.coverageVersion;
                failures.push_back(std::move(recorded));
            } else {
                ++diagnosticNoMismatch;
            }
            releaseReplayBytes(report, replay);

            if (failedCandidatesInBatch >= 8) {
                if (report.candidates == 8) {
                    std::cerr << "TRACE_WORK batch8=" << report.work
                              << " scans=" << report.candidateScans << '\n';
                }
                if (report.candidates != 0 && report.candidates % 64 == 0) {
                    std::cerr << "TRACE_WORK milestone candidates=" << report.candidates
                              << " work=" << report.work
                              << " bytes=" << report.bytes
                              << " scans=" << report.candidateScans
                              << " trie_nodes=" << commandSequences.diagnosticNodeCount()
                              << " frame_builds=" << iterator->diagnosticFrameBuilds()
                              << " frame_terms=" << iterator->diagnosticFrameCursorTerms()
                              << " range_lookup=" << iterator->diagnosticRangeLookupWork()
                              << " range_scan=" << iterator->diagnosticRangeScanWork()
                              << " dup_goal=" << iterator->diagnosticDuplicateGoalWork()
                              << '\n';
                }
                iterator->releaseStaticIndexesForRepair();
                const std::optional<bool> repaired = tryOneRepair();
                if (!repaired.has_value()) {
                    return makeCurrentFailure(
                        SolveKind::ModelError,
                        "repair inspection found an invalid checked edge");
                }
                if (*repaired) {
                    std::cerr << "TRACE_GENERATION repair=" << report.repairs
                              << " phase=snapshot work=" << report.work
                              << " bytes=" << report.bytes << '\n';
                    if (std::optional<SolveResult> result = rebuildAfterRepair()) {
                        return *result;
                    }
                    std::cerr << "TRACE_GENERATION repair=" << report.repairs
                              << " phase=iterator work=" << report.work
                              << " bytes=" << report.bytes << '\n';
                    continue;
                }
                std::string restoreReason;
                if (!iterator->restoreStaticIndexesAfterRepairFailure(restoreReason)) {
                    return makeCurrentFailure(
                        isBudgetFailure(restoreReason) ? SolveKind::Unknown : SolveKind::ModelError,
                        restoreReason);
                }
                failures.clear();
                releaseSearchBytes(report, failureBatchBytes);
                failureBatchBytes = 0;
                failedCandidatesInBatch = 0;
            }
        }
    }

    SolveResult WorldlineSolver::extendPrefix(
        const Problem &initialProblem,
        const std::vector<int> &prefix,
        int horizon,
        const ProofBudget &budget,
        const std::vector<int> &candidateHint,
        bool proveMinimal) const {
        const Clock::time_point start = Clock::now();
        const ProofBudget limits = effectiveProofBudget(budget, start);
        BudgetReport report;

        if (limits.totalTimeMs <= 0 || limits.totalTimeMs > 15000) {
            return makeFailure(
                SolveKind::InvalidInput,
                horizon,
                "total_time must be 1..15000 ms",
                start);
        }
        if (horizon < 0) {
            return makeFailure(
                SolveKind::InvalidInput,
                horizon,
                "negative H_limit",
                start);
        }

        const Clock::time_point prefixReplayStart = Clock::now();
        PrefixReceipt receipt = ExactReplay::replayPrefix(
            bundle_, initialProblem, prefix, &limits, &report);
        report.prefixReplayMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now() - prefixReplayStart).count());
        if (!receipt.valid) {
            if (receipt.failureKind == SolveKind::Unknown) {
                releaseReplayBytes(report, receipt.replay);
                SolveResult interrupted = makeFailure(
                    SolveKind::Unknown,
                    horizon,
                    receipt.reason.empty() ? "prefix replay exhausted total_time" : receipt.reason,
                    start);
                interrupted.budget = report;
                stampElapsed(interrupted.budget, start);
                return interrupted;
            }
            const SolveKind failureKind =
                receipt.failureKind == SolveKind::UnsupportedInput ||
                receipt.failureKind == SolveKind::ModelError
                    ? receipt.failureKind
                    : SolveKind::InvalidPrefix;
            releaseReplayBytes(report, receipt.replay);
            SolveResult invalid = makeFailure(failureKind, horizon, receipt.reason, start);
            invalid.budget = report;
            stampElapsed(invalid.budget, start);
            return invalid;
        }
        const std::uint64_t receiptBytes = accountedPrefixReceiptBytes(receipt);
        if (!chargeSearchBudget(
                report,
                limits,
                0,
                receiptBytes)) {
            releaseReplayBytes(report, receipt.replay);
            SolveResult result = makeFailure(
                SolveKind::Unknown,
                horizon,
                "prefix receipt exceeded shared proof byte budget",
                start);
            result.budget = report;
            stampElapsed(result.budget, start);
            return result;
        }
        receipt.accountedBytes = receiptBytes;
        if (deadlineReached(start, limits, report)) {
            releasePrefixReceiptBytes(report, receipt);
            SolveResult result = makeFailure(
                SolveKind::Unknown,
                horizon,
                "prefix replay exhausted total_time",
                start);
            result.budget = report;
            stampElapsed(result.budget, start);
            return result;
        }

        Problem suffixProblem = ExactReplay::bindSuffixProblem(initialProblem, receipt);
        const std::string suffixValidation = ExactReplay::validateProblem(bundle_, suffixProblem, horizon);
        if (!suffixValidation.empty()) {
            releasePrefixReceiptBytes(report, receipt);
            SolveResult invalid = makeFailure(
                SolveKind::UnsupportedInput,
                horizon,
                suffixValidation,
                start);
            invalid.budget = report;
            return invalid;
        }
        if (suffixProblem.s0.players[1].hp == 0) {
            SolveResult result;
            result.kind = SolveKind::Win;
            result.horizon = horizon;
            result.firstWinningTurn = 0;
            if (proveMinimal) {
                result.minimality = MinimalityKind::Optimal;
                result.minimumTurnLowerBound = 0;
                result.minimumTurnUpperBound = 0;
                result.reason = "prefix already reaches the goal; minimum suffix length is zero";
            }
            result.replay = ExactReplay::replay(
                bundle_, suffixProblem, {}, true, &limits, &report);
            stampFirstWin(report, start);
            result.budget = report;
            bindProblemKey(result, suffixProblem);
            return attachCheckedPrefix(
                std::move(receipt), suffixProblem, std::move(result), limits, start);
        }

        CheckedKernelCache checkedCache;
        if (const CheckResult cacheCheck = ProofKernel::bindCheckedCache(checkedCache, suffixProblem);
            !cacheCheck.accepted) {
            releasePrefixReceiptBytes(report, receipt);
            SolveResult invalid = makeFailure(
                SolveKind::ModelError,
                horizon,
                cacheCheck.reason,
                start);
            invalid.budget = report;
            return invalid;
        }

        SolveResult result = solveSuffixWithBudget(
            suffixProblem,
            horizon,
            limits,
            candidateHint,
            start,
            report,
            checkedCache);
        if (result.kind == SolveKind::Win && proveMinimal) {
            result = tightenMinimumWithBudget(
                suffixProblem,
                std::move(result),
                limits,
                start,
                checkedCache);
        }
        return attachCheckedPrefix(
            std::move(receipt), suffixProblem, std::move(result), limits, start);
    }
} // namespace d20proof
