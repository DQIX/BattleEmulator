#include "WorldlineSolver.h"

#include <algorithm>
#include <chrono>
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
                clamp(result.maxLeavesPerPosition, 8u);
                clamp(result.maxDetailedTermsPerAction, 8u);
                clamp(result.maxCompletionTermsPerAction, 3u);
                clamp(result.maxPriceEvaluations, 8u);
                clamp(result.maxWork, 8'000'000ull);
                clamp(result.maxBytes, 128ull * 1024ull * 1024ull);
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

        std::uint64_t accountedSnapshotBytes(const CheckedSnapshot &snapshot) {
            std::uint64_t bytes = accountedPartitionFamilyBytes(snapshot.partitions);
            auto add = [&](std::uint64_t amount) {
                if (amount > std::numeric_limits<std::uint64_t>::max() - bytes) {
                    bytes = std::numeric_limits<std::uint64_t>::max();
                } else {
                    bytes += amount;
                }
            };
            add(snapshot.proofs.size() * sizeof(RootProofRecord));
            for (const RootProofRecord &proof: snapshot.proofs) {
                add(proof.completionCases.size() * sizeof(CompletionProofCase));
            }
            add(snapshot.coverage.size() * sizeof(CheckedRootRecord));
            add(snapshot.completionCheckpoints.size() * sizeof(CompletionCheckpoint));
            for (const auto &layer: snapshot.edgesByElapsedTurn) {
                add(layer.size() * sizeof(CheckedEdge));
                for (const CheckedEdge &edge: layer) {
                    add(edge.weightTerms.size() * sizeof(CompletionWeightTerm));
                    add(edge.targets.size() * sizeof(CellKey));
                }
            }
            for (const auto &layer: snapshot.support) {
                add(layer.size() * sizeof(CellKey));
            }
            return bytes;
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

        struct GoalDistances {
            bool accepted = false;
            std::string reason;
            std::vector<std::vector<int>> values;
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

            result.values.resize(static_cast<std::size_t>(horizon) + 1);
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
                std::vector<int> current(sources.size(), kInfiniteDistance);

                for (std::size_t sourceIndex = 0; sourceIndex < sources.size(); ++sourceIndex) {
                    int best = kInfiniteDistance;
                    const CellKey &source = sources[sourceIndex];
                    const std::vector<CheckedEdge> &layerEdges =
                        snapshot.edgesByElapsedTurn[elapsedTurn];
                    std::size_t edgeLo = 0;
                    std::size_t edgeHi = layerEdges.size();
                    std::uint64_t edgeComparisons = 0;
                    while (edgeLo < edgeHi) {
                        ++edgeComparisons;
                        const std::size_t mid = edgeLo + (edgeHi - edgeLo) / 2;
                        if (layerEdges[mid].source < source) {
                            edgeLo = mid + 1;
                        } else {
                            edgeHi = mid;
                        }
                    }
                    if (!chargeSearchBudget(report, limits, 1, 0)) {
                        result.reason = "goal-distance source-edge lookup exceeded proof budget";
                        return result;
                    }
                    for (std::size_t edgeIndex = edgeLo;
                         edgeIndex < layerEdges.size() && layerEdges[edgeIndex].source == source;
                         ++edgeIndex) {
                        const CheckedEdge &edge = layerEdges[edgeIndex];
                        if (edge.mayReachGoal) {
                            best = 1;
                        }
                        for (const CellKey &target: edge.targets) {
                            const std::optional<std::size_t> targetIndex = cellIndex(next, target);
                            if (!targetIndex || *targetIndex >= nextDistances.size()) {
                                result.reason = "goal-distance edge target is missing from Support";
                                return result;
                            }
                            const int destinationDistance = nextDistances[*targetIndex];
                            if (destinationDistance < kInfiniteDistance - 1) {
                                best = std::min(best, destinationDistance + 1);
                            }
                            if (!chargeSearchBudget(report, limits, 1, 0)) {
                                result.reason = "goal-distance target scan exceeded proof budget";
                                return result;
                            }
                        }
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
        };

        class GoalCandidateIterator {
        public:
            GoalCandidateIterator(
                const RuleBundle &bundle,
                const CheckedSnapshot &snapshot,
                const GoalDistances &distances,
                int horizon,
                const ProofBudget &limits,
                BudgetReport &report)
                : bundle_(bundle),
                  snapshot_(snapshot),
                  distances_(distances),
                  horizon_(horizon),
                  limits_(limits),
                  report_(report) {
                if (!buildDestinationOrders()) {
                    return;
                }
                Frame root;
                root.elapsedTurn = 0;
                root.source = snapshot.root;
                if (!buildFrame(root)) {
                    return;
                }
                stack_.push_back(std::move(root));
                ready_ = true;
            }

            ~GoalCandidateIterator() {
                releaseSearchBytes(report_, ownedBytes_);
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

                    if (choice.goal) {
                        path.commands = commandPath_;
                        path.steps = stepPath_;
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
                std::size_t edgeIndex = 0;
                bool goalPending = false;
                std::size_t destinationCursor = 0;
                std::optional<CandidateChoice> targetHead;
            };

            struct Frame {
                int elapsedTurn = 0;
                CellKey source;
                std::vector<EdgeCursor> edgeCursors;
            };

            static std::uint64_t sortWorkEstimate(std::size_t count) {
                if (count <= 1) {
                    return count;
                }
                std::uint64_t levels = 0;
                for (std::size_t value = count - 1; value != 0; value >>= 1) {
                    ++levels;
                }
                return static_cast<std::uint64_t>(count) * levels;
            }

            bool buildDestinationOrders() {
                orderedDestinations_.resize(static_cast<std::size_t>(horizon_) + 1);
                for (int elapsedTurn = 1; elapsedTurn <= horizon_; ++elapsedTurn) {
                    const std::vector<CellKey> &support = snapshot_.support[elapsedTurn];
                    const std::vector<int> &layerDistances = distances_.values[elapsedTurn];
                    if (support.size() != layerDistances.size()) {
                        error_ = "candidate destination index does not match goal-distance layer";
                        return false;
                    }

                    std::vector<OrderedDestination> &ordered = orderedDestinations_[elapsedTurn];
                    ordered.reserve(support.size());
                    for (std::size_t index = 0; index < support.size(); ++index) {
                        if (layerDistances[index] < kInfiniteDistance) {
                            ordered.push_back({layerDistances[index], support[index]});
                        }
                    }
                    std::sort(
                        ordered.begin(),
                        ordered.end(),
                        [](const OrderedDestination &a, const OrderedDestination &b) {
                            return std::tuple{a.distance, a.key.rngPosition, a.key.localCellId} <
                                   std::tuple{b.distance, b.key.rngPosition, b.key.localCellId};
                        });
                    if (!chargeSearchBudget(
                            report_,
                            limits_,
                            sortWorkEstimate(ordered.size()),
                            ordered.size() * sizeof(OrderedDestination))) {
                        error_ = "candidate destination index exceeded proof budget";
                        return false;
                    }
                    ownedBytes_ += ordered.size() * sizeof(OrderedDestination);
                }
                return true;
            }

            bool buildFrame(Frame &frame) {
                if (frame.elapsedTurn < 0 || frame.elapsedTurn >= horizon_) {
                    error_ = "candidate iterator requested choices outside horizon";
                    return false;
                }
                const std::vector<CheckedEdge> &edges = snapshot_.edgesByElapsedTurn[frame.elapsedTurn];
                std::size_t edgeLo = 0;
                std::size_t edgeHi = edges.size();
                std::uint64_t comparisons = 0;
                while (edgeLo < edgeHi) {
                    ++comparisons;
                    const std::size_t mid = edgeLo + (edgeHi - edgeLo) / 2;
                    if (edges[mid].source < frame.source) {
                        edgeLo = mid + 1;
                    } else {
                        edgeHi = mid;
                    }
                }
                if (!chargeSearchBudget(report_, limits_, 1, 0)) {
                    error_ = "candidate source-edge lookup exceeded proof budget";
                    return false;
                }

                for (std::size_t edgeIndex = edgeLo;
                     edgeIndex < edges.size() && edges[edgeIndex].source == frame.source;
                     ++edgeIndex) {
                    const CheckedEdge &edge = edges[edgeIndex];
                    const int profileOrder = commandOrder(bundle_, edge.selectedCommand);
                    if (profileOrder == std::numeric_limits<int>::max()) {
                        error_ = "checked model contains a command outside registered profile";
                        return false;
                    }
                    frame.edgeCursors.push_back({edgeIndex, edge.mayReachGoal, 0, std::nullopt});
                }
                if (!chargeSearchBudget(
                        report_,
                        limits_,
                        frame.edgeCursors.size(),
                        frame.edgeCursors.size() * sizeof(EdgeCursor))) {
                    error_ = "candidate frame construction exceeded proof budget";
                    return false;
                }
                ownedBytes_ += frame.edgeCursors.size() * sizeof(EdgeCursor);
                return true;
            }

            void releaseOwnedBytes(std::uint64_t bytes) {
                const std::uint64_t released = std::min(bytes, ownedBytes_);
                releaseSearchBytes(report_, released);
                ownedBytes_ -= released;
            }

            ChoiceStatus ensureTargetHead(Frame &frame, EdgeCursor &cursor) {
                if (cursor.targetHead) {
                    return ChoiceStatus::Choice;
                }

                const CheckedEdge &edge = snapshot_.edgesByElapsedTurn[frame.elapsedTurn][cursor.edgeIndex];
                const int profileOrder = commandOrder(bundle_, edge.selectedCommand);
                const std::vector<OrderedDestination> &ordered = orderedDestinations_[frame.elapsedTurn + 1];
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
                    if (!std::binary_search(edge.targets.begin(), edge.targets.end(), destination.key)) {
                        continue;
                    }
                    cursor.targetHead = CandidateChoice{
                        destination.distance + 1,
                        edge.kind == CheckedEdgeKind::Completion,
                        edge.selectedCommand,
                        profileOrder,
                        cursor.edgeIndex,
                        false,
                        destination.key};
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
                    const CheckedEdge &edge = snapshot_.edgesByElapsedTurn[frame.elapsedTurn][cursor.edgeIndex];
                    const int profileOrder = commandOrder(bundle_, edge.selectedCommand);

                    if (cursor.goalPending) {
                        const CandidateChoice goalChoice{
                            1,
                            edge.kind == CheckedEdgeKind::Completion,
                            edge.selectedCommand,
                            profileOrder,
                            cursor.edgeIndex,
                            true,
                            {}};
                        if (!haveChoice || candidateChoiceLess(goalChoice, selected)) {
                            selected = goalChoice;
                            selectedCursor = cursorIndex;
                            selectedGoal = true;
                            haveChoice = true;
                        }
                    }

                    const ChoiceStatus targetStatus = ensureTargetHead(frame, cursor);
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
                if (selectedGoal) {
                    cursor.goalPending = false;
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
            int horizon_ = 0;
            const ProofBudget &limits_;
            BudgetReport &report_;
            std::vector<std::vector<OrderedDestination>> orderedDestinations_;
            std::vector<Frame> stack_;
            std::vector<int> commandPath_;
            std::vector<CandidateStep> stepPath_;
            std::string error_;
            bool ready_ = false;
            std::uint64_t ownedBytes_ = 0;
        };

        enum class CandidateFailureKind {
            IllegalCommand,
            Lost,
            NotWon,
            AbstractMismatch,
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
                if (!contains(edge->continuingOutput, *after)) {
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
                        edge->kind == CheckedEdgeKind::Completion
                            ? "exact output escapes the kernel-checked COMPLETE envelope"
                            : "exact output escapes a detailed checked image"};
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
            const PrefixReceipt &receipt,
            const Problem &suffixProblem,
            SolveResult result,
            const ProofBudget &limits,
            Clock::time_point start) {
            if (!receipt.valid || !receipt.observationsChecked) {
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
                result.kind = SolveKind::ModelError;
                result.reason = "prefix/suffix raw-state, turn, rule, seed, or future-constraint connection mismatch";
                stampElapsed(result.budget, start);
                return result;
            }
            if (result.hasProblemKey && !sameProblemKey(result.problemKey, suffixProblem)) {
                result.kind = SolveKind::ModelError;
                result.reason = "suffix result belongs to a different problem_key";
                stampElapsed(result.budget, start);
                return result;
            }
            const std::uint64_t attachmentBytes =
                sizeof(PrefixReceipt) + receipt.prefix.size() * sizeof(int);
            if (!chargeSearchBudget(result.budget, limits, 1, attachmentBytes)) {
                result.kind = SolveKind::Unknown;
                result.reason = "prefix attachment exceeded the shared proof budget";
                result.prefixConnected = false;
                stampElapsed(result.budget, start);
                return result;
            }

            bindProblemKey(result, suffixProblem);
            result.prefixConnected = true;
            result.prefixReceipt = receipt;
            if (result.kind == SolveKind::Win) {
                std::vector<int> joined = receipt.prefix;
                joined.insert(joined.end(), result.commands.begin(), result.commands.end());
                result.commands = std::move(joined);
            }
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
            result.replay = ExactReplay::replay(bundle_, problem, {}, true, &limits);
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

        if (!chargeSearchBudget(
                witness.budget,
                budget,
                witness.commands.size() + 1,
                0)) {
            witness.minimality = MinimalityKind::Unknown;
            witness.reason = "exact witness retained; fresh TRUE replay could not fit the shared proof budget";
            stampElapsed(witness.budget, start);
            return witness;
        }
        ReplayResult freshWitness = ExactReplay::replay(
            bundle_,
            problem,
            witness.commands,
            true,
            &budget);
        if (freshWitness.interrupted) {
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
            bindProblemKey(error, problem);
            stampElapsed(error.budget, start);
            return error;
        }
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
                    !bound.replay.won) {
                    SolveResult error = bound;
                    error.kind = SolveKind::ModelError;
                    error.reason = "tighten_minimum received a non-shortening checked witness";
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

        const std::string validationError = ExactReplay::validateProblem(bundle_, problem, horizon);
        if (!validationError.empty()) {
            return makeFailure(SolveKind::UnsupportedInput, horizon, validationError, start);
        }

        if (problem.s0.players[1].hp == 0) {
            SolveResult result;
            result.kind = SolveKind::Win;
            result.horizon = horizon;
            result.firstWinningTurn = 0;
            result.replay = ExactReplay::replay(bundle_, problem, {}, true, &budget);
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
            if (!chargeSearchBudget(
                    report,
                    budget,
                    candidateHint.size() + 1,
                    candidateHint.size() * sizeof(int))) {
                SolveResult result = makeFailure(
                    SolveKind::Unknown,
                    horizon,
                    "candidate hint exceeded shared proof budget",
                    start);
                result.budget = report;
                stampElapsed(result.budget, start);
                return result;
            }
            ReplayResult candidateReplay = ExactReplay::replay(bundle_, problem, candidateHint, false, &budget);
            ++report.candidates;
            if (candidateReplay.interrupted) {
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

        GoalDistances distances = buildGoalDistances(falseCheck.snapshot, horizon, budget, report);
        if (!distances.accepted) {
            releaseSearchBytes(report, distances.chargedBytes);
            SolveResult result;
            result.kind = isBudgetFailure(distances.reason) ? SolveKind::Unknown : SolveKind::ModelError;
            result.horizon = horizon;
            result.reason = distances.reason;
            result.partitionVersion = family.partitionVersion;
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
            result.partitionVersion = family.partitionVersion;
            result.coverageVersion = falseCheck.snapshot.coverageVersion;
            result.budget = report;
            stampElapsed(result.budget, start);
            return result;
        }

        auto iterator = std::make_unique<GoalCandidateIterator>(
            bundle_, falseCheck.snapshot, distances, horizon, budget, report);
        std::map<std::vector<int>, CandidateFailureKind> failedCommands;
        if (rejectedHint.has_value()) {
            const std::uint64_t failedHintBytes =
                sizeof(CandidateFailureKind) + rejectedHint->first.size() * sizeof(int);
            if (!chargeSearchBudget(report, budget, 1, failedHintBytes)) {
                return makeFailure(
                    SolveKind::Unknown,
                    horizon,
                    "failed candidate hint could not fit FailedCommands budget",
                    start);
            }
            failedCommands.emplace(rejectedHint->first, rejectedHint->second);
        }
        std::vector<CandidateMismatch> failures;
        std::uint64_t failureBatchBytes = 0;
        std::uint32_t trialOrder = 0;
        std::uint32_t failedCandidatesInBatch = rejectedHint.has_value() ? 1u : 0u;

        auto makeCurrentFailure = [&](SolveKind kind, std::string reason) {
            SolveResult result = makeFailure(kind, horizon, std::move(reason), start);
            result.partitionVersion = family.partitionVersion;
            result.coverageVersion = falseCheck.snapshot.coverageVersion;
            result.budget = report;
            stampElapsed(result.budget, start);
            return result;
        };

        auto checkedCellDomain = [&](const CellKey &source, Box &domain, std::string &error) -> bool {
            const PredicatePartition *partition = partitionAt(family, source.rngPosition);
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
                return 100;
            }
            switch (value.cut) {
                case CompletionCutId::TurnEntry:
                    return 0;
                case CompletionCutId::AllyDoneEnemyPending:
                case CompletionCutId::EnemyDoneAllyPending:
                    return 10;
                case CompletionCutId::ActionsDone:
                    return 20;
            }
            return 0;
        };

        auto repairProposalTooLarge = [](const std::string &reason) {
            return reason.find("detailed term limit J exceeded") != std::string::npos ||
                   reason.find("completion term limit C exceeded") != std::string::npos ||
                   reason.find("partition leaf limit exceeded") != std::string::npos;
        };

        bool repairSnapshotPrepared = false;

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
                if (failure.partitionVersion != family.partitionVersion ||
                    failure.coverageVersion != falseCheck.snapshot.coverageVersion) {
                    return std::nullopt;
                }
                const CheckedEdge *edge = edgeForStep(falseCheck.snapshot, failure.step);
                if (edge == nullptr) {
                    return std::nullopt;
                }

                if (failure.kind == CandidateMismatchKind::Completion) {
                    const CompletionCheckpoint *checkpoint = nullptr;
                    for (const CompletionCheckpoint &candidate: falseCheck.snapshot.completionCheckpoints) {
                        if (candidate.elapsedTurn != failure.elapsedTurn ||
                            !(candidate.source == failure.step.source) ||
                            candidate.selectedCommand != failure.step.selectedCommand ||
                            candidate.cut != edge->completionCut) {
                            continue;
                        }
                        if (contains(candidate.frame.inputDomain, edge->rootDomain) &&
                            contains(edge->rootDomain, candidate.frame.inputDomain)) {
                            checkpoint = &candidate;
                            break;
                        }
                    }
                    if (checkpoint == nullptr) {
                        continue;
                    }

                    ProofTemplate advanced;
                    const CheckResult advance = ProofKernel::advanceCompletionCheckpoint(
                        bundle_, problem, *checkpoint, budget, report, advanced);
                    if (!advance.accepted) {
                        if (isBudgetFailure(advance.reason)) {
                            return false;
                        }
                        continue;
                    }
                    ProofTemplate current;
                    current.elapsedTurn = failure.elapsedTurn;
                    current.rngPosition = failure.step.source.rngPosition;
                    current.selectedCommand = failure.step.selectedCommand;
                    current.coveredDomain = checkpoint->rootDomain;
                    current.kind = RootProofKind::Completion;
                    current.cut = edge->completionCut;
                    if (templateRank(advanced) <= templateRank(current)) {
                        continue;
                    }

                    if (coverageVersion == std::numeric_limits<std::uint64_t>::max()) {
                        return std::nullopt;
                    }
                    const std::uint64_t trialCoverageVersion = coverageVersion + 1;
                    const std::uint64_t retainedBytesBeforeTrial = report.bytes;
                    const std::uint64_t oldSnapshotBytes = accountedSnapshotBytes(falseCheck.snapshot);
                    CheckedKernelCache trialCache = checkedCache;
                    BudgetReport trialBudget = report;
                    bool cacheChanged = false;
                    const CheckResult cacheUpdate = ProofKernel::rememberCheckedTemplate(
                        trialCache,
                        problem,
                        advanced,
                        budget,
                        trialBudget,
                        cacheChanged);
                    if (!cacheUpdate.accepted) {
                        absorbDiscardedTrial(report, trialBudget, retainedBytesBeforeTrial);
                        if (isBudgetFailure(cacheUpdate.reason)) {
                            return false;
                        }
                        return std::nullopt;
                    }
                    if (!cacheChanged) {
                        absorbDiscardedTrial(report, trialBudget, retainedBytesBeforeTrial);
                        continue;
                    }

                    CheckedSnapshot trialSnapshot = ProofKernel::rebuildSupport(
                        bundle_,
                        problem,
                        horizon,
                        family,
                        budget,
                        trialBudget,
                        nullptr,
                        &trialCache.templates,
                        trialCoverageVersion);
                    if (!trialSnapshot.check.accepted) {
                        absorbDiscardedTrial(report, trialBudget, retainedBytesBeforeTrial);
                        if (repairProposalTooLarge(trialSnapshot.check.reason)) {
                            continue;
                        }
                        if (isBudgetFailure(trialSnapshot.check.reason)) {
                            return false;
                        }
                        return std::nullopt;
                    }

                    report = trialBudget;
                    releaseSearchBytes(report, oldSnapshotBytes);
                    checkedCache = std::move(trialCache);
                    falseCheck = {};
                    falseCheck.check.accepted = true;
                    falseCheck.snapshot = std::move(trialSnapshot);
                    coverageVersion = trialCoverageVersion;
                    repairSnapshotPrepared = true;
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
                        continue;
                    }
                    const std::uint64_t retainedBytesBeforeTrial = report.bytes;
                    const std::uint64_t refinedWorkingBytes =
                        accountedPartitionFamilyBytes(family) + 2 * sizeof(PartitionNode);
                    std::uint64_t refinementWork = family.trees.size() + 1;
                    for (const PredicatePartition &tree: family.trees) {
                        refinementWork += tree.nodes.size();
                    }
                    if (!chargeSearchBudget(
                            report,
                            budget,
                            refinementWork,
                            refinedWorkingBytes)) {
                        return false;
                    }
                    PartitionFamily refined;
                    const CheckResult refinement = ProofKernel::refineLeaf(
                        family,
                        ProofKernel::baseBox(bundle_, problem.s0),
                        failure.step.source.rngPosition,
                        failure.step.source.localCellId,
                        predicate,
                        budget.maxLeavesPerPosition,
                        refined);
                    if (!refinement.accepted) {
                        releaseSearchBytes(report, refinedWorkingBytes);
                        continue;
                    }

                    if (coverageVersion == std::numeric_limits<std::uint64_t>::max()) {
                        releaseSearchBytes(report, refinedWorkingBytes);
                        return std::nullopt;
                    }
                    const std::uint64_t trialCoverageVersion = coverageVersion + 1;
                    const std::uint64_t oldSnapshotBytes = accountedSnapshotBytes(falseCheck.snapshot);
                    CheckedKernelCache trialCache = checkedCache;
                    BudgetReport trialBudget = report;
                    CheckedSnapshot trialSnapshot = ProofKernel::rebuildSupport(
                        bundle_,
                        problem,
                        horizon,
                        refined,
                        budget,
                        trialBudget,
                        nullptr,
                        &trialCache.templates,
                        trialCoverageVersion);
                    if (!trialSnapshot.check.accepted) {
                        absorbDiscardedTrial(report, trialBudget, retainedBytesBeforeTrial);
                        if (repairProposalTooLarge(trialSnapshot.check.reason)) {
                            continue;
                        }
                        if (isBudgetFailure(trialSnapshot.check.reason)) {
                            return false;
                        }
                        return std::nullopt;
                    }

                    report = trialBudget;
                    releaseSearchBytes(report, oldSnapshotBytes);
                    releaseSearchBytes(report, refinedWorkingBytes);
                    family = std::move(refined);
                    checkedCache = std::move(trialCache);
                    falseCheck = {};
                    falseCheck.check.accepted = true;
                    falseCheck.snapshot = std::move(trialSnapshot);
                    coverageVersion = trialCoverageVersion;
                    repairSnapshotPrepared = true;
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
                    family,
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
                bundle_, falseCheck.snapshot, distances, horizon, budget, report);
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
                result.partitionVersion = family.partitionVersion;
                result.coverageVersion = falseCheck.snapshot.coverageVersion;
                result.budget = report;
                stampElapsed(result.budget, start);
                return result;
            }
            if (report.candidates >= budget.maxCandidates) {
                SolveResult result = makeFailure(
                    SolveKind::Unknown,
                    horizon,
                    "candidate limit reached without checked witness",
                    start);
                result.partitionVersion = family.partitionVersion;
                result.coverageVersion = falseCheck.snapshot.coverageVersion;
                result.budget = report;
                stampElapsed(result.budget, start);
                return result;
            }

            CandidatePath path;
            std::string iteratorReason;
            const IteratorStatus iteratorStatus = iterator->next(path, iteratorReason);
            if (iteratorStatus == IteratorStatus::Exhausted) {
                const std::optional<bool> repaired = tryOneRepair();
                if (!repaired.has_value()) {
                    return makeCurrentFailure(
                        SolveKind::ModelError,
                        "repair inspection found an invalid checked edge");
                }
                if (*repaired) {
                    if (std::optional<SolveResult> result = rebuildAfterRepair()) {
                        return *result;
                    }
                    continue;
                }
                return makeCurrentFailure(
                    SolveKind::Unknown,
                    "candidate iterator exhausted; search exhaustion is not a FALSE proof");
            }
            if (iteratorStatus == IteratorStatus::BudgetExceeded) {
                SolveResult result = makeFailure(
                    SolveKind::Unknown,
                    horizon,
                    iteratorReason,
                    start);
                result.partitionVersion = family.partitionVersion;
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
                result.partitionVersion = family.partitionVersion;
                result.coverageVersion = falseCheck.snapshot.coverageVersion;
                result.budget = report;
                stampElapsed(result.budget, start);
                return result;
            }

            if (failedCommands.contains(path.commands)) {
                continue;
            }

            ReplayResult replay = ExactReplay::replay(bundle_, problem, path.commands, false, &budget);
            ++report.candidates;
            if (replay.interrupted) {
                SolveResult result = makeFailure(
                    SolveKind::Unknown,
                    horizon,
                    replay.reason,
                    start);
                result.partitionVersion = family.partitionVersion;
                result.coverageVersion = falseCheck.snapshot.coverageVersion;
                result.budget = report;
                stampElapsed(result.budget, start);
                return result;
            }
            if (!replay.supported) {
                SolveResult result = makeFailure(
                    SolveKind::ModelError,
                    horizon,
                    replay.reason,
                    start);
                result.partitionVersion = family.partitionVersion;
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
                result.partitionVersion = family.partitionVersion;
                result.coverageVersion = falseCheck.snapshot.coverageVersion;
                result.budget = report;
                bindProblemKey(result, problem);
                stampElapsed(result.budget, start);
                return result;
            }

            const CandidateFailureKind failure = classifyCandidateFailure(
                falseCheck.snapshot,
                path,
                replay);
            const std::uint64_t failedBytes =
                sizeof(CandidateFailureKind) + path.commands.size() * sizeof(int);
            if (!chargeSearchBudget(report, budget, 1, failedBytes)) {
                SolveResult result = makeFailure(
                    SolveKind::Unknown,
                    horizon,
                    "FailedCommands exceeded proof budget",
                    start);
                result.partitionVersion = family.partitionVersion;
                result.coverageVersion = falseCheck.snapshot.coverageVersion;
                result.budget = report;
                stampElapsed(result.budget, start);
                return result;
            }
            failedCommands.emplace(path.commands, failure);
            ++failedCandidatesInBatch;

            const std::optional<CandidateMismatch> mismatch = firstCandidateMismatch(
                bundle_, falseCheck.snapshot, path, replay, trialOrder++);
            if (mismatch.has_value()) {
                if (mismatch->kind == CandidateMismatchKind::DetailedModelError) {
                    return makeCurrentFailure(
                        SolveKind::ModelError,
                        "detailed checked edge disagrees with exact replay: " + mismatch->reason);
                }
                if (!chargeSearchBudget(report, budget, 1, sizeof(CandidateMismatch))) {
                    return makeCurrentFailure(
                        SolveKind::Unknown,
                        "candidate mismatch batch exceeded proof budget");
                }
                failureBatchBytes += sizeof(CandidateMismatch);
                CandidateMismatch recorded = *mismatch;
                recorded.partitionVersion = family.partitionVersion;
                recorded.coverageVersion = falseCheck.snapshot.coverageVersion;
                failures.push_back(std::move(recorded));
            }

            if (failedCandidatesInBatch >= 8) {
                const std::optional<bool> repaired = tryOneRepair();
                if (!repaired.has_value()) {
                    return makeCurrentFailure(
                        SolveKind::ModelError,
                        "repair inspection found an invalid checked edge");
                }
                if (*repaired) {
                    if (std::optional<SolveResult> result = rebuildAfterRepair()) {
                        return *result;
                    }
                    continue;
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

        PrefixReceipt receipt = ExactReplay::replayPrefix(bundle_, initialProblem, prefix, &limits);
        if (!receipt.valid) {
            if (receipt.failureKind == SolveKind::Unknown) {
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
            return makeFailure(failureKind, horizon, receipt.reason, start);
        }
        const std::uint64_t prefixBytes =
            receipt.prefix.size() * sizeof(int) +
            receipt.replay.turns.size() * sizeof(ReplayTurn);
        if (!chargeSearchBudget(
                report,
                limits,
                receipt.replay.turns.size(),
                prefixBytes)) {
            SolveResult result = makeFailure(
                SolveKind::Unknown,
                horizon,
                "prefix replay exceeded shared proof budget",
                start);
            result.budget = report;
            stampElapsed(result.budget, start);
            return result;
        }
        if (deadlineReached(start, limits, report)) {
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
            result.replay = ExactReplay::replay(bundle_, suffixProblem, {}, true, &limits);
            result.budget = report;
            bindProblemKey(result, suffixProblem);
            return attachCheckedPrefix(receipt, suffixProblem, std::move(result), limits, start);
        }

        CheckedKernelCache checkedCache;
        if (const CheckResult cacheCheck = ProofKernel::bindCheckedCache(checkedCache, suffixProblem);
            !cacheCheck.accepted) {
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
        return attachCheckedPrefix(receipt, suffixProblem, std::move(result), limits, start);
    }
} // namespace d20proof
