#include "ProofKernel.h"

#include "../BattleEmulator.h"
#include "../BattleInitialPlayers.h"
#include "SymbolicStepper.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>

namespace d20proof {
    namespace {
        std::uint16_t encodeCharge(const Player &player, std::string &error) {
            if (!player.specialCharge) {
                return 1u;
            }
            if (player.specialChargeTurn < 0 || player.specialChargeTurn > 6) {
                error = "charge timer outside alpha range";
                return 0;
            }
            return static_cast<std::uint16_t>(1u << (player.specialChargeTurn + 1));
        }

        std::uint16_t encodeParalysis(const Player &player, std::string &error) {
            if (!player.paralysis) {
                return 1u;
            }
            if (player.paralysisTurns < -2 || player.paralysisTurns > 4) {
                error = "paralysis timer outside alpha range";
                return 0;
            }
            return static_cast<std::uint16_t>(1u << (player.paralysisTurns + 3));
        }

        std::uint16_t encodeAcro(const Player &player, std::string &error) {
            if (!player.acrobaticStar) {
                return 1u;
            }
            if (player.acrobaticStarTurn < 1 || player.acrobaticStarTurn > 6) {
                error = "acro timer outside alpha range";
                return 0;
            }
            return static_cast<std::uint16_t>(1u << player.acrobaticStarTurn);
        }

        std::uint16_t encodeRage(const Player &player, std::string &error) {
            if (!player.rage) {
                return 1u;
            }
            if (player.rageTurns < 1 || player.rageTurns > 4) {
                error = "rage timer outside alpha range";
                return 0;
            }
            return static_cast<std::uint16_t>(1u << player.rageTurns);
        }

        bool predicateIsValid(const Predicate &predicate) noexcept {
            if (predicate.kind != PredicateKind::ModeInMask) {
                return true;
            }

            const std::uint16_t validMask = allMask(predicate.mode);
            return predicate.mask != 0 && (predicate.mask & ~validMask) == 0;
        }

        bool evaluatePredicate(const Predicate &predicate, const RawState &state, std::string &error) {
            if (predicate.kind == PredicateKind::ResourceLe) {
                std::int64_t value = 0;
                switch (predicate.resource) {
                    case ResourceAxis::EnemyHp:
                        value = state.players[1].hp;
                        break;
                    case ResourceAxis::HeroHp:
                        value = state.players[0].hp;
                        break;
                    case ResourceAxis::Mp:
                        value = state.players[0].mp;
                        break;
                    case ResourceAxis::Herb:
                        value = state.players[0].medicinal_herbs_count;
                        break;
                }
                return value <= predicate.threshold;
            }

            const auto singleton = ProofKernel::alphaSingleton(state, error);
            if (!singleton) {
                return false;
            }
            return (modeMaskOf(*singleton, predicate.mode) & predicate.mask) != 0;
        }

        class PartitionValidator {
        public:
            PartitionValidator(
                const PredicatePartition &partition,
                const Box &baseDomain,
                std::uint32_t maximumLeaves)
                : partition_(partition),
                  baseDomain_(baseDomain),
                  maximumLeaves_(maximumLeaves),
                  visitState_(partition.nodes.size(), 0) {
            }

            CheckedPartition run() {
                if (partition_.nodes.empty() || partition_.root < 0 ||
                    partition_.root >= static_cast<int>(partition_.nodes.size())) {
                    result_.check.reason = "invalid partition root";
                    return result_;
                }

                if (!visit(partition_.root, baseDomain_)) {
                    return result_;
                }

                for (int state: visitState_) {
                    if (state == 0) {
                        result_.check.reason = "unreachable partition node";
                        return result_;
                    }
                }

                result_.check.accepted = true;
                return result_;
            }

        private:
            bool visit(int nodeIndex, const Box &domain) {
                if (nodeIndex < 0 || nodeIndex >= static_cast<int>(partition_.nodes.size())) {
                    result_.check.reason = "child outside partition";
                    return false;
                }
                if (visitState_[nodeIndex] == 1) {
                    result_.check.reason = "partition cycle";
                    return false;
                }
                if (visitState_[nodeIndex] == 2) {
                    result_.check.reason = "partition node has multiple parents";
                    return false;
                }

                visitState_[nodeIndex] = 1;
                const PartitionNode &node = partition_.nodes[nodeIndex];
                const bool accepted = node.leaf ? visitLeaf(node, domain) : visitBranch(node, domain);
                if (accepted) {
                    visitState_[nodeIndex] = 2;
                }
                return accepted;
            }

            bool visitLeaf(const PartitionNode &node, const Box &domain) {
                if (domain.empty()) {
                    result_.check.reason = "empty partition leaf";
                    return false;
                }
                if (!leafIds_.insert(node.localCellId).second) {
                    result_.check.reason = "duplicate local cell id";
                    return false;
                }

                ++leafCount_;
                if (leafCount_ > maximumLeaves_) {
                    result_.check.reason = "partition leaf limit exceeded";
                    return false;
                }

                result_.leaves.push_back({node.localCellId, domain});
                return true;
            }

            bool visitBranch(const PartitionNode &node, const Box &domain) {
                if (!predicateIsValid(node.predicate)) {
                    result_.check.reason = "invalid partition predicate";
                    return false;
                }
                if (node.trueChild < 0 || node.falseChild < 0 || node.trueChild == node.falseChild) {
                    result_.check.reason = "invalid partition children";
                    return false;
                }

                auto [trueDomain, falseDomain] = split(domain, node.predicate);
                if (trueDomain.empty() || falseDomain.empty()) {
                    result_.check.reason = "partition predicate has empty child";
                    return false;
                }

                return visit(node.trueChild, trueDomain) && visit(node.falseChild, falseDomain);
            }

            const PredicatePartition &partition_;
            const Box &baseDomain_;
            std::uint32_t maximumLeaves_;
            std::vector<int> visitState_;
            std::set<std::uint32_t> leafIds_;
            std::uint32_t leafCount_ = 0;
            CheckedPartition result_;
        };



        bool deadlineExceeded(
            const ProofBudget &limits,
            BudgetReport &budget) {
            if (!limits.hasDeadline) {
                return false;
            }
            const auto now = std::chrono::steady_clock::now();
            const auto start = limits.deadline - std::chrono::milliseconds(limits.totalTimeMs);
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            if (elapsed > budget.elapsedMs) {
                budget.elapsedMs = elapsed > std::numeric_limits<int>::max()
                    ? std::numeric_limits<int>::max()
                    : static_cast<int>(elapsed);
            }
            return now >= limits.deadline;
        }

        bool chargeBudget(
            BudgetReport &budget,
            const ProofBudget &limits,
            std::uint64_t work,
            std::uint64_t bytes) {
            if (deadlineExceeded(limits, budget)) {
                return false;
            }
            if (work > limits.maxWork - std::min(budget.work, limits.maxWork)) {
                return false;
            }
            if (bytes > limits.maxBytes - std::min(budget.bytes, limits.maxBytes)) {
                return false;
            }
            budget.work += work;
            budget.bytes += bytes;
            return true;
        }

        std::optional<std::uint64_t> nativeInternalWorkAt(
            const RuleBundle &bundle,
            const SymbolicFrame &frame,
            std::string &error) {
            const Routine *routine = nullptr;
            for (const Routine &candidate: bundle.program.routines) {
                if (candidate.id == frame.routineId) {
                    routine = &candidate;
                    break;
                }
            }
            if (routine == nullptr || frame.pc < 0 ||
                frame.pc >= static_cast<int>(routine->instructions.size())) {
                error = "symbolic frame pc is outside the registered RuleProgram";
                return std::nullopt;
            }

            const Instruction &instruction = routine->instructions[frame.pc];
            if (instruction.opcode != Opcode::Native) {
                return 0;
            }
            for (const NativeContract &contract: bundle.nativeContracts) {
                if (contract.id == instruction.nativeId) {
                    if (contract.maxWork < 0) {
                        error = "registered NATIVE has a negative internal work bound";
                        return std::nullopt;
                    }
                    return static_cast<std::uint64_t>(contract.maxWork);
                }
            }
            error = "symbolic NATIVE references an unregistered contract";
            return std::nullopt;
        }

        const Instruction *instructionAt(
            const RuleBundle &bundle,
            const SymbolicFrame &frame,
            std::string &error) {
            for (const Routine &routine: bundle.program.routines) {
                if (routine.id != frame.routineId) {
                    continue;
                }
                if (frame.pc < 0 || frame.pc >= static_cast<int>(routine.instructions.size())) {
                    error = "proof frame pc is outside the registered routine";
                    return nullptr;
                }
                return &routine.instructions[frame.pc];
            }
            error = "proof frame routine is outside the registered RuleProgram";
            return nullptr;
        }

        DetailedProofNodeKind detailedNodeKindForOpcode(Opcode opcode) noexcept {
            switch (opcode) {
                case Opcode::Branch:
                    return DetailedProofNodeKind::Branch;
                case Opcode::Switch:
                    return DetailedProofNodeKind::Switch;
                case Opcode::Call:
                case Opcode::Return:
                    return DetailedProofNodeKind::Control;
                case Opcode::ReadRng:
                    return DetailedProofNodeKind::Rng;
                case Opcode::SkipRng:
                    return DetailedProofNodeKind::RngSkip;
                case Opcode::Finish:
                    return DetailedProofNodeKind::Finish;
                case Opcode::Step:
                case Opcode::Native:
                case Opcode::ResourceUpdate:
                case Opcode::ModeUpdate:
                case Opcode::ScalarUpdate:
                case Opcode::RecordAction:
                    return DetailedProofNodeKind::Step;
            }
            return DetailedProofNodeKind::Step;
        }

        void releaseBudgetBytes(BudgetReport &budget, std::uint64_t bytes) noexcept {
            budget.bytes = bytes >= budget.bytes ? 0 : budget.bytes - bytes;
        }

        class ScopedBudgetBytes {
        public:
            explicit ScopedBudgetBytes(BudgetReport &budget) noexcept : budget_(budget) {}
            ScopedBudgetBytes(const ScopedBudgetBytes &) = delete;
            ScopedBudgetBytes &operator=(const ScopedBudgetBytes &) = delete;

            ~ScopedBudgetBytes() {
                releaseBudgetBytes(budget_, bytes_);
            }

            void add(std::uint64_t bytes) noexcept {
                if (bytes > std::numeric_limits<std::uint64_t>::max() - bytes_) {
                    bytes_ = std::numeric_limits<std::uint64_t>::max();
                } else {
                    bytes_ += bytes;
                }
            }

            void release(std::uint64_t bytes) noexcept {
                const std::uint64_t released = std::min(bytes, bytes_);
                bytes_ -= released;
                releaseBudgetBytes(budget_, released);
            }

            void disarm() noexcept { bytes_ = 0; }

            [[nodiscard]] std::uint64_t bytes() const noexcept { return bytes_; }

        private:
            BudgetReport &budget_;
            std::uint64_t bytes_ = 0;
        };

        std::uint64_t partitionFamilyBytes(const PartitionFamily &family) noexcept {
            std::uint64_t bytes = family.trees.size() * sizeof(PredicatePartition);
            for (const PredicatePartition &tree: family.trees) {
                const std::uint64_t nodes = tree.nodes.size() * sizeof(PartitionNode);
                if (nodes > std::numeric_limits<std::uint64_t>::max() - bytes) {
                    return std::numeric_limits<std::uint64_t>::max();
                }
                bytes += nodes;
            }
            return bytes;
        }

        std::uint64_t symbolicFrameRecordBytes(const SymbolicFrame &frame) noexcept {
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

        std::uint64_t symbolicFrameVectorBytes(const std::vector<SymbolicFrame> &frames) noexcept {
            std::uint64_t bytes = 0;
            for (const SymbolicFrame &frame: frames) {
                const std::uint64_t frameBytes = symbolicFrameRecordBytes(frame);
                if (frameBytes > std::numeric_limits<std::uint64_t>::max() - bytes) {
                    return std::numeric_limits<std::uint64_t>::max();
                }
                bytes += frameBytes;
            }
            return bytes;
        }

        std::uint64_t outputLeafClassificationVectorBytes(
            const std::vector<OutputLeafClassification> &leaves) noexcept {
            std::uint64_t bytes = 0;
            for (const OutputLeafClassification &leaf: leaves) {
                std::uint64_t leafBytes = sizeof(OutputLeafClassification);
                const std::uint64_t frameBytes = symbolicFrameRecordBytes(leaf.frame);
                if (frameBytes < sizeof(SymbolicFrame)) {
                    return std::numeric_limits<std::uint64_t>::max();
                }
                const std::uint64_t dynamicFrameBytes = frameBytes - sizeof(SymbolicFrame);
                if (dynamicFrameBytes > std::numeric_limits<std::uint64_t>::max() - leafBytes) {
                    return std::numeric_limits<std::uint64_t>::max();
                }
                leafBytes += dynamicFrameBytes;
                if (leafBytes > std::numeric_limits<std::uint64_t>::max() - bytes) {
                    return std::numeric_limits<std::uint64_t>::max();
                }
                bytes += leafBytes;
            }
            return bytes;
        }

        std::uint64_t detailedProofNodeBytes(const DetailedProofNode &node) noexcept {
            std::uint64_t bytes = sizeof(DetailedProofNode);
            auto add = [&](std::uint64_t amount) {
                if (amount > std::numeric_limits<std::uint64_t>::max() - bytes) {
                    bytes = std::numeric_limits<std::uint64_t>::max();
                } else {
                    bytes += amount;
                }
            };
            // SymbolicFrame is already part of sizeof(DetailedProofNode); only
            // count its dynamically owned storage here.
            const std::uint64_t frameBytes = symbolicFrameRecordBytes(node.claimedFrame);
            if (frameBytes >= sizeof(SymbolicFrame)) {
                add(frameBytes - sizeof(SymbolicFrame));
            }
            add(node.expectedPc.routineId.size());
            add(node.children.size() * sizeof(std::uint32_t));
            return bytes;
        }

        std::uint64_t rootProofRecordBytes(const RootProofRecord &proof) noexcept {
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
                add(detailedProofNodeBytes(node));
            }
            return bytes;
        }

        std::uint64_t checkedRootRecordBytes(const CheckedRootRecord &record) noexcept {
            if (record.verifiedPc.routineId.size() > std::numeric_limits<std::uint64_t>::max() -
                                                      sizeof(CheckedRootRecord)) {
                return std::numeric_limits<std::uint64_t>::max();
            }
            return sizeof(CheckedRootRecord) + record.verifiedPc.routineId.size();
        }

        std::uint64_t completionCheckpointBytes(const CompletionCheckpoint &checkpoint) noexcept {
            std::uint64_t bytes = sizeof(CompletionCheckpoint);
            const std::uint64_t frameBytes = symbolicFrameRecordBytes(checkpoint.frame);
            if (frameBytes < sizeof(SymbolicFrame)) {
                return std::numeric_limits<std::uint64_t>::max();
            }
            const std::uint64_t dynamicFrameBytes = frameBytes - sizeof(SymbolicFrame);
            if (dynamicFrameBytes > std::numeric_limits<std::uint64_t>::max() - bytes) {
                return std::numeric_limits<std::uint64_t>::max();
            }
            return bytes + dynamicFrameBytes;
        }

        std::uint64_t checkedEdgeBytes(const CheckedEdge &edge) noexcept {
            std::uint64_t bytes = sizeof(CheckedEdge);
            const std::uint64_t targetBytes = edge.targets.size() * sizeof(CellKey);
            const std::uint64_t termBytes = edge.weightTerms.size() * sizeof(CompletionWeightTerm);
            if (targetBytes > std::numeric_limits<std::uint64_t>::max() - bytes) {
                return std::numeric_limits<std::uint64_t>::max();
            }
            bytes += targetBytes;
            if (termBytes > std::numeric_limits<std::uint64_t>::max() - bytes) {
                return std::numeric_limits<std::uint64_t>::max();
            }
            return bytes + termBytes;
        }

        std::uint64_t snapshotAccountedBytes(const CheckedSnapshot &snapshot) noexcept {
            std::uint64_t bytes = partitionFamilyBytes(snapshot.partitions);
            auto add = [&](std::uint64_t amount) {
                if (amount > std::numeric_limits<std::uint64_t>::max() - bytes) {
                    bytes = std::numeric_limits<std::uint64_t>::max();
                } else {
                    bytes += amount;
                }
            };
            for (const RootProofRecord &proof: snapshot.proofs) {
                add(rootProofRecordBytes(proof));
            }
            for (const CheckedRootRecord &record: snapshot.coverage) {
                add(checkedRootRecordBytes(record));
            }
            for (const CompletionCheckpoint &checkpoint: snapshot.completionCheckpoints) {
                add(completionCheckpointBytes(checkpoint));
            }
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

        std::uint64_t logarithmicSearchWork(std::size_t count) noexcept {
            std::uint64_t work = 1;
            for (std::size_t n = count; n > 1; n >>= 1) {
                ++work;
            }
            return work;
        }

        std::uint64_t sortWorkEstimate(std::size_t count) noexcept {
            if (count <= 1) {
                return count;
            }
            const std::uint64_t levels = logarithmicSearchWork(count);
            if (count > std::numeric_limits<std::uint64_t>::max() / levels) {
                return std::numeric_limits<std::uint64_t>::max();
            }
            return static_cast<std::uint64_t>(count) * levels;
        }

        Box selectableDomain(const CommandProfile &profile, Box domain) {
            domain.mp.lo = std::max<std::int64_t>(domain.mp.lo, profile.minimumMp);
            domain.herb.lo = std::max<std::int64_t>(domain.herb.lo, profile.minimumHerbs);
            domain.chargeMask &= profile.selectableChargeMask;
            domain.acroMask &= profile.selectableAcroMask;
            domain.paralysisMask &= profile.selectableParalysisMask;
            return domain;
        }

        const std::vector<CompletionWeightTerm> &fullTurnCompletionTerms(
            const RuleBundle &bundle,
            int command) {
            static const std::vector<CompletionWeightTerm> emptyTerms;
            const CommandProfile *profile = lookupCommandProfile(bundle.profile, command);
            if (profile == nullptr) {
                return emptyTerms;
            }
            return profile->turnEntryCompletionTerms;
        }

        int maximumDamage(const std::vector<CompletionWeightTerm> &terms) {
            int value = 0;
            for (const CompletionWeightTerm &term: terms) {
                value = std::max(value, term.enemyDamageUpper);
            }
            return value;
        }

        Box fullTurnOutputEnvelope(
            const RuleBundle &bundle,
            const Box &root,
            const std::vector<CompletionWeightTerm> &terms) {
            Box output = root;
            output.enemyHp.lo = std::max<std::int64_t>(0, root.enemyHp.lo - maximumDamage(terms));
            output.enemyHp.hi = bundle.profile.enemyMaxHp;
            output.heroHp.lo = 0;

            int minimumMpDelta = 0;
            int minimumHerbDelta = 0;
            bool mayHealHero = false;
            for (const CompletionWeightTerm &term: terms) {
                minimumMpDelta = std::min(minimumMpDelta, term.mpDelta);
                minimumHerbDelta = std::min(minimumHerbDelta, term.herbDelta);
                mayHealHero = mayHealHero || term.heroHpGainUpper > 0;
            }

            if (mayHealHero) {
                output.heroHp.hi = bundle.profile.heroMaxHp;
            }
            output.mp.lo = root.mp.lo + minimumMpDelta;
            output.herb.lo = root.herb.lo + minimumHerbDelta;

            output.chargeMask = kChargeMaskAll;
            output.paralysisMask = kParalysisMaskAll;
            output.acroMask = kAcroMaskAll;
            output.rageMask = kRageMaskAll;
            output.inactiveMask = kInactiveMaskAll;
            output.cameraMask = kCameraMaskAll;
            return output;
        }

        std::vector<CompletionWeightTerm> completionTermsForCut(
            const RuleBundle &bundle,
            int command,
            CompletionCutId cut) {
            switch (cut) {
                case CompletionCutId::TurnEntry:
                    return fullTurnCompletionTerms(bundle, command);
                case CompletionCutId::AllyDoneEnemyPending:
                    return {{64, 0, 0, 0}};
                case CompletionCutId::EnemyDoneAllyPending:
                    if (command == BattleEmulator::ATTACK_ALLY) {
                        return {{64, 0, 0, 0}};
                    }
                    if (command == BattleEmulator::DRAGON_SLASH) {
                        return {{34, 0, 0, 0}};
                    }
                    if (command == BattleEmulator::DEFENCE ||
                        command == BattleEmulator::FLEE_ALLY ||
                        command == BattleEmulator::ACROBATIC_STAR) {
                        return {{0, 0, 0, 0}};
                    }
                    if (command == BattleEmulator::CRACK_ALLY) {
                        return {{0, 0, 0, 0}, {34, 0, -3, 0}};
                    }
                    if (command == BattleEmulator::HEAL) {
                        return {{0, 0, 0, 0}, {0, 65, -2, 0}};
                    }
                    if (command == BattleEmulator::MEDICINAL_HERBS) {
                        return {{0, 0, 0, 0}, {0, 39, 0, -1}};
                    }
                    return {};
                case CompletionCutId::ActionsDone:
                    return {{0, 0, 0, 0}};
            }
            return {};
        }

        bool materializeDefaultProofRecords(
            const RuleBundle &bundle,
            CheckedSnapshot &snapshot,
            const ProofBudget &limits,
            BudgetReport &budget,
            std::string &error) {
            if (snapshot.proofs.size() == snapshot.coverage.size()) {
                return true;
            }

            const CompletionSite *turnEntry = lookupCompletionSite(
                bundle.profile,
                CompletionCutId::TurnEntry);
            if (turnEntry == nullptr) {
                error = "registered TurnEntry COMPLETE site is missing while materializing certificate proofs";
                return false;
            }

            const std::vector<RootProofRecord> &retainedStrongProofs = snapshot.proofs;
            std::vector<RootProofRecord> materialized;
            materialized.reserve(snapshot.coverage.size());
            std::size_t strongIndex = 0;
            ScopedBudgetBytes replacementBytes(budget);
            std::uint64_t oldStrongProofBytes = 0;

            for (const CheckedRootRecord &record: snapshot.coverage) {
                if (!chargeBudget(budget, limits, 1, 0)) {
                    error = "certificate proof materialization exceeded proof budget";
                    return false;
                }
                const bool defaultTurnEntry =
                    record.proofKind == RootProofKind::Completion &&
                    record.verifiedCut == CompletionCutId::TurnEntry;
                if (defaultTurnEntry) {
                    RootProofRecord proof;
                    proof.kind = RootProofKind::Completion;
                    proof.elapsedTurn = record.elapsedTurn;
                    proof.source = record.source;
                    proof.selectedCommand = record.selectedCommand;
                    proof.partitionVersion = record.partitionVersion;
                    proof.coverageVersion = record.coverageVersion;
                    proof.expectedPc = turnEntry->pc;
                    proof.cut = CompletionCutId::TurnEntry;
                    const std::vector<CompletionWeightTerm> terms =
                        completionTermsForCut(bundle, record.selectedCommand, proof.cut);
                    if (terms.empty() || terms.size() > limits.maxCompletionTermsPerAction) {
                        error = "default TurnEntry proof materialization has an invalid completion case count";
                        return false;
                    }
                    proof.completionCases.reserve(terms.size());
                    for (std::size_t caseIndex = 0; caseIndex < terms.size(); ++caseIndex) {
                        proof.completionCases.push_back({
                            static_cast<std::uint32_t>(caseIndex),
                            CompletionTargetKind::AllLeavesInCheckedRange,
                        });
                    }
                    const std::uint64_t proofBytes = rootProofRecordBytes(proof);
                    if (!chargeBudget(budget, limits, 0, proofBytes)) {
                        error = "certificate default proof storage exceeded proof budget";
                        return false;
                    }
                    replacementBytes.add(proofBytes);
                    materialized.push_back(std::move(proof));
                    continue;
                }

                if (strongIndex >= retainedStrongProofs.size()) {
                    error = "certificate materialization is missing a retained stronger proof";
                    return false;
                }
                const RootProofRecord &proof = retainedStrongProofs[strongIndex++];
                if (proof.kind != record.proofKind ||
                    proof.elapsedTurn != record.elapsedTurn ||
                    !(proof.source == record.source) ||
                    proof.selectedCommand != record.selectedCommand ||
                    proof.partitionVersion != record.partitionVersion ||
                    proof.coverageVersion != record.coverageVersion ||
                    proof.cut != record.verifiedCut ||
                    proof.expectedPc != record.verifiedPc) {
                    error = "retained stronger proof does not match checked coverage during certificate materialization";
                    return false;
                }
                const std::uint64_t proofBytes = rootProofRecordBytes(proof);
                if (!chargeBudget(budget, limits, 0, proofBytes)) {
                    error = "certificate stronger proof replacement exceeded proof budget";
                    return false;
                }
                replacementBytes.add(proofBytes);
                if (proofBytes > std::numeric_limits<std::uint64_t>::max() - oldStrongProofBytes) {
                    error = "certificate stronger proof byte count overflow";
                    return false;
                }
                oldStrongProofBytes += proofBytes;
                materialized.push_back(proof);
            }
            if (strongIndex != retainedStrongProofs.size()) {
                error = "certificate materialization has unused retained stronger proofs";
                return false;
            }

            snapshot.proofs = std::move(materialized);
            releaseBudgetBytes(budget, oldStrongProofBytes);
            replacementBytes.disarm();
            return true;
        }

        bool completionCasesMatch(
            const RootProofRecord &proof,
            const std::vector<CompletionWeightTerm> &remainingTerms,
            std::uint32_t maximumTerms,
            std::string &error) {
            if (remainingTerms.empty() || remainingTerms.size() > maximumTerms ||
                proof.completionCases.size() != remainingTerms.size()) {
                error = "COMPLETE does not contain every required case_id";
                return false;
            }
            std::vector<bool> seen(remainingTerms.size(), false);
            for (const CompletionProofCase &proofCase: proof.completionCases) {
                if (proofCase.caseId >= remainingTerms.size() ||
                    proofCase.targetKind != CompletionTargetKind::AllLeavesInCheckedRange ||
                    seen[proofCase.caseId]) {
                    error = "COMPLETE has an invalid, duplicate, or unsupported case";
                    return false;
                }
                seen[proofCase.caseId] = true;
            }
            return true;
        }

        bool prefixWeightTerm(
            const SymbolicFrame &frame,
            CompletionWeightTerm &term,
            std::string &error) {
            const ResourceExpression &enemy = frame.resources[static_cast<std::size_t>(ResourceAxis::EnemyHp)];
            const ResourceExpression &hero = frame.resources[static_cast<std::size_t>(ResourceAxis::HeroHp)];
            const ResourceExpression &mp = frame.resources[static_cast<std::size_t>(ResourceAxis::Mp)];
            const ResourceExpression &herb = frame.resources[static_cast<std::size_t>(ResourceAxis::Herb)];

            auto maximumInputMinusOutput = [&](ResourceAxis axis, const ResourceExpression &expression,
                                               std::int64_t &value) -> bool {
                if (expression.kind == ResourceExpressionKind::InputOffset) {
                    value = std::max<std::int64_t>(0, -expression.value);
                    return true;
                }
                const Interval &input = intervalOf(frame.inputDomain, axis);
                value = std::max<std::int64_t>(0, input.hi - expression.value);
                return true;
            };
            auto maximumOutputMinusInput = [&](ResourceAxis axis, const ResourceExpression &expression,
                                               std::int64_t &value) -> bool {
                if (expression.kind == ResourceExpressionKind::InputOffset) {
                    value = std::max<std::int64_t>(0, expression.value);
                    return true;
                }
                const Interval &input = intervalOf(frame.inputDomain, axis);
                value = std::max<std::int64_t>(0, expression.value - input.lo);
                return true;
            };
            auto maximumDelta = [&](ResourceAxis axis, const ResourceExpression &expression,
                                    std::int64_t &value) -> bool {
                if (expression.kind == ResourceExpressionKind::InputOffset) {
                    value = expression.value;
                    return true;
                }
                const Interval &input = intervalOf(frame.inputDomain, axis);
                value = expression.value - input.lo;
                return true;
            };

            std::int64_t enemyDamage = 0;
            std::int64_t heroGain = 0;
            std::int64_t mpDelta = 0;
            std::int64_t herbDelta = 0;
            if (!maximumInputMinusOutput(ResourceAxis::EnemyHp, enemy, enemyDamage) ||
                !maximumOutputMinusInput(ResourceAxis::HeroHp, hero, heroGain) ||
                !maximumDelta(ResourceAxis::Mp, mp, mpDelta) ||
                !maximumDelta(ResourceAxis::Herb, herb, herbDelta)) {
                error = "failed to derive exact prefix resource bound";
                return false;
            }
            if (enemyDamage > std::numeric_limits<int>::max() ||
                heroGain > std::numeric_limits<int>::max() ||
                mpDelta < std::numeric_limits<int>::min() || mpDelta > 0 ||
                herbDelta < std::numeric_limits<int>::min() || herbDelta > 0) {
                error = "prefix resource bound is outside registered completion domain";
                return false;
            }
            term.enemyDamageUpper = static_cast<int>(enemyDamage);
            term.heroHpGainUpper = static_cast<int>(heroGain);
            term.mpDelta = static_cast<int>(mpDelta);
            term.herbDelta = static_cast<int>(herbDelta);
            return true;
        }

        bool addCompletionTerm(
            const CompletionWeightTerm &prefix,
            const CompletionWeightTerm &remaining,
            CompletionWeightTerm &total,
            std::string &error) {
            const long long enemy = static_cast<long long>(prefix.enemyDamageUpper) +
                                    remaining.enemyDamageUpper;
            const long long hero = static_cast<long long>(prefix.heroHpGainUpper) +
                                   remaining.heroHpGainUpper;
            const long long mp = static_cast<long long>(prefix.mpDelta) + remaining.mpDelta;
            const long long herb = static_cast<long long>(prefix.herbDelta) + remaining.herbDelta;
            if (enemy < 0 || enemy > std::numeric_limits<int>::max() ||
                hero < 0 || hero > std::numeric_limits<int>::max() ||
                mp < std::numeric_limits<int>::min() || mp > 0 ||
                herb < std::numeric_limits<int>::min() || herb > 0) {
                error = "COMPLETE total resource bound overflow";
                return false;
            }
            total = {
                static_cast<int>(enemy),
                static_cast<int>(hero),
                static_cast<int>(mp),
                static_cast<int>(herb),
            };
            return true;
        }

        bool shiftInterval(Interval &interval, int delta, std::string &error) {
            const std::int64_t amount = delta;
            if ((amount > 0 &&
                 (interval.lo > std::numeric_limits<std::int64_t>::max() - amount ||
                  interval.hi > std::numeric_limits<std::int64_t>::max() - amount)) ||
                (amount < 0 &&
                 (interval.lo < std::numeric_limits<std::int64_t>::min() - amount ||
                  interval.hi < std::numeric_limits<std::int64_t>::min() - amount))) {
                error = "COMPLETE resource interval overflow";
                return false;
            }
            interval = {interval.lo + amount, interval.hi + amount};
            return true;
        }

        bool completionOutputEnvelope(
            const RuleBundle &bundle,
            CompletionCutId cut,
            const Box &current,
            const CompletionWeightTerm &remaining,
            Box &output,
            std::string &error) {
            output = current;
            switch (cut) {
                case CompletionCutId::TurnEntry:
                    output.enemyHp.lo = std::max<std::int64_t>(0, current.enemyHp.lo - remaining.enemyDamageUpper);
                    output.enemyHp.hi = bundle.profile.enemyMaxHp;
                    output.heroHp.lo = 0;
                    output.heroHp.hi = remaining.heroHpGainUpper > 0
                        ? bundle.profile.heroMaxHp
                        : current.heroHp.hi;
                    break;
                case CompletionCutId::AllyDoneEnemyPending:
                    output.enemyHp.lo = std::max<std::int64_t>(0, current.enemyHp.lo - remaining.enemyDamageUpper);
                    output.enemyHp.hi = bundle.profile.enemyMaxHp;
                    output.heroHp.lo = 0;
                    output.heroHp.hi = current.heroHp.hi;
                    break;
                case CompletionCutId::EnemyDoneAllyPending:
                    output.enemyHp.lo = std::max<std::int64_t>(0, current.enemyHp.lo - remaining.enemyDamageUpper);
                    output.enemyHp.hi = current.enemyHp.hi;
                    output.heroHp.lo = 0;
                    output.heroHp.hi = remaining.heroHpGainUpper > 0
                        ? bundle.profile.heroMaxHp
                        : current.heroHp.hi;
                    break;
                case CompletionCutId::ActionsDone:
                    break;
            }

            if (remaining.mpDelta < 0) {
                if (current.mp.lo < -remaining.mpDelta) {
                    error = "COMPLETE MP cost is not affordable over the whole cut domain";
                    return false;
                }
                if (!shiftInterval(output.mp, remaining.mpDelta, error)) {
                    return false;
                }
            }
            if (remaining.herbDelta < 0) {
                if (current.herb.lo < -remaining.herbDelta) {
                    error = "COMPLETE herb cost is not affordable over the whole cut domain";
                    return false;
                }
                if (!shiftInterval(output.herb, remaining.herbDelta, error)) {
                    return false;
                }
            }

            output.chargeMask = kChargeMaskAll;
            output.paralysisMask = kParalysisMaskAll;
            output.acroMask = kAcroMaskAll;
            output.rageMask = kRageMaskAll;
            output.inactiveMask = kInactiveMaskAll;
            output.cameraMask = kCameraMaskAll;
            return !output.empty();
        }

        CompletionCutId cutForPoint(const RuleProfile &profile, const ProgramPoint &point, bool &found) {
            for (const CompletionSite &site: profile.completionSites) {
                if (site.pc == point) {
                    found = true;
                    return site.cut;
                }
            }
            found = false;
            return CompletionCutId::TurnEntry;
        }

        struct ReservationControlState {
            int routineIndex = -1;
            int pc = -1;
            std::vector<std::pair<int, int>> returnStack;
        };

        struct ReservationControlStateLess {
            bool operator()(const ReservationControlState &a, const ReservationControlState &b) const noexcept {
                if (a.routineIndex != b.routineIndex) {
                    return a.routineIndex < b.routineIndex;
                }
                if (a.pc != b.pc) {
                    return a.pc < b.pc;
                }
                return a.returnStack < b.returnStack;
            }
        };

        struct FirstCutSummary {
            std::uint8_t firstCutMask = 0;
            bool mayFinishWithoutCut = false;
        };

        struct ReservationMemoValue {
            bool visiting = false;
            bool ready = false;
            std::uint64_t work = 0;
        };

        int reservationRoutineIndex(const RuleProgram &program, std::string_view id) {
            for (std::size_t index = 0; index < program.routines.size(); ++index) {
                if (program.routines[index].id == id) {
                    return static_cast<int>(index);
                }
            }
            return -1;
        }

        std::uint64_t saturatedReservationAdd(
            std::uint64_t a,
            std::uint64_t b,
            std::uint64_t cap) noexcept {
            if (a >= cap || b >= cap || b > cap - a) {
                return cap;
            }
            return a + b;
        }

        std::uint64_t saturatedReservationMultiply(
            std::uint64_t a,
            std::uint64_t b,
            std::uint64_t cap) noexcept {
            if (a == 0 || b == 0) {
                return 0;
            }
            if (a >= cap || b >= cap || a > cap / b) {
                return cap;
            }
            return a * b;
        }

        std::uint64_t branchPieceUpperBound(
            const BranchCondition &condition,
            std::uint64_t cap) noexcept {
            std::uint64_t pieces = 1;
            for (const ConditionClause &clause: condition.any) {
                for ([[maybe_unused]] const Comparison &comparison: clause.all) {
                    pieces = saturatedReservationMultiply(pieces, 3, cap);
                    if (pieces >= cap) {
                        return cap;
                    }
                }
            }
            return pieces;
        }

        std::uint64_t instructionFrameUpperBound(
            const Instruction &instruction,
            std::uint64_t cap) noexcept {
            switch (instruction.opcode) {
                case Opcode::Branch:
                    return branchPieceUpperBound(instruction.branchCondition, cap);
                case Opcode::Switch:
                    return instruction.switchOperand.kind == SwitchSourceKind::CurrentAction
                        ? 8u
                        : 1u;
                case Opcode::ReadRng:
                    return instruction.rngRead.kind == RngReadKind::PercentCameraRemaining
                        ? 6u
                        : 1u;
                case Opcode::ResourceUpdate:
                    return instruction.resourceUpdate.kind == ResourceUpdateKind::AddConstant
                        ? 1u
                        : 3u;
                case Opcode::RecordAction:
                    return 8u;
                case Opcode::Step:
                case Opcode::Call:
                case Opcode::Return:
                case Opcode::Finish:
                case Opcode::SkipRng:
                case Opcode::Native:
                case Opcode::ModeUpdate:
                case Opcode::ScalarUpdate:
                    return 1u;
            }
            return cap;
        }

        std::optional<std::uint64_t> nativeWorkForReservation(
            const RuleBundle &bundle,
            const Instruction &instruction,
            std::string &error) {
            if (instruction.opcode != Opcode::Native) {
                return 0;
            }
            for (const NativeContract &contract: bundle.nativeContracts) {
                if (contract.id == instruction.nativeId) {
                    if (contract.maxWork < 0) {
                        error = "registered NATIVE has a negative internal work bound";
                        return std::nullopt;
                    }
                    return static_cast<std::uint64_t>(contract.maxWork);
                }
            }
            error = "reservation planner found an unregistered NATIVE contract";
            return std::nullopt;
        }

        bool reservationSuccessors(
            const RuleBundle &bundle,
            const ReservationControlState &state,
            const Instruction &instruction,
            std::vector<ReservationControlState> &successors,
            std::string &error) {
            successors.clear();
            if (state.routineIndex < 0 || state.routineIndex >= static_cast<int>(bundle.program.routines.size())) {
                error = "reservation planner references an invalid routine";
                return false;
            }
            switch (instruction.opcode) {
                case Opcode::Finish:
                    return true;
                case Opcode::Call: {
                    if (instruction.successors.size() != 1) {
                        error = "reservation planner found CALL without one return pc";
                        return false;
                    }
                    const int callee = reservationRoutineIndex(bundle.program, instruction.callTarget);
                    if (callee < 0) {
                        error = "reservation planner found an unresolved CALL target";
                        return false;
                    }
                    ReservationControlState child;
                    child.routineIndex = callee;
                    child.pc = 0;
                    child.returnStack = state.returnStack;
                    child.returnStack.emplace_back(state.routineIndex, instruction.successors.front());
                    if (child.returnStack.size() > static_cast<std::size_t>(bundle.bounds.maximumCallDepth)) {
                        error = "reservation planner exceeded registered call depth";
                        return false;
                    }
                    successors.push_back(std::move(child));
                    return true;
                }
                case Opcode::Return: {
                    if (state.returnStack.empty()) {
                        error = "reservation planner reached RETURN with empty stack";
                        return false;
                    }
                    ReservationControlState child = state;
                    const auto [routineIndex, pc] = child.returnStack.back();
                    child.returnStack.pop_back();
                    child.routineIndex = routineIndex;
                    child.pc = pc;
                    successors.push_back(std::move(child));
                    return true;
                }
                default:
                    break;
            }
            if (instruction.successors.empty()) {
                error = "reservation planner found a nonterminal instruction without successor";
                return false;
            }
            for (int pc: instruction.successors) {
                ReservationControlState child = state;
                child.pc = pc;
                successors.push_back(std::move(child));
            }
            return true;
        }

        std::uint64_t reservationStateBytes(const ReservationControlState &state) noexcept {
            return sizeof(ReservationControlState) +
                   state.returnStack.size() * sizeof(std::pair<int, int>) +
                   sizeof(ReservationMemoValue) + 4 * sizeof(void *);
        }

        bool remainingConcreteWorkBound(
            const RuleBundle &bundle,
            const SymbolicFrame &frame,
            std::uint64_t &remaining,
            std::string &error) {
            remaining = 0;
            auto addPc = [&](const std::string &routineId, int pc) -> bool {
                const PcStaticBounds *bounds = lookupPcBounds(bundle.bounds, routineId, pc);
                if (bounds == nullptr) {
                    error = "missing static work bound for completion resume point";
                    return false;
                }
                if (bounds->maximumWork > std::numeric_limits<std::uint64_t>::max() - remaining) {
                    error = "static work bound overflow for completion resume point";
                    return false;
                }
                remaining += bounds->maximumWork;
                return true;
            };
            if (!addPc(frame.routineId, frame.pc)) {
                return false;
            }
            for (auto it = frame.callStack.rbegin(); it != frame.callStack.rend(); ++it) {
                if (!addPc(it->routineId, it->pc)) {
                    return false;
                }
            }
            return true;
        }

        bool reserveCompletionProofWork(
            const RuleBundle &bundle,
            const SymbolicFrame &frame,
            CompletionCutId currentCut,
            const ProofBudget &limits,
            BudgetReport &budget,
            std::uint64_t &requiredWork,
            std::string &error) {
            ReservationControlState root;
            root.routineIndex = reservationRoutineIndex(bundle.program, frame.routineId);
            root.pc = frame.pc;
            if (root.routineIndex < 0) {
                error = "completion reservation root routine is not registered";
                return false;
            }
            root.returnStack.reserve(frame.callStack.size());
            for (const ReturnAddress &address: frame.callStack) {
                const int routineIndex = reservationRoutineIndex(bundle.program, address.routineId);
                if (routineIndex < 0) {
                    error = "completion reservation stack contains an unregistered routine";
                    return false;
                }
                root.returnStack.emplace_back(routineIndex, address.pc);
            }
            if (root.returnStack.size() > static_cast<std::size_t>(bundle.bounds.maximumCallDepth)) {
                error = "completion reservation root exceeds registered call depth";
                return false;
            }

            const std::uint64_t cap = limits.maxWork == std::numeric_limits<std::uint64_t>::max()
                ? limits.maxWork
                : limits.maxWork + 1;
            std::uint64_t temporaryBytes = 0;
            auto releaseTemporary = [&]() {
                releaseBudgetBytes(budget, temporaryBytes);
                temporaryBytes = 0;
            };

            std::map<ReservationControlState, FirstCutSummary, ReservationControlStateLess> firstCutMemo;
            std::set<ReservationControlState, ReservationControlStateLess> firstCutVisiting;
            std::function<std::optional<FirstCutSummary>(const ReservationControlState &)> firstCuts =
                [&](const ReservationControlState &state) -> std::optional<FirstCutSummary> {
                    if (const auto found = firstCutMemo.find(state); found != firstCutMemo.end()) {
                        return found->second;
                    }
                    if (firstCutVisiting.contains(state)) {
                        error = "completion reservation found a control cycle";
                        return std::nullopt;
                    }
                    const std::uint64_t stateBytes = reservationStateBytes(state);
                    if (!chargeBudget(budget, limits, 1, stateBytes)) {
                        error = "completion reservation analysis exceeded proof budget";
                        return std::nullopt;
                    }
                    temporaryBytes += stateBytes;
                    firstCutVisiting.insert(state);

                    if (state.routineIndex < 0 || state.routineIndex >= static_cast<int>(bundle.program.routines.size())) {
                        error = "completion reservation references an invalid routine";
                        return std::nullopt;
                    }
                    const Routine &routine = bundle.program.routines[state.routineIndex];
                    if (state.pc < 0 || state.pc >= static_cast<int>(routine.instructions.size())) {
                        error = "completion reservation references an invalid pc";
                        return std::nullopt;
                    }
                    const ProgramPoint point{routine.id, state.pc};
                    bool isCut = false;
                    const CompletionCutId cut = cutForPoint(bundle.profile, point, isCut);
                    FirstCutSummary summary;
                    if (isCut && cut != currentCut) {
                        summary.firstCutMask = static_cast<std::uint8_t>(1u << static_cast<unsigned>(cut));
                    } else {
                        const Instruction &instruction = routine.instructions[state.pc];
                        if (instruction.opcode == Opcode::Finish) {
                            summary.mayFinishWithoutCut = true;
                        } else {
                            std::vector<ReservationControlState> children;
                            if (!reservationSuccessors(bundle, state, instruction, children, error)) {
                                return std::nullopt;
                            }
                            for (const ReservationControlState &child: children) {
                                const std::optional<FirstCutSummary> childSummary = firstCuts(child);
                                if (!childSummary.has_value()) {
                                    return std::nullopt;
                                }
                                summary.firstCutMask |= childSummary->firstCutMask;
                                summary.mayFinishWithoutCut =
                                    summary.mayFinishWithoutCut || childSummary->mayFinishWithoutCut;
                            }
                        }
                    }
                    firstCutVisiting.erase(state);
                    firstCutMemo.emplace(state, summary);
                    return summary;
                };

            const std::optional<FirstCutSummary> rootSummary = firstCuts(root);
            if (!rootSummary.has_value()) {
                releaseTemporary();
                return false;
            }

            auto workForTarget = [&](std::optional<CompletionCutId> target) -> std::optional<std::uint64_t> {
                std::map<ReservationControlState, ReservationMemoValue, ReservationControlStateLess> memo;
                std::function<std::optional<std::uint64_t>(const ReservationControlState &)> visit =
                    [&](const ReservationControlState &state) -> std::optional<std::uint64_t> {
                        auto found = memo.find(state);
                        if (found != memo.end()) {
                            if (found->second.visiting) {
                                error = "completion reservation found a control cycle";
                                return std::nullopt;
                            }
                            if (found->second.ready) {
                                return found->second.work;
                            }
                        } else {
                            const std::uint64_t stateBytes = reservationStateBytes(state);
                            if (!chargeBudget(budget, limits, 1, stateBytes)) {
                                error = "completion reservation analysis exceeded proof budget";
                                return std::nullopt;
                            }
                            temporaryBytes += stateBytes;
                            ReservationMemoValue initial;
                            initial.visiting = true;
                            found = memo.emplace(state, initial).first;
                        }

                        if (state.routineIndex < 0 || state.routineIndex >= static_cast<int>(bundle.program.routines.size())) {
                            error = "completion reservation references an invalid routine";
                            return std::nullopt;
                        }
                        const Routine &routine = bundle.program.routines[state.routineIndex];
                        if (state.pc < 0 || state.pc >= static_cast<int>(routine.instructions.size())) {
                            error = "completion reservation references an invalid pc";
                            return std::nullopt;
                        }
                        const ProgramPoint point{routine.id, state.pc};
                        bool isCut = false;
                        const CompletionCutId cut = cutForPoint(bundle.profile, point, isCut);
                        if (target.has_value() && isCut && cut == *target) {
                            found->second.visiting = false;
                            found->second.ready = true;
                            found->second.work = 0;
                            return 0;
                        }

                        const Instruction &instruction = routine.instructions[state.pc];
                        const std::optional<std::uint64_t> nativeWork =
                            nativeWorkForReservation(bundle, instruction, error);
                        if (!nativeWork.has_value()) {
                            return std::nullopt;
                        }
                        const std::uint64_t frameCount = instructionFrameUpperBound(instruction, cap);
                        std::uint64_t ownWork = saturatedReservationAdd(1, *nativeWork, cap);
                        ownWork = saturatedReservationAdd(ownWork, frameCount, cap);
                        if (instruction.opcode == Opcode::Finish) {
                            found->second.visiting = false;
                            found->second.ready = true;
                            found->second.work = ownWork;
                            return ownWork;
                        }

                        std::vector<ReservationControlState> children;
                        if (!reservationSuccessors(bundle, state, instruction, children, error)) {
                            return std::nullopt;
                        }
                        std::uint64_t childMaximum = 0;
                        for (const ReservationControlState &child: children) {
                            const std::optional<std::uint64_t> childWork = visit(child);
                            if (!childWork.has_value()) {
                                return std::nullopt;
                            }
                            childMaximum = std::max(childMaximum, *childWork);
                        }
                        const std::uint64_t descendants =
                            saturatedReservationMultiply(frameCount, childMaximum, cap);
                        const std::uint64_t total = saturatedReservationAdd(ownWork, descendants, cap);
                        found->second.visiting = false;
                        found->second.ready = true;
                        found->second.work = total;
                        return total;
                    };
                return visit(root);
            };

            requiredWork = 0;
            for (unsigned rawCut = 0; rawCut < 4; ++rawCut) {
                if ((rootSummary->firstCutMask & (1u << rawCut)) == 0) {
                    continue;
                }
                const std::optional<std::uint64_t> candidate =
                    workForTarget(static_cast<CompletionCutId>(rawCut));
                if (!candidate.has_value()) {
                    releaseTemporary();
                    return false;
                }
                requiredWork = std::max(requiredWork, *candidate);
            }
            if (rootSummary->mayFinishWithoutCut) {
                const std::optional<std::uint64_t> candidate = workForTarget(std::nullopt);
                if (!candidate.has_value()) {
                    releaseTemporary();
                    return false;
                }
                requiredWork = std::max(requiredWork, *candidate);
            }
            releaseTemporary();

            const std::uint64_t remainingWork =
                budget.work >= limits.maxWork ? 0 : limits.maxWork - budget.work;
            if (requiredWork > remainingWork) {
                error = "completion resume cannot reserve proof work to the next registered cut";
                return false;
            }
            if (deadlineExceeded(limits, budget)) {
                error = "completion resume cannot reserve work before the shared deadline";
                return false;
            }
            return true;
        }

        const PredicatePartition *partitionAt(const PartitionFamily &family, int position) {
            for (const PredicatePartition &partition: family.trees) {
                if (partition.rngPosition == position) {
                    return &partition;
                }
            }
            return nullptr;
        }

        const Box *leafDomain(const CheckedPartition &partition, std::uint32_t localCellId) {
            for (const auto &[leafId, domain]: partition.leaves) {
                if (leafId == localCellId) {
                    return &domain;
                }
            }
            return nullptr;
        }

        bool checkedMultiply(std::int64_t a, std::int64_t b, std::int64_t &out) {
            if (a == 0 || b == 0) {
                out = 0;
                return true;
            }
            if (a == -1 && b == std::numeric_limits<std::int64_t>::min()) {
                return false;
            }
            if (b == -1 && a == std::numeric_limits<std::int64_t>::min()) {
                return false;
            }
            if (a > 0) {
                if (b > 0 && a > std::numeric_limits<std::int64_t>::max() / b) {
                    return false;
                }
                if (b < 0 && b < std::numeric_limits<std::int64_t>::min() / a) {
                    return false;
                }
            } else {
                if (b > 0 && a < std::numeric_limits<std::int64_t>::min() / b) {
                    return false;
                }
                if (b < 0 && a < std::numeric_limits<std::int64_t>::max() / b) {
                    return false;
                }
            }
            out = a * b;
            return true;
        }

        bool checkedAdd(std::int64_t a, std::int64_t b, std::int64_t &out) {
            if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
                (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b)) {
                return false;
            }
            out = a + b;
            return true;
        }

        bool completionTermWeight(
            const CompletionWeightTerm &term,
            int q,
            int u,
            int v,
            int w,
            std::int64_t &value) {
            value = 0;
            const std::pair<int, int> factors[] = {
                {q, term.enemyDamageUpper},
                {u, term.heroHpGainUpper},
                {v, term.mpDelta},
                {w, term.herbDelta},
            };
            for (const auto &[coefficient, delta]: factors) {
                std::int64_t product = 0;
                std::int64_t sum = 0;
                if (!checkedMultiply(coefficient, delta, product) || !checkedAdd(value, product, sum)) {
                    return false;
                }
                value = sum;
            }
            return true;
        }

        const std::vector<CompletionWeightTerm> *checkedEdgeWeightTerms(
            const RuleBundle &bundle,
            const CheckedEdge &edge) {
            if (!edge.useRegisteredTurnEntryTerms) {
                return &edge.weightTerms;
            }
            if (edge.kind != CheckedEdgeKind::Completion ||
                edge.completionCut != CompletionCutId::TurnEntry ||
                !edge.weightTerms.empty()) {
                return nullptr;
            }
            const CommandProfile *profile = lookupCommandProfile(bundle.profile, edge.selectedCommand);
            if (profile == nullptr || profile->turnEntryCompletionTerms.empty()) {
                return nullptr;
            }
            return &profile->turnEntryCompletionTerms;
        }

        bool completionWeight(
            const CheckedEdge &edge,
            int q,
            int u,
            int v,
            int w,
            std::int64_t &weight) {
            bool any = false;
            std::int64_t best = 0;
            for (const CompletionWeightTerm &term: edge.weightTerms) {
                std::int64_t value = 0;
                if (!completionTermWeight(term, q, u, v, w, value)) {
                    return false;
                }
                best = any ? std::max(best, value) : value;
                any = true;
            }
            if (!any) {
                return false;
            }
            weight = best;
            return true;
        }

        bool sameCellVector(const std::vector<CellKey> &a, const std::vector<CellKey> &b) {
            return a == b;
        }

        bool hasSuccessDpDestination(const CheckedEdge &edge) noexcept {
            return edge.mayReachGoal || edge.hasContinuingOutput;
        }

        bool sameSuccessDpTerm(const CheckedEdge &a, const CheckedEdge &b) {
            if (a.kind != b.kind || !(a.source == b.source) ||
                a.selectedCommand != b.selectedCommand ||
                a.mayReachGoal != b.mayReachGoal ||
                a.hasContinuingOutput != b.hasContinuingOutput ||
                a.weightTerms != b.weightTerms) {
                return false;
            }
            if (!a.hasContinuingOutput) {
                return true;
            }
            if (a.kind == CheckedEdgeKind::Completion) {
                return a.firstOutputPosition == b.firstOutputPosition &&
                       a.lastOutputPosition == b.lastOutputPosition;
            }
            return sameCellVector(a.targets, b.targets);
        }

        void includeDominatingCompletionTerm(
            CompletionWeightTerm &aggregate,
            bool &initialized,
            const CompletionWeightTerm &term) noexcept {
            if (!initialized) {
                aggregate = term;
                initialized = true;
                return;
            }
            // All proof-price coefficients are nonnegative.  Componentwise
            // maxima therefore dominate every source linear form for every
            // admissible integer price, including negative resource deltas.
            aggregate.enemyDamageUpper = std::max(aggregate.enemyDamageUpper, term.enemyDamageUpper);
            aggregate.heroHpGainUpper = std::max(aggregate.heroHpGainUpper, term.heroHpGainUpper);
            aggregate.mpDelta = std::max(aggregate.mpDelta, term.mpDelta);
            aggregate.herbDelta = std::max(aggregate.herbDelta, term.herbDelta);
        }

        bool buildDetailedProofTree(
            const RuleBundle &bundle,
            const Problem &problem,
            const SymbolicFrame &rootFrame,
            RootProofKind proofKind,
            CompletionCutId completionCut,
            const ProofBudget &limits,
            BudgetReport &budget,
            std::vector<DetailedProofNode> &nodes,
            std::string &error) {
            nodes.clear();
            if (proofKind == RootProofKind::Completion &&
                completionCut == CompletionCutId::TurnEntry) {
                error = "turn-entry COMPLETE does not use a detailed proof tree";
                return false;
            }

            const CompletionSite *completionSite = nullptr;
            if (proofKind == RootProofKind::Completion) {
                completionSite = lookupCompletionSite(bundle.profile, completionCut);
                if (completionSite == nullptr) {
                    error = "detailed proof tree references an unregistered COMPLETE cut";
                    return false;
                }
            }

            SymbolicStepper stepper(bundle, problem);
            ScopedBudgetBytes temporaryBytes(budget);
            std::vector<std::uint32_t> pending;

            auto appendNode = [&](const SymbolicFrame &frame, std::uint32_t &index) -> bool {
                if (nodes.size() >= std::numeric_limits<std::uint32_t>::max()) {
                    error = "detailed proof tree node index overflow";
                    return false;
                }
                DetailedProofNode node;
                node.expectedPc = stepper.point(frame);
                node.claimedFrame = frame;
                const std::uint64_t bytes = detailedProofNodeBytes(node);
                if (!chargeBudget(budget, limits, 0, bytes)) {
                    error = "detailed proof tree generation exceeded byte budget";
                    return false;
                }
                temporaryBytes.add(bytes);
                index = static_cast<std::uint32_t>(nodes.size());
                nodes.push_back(std::move(node));
                return true;
            };

            std::uint32_t rootIndex = 0;
            if (!appendNode(rootFrame, rootIndex) || rootIndex != 0) {
                if (error.empty()) {
                    error = "detailed proof tree could not create its root";
                }
                return false;
            }
            if (!chargeBudget(budget, limits, 0, sizeof(std::uint32_t))) {
                error = "detailed proof tree work stack exceeded byte budget";
                return false;
            }
            temporaryBytes.add(sizeof(std::uint32_t));
            pending.push_back(rootIndex);

            while (!pending.empty()) {
                const std::uint32_t nodeIndex = pending.back();
                pending.pop_back();
                temporaryBytes.release(sizeof(std::uint32_t));
                if (nodeIndex >= nodes.size()) {
                    error = "detailed proof tree generator produced an invalid node index";
                    return false;
                }
                if (!chargeBudget(budget, limits, 1, 0)) {
                    error = "detailed proof tree generation exceeded work budget";
                    return false;
                }

                const SymbolicFrame &frame = nodes[nodeIndex].claimedFrame;
                if (completionSite != nullptr && stepper.point(frame) == completionSite->pc) {
                    nodes[nodeIndex].kind = DetailedProofNodeKind::Complete;
                    continue;
                }

                const Instruction *instruction = instructionAt(bundle, frame, error);
                if (instruction == nullptr) {
                    return false;
                }
                nodes[nodeIndex].kind = detailedNodeKindForOpcode(instruction->opcode);

                std::string nativeError;
                const std::optional<std::uint64_t> nativeWork =
                    nativeInternalWorkAt(bundle, frame, nativeError);
                if (!nativeWork.has_value()) {
                    error = nativeError;
                    return false;
                }
                if (*nativeWork != 0 && !chargeBudget(budget, limits, *nativeWork, 0)) {
                    error = "detailed proof tree NATIVE generation exceeded work budget";
                    return false;
                }

                SymbolicStepResult step = stepper.step(frame);
                if (!step.accepted) {
                    error = "detailed proof tree symbolic generation failed at " +
                            frame.routineId + ":" + std::to_string(frame.pc) + ": " + step.reason;
                    return false;
                }
                ScopedBudgetBytes stepFrameBytes(budget);
                const std::uint64_t generatedFrameBytes = symbolicFrameVectorBytes(step.frames);
                if (generatedFrameBytes != 0) {
                    if (!chargeBudget(budget, limits, 0, generatedFrameBytes)) {
                        error = "detailed proof child materialization exceeded byte budget";
                        return false;
                    }
                    stepFrameBytes.add(generatedFrameBytes);
                }
                if (step.finished) {
                    if (instruction->opcode != Opcode::Finish || step.frames.size() != 1 ||
                        !(step.frames.front() == frame)) {
                        error = "detailed proof FINISH generation disagrees with SymbolicStepper";
                        return false;
                    }
                    nodes[nodeIndex].kind = DetailedProofNodeKind::Finish;
                    continue;
                }
                if (step.frames.empty()) {
                    error = "nonterminal detailed proof instruction produced no nonempty child";
                    return false;
                }
                if (!chargeBudget(budget, limits, step.frames.size(), 0)) {
                    error = "detailed proof child generation exceeded work budget";
                    return false;
                }

                const std::uint64_t childIndexBytes =
                    step.frames.size() * sizeof(std::uint32_t);
                if (!chargeBudget(budget, limits, 0, childIndexBytes)) {
                    error = "detailed proof temporary child-index storage exceeded byte budget";
                    return false;
                }
                temporaryBytes.add(childIndexBytes);
                std::vector<std::uint32_t> childIndices;
                childIndices.reserve(step.frames.size());
                for (const SymbolicFrame &childFrame: step.frames) {
                    std::uint32_t childIndex = 0;
                    if (!appendNode(childFrame, childIndex)) {
                        return false;
                    }
                    childIndices.push_back(childIndex);
                }
                if (!chargeBudget(budget, limits, 0, childIndexBytes)) {
                    error = "detailed proof child-index storage exceeded byte budget";
                    return false;
                }
                temporaryBytes.add(childIndexBytes);
                nodes[nodeIndex].children = childIndices;

                for (auto it = childIndices.rbegin(); it != childIndices.rend(); ++it) {
                    if (!chargeBudget(budget, limits, 0, sizeof(std::uint32_t))) {
                        error = "detailed proof work stack exceeded byte budget";
                        return false;
                    }
                    temporaryBytes.add(sizeof(std::uint32_t));
                    pending.push_back(*it);
                }
            }

            if (nodes.empty()) {
                error = "detailed proof tree generation produced no nodes";
                return false;
            }
            return true;
        }
    } // namespace

    Box ProofKernel::baseBox(const RuleBundle &bundle, const RawState &state) {
        Box box;
        box.enemyHp = {1, bundle.profile.enemyMaxHp};
        box.heroHp = {1, bundle.profile.heroMaxHp};
        box.mp = {0, state.players[0].mp};
        box.herb = {0, state.players[0].medicinal_herbs_count};
        box.chargeMask = kChargeMaskAll;
        box.paralysisMask = kParalysisMaskAll;
        box.acroMask = kAcroMaskAll;
        box.rageMask = kRageMaskAll;
        box.inactiveMask = kInactiveMaskAll;
        box.cameraMask = kCameraMaskAll;
        return box;
    }

    std::optional<Box> ProofKernel::alphaSingleton(const RawState &state, std::string &error) {
        if (state.players[0].sleeping || state.players[1].sleeping) {
            error = "sleeping state unsupported by alpha";
            return std::nullopt;
        }

        Box box;
        box.enemyHp = {state.players[1].hp, state.players[1].hp};
        box.heroHp = {state.players[0].hp, state.players[0].hp};
        box.mp = {state.players[0].mp, state.players[0].mp};
        box.herb = {state.players[0].medicinal_herbs_count, state.players[0].medicinal_herbs_count};

        box.chargeMask = encodeCharge(state.players[0], error);
        if (!error.empty()) {
            return std::nullopt;
        }
        box.paralysisMask = encodeParalysis(state.players[0], error);
        if (!error.empty()) {
            return std::nullopt;
        }
        box.acroMask = encodeAcro(state.players[0], error);
        if (!error.empty()) {
            return std::nullopt;
        }
        box.rageMask = encodeRage(state.players[1], error);
        if (!error.empty()) {
            return std::nullopt;
        }

        box.inactiveMask = static_cast<std::uint16_t>(1u << (state.players[0].inactive ? 1 : 0));

        const int cameraMode = static_cast<int>((state.nowState >> 8) & 0xfULL);
        if (cameraMode < 0 || cameraMode > 5) {
            error = "camera outside alpha range";
            return std::nullopt;
        }
        box.cameraMask = static_cast<std::uint16_t>(1u << cameraMode);

        if (box.empty()) {
            error = "empty alpha singleton";
            return std::nullopt;
        }
        return box;
    }

    CheckedPartition ProofKernel::validatePartition(
        const PredicatePartition &partition,
        const Box &base,
        std::uint32_t maxLeaves) {
        return PartitionValidator(partition, base, maxLeaves).run();
    }

    CheckResult ProofKernel::validateFamily(
        const PartitionFamily &family,
        const Box &base,
        int firstPosition,
        int lastPosition,
        std::uint32_t maxLeaves) {
        CheckResult result;
        if (firstPosition > lastPosition) {
            result.reason = "invalid position range";
            return result;
        }

        const auto expectedTreeCount = static_cast<std::size_t>(lastPosition - firstPosition + 1);
        if (family.trees.size() != expectedTreeCount) {
            result.reason = "partition family does not cover every RNG position exactly once";
            return result;
        }

        std::set<int> positions;
        for (const PredicatePartition &tree: family.trees) {
            if (tree.rngPosition < firstPosition || tree.rngPosition > lastPosition ||
                !positions.insert(tree.rngPosition).second) {
                result.reason = "partition family position mismatch";
                return result;
            }

            const CheckedPartition checked = validatePartition(tree, base, maxLeaves);
            if (!checked.check.accepted) {
                result.reason = "position " + std::to_string(tree.rngPosition) + ": " + checked.check.reason;
                return result;
            }
        }

        result.accepted = true;
        return result;
    }

    std::optional<std::uint32_t> ProofKernel::project(
        const RawState &state,
        const PredicatePartition &partition,
        std::string &error) {
        int nodeIndex = partition.root;
        std::vector<bool> visited(partition.nodes.size(), false);

        while (true) {
            if (nodeIndex < 0 || nodeIndex >= static_cast<int>(partition.nodes.size())) {
                error = "projection escaped partition";
                return std::nullopt;
            }
            if (visited[nodeIndex]) {
                error = "projection saw partition cycle";
                return std::nullopt;
            }
            visited[nodeIndex] = true;

            const PartitionNode &node = partition.nodes[nodeIndex];
            if (node.leaf) {
                return node.localCellId;
            }

            const bool takeTrueBranch = evaluatePredicate(node.predicate, state, error);
            if (!error.empty()) {
                return std::nullopt;
            }
            nodeIndex = takeTrueBranch ? node.trueChild : node.falseChild;
        }
    }

    PartitionFamily ProofKernel::makeInitialFamily(
        const RuleBundle &bundle,
        const RawState &state,
        int horizon,
        std::uint32_t maxLeaves,
        std::string &error) {
        PartitionFamily family;
        family.partitionVersion = 1;

        if (horizon < 0 || maxLeaves == 0) {
            error = "invalid initial partition request";
            return family;
        }

        const long long lastPosition = static_cast<long long>(state.position) +
                                       static_cast<long long>(horizon) * bundle.bounds.rMax;
        if (lastPosition >= ExactReplay::kRngTapeSize) {
            error = "registered Rmax exceeds RNG tape for requested horizon";
            return family;
        }

        family.trees.reserve(static_cast<std::size_t>(lastPosition - state.position + 1));
        for (int position = state.position; position <= lastPosition; ++position) {
            PredicatePartition tree;
            tree.rngPosition = position;
            tree.root = 0;
            tree.nodes.push_back({true, 0, {}});
            family.trees.push_back(std::move(tree));
        }

        const CheckResult familyCheck = validateFamily(
            family,
            baseBox(bundle, state),
            state.position,
            static_cast<int>(lastPosition),
            maxLeaves);
        if (!familyCheck.accepted) {
            error = familyCheck.reason;
            family.trees.clear();
        }
        return family;
    }

    CheckedSnapshot ProofKernel::rebuildSupport(
        const RuleBundle &bundle,
        const Problem &problem,
        int horizon,
        const PartitionFamily &partitions,
        const ProofBudget &limits,
        BudgetReport &budget,
        const std::vector<RootProofRecord> *submittedProofs,
        const std::vector<ProofTemplate> *generationTemplates,
        std::uint64_t coverageVersion,
        bool retainDefaultProofRecords,
        bool retainPartitionFamily,
        const CheckedSnapshot *checkedReuseSnapshot) {
        CheckedSnapshot snapshot;
        snapshot.coverageVersion = coverageVersion;
        std::uint64_t checkedProofRoots = 0;
        std::uint64_t checkedCompletionCases = 0;
        ScopedBudgetBytes temporaryBytes(budget);
        ScopedBudgetBytes retainedSnapshotBytes(budget);
        if (coverageVersion == 0) {
            snapshot.check.reason = "coverage_version must be nonzero";
            return snapshot;
        }

        const std::string inputError = ExactReplay::validateProblem(bundle, problem, horizon);
        if (!inputError.empty()) {
            snapshot.check.reason = inputError;
            return snapshot;
        }
        if (problem.s0.players[1].hp == 0 || problem.s0.players[0].hp == 0) {
            snapshot.check.reason = "rebuild_support requires a nonterminal root";
            return snapshot;
        }

        const Box base = baseBox(bundle, problem.s0);
        std::string alphaError;
        const std::optional<Box> alpha = alphaSingleton(problem.s0, alphaError);
        if (!alpha || !contains(base, *alpha)) {
            snapshot.check.reason = alphaError.empty() ? "alpha(S0) outside BaseBox" : alphaError;
            return snapshot;
        }

        const long long lastPosition = static_cast<long long>(problem.s0.position) +
                                       static_cast<long long>(horizon) * bundle.bounds.rMax;
        std::uint64_t familyValidationWork = partitions.trees.size();
        for (const PredicatePartition &tree: partitions.trees) {
            if (tree.nodes.size() >
                std::numeric_limits<std::uint64_t>::max() - familyValidationWork) {
                snapshot.check.reason = "partition-family validation work overflow";
                return snapshot;
            }
            familyValidationWork += tree.nodes.size();
        }
        if (!chargeBudget(budget, limits, familyValidationWork, 0)) {
            snapshot.check.reason = "partition-family validation exceeded proof budget";
            return snapshot;
        }
        if (problem.s0.position > lastPosition) {
            snapshot.check.reason = "invalid position range";
            return snapshot;
        }
        const std::size_t expectedHotelCount =
            static_cast<std::size_t>(lastPosition - problem.s0.position + 1);
        if (partitions.trees.size() != expectedHotelCount) {
            snapshot.check.reason = "partition family does not cover every RNG position exactly once";
            return snapshot;
        }

        const int firstPosition = problem.s0.position;
        const std::size_t hotelCount = partitions.trees.size();
        std::vector<CheckedPartition> checkedPartitions(hotelCount);
        std::vector<std::uint8_t> seenHotels(hotelCount, 0);
        for (const PredicatePartition &partition: partitions.trees) {
            if (partition.rngPosition < firstPosition || partition.rngPosition > lastPosition) {
                snapshot.check.reason = "partition family position mismatch";
                return snapshot;
            }
            const std::size_t hotelIndex =
                static_cast<std::size_t>(partition.rngPosition - firstPosition);
            if (seenHotels[hotelIndex] != 0) {
                snapshot.check.reason = "partition family position mismatch";
                return snapshot;
            }
            seenHotels[hotelIndex] = 1;
            CheckedPartition checked = validatePartition(partition, base, limits.maxLeavesPerPosition);
            if (!checked.check.accepted) {
                snapshot.check.reason =
                    "position " + std::to_string(partition.rngPosition) + ": " + checked.check.reason;
                return snapshot;
            }
            if (!chargeBudget(
                    budget,
                    limits,
                    checked.leaves.size(),
                    sizeof(CheckedPartition) + checked.leaves.size() * sizeof(std::pair<std::uint32_t, Box>))) {
                snapshot.check.reason = "partition validation exceeded proof budget";
                return snapshot;
            }
            temporaryBytes.add(
                sizeof(CheckedPartition) +
                checked.leaves.size() * sizeof(std::pair<std::uint32_t, Box>));
            std::sort(
                checked.leaves.begin(),
                checked.leaves.end(),
                [](const auto &a, const auto &b) { return a.first < b.first; });
            checkedPartitions[hotelIndex] = std::move(checked);
        }
        auto checkedPartitionAt = [&](int position) -> const CheckedPartition * {
            if (position < firstPosition || position > lastPosition) {
                return nullptr;
            }
            return &checkedPartitions[static_cast<std::size_t>(position - firstPosition)];
        };
        auto checkedLeafDomain = [](const CheckedPartition &partition,
                                    std::uint32_t localCellId) -> const Box * {
            const auto it = std::lower_bound(
                partition.leaves.begin(),
                partition.leaves.end(),
                localCellId,
                [](const auto &leaf, std::uint32_t id) { return leaf.first < id; });
            return it != partition.leaves.end() && it->first == localCellId
                ? &it->second
                : nullptr;
        };

        std::vector<bool> submittedProofUsed(
            submittedProofs == nullptr ? 0 : submittedProofs->size(),
            false);
        if (!submittedProofUsed.empty()) {
            const std::uint64_t usedBytes = submittedProofUsed.size();
            if (!chargeBudget(budget, limits, 0, usedBytes)) {
                snapshot.check.reason = "submitted-proof usage bitmap exceeded proof budget";
                return snapshot;
            }
            temporaryBytes.add(usedBytes);
        }

        using SubmittedProofKey =
            std::tuple<int, int, std::uint32_t, int, std::uint64_t, std::uint64_t>;
        struct SubmittedProofLookupEntry {
            SubmittedProofKey key;
            std::size_t index = 0;
        };
        std::vector<SubmittedProofLookupEntry> submittedProofIndex;
        if (submittedProofs != nullptr) {
            submittedProofIndex.reserve(submittedProofs->size());
            for (std::size_t index = 0; index < submittedProofs->size(); ++index) {
                const RootProofRecord &proof = (*submittedProofs)[index];
                submittedProofIndex.push_back({
                    {proof.elapsedTurn,
                     proof.source.rngPosition,
                     proof.source.localCellId,
                     proof.selectedCommand,
                     proof.partitionVersion,
                     proof.coverageVersion},
                    index,
                });
            }
            const std::uint64_t indexBytes =
                submittedProofIndex.size() * sizeof(SubmittedProofLookupEntry);
            if (!chargeBudget(
                    budget,
                    limits,
                    sortWorkEstimate(submittedProofIndex.size()),
                    indexBytes)) {
                snapshot.check.reason = "submitted-proof index exceeded proof budget";
                return snapshot;
            }
            temporaryBytes.add(indexBytes);
            std::sort(
                submittedProofIndex.begin(),
                submittedProofIndex.end(),
                [](const SubmittedProofLookupEntry &a, const SubmittedProofLookupEntry &b) {
                    return a.key < b.key;
                });
            for (std::size_t index = 1; index < submittedProofIndex.size(); ++index) {
                if (!chargeBudget(budget, limits, 1, 0)) {
                    snapshot.check.reason = "submitted-proof duplicate scan exceeded proof budget";
                    return snapshot;
                }
                if (submittedProofIndex[index - 1].key == submittedProofIndex[index].key) {
                    snapshot.check.reason = "submitted proof contains duplicate required root records";
                    return snapshot;
                }
            }
        }

        using TemplateLookupKey = std::tuple<int, int, int>;
        struct TemplateLookupEntry {
            TemplateLookupKey key;
            std::size_t index = 0;
        };
        std::vector<TemplateLookupEntry> templateIndex;
        if (generationTemplates != nullptr) {
            templateIndex.reserve(generationTemplates->size());
            for (std::size_t index = 0; index < generationTemplates->size(); ++index) {
                const ProofTemplate &proofTemplate = (*generationTemplates)[index];
                templateIndex.push_back({
                    {proofTemplate.elapsedTurn,
                     proofTemplate.rngPosition,
                     proofTemplate.selectedCommand},
                    index,
                });
            }
            const std::uint64_t indexBytes = templateIndex.size() * sizeof(TemplateLookupEntry);
            std::uint64_t orderingWork = templateIndex.empty() ? 0 : templateIndex.size() - 1;
            bool alreadySorted = true;
            for (std::size_t index = 1; index < templateIndex.size(); ++index) {
                if (std::tuple{templateIndex[index].key, templateIndex[index].index} <
                    std::tuple{templateIndex[index - 1].key, templateIndex[index - 1].index}) {
                    alreadySorted = false;
                    break;
                }
            }
            if (!alreadySorted) {
                orderingWork += sortWorkEstimate(templateIndex.size());
            }
            if (!chargeBudget(budget, limits, orderingWork, indexBytes)) {
                snapshot.check.reason = "proof-template index exceeded proof budget";
                return snapshot;
            }
            temporaryBytes.add(indexBytes);
            if (!alreadySorted) {
                std::sort(
                    templateIndex.begin(),
                    templateIndex.end(),
                    [](const TemplateLookupEntry &a, const TemplateLookupEntry &b) {
                        return std::tuple{a.key, a.index} < std::tuple{b.key, b.index};
                    });
            }
        }

        const CompletionSite *turnEntrySite = lookupCompletionSite(
            bundle.profile,
            CompletionCutId::TurnEntry);
        if (turnEntrySite == nullptr) {
            snapshot.check.reason = "registered TurnEntry COMPLETE site is missing";
            return snapshot;
        }
        const PcStaticBounds *turnEntryRemaining = lookupPcBounds(
            bundle.bounds,
            turnEntrySite->pc.routineId,
            turnEntrySite->pc.instructionIndex);
        if (turnEntryRemaining == nullptr) {
            snapshot.check.reason = "registered TurnEntry COMPLETE site has no kernel-computed pc bounds";
            return snapshot;
        }

        struct TurnEntrySummary {
            int maximumDamage = 0;
            int minimumMpDelta = 0;
            int minimumHerbDelta = 0;
            bool mayHealHero = false;
        };
        std::vector<TurnEntrySummary> turnEntrySummaries(bundle.profile.commandProfiles.size());
        for (std::size_t commandIndex = 0;
             commandIndex < bundle.profile.commandProfiles.size();
             ++commandIndex) {
            const CommandProfile &commandProfile = bundle.profile.commandProfiles[commandIndex];
            TurnEntrySummary &summary = turnEntrySummaries[commandIndex];
            for (const CompletionWeightTerm &term: commandProfile.turnEntryCompletionTerms) {
                summary.maximumDamage = std::max(summary.maximumDamage, term.enemyDamageUpper);
                summary.minimumMpDelta = std::min(summary.minimumMpDelta, term.mpDelta);
                summary.minimumHerbDelta = std::min(summary.minimumHerbDelta, term.herbDelta);
                summary.mayHealHero = summary.mayHealHero || term.heroHpGainUpper > 0;
            }
        }
        auto turnEntryOutputEnvelope = [&](const Box &root,
                                           const TurnEntrySummary &summary) {
            Box output = root;
            output.enemyHp.lo = std::max<std::int64_t>(0, root.enemyHp.lo - summary.maximumDamage);
            output.enemyHp.hi = bundle.profile.enemyMaxHp;
            output.heroHp.lo = 0;
            if (summary.mayHealHero) {
                output.heroHp.hi = bundle.profile.heroMaxHp;
            }
            output.mp.lo = root.mp.lo + summary.minimumMpDelta;
            output.herb.lo = root.herb.lo + summary.minimumHerbDelta;
            output.chargeMask = kChargeMaskAll;
            output.paralysisMask = kParalysisMaskAll;
            output.acroMask = kAcroMaskAll;
            output.rageMask = kRageMaskAll;
            output.inactiveMask = kInactiveMaskAll;
            output.cameraMask = kCameraMaskAll;
            return output;
        };

        auto generatedProof = [&](int elapsedTurn,
                                  const CellKey &source,
                                  int command,
                                  RootProofKind kind,
                                  CompletionCutId cut) {
            RootProofRecord proof;
            proof.kind = kind;
            proof.elapsedTurn = elapsedTurn;
            proof.source = source;
            proof.selectedCommand = command;
            proof.partitionVersion = partitions.partitionVersion;
            proof.coverageVersion = snapshot.coverageVersion;
            proof.cut = cut;

            if (kind == RootProofKind::Completion) {
                const CompletionSite *site = lookupCompletionSite(bundle.profile, proof.cut);
                if (site != nullptr) {
                    proof.expectedPc = site->pc;
                }
                const std::vector<CompletionWeightTerm> terms = completionTermsForCut(bundle, command, cut);
                proof.completionCases.reserve(terms.size());
                for (std::size_t caseIndex = 0; caseIndex < terms.size(); ++caseIndex) {
                    proof.completionCases.push_back({
                        static_cast<std::uint32_t>(caseIndex),
                        CompletionTargetKind::AllLeavesInCheckedRange,
                    });
                }
            }
            return proof;
        };

        auto acquireRequiredProof = [&](int elapsedTurn,
                                        const CellKey &source,
                                        int command,
                                        const Box &rootDomain,
                                        std::size_t templateRangeBegin,
                                        std::size_t templateRangeEnd,
                                        bool &generatedDefaultTurnEntry)
            -> std::optional<RootProofRecord> {
            generatedDefaultTurnEntry = false;
            if (submittedProofs != nullptr) {
                const SubmittedProofKey key{
                    elapsedTurn,
                    source.rngPosition,
                    source.localCellId,
                    command,
                    partitions.partitionVersion,
                    snapshot.coverageVersion,
                };
                std::size_t lo = 0;
                std::size_t hi = submittedProofIndex.size();
                std::uint64_t comparisons = 0;
                while (lo < hi) {
                    ++comparisons;
                    const std::size_t mid = lo + (hi - lo) / 2;
                    if (submittedProofIndex[mid].key < key) {
                        lo = mid + 1;
                    } else {
                        hi = mid;
                    }
                }
                if (!chargeBudget(budget, limits, 1, 0)) {
                    snapshot.check.reason = "required-root proof lookup exceeded proof budget";
                    return std::nullopt;
                }
                if (lo >= submittedProofIndex.size() || submittedProofIndex[lo].key != key) {
                    snapshot.check.reason = "submitted proof is missing a kernel-required root";
                    return std::nullopt;
                }
                const std::size_t match = submittedProofIndex[lo].index;
                submittedProofUsed[match] = true;
                return (*submittedProofs)[match];
            }

            ++budget.proofTemplateRequests;

            const ProofTemplate *bestTemplate = nullptr;
            auto rank = [](const ProofTemplate &candidate) {
                if (candidate.kind == RootProofKind::FullyDetailed) {
                    return 100;
                }
                switch (candidate.cut) {
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
            if (generationTemplates != nullptr) {
                if (!chargeBudget(budget, limits, 1, 0)) {
                    snapshot.check.reason = "proof-template lookup exceeded proof budget";
                    return std::nullopt;
                }
                for (std::size_t index = templateRangeBegin;
                     index < templateRangeEnd;
                     ++index) {
                    if (!chargeBudget(budget, limits, 1, 0)) {
                        snapshot.check.reason = "proof-template key scan exceeded proof budget";
                        return std::nullopt;
                    }
                    if (std::get<2>(templateIndex[index].key) != command) {
                        continue;
                    }
                    const ProofTemplate &candidate =
                        (*generationTemplates)[templateIndex[index].index];
                    if (!contains(candidate.coveredDomain, rootDomain)) {
                        continue;
                    }
                    if (bestTemplate == nullptr || rank(candidate) > rank(*bestTemplate)) {
                        bestTemplate = &candidate;
                    }
                }
            }
            if (bestTemplate != nullptr) {
                ++budget.proofTemplateReuseHits;
                RootProofRecord proof = generatedProof(
                    elapsedTurn,
                    source,
                    command,
                    bestTemplate->kind,
                    bestTemplate->cut);
                if (proof.kind == RootProofKind::FullyDetailed ||
                    proof.cut != CompletionCutId::TurnEntry) {
                    SymbolicStepper stepper(bundle, problem);
                    const SymbolicFrame rootFrame = stepper.makeRootFrame(
                        elapsedTurn,
                        command,
                        source.rngPosition,
                        rootDomain);
                    std::string treeError;
                    if (!buildDetailedProofTree(
                            bundle,
                            problem,
                            rootFrame,
                            proof.kind,
                            proof.cut,
                            limits,
                            budget,
                            proof.detailedNodes,
                            treeError)) {
                        snapshot.check.reason = treeError;
                        return std::nullopt;
                    }
                }
                return proof;
            }
            // The default provider always submits the canonical TurnEntry
            // COMPLETE proof.  Keep its case list implicit here: the kernel
            // derives and checks the canonical cases below, so materializing a
            // tiny heap-backed vector for every required root only duplicates
            // kernel-owned data during repeated full Support rebuilds.
            RootProofRecord proof;
            proof.kind = RootProofKind::Completion;
            proof.elapsedTurn = elapsedTurn;
            proof.source = source;
            proof.selectedCommand = command;
            proof.partitionVersion = partitions.partitionVersion;
            proof.coverageVersion = snapshot.coverageVersion;
            proof.cut = CompletionCutId::TurnEntry;
            proof.expectedPc = turnEntrySite->pc;
            generatedDefaultTurnEntry = true;
            return proof;
        };

        auto verifyTurnEntryProof = [&](
            const RootProofRecord &proof,
            int elapsedTurn,
            const CellKey &source,
            int command,
            const CommandProfile &commandProfile,
            const Box &rootDomain,
            CheckedRootRecord &record,
            CheckedEdge &edge,
            bool generatedDefaultTurnEntry) -> bool {
            if (proof.kind != RootProofKind::Completion ||
                proof.cut != CompletionCutId::TurnEntry || proof.expectedPc != turnEntrySite->pc) {
                snapshot.check.reason = "COMPLETE cut_id or expected_pc is not the registered turn-entry site";
                return false;
            }
            if (!proof.detailedNodes.empty()) {
                snapshot.check.reason = "turn-entry COMPLETE must not contain a detailed proof tree";
                return false;
            }
            if (proof.elapsedTurn != elapsedTurn || !(proof.source == source) ||
                proof.selectedCommand != command ||
                proof.partitionVersion != partitions.partitionVersion ||
                proof.coverageVersion != snapshot.coverageVersion) {
                snapshot.check.reason = "COMPLETE root identity differs from the kernel-required root";
                return false;
            }

            const std::vector<CompletionWeightTerm> &terms = commandProfile.turnEntryCompletionTerms;
            if (terms.empty() || terms.size() > limits.maxCompletionTermsPerAction) {
                snapshot.check.reason = "turn-entry COMPLETE does not contain every required case_id";
                return false;
            }
            if (generatedDefaultTurnEntry) {
                if (!proof.completionCases.empty()) {
                    snapshot.check.reason = "kernel-generated TurnEntry COMPLETE unexpectedly stores explicit cases";
                    return false;
                }
            } else {
                if (proof.completionCases.size() != terms.size()) {
                    snapshot.check.reason = "turn-entry COMPLETE does not contain every required case_id";
                    return false;
                }
                for (std::size_t caseIndex = 0; caseIndex < proof.completionCases.size(); ++caseIndex) {
                    const CompletionProofCase &proofCase = proof.completionCases[caseIndex];
                    bool duplicate = false;
                    for (std::size_t previous = 0; previous < caseIndex; ++previous) {
                        if (proof.completionCases[previous].caseId == proofCase.caseId) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (proofCase.caseId >= terms.size() ||
                        proofCase.targetKind != CompletionTargetKind::AllLeavesInCheckedRange ||
                        duplicate) {
                        snapshot.check.reason = "turn-entry COMPLETE has an invalid, duplicate, or unsupported case";
                        return false;
                    }
                }
            }

            record.proofKind = proof.kind;
            record.elapsedTurn = elapsedTurn;
            record.source = source;
            record.selectedCommand = command;
            record.rootDomain = rootDomain;
            record.partitionVersion = partitions.partitionVersion;
            record.coverageVersion = snapshot.coverageVersion;
            record.verifiedPc = turnEntrySite->pc;
            record.verifiedCut = proof.cut;

            edge.kind = CheckedEdgeKind::Completion;
            edge.completionCut = proof.cut;
            edge.elapsedTurn = elapsedTurn;
            edge.source = source;
            edge.selectedCommand = command;
            edge.rootDomain = rootDomain;
            edge.useRegisteredTurnEntryTerms = true;

            const int envelopeLastPosition = problem.s0.position +
                                             (elapsedTurn + 1) * bundle.bounds.rMax;
            edge.firstOutputPosition = source.rngPosition;
            edge.lastOutputPosition = source.rngPosition + turnEntryRemaining->maximumRngReads;
            if (edge.firstOutputPosition < problem.s0.position ||
                edge.lastOutputPosition > envelopeLastPosition ||
                edge.firstOutputPosition > edge.lastOutputPosition) {
                snapshot.check.reason = "COMPLETE Rremaining escapes Envelope[t+1]";
                return false;
            }
            return true;
        };

        auto verifyDetailedProof = [&](const RootProofRecord &proof,
                                       int elapsedTurn,
                                       const CellKey &source,
                                       int command,
                                       const Box &rootDomain,
                                       CheckedRootRecord &record,
                                       std::vector<CheckedEdge> &edges,
                                       std::vector<CompletionCheckpoint> &checkpoints,
                                       std::uint64_t &checkedOutputBytes) -> bool {
            checkedOutputBytes = 0;
            const CompletionSite *site = nullptr;
            std::vector<CompletionWeightTerm> remainingTerms;
            if (proof.kind == RootProofKind::Completion) {
                site = lookupCompletionSite(bundle.profile, proof.cut);
                if (site == nullptr || proof.cut == CompletionCutId::TurnEntry || proof.expectedPc != site->pc) {
                    snapshot.check.reason = "detailed COMPLETE cut_id or expected_pc is not a registered partial site";
                    return false;
                }
                remainingTerms = completionTermsForCut(bundle, command, proof.cut);
                if (!completionCasesMatch(proof,
                                          remainingTerms,
                                          limits.maxCompletionTermsPerAction,
                                          snapshot.check.reason)) {
                    return false;
                }
            } else if (!proof.completionCases.empty()) {
                snapshot.check.reason = "fully detailed root must not contain COMPLETE cases";
                return false;
            }
            if (proof.elapsedTurn != elapsedTurn || !(proof.source == source) ||
                proof.selectedCommand != command ||
                proof.partitionVersion != partitions.partitionVersion ||
                proof.coverageVersion != snapshot.coverageVersion) {
                snapshot.check.reason = "detailed root identity differs from the kernel-required root";
                return false;
            }
            if (proof.detailedNodes.empty()) {
                snapshot.check.reason = "detailed root is missing its proof-node tree";
                return false;
            }

            record.proofKind = proof.kind;
            record.elapsedTurn = elapsedTurn;
            record.source = source;
            record.selectedCommand = command;
            record.rootDomain = rootDomain;
            record.partitionVersion = partitions.partitionVersion;
            record.coverageVersion = snapshot.coverageVersion;
            record.verifiedPc = site != nullptr
                ? site->pc
                : ProgramPoint{bundle.program.entryRoutine, 0};
            record.verifiedCut = proof.cut;
            ScopedBudgetBytes outputBytes(budget);
            const std::uint64_t recordBytes = checkedRootRecordBytes(record);
            if (!chargeBudget(budget, limits, 0, recordBytes)) {
                snapshot.check.reason = "detailed checked-root record exceeded proof budget";
                return false;
            }
            outputBytes.add(recordBytes);

            SymbolicStepper stepper(bundle, problem);
            struct PendingDetailedNode {
                SymbolicFrame frame;
                std::uint32_t nodeIndex = 0;
            };
            auto pendingDetailedNodeBytes = [](const PendingDetailedNode &node) noexcept {
                std::uint64_t bytes = sizeof(PendingDetailedNode);
                const std::uint64_t frameBytes = symbolicFrameRecordBytes(node.frame);
                if (frameBytes < sizeof(SymbolicFrame)) {
                    return std::numeric_limits<std::uint64_t>::max();
                }
                const std::uint64_t dynamicFrameBytes = frameBytes - sizeof(SymbolicFrame);
                if (dynamicFrameBytes > std::numeric_limits<std::uint64_t>::max() - bytes) {
                    return std::numeric_limits<std::uint64_t>::max();
                }
                return bytes + dynamicFrameBytes;
            };
            std::vector<PendingDetailedNode> pending;
            SymbolicFrame requiredRoot = stepper.makeRootFrame(
                elapsedTurn,
                command,
                source.rngPosition,
                rootDomain);
            ScopedBudgetBytes pendingBytes(budget);
            PendingDetailedNode rootPending{std::move(requiredRoot), 0};
            const std::uint64_t rootPendingBytes = pendingDetailedNodeBytes(rootPending);
            if (!chargeBudget(budget, limits, 0, rootPendingBytes)) {
                snapshot.check.reason = "detailed verify_root stack exceeded proof budget";
                return false;
            }
            pendingBytes.add(rootPendingBytes);
            pending.push_back(std::move(rootPending));
            ScopedBudgetBytes visitedBytes(budget);
            std::vector<std::uint8_t> visited(proof.detailedNodes.size(), 0);
            if (!chargeBudget(budget, limits, 0, visited.size())) {
                snapshot.check.reason = "detailed proof visited-set exceeded proof budget";
                return false;
            }
            visitedBytes.add(visited.size());
            bool reachedCut = proof.kind == RootProofKind::FullyDetailed;

            auto addCheckedOutput = [&](const SymbolicFrame &frame,
                                        const Box &output,
                                        const CompletionWeightTerm &weight,
                                        int firstPosition,
                                        int lastPosition,
                                        bool completion) -> bool {
                if (output.enemyHp.lo < 0 || output.heroHp.lo < 0 ||
                    output.mp.lo < 0 || output.herb.lo < 0) {
                    snapshot.check.reason = "checked symbolic output contains a negative resource";
                    return false;
                }

                CheckedEdge edge;
                edge.kind = completion ? CheckedEdgeKind::Completion : CheckedEdgeKind::Detailed;
                edge.completionCut = completion ? proof.cut : CompletionCutId::TurnEntry;
                edge.elapsedTurn = elapsedTurn;
                edge.source = source;
                edge.selectedCommand = command;
                edge.rootDomain = frame.inputDomain;
                edge.weightTerms = {weight};
                edge.firstOutputPosition = firstPosition;
                edge.lastOutputPosition = lastPosition;
                edge.mayReachGoal = output.enemyHp.lo == 0;
                edge.mayReachFailure = output.heroHp.lo == 0 && output.enemyHp.hi > 0;

                Box continuing = output;
                continuing.enemyHp.lo = std::max<std::int64_t>(continuing.enemyHp.lo, 1);
                continuing.heroHp.lo = std::max<std::int64_t>(continuing.heroHp.lo, 1);
                if (!continuing.empty()) {
                    if (!contains(base, continuing)) {
                        snapshot.check.reason = "checked symbolic output escapes Envelope[t+1]";
                        return false;
                    }
                    edge.hasContinuingOutput = true;
                    edge.continuingOutput = continuing;
                    if (completion) {
                        for (int position = firstPosition; position <= lastPosition; ++position) {
                            if (checkedPartitionAt(position) == nullptr) {
                                snapshot.check.reason = "COMPLETE output references missing partition";
                                return false;
                            }
                        }
                    } else {
                        if (firstPosition != lastPosition) {
                            snapshot.check.reason = "detailed output has non-singleton RNG position";
                            return false;
                        }
                        const CheckedPartition *partition = checkedPartitionAt(firstPosition);
                        if (partition == nullptr) {
                            snapshot.check.reason = "detailed output references missing partition";
                            return false;
                        }
                        for (const auto &[leafId, leafBox]: partition->leaves) {
                            if (!intersect(continuing, leafBox).empty()) {
                                edge.targets.push_back({firstPosition, leafId});
                            }
                        }
                        if (edge.targets.empty()) {
                            snapshot.check.reason = "detailed continuing output has no classified destination";
                            return false;
                        }
                    }
                }
                if (!completion) {
                    std::sort(edge.targets.begin(), edge.targets.end());
                    edge.targets.erase(std::unique(edge.targets.begin(), edge.targets.end()), edge.targets.end());
                }
                const std::uint64_t edgeBytes = checkedEdgeBytes(edge);
                if (!chargeBudget(budget, limits, 0, edgeBytes)) {
                    snapshot.check.reason = "checked symbolic edge storage exceeded proof budget";
                    return false;
                }
                outputBytes.add(edgeBytes);
                edges.push_back(std::move(edge));
                return true;
            };

            auto addExactFinishedOutput = [&](const SymbolicFrame &finished) -> bool {
                Box wholeOutput;
                std::string frameError;
                if (!stepper.outputBox(finished, wholeOutput, frameError)) {
                    snapshot.check.reason = "exact output image failed: " + frameError;
                    return false;
                }
                if (wholeOutput.enemyHp.lo < 0 || wholeOutput.heroHp.lo < 0 ||
                    wholeOutput.mp.lo < 0 || wholeOutput.herb.lo < 0) {
                    snapshot.check.reason = "exact symbolic output contains a negative resource";
                    return false;
                }

                auto addTerminal = [&](const SymbolicFrame &terminal, bool goal, bool failure) -> bool {
                    CompletionWeightTerm weight;
                    std::string weightError;
                    if (!prefixWeightTerm(terminal, weight, weightError)) {
                        snapshot.check.reason = "exact terminal weight failed: " + weightError;
                        return false;
                    }
                    Box terminalOutput;
                    if (!stepper.outputBox(terminal, terminalOutput, weightError)) {
                        snapshot.check.reason = "exact terminal image failed: " + weightError;
                        return false;
                    }
                    if (terminalOutput.enemyHp.lo < 0 || terminalOutput.heroHp.lo < 0 ||
                        terminalOutput.mp.lo < 0 || terminalOutput.herb.lo < 0) {
                        snapshot.check.reason = "exact terminal output contains a negative resource";
                        return false;
                    }
                    CheckedEdge edge;
                    edge.kind = CheckedEdgeKind::Detailed;
                    edge.elapsedTurn = elapsedTurn;
                    edge.source = source;
                    edge.selectedCommand = command;
                    edge.rootDomain = terminal.inputDomain;
                    edge.firstOutputPosition = terminal.rngPosition;
                    edge.lastOutputPosition = terminal.rngPosition;
                    edge.mayReachGoal = goal;
                    edge.mayReachFailure = failure;
                    edge.weightTerms = {weight};
                    const std::uint64_t edgeBytes = checkedEdgeBytes(edge);
                    if (!chargeBudget(budget, limits, 0, edgeBytes)) {
                        snapshot.check.reason = "exact terminal edge storage exceeded proof budget";
                        return false;
                    }
                    outputBytes.add(edgeBytes);
                    edges.push_back(std::move(edge));
                    return true;
                };

                Predicate enemyZero;
                enemyZero.kind = PredicateKind::ResourceLe;
                enemyZero.resource = ResourceAxis::EnemyHp;
                enemyZero.threshold = 0;
                std::vector<SymbolicFrame> goalFrames;
                std::vector<SymbolicFrame> enemyAliveFrames;
                if (!stepper.splitOutputPredicate(
                        finished,
                        enemyZero,
                        goalFrames,
                        enemyAliveFrames,
                        frameError)) {
                    snapshot.check.reason = "enemy terminal inverse image failed: " + frameError;
                    return false;
                }
                ScopedBudgetBytes enemySplitBytes(budget);
                const std::uint64_t enemySplitStorage = saturatedReservationAdd(
                    symbolicFrameVectorBytes(goalFrames),
                    symbolicFrameVectorBytes(enemyAliveFrames),
                    std::numeric_limits<std::uint64_t>::max());
                if (enemySplitStorage != 0) {
                    if (!chargeBudget(
                            budget,
                            limits,
                            goalFrames.size() + enemyAliveFrames.size(),
                            enemySplitStorage)) {
                        snapshot.check.reason = "enemy terminal split exceeded proof budget";
                        return false;
                    }
                    enemySplitBytes.add(enemySplitStorage);
                }
                for (const SymbolicFrame &goalFrame: goalFrames) {
                    if (!addTerminal(goalFrame, true, false)) {
                        return false;
                    }
                }

                Predicate heroZero;
                heroZero.kind = PredicateKind::ResourceLe;
                heroZero.resource = ResourceAxis::HeroHp;
                heroZero.threshold = 0;
                for (const SymbolicFrame &enemyAlive: enemyAliveFrames) {
                    std::vector<SymbolicFrame> failureFrames;
                    std::vector<SymbolicFrame> continuingFrames;
                    if (!stepper.splitOutputPredicate(
                            enemyAlive,
                            heroZero,
                            failureFrames,
                            continuingFrames,
                            frameError)) {
                        snapshot.check.reason = "hero terminal inverse image failed: " + frameError;
                        return false;
                    }
                    ScopedBudgetBytes heroSplitBytes(budget);
                    const std::uint64_t heroSplitStorage = saturatedReservationAdd(
                        symbolicFrameVectorBytes(failureFrames),
                        symbolicFrameVectorBytes(continuingFrames),
                        std::numeric_limits<std::uint64_t>::max());
                    if (heroSplitStorage != 0) {
                        if (!chargeBudget(
                                budget,
                                limits,
                                failureFrames.size() + continuingFrames.size(),
                                heroSplitStorage)) {
                            snapshot.check.reason = "hero terminal split exceeded proof budget";
                            return false;
                        }
                        heroSplitBytes.add(heroSplitStorage);
                    }
                    for (const SymbolicFrame &failureFrame: failureFrames) {
                        if (!addTerminal(failureFrame, false, true)) {
                            return false;
                        }
                    }
                    for (const SymbolicFrame &continuingFrame: continuingFrames) {
                        Box continuingOutput;
                        if (!stepper.outputBox(continuingFrame, continuingOutput, frameError)) {
                            snapshot.check.reason = "continuing exact image failed: " + frameError;
                            return false;
                        }
                        if (!contains(base, continuingOutput)) {
                            snapshot.check.reason = "continuing detailed output escapes Envelope[t+1]";
                            return false;
                        }
                        const PredicatePartition *outputPartition =
                            partitionAt(partitions, continuingFrame.rngPosition);
                        if (outputPartition == nullptr) {
                            snapshot.check.reason = "detailed output references missing partition";
                            return false;
                        }
                        std::vector<OutputLeafClassification> classified;
                        if (!stepper.classifyOutput(
                                continuingFrame,
                                *outputPartition,
                                classified,
                                frameError)) {
                            snapshot.check.reason = "detailed output inverse classification failed: " + frameError;
                            return false;
                        }
                        ScopedBudgetBytes classifiedBytes(budget);
                        const std::uint64_t classifiedStorage =
                            outputLeafClassificationVectorBytes(classified);
                        if (!chargeBudget(
                                budget,
                                limits,
                                classified.size(),
                                classifiedStorage)) {
                            snapshot.check.reason = "detailed output classification exceeded proof budget";
                            return false;
                        }
                        classifiedBytes.add(classifiedStorage);
                        for (const OutputLeafClassification &leaf: classified) {
                            Box leafOutput;
                            CompletionWeightTerm weight;
                            if (!stepper.outputBox(leaf.frame, leafOutput, frameError) ||
                                !prefixWeightTerm(leaf.frame, weight, frameError)) {
                                snapshot.check.reason = "classified detailed leaf failed: " + frameError;
                                return false;
                            }
                            if (!contains(base, leafOutput)) {
                                snapshot.check.reason = "classified detailed leaf escapes Envelope[t+1]";
                                return false;
                            }
                            CheckedEdge edge;
                            edge.kind = CheckedEdgeKind::Detailed;
                            edge.elapsedTurn = elapsedTurn;
                            edge.source = source;
                            edge.selectedCommand = command;
                            edge.rootDomain = leaf.frame.inputDomain;
                            edge.continuingOutput = leafOutput;
                            edge.firstOutputPosition = leaf.frame.rngPosition;
                            edge.lastOutputPosition = leaf.frame.rngPosition;
                            edge.hasContinuingOutput = true;
                            edge.targets.push_back({leaf.frame.rngPosition, leaf.localCellId});
                            edge.weightTerms = {weight};
                            const std::uint64_t edgeBytes = checkedEdgeBytes(edge);
                            if (!chargeBudget(budget, limits, 0, edgeBytes)) {
                                snapshot.check.reason = "classified detailed edge storage exceeded proof budget";
                                return false;
                            }
                            outputBytes.add(edgeBytes);
                            edges.push_back(std::move(edge));
                        }
                    }
                }
                return true;
            };

            while (!pending.empty()) {
                const std::uint64_t currentPendingBytes = pendingDetailedNodeBytes(pending.back());
                PendingDetailedNode pendingNode = std::move(pending.back());
                pending.pop_back();
                if (pendingNode.nodeIndex >= proof.detailedNodes.size() ||
                    visited[pendingNode.nodeIndex] != 0) {
                    snapshot.check.reason = "detailed proof tree has an invalid, shared, or cyclic child reference";
                    return false;
                }
                visited[pendingNode.nodeIndex] = 1;
                const DetailedProofNode &proofNode = proof.detailedNodes[pendingNode.nodeIndex];
                SymbolicFrame frame = std::move(pendingNode.frame);
                if (proofNode.expectedPc != stepper.point(frame) ||
                    !(proofNode.claimedFrame == frame)) {
                    snapshot.check.reason = "detailed proof-node pc or payload disagrees with the recomputed Frame";
                    return false;
                }
                if (!chargeBudget(budget, limits, 1, 0)) {
                    snapshot.check.reason = "detailed verify_root exceeded proof budget";
                    return false;
                }

                if (site != nullptr && stepper.point(frame) == site->pc) {
                    if (proofNode.kind != DetailedProofNodeKind::Complete ||
                        !proofNode.children.empty()) {
                        snapshot.check.reason = "partial COMPLETE proof node is missing, mis-typed, or has children beyond the cut";
                        return false;
                    }
                    reachedCut = true;

                    Box current;
                    std::string frameError;
                    if (!stepper.outputBox(frame, current, frameError)) {
                        snapshot.check.reason = "COMPLETE current image failed: " + frameError;
                        return false;
                    }
                    CompletionWeightTerm prefix;
                    if (!prefixWeightTerm(frame, prefix, frameError)) {
                        snapshot.check.reason = "COMPLETE prefix bound failed: " + frameError;
                        return false;
                    }

                    if (current.enemyHp.hi == 0 ||
                        (current.heroHp.hi == 0 && current.enemyHp.lo > 0)) {
                        if (!addExactFinishedOutput(frame)) {
                            return false;
                        }
                        continue;
                    }

                    int remainingRng = 0;
                    if (!stepper.remainingRngBound(frame, remainingRng, frameError)) {
                        snapshot.check.reason = "COMPLETE Rremaining failed: " + frameError;
                        return false;
                    }
                    const int lastOutputPosition = frame.rngPosition + remainingRng;
                    const int envelopeLastPosition = problem.s0.position +
                                                     (elapsedTurn + 1) * bundle.bounds.rMax;
                    if (frame.rngPosition < problem.s0.position ||
                        lastOutputPosition > envelopeLastPosition ||
                        lastOutputPosition >= ExactReplay::kRngTapeSize) {
                        snapshot.check.reason = "partial COMPLETE Rremaining escapes Envelope[t+1]";
                        return false;
                    }

                    CompletionCheckpoint checkpoint{
                        elapsedTurn,
                        source,
                        command,
                        proof.cut,
                        rootDomain,
                        frame,
                    };
                    const std::uint64_t checkpointBytes = completionCheckpointBytes(checkpoint);
                    if (!chargeBudget(budget, limits, 0, checkpointBytes)) {
                        snapshot.check.reason = "partial COMPLETE checkpoint storage exceeded proof budget";
                        return false;
                    }
                    outputBytes.add(checkpointBytes);
                    checkpoints.push_back(std::move(checkpoint));
                    for (const CompletionWeightTerm &remaining: remainingTerms) {
                        CompletionWeightTerm total;
                        if (!addCompletionTerm(prefix, remaining, total, frameError)) {
                            snapshot.check.reason = frameError;
                            return false;
                        }
                        Box output;
                        if (!completionOutputEnvelope(bundle,
                                                      proof.cut,
                                                      current,
                                                      remaining,
                                                      output,
                                                      frameError)) {
                            snapshot.check.reason = frameError.empty()
                                ? "partial COMPLETE produced an empty output"
                                : frameError;
                            return false;
                        }
                        if (!addCheckedOutput(frame,
                                              output,
                                              total,
                                              frame.rngPosition,
                                              lastOutputPosition,
                                              true)) {
                            return false;
                        }
                    }
                    pendingBytes.release(currentPendingBytes);
                    continue;
                }

                if (proofNode.kind == DetailedProofNodeKind::Complete) {
                    snapshot.check.reason = "detailed proof uses COMPLETE at an unregistered or wrong cut";
                    return false;
                }

                std::string instructionError;
                const Instruction *instruction = instructionAt(bundle, frame, instructionError);
                if (instruction == nullptr) {
                    snapshot.check.reason = instructionError;
                    return false;
                }

                std::string instructionWorkError;
                const std::optional<std::uint64_t> nativeWork =
                    nativeInternalWorkAt(bundle, frame, instructionWorkError);
                if (!nativeWork.has_value()) {
                    snapshot.check.reason = instructionWorkError;
                    return false;
                }
                if (*nativeWork != 0 && !chargeBudget(budget, limits, *nativeWork, 0)) {
                    snapshot.check.reason = "NATIVE internal work exceeded proof budget";
                    return false;
                }

                SymbolicStepResult step = stepper.step(frame);
                if (!step.accepted) {
                    snapshot.check.reason = "verify_root symbolic step failed at " + frame.routineId + ":" +
                                            std::to_string(frame.pc) + ": " + step.reason;
                    return false;
                }
                const DetailedProofNodeKind expectedKind =
                    detailedNodeKindForOpcode(instruction->opcode);
                if (proofNode.kind != expectedKind) {
                    snapshot.check.reason = "detailed proof-node kind disagrees with the registered opcode";
                    return false;
                }
                ScopedBudgetBytes stepBytes(budget);
                const std::uint64_t producedBytes = symbolicFrameVectorBytes(step.frames);
                if (producedBytes != 0) {
                    if (!chargeBudget(budget, limits, step.frames.size(), producedBytes)) {
                        snapshot.check.reason = "symbolic child materialization exceeded proof budget";
                        return false;
                    }
                    stepBytes.add(producedBytes);
                }
                if (step.finished) {
                    if (proofNode.kind != DetailedProofNodeKind::Finish ||
                        !proofNode.children.empty() || step.frames.size() != 1 ||
                        !(step.frames.front() == frame)) {
                        snapshot.check.reason = "FINISH proof node or terminal payload disagrees with SymbolicStepper";
                        return false;
                    }
                    for (const SymbolicFrame &finished: step.frames) {
                        if (!addExactFinishedOutput(finished)) {
                            return false;
                        }
                    }
                    pendingBytes.release(currentPendingBytes);
                    continue;
                }
                if (proofNode.children.size() != step.frames.size()) {
                    snapshot.check.reason = "detailed proof omitted or invented a nonempty symbolic child";
                    return false;
                }
                for (std::size_t childIndex = step.frames.size(); childIndex > 0; --childIndex) {
                    const std::size_t index = childIndex - 1;
                    const std::uint32_t proofChild = proofNode.children[index];
                    if (proofChild >= proof.detailedNodes.size()) {
                        snapshot.check.reason = "detailed proof child index is outside the submitted tree";
                        return false;
                    }
                    PendingDetailedNode childPending{std::move(step.frames[index]), proofChild};
                    const std::uint64_t childPendingBytes = pendingDetailedNodeBytes(childPending);
                    if (!chargeBudget(budget, limits, 0, childPendingBytes)) {
                        snapshot.check.reason = "detailed verify_root stack exceeded proof budget";
                        return false;
                    }
                    pendingBytes.add(childPendingBytes);
                    pending.push_back(std::move(childPending));
                }
                pendingBytes.release(currentPendingBytes);
            }

            if (std::find(visited.begin(), visited.end(), static_cast<std::uint8_t>(0)) != visited.end()) {
                snapshot.check.reason = "detailed proof contains unreachable or unreferenced proof nodes";
                return false;
            }

            if (!reachedCut) {
                snapshot.check.reason = "submitted partial COMPLETE pc is unreachable from this required root";
                return false;
            }
            if (edges.empty()) {
                snapshot.check.reason = "detailed verify_root produced no checked outputs";
                return false;
            }
            checkedOutputBytes = outputBytes.bytes();
            outputBytes.disarm();
            return true;
        };

        const PredicatePartition *rootPartition = partitionAt(partitions, problem.s0.position);
        if (rootPartition == nullptr) {
            snapshot.check.reason = "root RNG position has no partition";
            return snapshot;
        }
        std::string projectError;
        const std::optional<std::uint32_t> rootLeaf = project(problem.s0, *rootPartition, projectError);
        if (!rootLeaf) {
            snapshot.check.reason = projectError;
            return snapshot;
        }

        snapshot.root = {problem.s0.position, *rootLeaf};
        // The proof state is a hotel indexed directly by RNG position.  Give
        // every current (p, local_cell_id) a dense slot and keep Support as one
        // bit per room.  This merges all histories that reach the same hotel
        // room without storing an unbounded set of concrete individuals.
        std::vector<std::size_t> hotelCellOffsets(hotelCount + 1, 0);
        for (std::size_t hotelIndex = 0; hotelIndex < hotelCount; ++hotelIndex) {
            hotelCellOffsets[hotelIndex + 1] =
                hotelCellOffsets[hotelIndex] + checkedPartitions[hotelIndex].leaves.size();
        }
        const std::size_t denseCellCount = hotelCellOffsets.back();
        std::vector<CellKey> denseCells;
        denseCells.reserve(denseCellCount);
        for (std::size_t hotelIndex = 0; hotelIndex < hotelCount; ++hotelIndex) {
            const int position = firstPosition + static_cast<int>(hotelIndex);
            for (const auto &[leafId, leafBox]: checkedPartitions[hotelIndex].leaves) {
                (void) leafBox;
                denseCells.push_back({position, leafId});
            }
        }
        const std::size_t supportWordCount = (denseCellCount + 63u) / 64u;
        const std::size_t supportLayerCount = static_cast<std::size_t>(horizon) + 1;
        const std::uint64_t denseSupportBytes =
            hotelCellOffsets.size() * sizeof(std::size_t) +
            denseCells.size() * sizeof(CellKey) +
            supportLayerCount * supportWordCount * sizeof(std::uint64_t);
        if (!chargeBudget(budget, limits, 0, denseSupportBytes)) {
            snapshot.check.reason = "dense hotel Support working storage exceeded proof budget";
            return snapshot;
        }
        temporaryBytes.add(denseSupportBytes);
        std::vector<std::uint64_t> supportBits(
            supportLayerCount * supportWordCount,
            0);

        auto denseCellIndex = [&](const CellKey &key) -> std::optional<std::size_t> {
            const CheckedPartition *partition = checkedPartitionAt(key.rngPosition);
            if (partition == nullptr) {
                return std::nullopt;
            }
            const auto it = std::lower_bound(
                partition->leaves.begin(),
                partition->leaves.end(),
                key.localCellId,
                [](const auto &leaf, std::uint32_t id) { return leaf.first < id; });
            if (it == partition->leaves.end() || it->first != key.localCellId) {
                return std::nullopt;
            }
            const std::size_t hotelIndex =
                static_cast<std::size_t>(key.rngPosition - firstPosition);
            return hotelCellOffsets[hotelIndex] +
                   static_cast<std::size_t>(it - partition->leaves.begin());
        };
        auto insertSupportKey = [&](int elapsedTurn, const CellKey &key) -> bool {
            if (!chargeBudget(budget, limits, 1, 0)) {
                snapshot.check.reason = "dense hotel Support lookup exceeded proof budget";
                return false;
            }
            if (elapsedTurn < 0 || elapsedTurn > horizon) {
                snapshot.check.reason = "dense hotel Support layer is outside horizon";
                return false;
            }
            const std::optional<std::size_t> denseIndex = denseCellIndex(key);
            if (!denseIndex.has_value()) {
                snapshot.check.reason = "Support references an undefined hotel room";
                return false;
            }
            const std::size_t wordIndex = *denseIndex / 64u;
            const unsigned bitIndex = static_cast<unsigned>(*denseIndex % 64u);
            supportBits[static_cast<std::size_t>(elapsedTurn) * supportWordCount + wordIndex] |=
                std::uint64_t{1} << bitIndex;
            return true;
        };
        if (!insertSupportKey(0, snapshot.root)) {
            return snapshot;
        }
        snapshot.support.resize(supportLayerCount);
        auto materializeSupportLayer = [&](int elapsedTurn) -> bool {
            std::vector<CellKey> &layer = snapshot.support[static_cast<std::size_t>(elapsedTurn)];
            if (!layer.empty()) {
                snapshot.check.reason = "dense hotel Support layer was materialized more than once";
                return false;
            }
            const std::size_t wordBase =
                static_cast<std::size_t>(elapsedTurn) * supportWordCount;
            std::size_t roomCount = 0;
            for (std::size_t wordIndex = 0; wordIndex < supportWordCount; ++wordIndex) {
                roomCount += static_cast<std::size_t>(
                    std::popcount(supportBits[wordBase + wordIndex]));
            }
            const std::uint64_t supportBytes = roomCount * sizeof(CellKey);
            if (!chargeBudget(budget, limits, roomCount, supportBytes)) {
                snapshot.check.reason = "Support materialization exceeded proof budget";
                return false;
            }
            retainedSnapshotBytes.add(supportBytes);
            layer.reserve(roomCount);
            for (std::size_t wordIndex = 0; wordIndex < supportWordCount; ++wordIndex) {
                std::uint64_t bits = supportBits[wordBase + wordIndex];
                while (bits != 0) {
                    const unsigned bitIndex = std::countr_zero(bits);
                    const std::size_t denseIndex = wordIndex * 64u + bitIndex;
                    if (denseIndex >= denseCells.size()) {
                        snapshot.check.reason = "dense hotel Support contains a bit outside the room table";
                        return false;
                    }
                    layer.push_back(denseCells[denseIndex]);
                    bits &= bits - 1;
                }
            }
            if (layer.size() != roomCount) {
                snapshot.check.reason = "dense hotel Support materialization count mismatch";
                return false;
            }
            return true;
        };
        snapshot.edgesByElapsedTurn.resize(static_cast<std::size_t>(horizon));
        std::uint64_t storedModelTerms = 0;
        const std::size_t commandCount = bundle.profile.heroCommands.size();
        int maximumCommandValue = -1;
        for (const int command: bundle.profile.heroCommands) {
            if (command < 0) {
                snapshot.check.reason = "registered hero command is negative";
                return snapshot;
            }
            maximumCommandValue = std::max(maximumCommandValue, command);
        }
        const std::uint64_t commandSlotBytes = maximumCommandValue < 0
            ? 0
            : (static_cast<std::uint64_t>(maximumCommandValue) + 1u) * sizeof(int);
        if (commandSlotBytes != 0 && !chargeBudget(budget, limits, 0, commandSlotBytes)) {
            snapshot.check.reason = "dense command-slot lookup exceeded proof budget";
            return snapshot;
        }
        temporaryBytes.add(commandSlotBytes);
        std::vector<int> commandSlotByValue(
            maximumCommandValue < 0 ? 0 : static_cast<std::size_t>(maximumCommandValue) + 1u,
            -1);
        for (std::size_t slot = 0; slot < commandCount; ++slot) {
            const int command = bundle.profile.heroCommands[slot];
            if (commandSlotByValue[static_cast<std::size_t>(command)] != -1) {
                snapshot.check.reason = "registered hero command list contains a duplicate";
                return snapshot;
            }
            commandSlotByValue[static_cast<std::size_t>(command)] = static_cast<int>(slot);
        }

        auto reuseCheckedTurnEntry = [&](int elapsedTurn,
                                         const CellKey &source,
                                         int command,
                                         const Box &rootDomain,
                                         const CheckedEdge *oldEdge,
                                         CheckedRootRecord &record,
                                         CheckedEdge &edge) -> std::optional<bool> {
            if (oldEdge == nullptr || checkedReuseSnapshot == nullptr || submittedProofs != nullptr ||
                retainDefaultProofRecords || !checkedReuseSnapshot->check.accepted ||
                elapsedTurn < 0 || elapsedTurn >= horizon) {
                return false;
            }
            if (!chargeBudget(budget, limits, 1, 0)) {
                snapshot.check.reason = "checked TurnEntry reuse lookup exceeded proof budget";
                return std::nullopt;
            }
            if (oldEdge->selectedCommand != command || oldEdge->source != source ||
                oldEdge->kind != CheckedEdgeKind::Completion ||
                oldEdge->completionCut != CompletionCutId::TurnEntry ||
                oldEdge->rootDomain != rootDomain || !oldEdge->useRegisteredTurnEntryTerms ||
                !oldEdge->weightTerms.empty() || !oldEdge->targets.empty()) {
                return false;
            }

            const int envelopeLastPosition = problem.s0.position +
                                             (elapsedTurn + 1) * bundle.bounds.rMax;
            if (oldEdge->firstOutputPosition < problem.s0.position ||
                oldEdge->lastOutputPosition > envelopeLastPosition ||
                oldEdge->firstOutputPosition > oldEdge->lastOutputPosition ||
                (oldEdge->hasContinuingOutput && !contains(base, oldEdge->continuingOutput))) {
                return false;
            }

            record = {};
            record.proofKind = RootProofKind::Completion;
            record.elapsedTurn = elapsedTurn;
            record.source = source;
            record.selectedCommand = command;
            record.rootDomain = rootDomain;
            record.partitionVersion = partitions.partitionVersion;
            record.coverageVersion = snapshot.coverageVersion;
            record.verifiedPc = turnEntrySite->pc;
            record.verifiedCut = CompletionCutId::TurnEntry;
            edge = *oldEdge;
            return true;
        };

        for (int elapsedTurn = 0; elapsedTurn < horizon; ++elapsedTurn) {
            if (!materializeSupportLayer(elapsedTurn)) {
                return snapshot;
            }
            ScopedBudgetBytes completionCoverageBytes(budget);
            const std::size_t completionCoverageCount = partitions.trees.size() + 1;
            const std::uint64_t completionCoverageStorage =
                completionCoverageCount * sizeof(std::int64_t);
            if (!chargeBudget(budget, limits, 0, completionCoverageStorage)) {
                snapshot.check.reason = "COMPLETE Support interval-union storage exceeded proof budget";
                return snapshot;
            }
            completionCoverageBytes.add(completionCoverageStorage);
            std::vector<std::int64_t> completionCoverageDiff(completionCoverageCount, 0);
            int cachedTemplatePosition = std::numeric_limits<int>::min();
            std::size_t cachedTemplateRangeBegin = 0;
            std::size_t cachedTemplateRangeEnd = 0;
            std::vector<std::size_t> cachedTemplateCommandBegin(commandCount, 0);
            std::vector<std::size_t> cachedTemplateCommandEnd(commandCount, 0);
            const std::vector<CheckedEdge> *oldReuseLayer = nullptr;
            std::size_t oldReuseCursor = 0;
            std::vector<const CheckedEdge *> reusableForCommand(commandCount, nullptr);
            if (checkedReuseSnapshot != nullptr && submittedProofs == nullptr &&
                !retainDefaultProofRecords && checkedReuseSnapshot->check.accepted &&
                elapsedTurn < static_cast<int>(checkedReuseSnapshot->edgesByElapsedTurn.size())) {
                oldReuseLayer = &checkedReuseSnapshot->edgesByElapsedTurn[static_cast<std::size_t>(elapsedTurn)];
            }

            for (const CellKey &source: snapshot.support[elapsedTurn]) {
                const CheckedPartition *checkedPartition = checkedPartitionAt(source.rngPosition);
                if (checkedPartition == nullptr) {
                    snapshot.check.reason = "Support references an undefined RNG position";
                    return snapshot;
                }
                const Box *cell = checkedLeafDomain(*checkedPartition, source.localCellId);
                if (cell == nullptr) {
                    snapshot.check.reason = "Support references an undefined local cell";
                    return snapshot;
                }

                std::fill(reusableForCommand.begin(), reusableForCommand.end(), nullptr);
                if (oldReuseLayer != nullptr) {
                    std::uint64_t reuseScanWork = 0;
                    while (oldReuseCursor < oldReuseLayer->size() &&
                           (*oldReuseLayer)[oldReuseCursor].source < source) {
                        ++oldReuseCursor;
                        ++reuseScanWork;
                    }
                    std::size_t scan = oldReuseCursor;
                    while (scan < oldReuseLayer->size() && (*oldReuseLayer)[scan].source == source) {
                        const CheckedEdge &candidate = (*oldReuseLayer)[scan];
                        ++scan;
                        ++reuseScanWork;
                        if (candidate.kind != CheckedEdgeKind::Completion ||
                            candidate.completionCut != CompletionCutId::TurnEntry ||
                            !candidate.useRegisteredTurnEntryTerms ||
                            !candidate.weightTerms.empty() || !candidate.targets.empty()) {
                            continue;
                        }
                        for (std::size_t slot = 0; slot < commandCount; ++slot) {
                            if (bundle.profile.heroCommands[slot] != candidate.selectedCommand) {
                                continue;
                            }
                            if (reusableForCommand[slot] != nullptr) {
                                snapshot.check.reason =
                                    "checked TurnEntry reuse contains duplicate canonical roots for one hotel room";
                                return snapshot;
                            }
                            reusableForCommand[slot] = &candidate;
                            break;
                        }
                    }
                    if (scan != oldReuseCursor) {
                        oldReuseCursor = scan;
                    }
                    if (reuseScanWork != 0 && !chargeBudget(budget, limits, reuseScanWork, 0)) {
                        snapshot.check.reason = "checked TurnEntry hotel merge scan exceeded proof budget";
                        return snapshot;
                    }
                }

                std::size_t templateRangeBegin = 0;
                std::size_t templateRangeEnd = 0;
                if (generationTemplates != nullptr && !templateIndex.empty()) {
                    if (cachedTemplatePosition != source.rngPosition) {
                        const TemplateLookupKey firstKey{
                            elapsedTurn,
                            source.rngPosition,
                            std::numeric_limits<int>::min(),
                        };
                        std::size_t lo = 0;
                        std::size_t hi = templateIndex.size();
                        while (lo < hi) {
                            const std::size_t mid = lo + (hi - lo) / 2;
                            if (templateIndex[mid].key < firstKey) {
                                lo = mid + 1;
                            } else {
                                hi = mid;
                            }
                        }
                        cachedTemplatePosition = source.rngPosition;
                        cachedTemplateRangeBegin = lo;
                        cachedTemplateRangeEnd = lo;
                        while (cachedTemplateRangeEnd < templateIndex.size() &&
                               std::get<0>(templateIndex[cachedTemplateRangeEnd].key) == elapsedTurn &&
                               std::get<1>(templateIndex[cachedTemplateRangeEnd].key) == source.rngPosition) {
                            ++cachedTemplateRangeEnd;
                        }
                        std::fill(
                            cachedTemplateCommandBegin.begin(),
                            cachedTemplateCommandBegin.end(),
                            cachedTemplateRangeEnd);
                        std::fill(
                            cachedTemplateCommandEnd.begin(),
                            cachedTemplateCommandEnd.end(),
                            cachedTemplateRangeEnd);
                        std::uint64_t commandRangeWork = 0;
                        for (std::size_t templateLookupIndex = cachedTemplateRangeBegin;
                             templateLookupIndex < cachedTemplateRangeEnd;
                             ++templateLookupIndex) {
                            ++commandRangeWork;
                            const int templateCommand =
                                std::get<2>(templateIndex[templateLookupIndex].key);
                            if (templateCommand < 0 ||
                                static_cast<std::size_t>(templateCommand) >= commandSlotByValue.size() ||
                                commandSlotByValue[static_cast<std::size_t>(templateCommand)] < 0) {
                                snapshot.check.reason =
                                    "proof-template index contains a command outside registered profile";
                                return snapshot;
                            }
                            const std::size_t slot = static_cast<std::size_t>(
                                commandSlotByValue[static_cast<std::size_t>(templateCommand)]);
                            if (cachedTemplateCommandBegin[slot] == cachedTemplateRangeEnd) {
                                cachedTemplateCommandBegin[slot] = templateLookupIndex;
                            }
                            cachedTemplateCommandEnd[slot] = templateLookupIndex + 1;
                        }
                        if (commandRangeWork != 0 &&
                            !chargeBudget(budget, limits, commandRangeWork, 0)) {
                            snapshot.check.reason =
                                "proof-template dense command range construction exceeded proof budget";
                            return snapshot;
                        }
                    }
                    templateRangeBegin = cachedTemplateRangeBegin;
                    templateRangeEnd = cachedTemplateRangeEnd;
                }

                for (std::size_t commandIndex = 0;
                     commandIndex < bundle.profile.heroCommands.size();
                     ++commandIndex) {
                    const int command = bundle.profile.heroCommands[commandIndex];
                    const CommandProfile &commandProfile = bundle.profile.commandProfiles[commandIndex];
                    const int absoluteTurn = problem.startTurn + elapsedTurn;
                    if (!commandAllowedByConstraints(problem, absoluteTurn, command)) {
                        continue;
                    }
                    const Box rootDomain = selectableDomain(commandProfile, *cell);
                    if (rootDomain.empty()) {
                        continue;
                    }

                    CheckedRootRecord record;
                    std::vector<CheckedEdge> checkedEdges;
                    CheckedEdge defaultTurnEntryEdge;
                    bool usesDefaultTurnEntryEdge = false;
                    std::vector<CompletionCheckpoint> checkpoints;
                    std::uint64_t localDerivedByteCount = 0;
                    bool generatedDefaultTurnEntry = false;
                    bool reusedCheckedTurnEntry = false;
                    std::optional<RootProofRecord> proof;
                    std::uint64_t proofRecordBytes = 0;
                    ScopedBudgetBytes localProofBytes(budget);
                    ScopedBudgetBytes localDerivedBytes(budget);

                    const std::size_t commandTemplateRangeBegin =
                        commandIndex < cachedTemplateCommandBegin.size()
                            ? cachedTemplateCommandBegin[commandIndex]
                            : templateRangeEnd;
                    const std::size_t commandTemplateRangeEnd =
                        commandIndex < cachedTemplateCommandEnd.size()
                            ? cachedTemplateCommandEnd[commandIndex]
                            : templateRangeEnd;

                    bool strongerTemplateCoversRoot = false;
                    if (generationTemplates != nullptr &&
                        commandTemplateRangeBegin < commandTemplateRangeEnd) {
                        std::uint64_t strongerTemplateScanWork = 0;
                        for (std::size_t templateLookupIndex = commandTemplateRangeBegin;
                             templateLookupIndex < commandTemplateRangeEnd;
                             ++templateLookupIndex) {
                            ++strongerTemplateScanWork;
                            const ProofTemplate &candidate =
                                (*generationTemplates)[templateIndex[templateLookupIndex].index];
                            if (!contains(candidate.coveredDomain, rootDomain)) {
                                continue;
                            }
                            if (candidate.kind == RootProofKind::FullyDetailed ||
                                candidate.cut != CompletionCutId::TurnEntry) {
                                strongerTemplateCoversRoot = true;
                                break;
                            }
                        }
                        if (strongerTemplateScanWork != 0 &&
                            !chargeBudget(budget, limits, strongerTemplateScanWork, 0)) {
                            snapshot.check.reason =
                                "checked TurnEntry stronger-template guard exceeded proof budget";
                            return snapshot;
                        }
                    }

                    const std::optional<bool> reuse = strongerTemplateCoversRoot
                        ? std::optional<bool>{false}
                        : reuseCheckedTurnEntry(
                            elapsedTurn,
                            source,
                            command,
                            rootDomain,
                            commandIndex < reusableForCommand.size()
                                ? reusableForCommand[commandIndex]
                                : nullptr,
                            record,
                            defaultTurnEntryEdge);
                    if (!reuse.has_value()) {
                        return snapshot;
                    }
                    if (*reuse) {
                        reusedCheckedTurnEntry = true;
                        generatedDefaultTurnEntry = true;
                        usesDefaultTurnEntryEdge = true;
                        localDerivedByteCount = checkedRootRecordBytes(record);
                        localDerivedByteCount = saturatedReservationAdd(
                            localDerivedByteCount,
                            checkedEdgeBytes(defaultTurnEntryEdge),
                            std::numeric_limits<std::uint64_t>::max());
                        if (!chargeBudget(budget, limits, 0, localDerivedByteCount)) {
                            snapshot.check.reason = "reused TurnEntry checked output storage exceeded proof budget";
                            return snapshot;
                        }
                        localDerivedBytes.add(localDerivedByteCount);
                    } else {
                        proof = acquireRequiredProof(
                            elapsedTurn,
                            source,
                            command,
                            rootDomain,
                            commandTemplateRangeBegin,
                            commandTemplateRangeEnd,
                            generatedDefaultTurnEntry);
                        if (!proof.has_value()) {
                            return snapshot;
                        }
                        proofRecordBytes = rootProofRecordBytes(*proof);
                        if (!chargeBudget(budget, limits, 0, proofRecordBytes)) {
                            snapshot.check.reason = "required-root proof record exceeded byte budget";
                            return snapshot;
                        }
                        localProofBytes.add(proofRecordBytes);
                    }

                    if (!reusedCheckedTurnEntry &&
                        proof->kind == RootProofKind::Completion &&
                        proof->cut == CompletionCutId::TurnEntry) {
                        CheckedEdge edge;
                        if (!verifyTurnEntryProof(
                                *proof,
                                elapsedTurn,
                                source,
                                command,
                                commandProfile,
                                rootDomain,
                                record,
                                edge,
                                generatedDefaultTurnEntry)) {
                            return snapshot;
                        }
                        // TurnEntry is the required root itself.  Its symbolic
                        // frame is uniquely reconstructible from the retained
                        // root identity/domain, so retaining one large frame
                        // per required root only duplicates derivable state.
                        // Partial COMPLETE cuts still retain their actual
                        // reached frames below because those are not directly
                        // reconstructible from the root without replaying the
                        // detailed prefix.
                        const Box output = turnEntryOutputEnvelope(
                            rootDomain,
                            turnEntrySummaries[commandIndex]);
                        if (output.enemyHp.lo < 0 || output.heroHp.lo < 0 ||
                            output.mp.lo < 0 || output.herb.lo < 0) {
                            snapshot.check.reason = "COMPLETE produced a negative terminal resource bound";
                            return snapshot;
                        }
                        edge.mayReachGoal = output.enemyHp.lo == 0;
                        edge.mayReachFailure = output.heroHp.lo == 0 && output.enemyHp.hi > 0;

                        Box continuing = output;
                        continuing.enemyHp.lo = std::max<std::int64_t>(continuing.enemyHp.lo, 1);
                        continuing.heroHp.lo = std::max<std::int64_t>(continuing.heroHp.lo, 1);
                        if (!continuing.empty()) {
                            if (!contains(base, continuing)) {
                                snapshot.check.reason = "continuing COMPLETE output escapes Envelope[t+1]";
                                return snapshot;
                            }
                            edge.hasContinuingOutput = true;
                            edge.continuingOutput = continuing;
                            for (int outputPosition = edge.firstOutputPosition;
                                 outputPosition <= edge.lastOutputPosition;
                                 ++outputPosition) {
                                if (checkedPartitionAt(outputPosition) == nullptr) {
                                    snapshot.check.reason = "COMPLETE output references missing partition";
                                    return snapshot;
                                }
                            }
                        }
                        defaultTurnEntryEdge = std::move(edge);
                        usesDefaultTurnEntryEdge = true;
                        localDerivedByteCount = checkedRootRecordBytes(record);
                        localDerivedByteCount = saturatedReservationAdd(
                            localDerivedByteCount,
                            checkedEdgeBytes(defaultTurnEntryEdge),
                            std::numeric_limits<std::uint64_t>::max());
                        for (const CompletionCheckpoint &checkpoint: checkpoints) {
                            localDerivedByteCount = saturatedReservationAdd(
                                localDerivedByteCount,
                                completionCheckpointBytes(checkpoint),
                                std::numeric_limits<std::uint64_t>::max());
                        }
                        if (!chargeBudget(budget, limits, 0, localDerivedByteCount)) {
                            snapshot.check.reason = "turn-entry checked output storage exceeded proof budget";
                            return snapshot;
                        }
                        localDerivedBytes.add(localDerivedByteCount);
                    } else if (!reusedCheckedTurnEntry && !verifyDetailedProof(
                                   *proof,
                                   elapsedTurn,
                                   source,
                                   command,
                                   rootDomain,
                                   record,
                                   checkedEdges,
                                   checkpoints,
                                   localDerivedByteCount)) {
                        return snapshot;
                    } else if (!reusedCheckedTurnEntry) {
                        // verifyDetailedProof leaves its checked outputs charged
                        // on success; adopt those bytes so later failures in this
                        // root roll them back transactionally.
                        localDerivedBytes.add(localDerivedByteCount);
                    }

                    std::size_t edgeTargets = 0;
                    std::size_t edgeTerms = 0;
                    std::uint64_t edgeStoredTermBytes = 0;

                    std::size_t detailedTerms = 0;
                    std::size_t completionTerms = 0;
                    std::uint64_t dpDedupWork = 0;
                    bool hasCompletionDpTerm = false;
                    const std::size_t checkedEdgeCount =
                        usesDefaultTurnEntryEdge ? 1 : checkedEdges.size();
                    auto checkedEdgeAt = [&](std::size_t index) -> const CheckedEdge & {
                        return usesDefaultTurnEntryEdge ? defaultTurnEntryEdge : checkedEdges[index];
                    };
                    for (std::size_t checkedEdgeIndex = 0;
                         checkedEdgeIndex < checkedEdgeCount;
                         ++checkedEdgeIndex) {
                        const CheckedEdge &edge = checkedEdgeAt(checkedEdgeIndex);
                        if (edge.kind == CheckedEdgeKind::Completion) {
                            if (!edge.targets.empty() ||
                                (edge.hasContinuingOutput &&
                                 edge.firstOutputPosition > edge.lastOutputPosition)) {
                                snapshot.check.reason = "COMPLETE edge violates virtual-target representation";
                                return snapshot;
                            }
                        } else if (edge.hasContinuingOutput) {
                            if (edge.targets.empty() ||
                                edge.firstOutputPosition != edge.lastOutputPosition ||
                                std::any_of(
                                    edge.targets.begin(),
                                    edge.targets.end(),
                                    [&](const CellKey &target) {
                                        return target.rngPosition != edge.firstOutputPosition;
                                    })) {
                                snapshot.check.reason = "detailed continuing edge violates explicit-target representation";
                                return snapshot;
                            }
                        }
                        edgeTargets += edge.targets.size();
                        const std::size_t logicalWeightTerms = edge.useRegisteredTurnEntryTerms
                            ? commandProfile.turnEntryCompletionTerms.size()
                            : edge.weightTerms.size();
                        if (logicalWeightTerms > std::numeric_limits<std::size_t>::max() - edgeTerms) {
                            snapshot.check.reason = "checked edge term count overflow";
                            return snapshot;
                        }
                        edgeTerms += logicalWeightTerms;
                        const std::uint64_t storedTermBytes =
                            edge.weightTerms.size() * sizeof(CompletionWeightTerm);
                        if (storedTermBytes >
                            std::numeric_limits<std::uint64_t>::max() - edgeStoredTermBytes) {
                            snapshot.check.reason = "checked edge stored term byte count overflow";
                            return snapshot;
                        }
                        edgeStoredTermBytes += storedTermBytes;

                        // J/C bound the distinct terms handed to the success-
                        // reachability DP, not proof-tree leaves.  Keep every
                        // checked edge for coverage and mismatch repair, but a
                        // failure-only terminal contributes no DP term and two
                        // proof branches with the same weight and G/live
                        // destination contribute the same max-plus term once.
                        bool duplicateDpTerm = false;
                        if (hasSuccessDpDestination(edge) && edge.kind == CheckedEdgeKind::Detailed) {
                            for (std::size_t previousIndex = 0;
                                 previousIndex < checkedEdgeIndex;
                                 ++previousIndex) {
                                const CheckedEdge &previous = checkedEdgeAt(previousIndex);
                                if (!hasSuccessDpDestination(previous) ||
                                    previous.kind != CheckedEdgeKind::Detailed) {
                                    continue;
                                }
                                ++dpDedupWork;
                                if (sameSuccessDpTerm(previous, edge)) {
                                    duplicateDpTerm = true;
                                    break;
                                }
                            }
                        }
                        if (hasSuccessDpDestination(edge)) {
                            if (edge.kind == CheckedEdgeKind::Detailed) {
                                if (!duplicateDpTerm) {
                                    detailedTerms += logicalWeightTerms;
                                }
                            } else {
                                hasCompletionDpTerm = true;
                            }
                        }
                        if (edge.hasContinuingOutput && edge.kind == CheckedEdgeKind::Completion) {
                            if (!edge.targets.empty() ||
                                edge.firstOutputPosition > edge.lastOutputPosition) {
                                snapshot.check.reason = "COMPLETE edge has explicit targets or an invalid virtual range";
                                return snapshot;
                            }
                            const std::int64_t firstIndex =
                                static_cast<std::int64_t>(edge.firstOutputPosition) - problem.s0.position;
                            const std::int64_t afterLastIndex =
                                static_cast<std::int64_t>(edge.lastOutputPosition) - problem.s0.position + 1;
                            if (firstIndex < 0 || afterLastIndex <= firstIndex ||
                                static_cast<std::uint64_t>(afterLastIndex) >= completionCoverageCount) {
                                snapshot.check.reason = "COMPLETE virtual target range escapes the partition family";
                                return snapshot;
                            }
                            ++completionCoverageDiff[static_cast<std::size_t>(firstIndex)];
                            --completionCoverageDiff[static_cast<std::size_t>(afterLastIndex)];
                            if (!chargeBudget(budget, limits, 1, 0)) {
                                snapshot.check.reason = "COMPLETE Support interval-union update exceeded proof budget";
                                return snapshot;
                            }
                        } else {
                            for (const CellKey &target: edge.targets) {
                                if (!insertSupportKey(elapsedTurn + 1, target)) {
                                    return snapshot;
                                }
                            }
                        }
                    }
                    if (dpDedupWork != 0 && !chargeBudget(budget, limits, dpDedupWork, 0)) {
                        snapshot.check.reason = "checked DP-term deduplication exceeded proof budget";
                        return snapshot;
                    }
                    // All completion proof leaves for one CellKey/action are
                    // represented to the DP by one conservative term: the
                    // componentwise maximum weight and the hull of their
                    // checked RNG ranges.  Proof coverage remains per-leaf.
                    completionTerms = hasCompletionDpTerm ? 1 : 0;
                    if (detailedTerms > limits.maxDetailedTermsPerAction) {
                        snapshot.check.reason =
                            "checked detailed term limit J exceeded for one CellKey/action: required=" +
                            std::to_string(detailedTerms) +
                            ", limit=" + std::to_string(limits.maxDetailedTermsPerAction);
                        return snapshot;
                    }
                    if (completionTerms > limits.maxCompletionTermsPerAction) {
                        snapshot.check.reason = "checked completion term limit C exceeded for one CellKey/action";
                        return snapshot;
                    }
                    if (edgeTerms > limits.maxModelTerms -
                            std::min<std::uint64_t>(storedModelTerms, limits.maxModelTerms)) {
                        snapshot.check.reason = "checked model term limit exceeded";
                        return snapshot;
                    }
                    storedModelTerms += edgeTerms;

                    constexpr std::uint64_t byteCap = std::numeric_limits<std::uint64_t>::max();
                    std::uint64_t checkpointBytes = 0;
                    for (const CompletionCheckpoint &checkpoint: checkpoints) {
                        checkpointBytes = saturatedReservationAdd(
                            checkpointBytes,
                            completionCheckpointBytes(checkpoint),
                            byteCap);
                    }
                    std::uint64_t derivedRootBytes = checkedRootRecordBytes(record);
                    derivedRootBytes = saturatedReservationAdd(
                        derivedRootBytes,
                        checkedEdgeCount * sizeof(CheckedEdge),
                        byteCap);
                    derivedRootBytes = saturatedReservationAdd(
                        derivedRootBytes,
                        edgeStoredTermBytes,
                        byteCap);
                    derivedRootBytes = saturatedReservationAdd(
                        derivedRootBytes,
                        edgeTargets * sizeof(CellKey),
                        byteCap);
                    derivedRootBytes = saturatedReservationAdd(
                        derivedRootBytes,
                        checkpointBytes,
                        byteCap);
                    if (derivedRootBytes != localDerivedByteCount) {
                        snapshot.check.reason = "checked-root working byte accounting disagrees with retained form";
                        return snapshot;
                    }
                    const std::size_t proofCompletionCaseCount =
                        proof.has_value() ? proof->completionCases.size() : 0;
                    const std::size_t proofDetailedNodeCount =
                        proof.has_value() ? proof->detailedNodes.size() : 0;
                    if (!chargeBudget(
                            budget,
                            limits,
                            1 + proofCompletionCaseCount + proofDetailedNodeCount +
                                edgeTerms + edgeTargets,
                            0)) {
                        snapshot.check.reason = "root coverage exceeded proof budget";
                        return snapshot;
                    }
                    ++checkedProofRoots;
                    checkedCompletionCases += generatedDefaultTurnEntry
                        ? commandProfile.turnEntryCompletionTerms.size()
                        : proofCompletionCaseCount;
                    const bool retainProofRecord =
                        proof.has_value() &&
                        (submittedProofs != nullptr ||
                         (retainDefaultProofRecords && !generatedDefaultTurnEntry) ||
                         proof->kind != RootProofKind::Completion ||
                         proof->cut != CompletionCutId::TurnEntry);
                    retainedSnapshotBytes.add(saturatedReservationAdd(
                        retainProofRecord ? proofRecordBytes : 0,
                        derivedRootBytes,
                        byteCap));
                    if (retainProofRecord) {
                        localProofBytes.disarm();
                    }
                    localDerivedBytes.disarm();

                    if (retainProofRecord) {
                        snapshot.proofs.push_back(std::move(*proof));
                    }
                    snapshot.coverage.push_back(std::move(record));
                    snapshot.completionCheckpoints.insert(
                        snapshot.completionCheckpoints.end(),
                        std::make_move_iterator(checkpoints.begin()),
                        std::make_move_iterator(checkpoints.end()));
                    if (checkedEdgeCount != 0 &&
                        !snapshot.edgesByElapsedTurn[elapsedTurn].empty() &&
                        checkedEdgeAt(0).source < snapshot.edgesByElapsedTurn[elapsedTurn].back().source) {
                        snapshot.check.reason = "checked edges are not grouped in CellKey source order";
                        return snapshot;
                    }
                    if (usesDefaultTurnEntryEdge) {
                        snapshot.edgesByElapsedTurn[elapsedTurn].push_back(
                            std::move(defaultTurnEntryEdge));
                    } else {
                        snapshot.edgesByElapsedTurn[elapsedTurn].insert(
                            snapshot.edgesByElapsedTurn[elapsedTurn].end(),
                            std::make_move_iterator(checkedEdges.begin()),
                            std::make_move_iterator(checkedEdges.end()));
                    }
                }
            }

            std::int64_t activeCompletionRanges = 0;
            for (std::size_t positionIndex = 0;
                 positionIndex + 1 < completionCoverageDiff.size();
                 ++positionIndex) {
                activeCompletionRanges += completionCoverageDiff[positionIndex];
                if (activeCompletionRanges < 0) {
                    snapshot.check.reason = "COMPLETE Support interval union became negative";
                    return snapshot;
                }
                if (!chargeBudget(budget, limits, 1, 0)) {
                    snapshot.check.reason = "COMPLETE Support interval-union scan exceeded proof budget";
                    return snapshot;
                }
                if (activeCompletionRanges == 0) {
                    continue;
                }
                const int position = problem.s0.position + static_cast<int>(positionIndex);
                const CheckedPartition *partition = checkedPartitionAt(position);
                if (partition == nullptr) {
                    snapshot.check.reason = "COMPLETE virtual target references missing partition";
                    return snapshot;
                }
                for (const auto &[leafId, leafBox]: partition->leaves) {
                    (void) leafBox;
                    if (!insertSupportKey(elapsedTurn + 1, {position, leafId})) {
                        return snapshot;
                    }
                }
            }
            if (activeCompletionRanges + completionCoverageDiff.back() != 0) {
                snapshot.check.reason = "COMPLETE Support interval union did not close";
                return snapshot;
            }
        }

        if (submittedProofs != nullptr) {
            for (bool used: submittedProofUsed) {
                if (!used) {
                    snapshot.check.reason = "submitted proof contains an unused root record";
                    return snapshot;
                }
            }
        }

        if (!materializeSupportLayer(horizon)) {
            return snapshot;
        }

        std::uint64_t supportCells = 0;
        std::uint64_t detailedEdges = 0;
        std::uint64_t completionEdges = 0;
        for (const auto &layer: snapshot.support) {
            supportCells += layer.size();
        }
        for (const auto &layer: snapshot.edgesByElapsedTurn) {
            for (const CheckedEdge &edge: layer) {
                if (edge.kind == CheckedEdgeKind::Detailed) {
                    ++detailedEdges;
                } else {
                    ++completionEdges;
                }
            }
        }
        budget.supportCells = supportCells;
        budget.detailedEdges = detailedEdges;
        budget.completionEdges = completionEdges;
        budget.proofRoots = checkedProofRoots;
        budget.completionCases = checkedCompletionCases;

        if (retainPartitionFamily) {
            const std::uint64_t partitionBytes = partitionFamilyBytes(partitions);
            std::uint64_t partitionWork = partitions.trees.size();
            for (const PredicatePartition &tree: partitions.trees) {
                partitionWork += tree.nodes.size();
            }
            if (!chargeBudget(budget, limits, partitionWork, partitionBytes)) {
                snapshot.check.reason = "snapshot partition-family storage exceeded proof budget";
                return snapshot;
            }
            retainedSnapshotBytes.add(partitionBytes);
            snapshot.partitions = partitions;
        }

        snapshot.check.accepted = true;
        retainedSnapshotBytes.disarm();
        return snapshot;
    }

    CheckResult ProofKernel::advanceCompletionCheckpoint(
        const RuleBundle &bundle,
        const Problem &problem,
        const CompletionCheckpoint &checkpoint,
        const ProofBudget &limits,
        BudgetReport &budget,
        ProofTemplate &advancedTemplate) {
        CheckResult result;
        if (!bundle.registered || problem.ruleId != bundle.id) {
            result.reason = "checkpoint rule_id is not the registered bundle";
            return result;
        }
        if (checkpoint.frame.inputDomain.empty() || checkpoint.rootDomain.empty() ||
            !contains(checkpoint.rootDomain, checkpoint.frame.inputDomain)) {
            result.reason = "checkpoint domain is empty or outside its required root";
            return result;
        }
        const CompletionSite *currentSite = lookupCompletionSite(bundle.profile, checkpoint.cut);
        const ProgramPoint checkpointPoint{checkpoint.frame.routineId, checkpoint.frame.pc};
        if (currentSite == nullptr || currentSite->pc != checkpointPoint) {
            result.reason = "checkpoint pc does not match its registered COMPLETE cut";
            return result;
        }

        std::uint64_t onePathRemainingWork = 0;
        std::string workBoundError;
        if (!remainingConcreteWorkBound(
                bundle,
                checkpoint.frame,
                onePathRemainingWork,
                workBoundError)) {
            result.reason = workBoundError;
            return result;
        }
        const std::uint64_t remainingGlobalWork =
            budget.work >= limits.maxWork ? 0 : limits.maxWork - budget.work;
        if (remainingGlobalWork <= onePathRemainingWork) {
            std::uint64_t requiredResumeWork = 0;
            std::string reservationError;
            if (!reserveCompletionProofWork(
                    bundle,
                    checkpoint.frame,
                    checkpoint.cut,
                    limits,
                    budget,
                    requiredResumeWork,
                    reservationError)) {
                result.reason = reservationError;
                return result;
            }
        }

        SymbolicStepper stepper(bundle, problem);
        std::vector<SymbolicFrame> pending;
        pending.push_back(checkpoint.frame);
        std::optional<CompletionCutId> nextCut;
        const std::uint64_t retainedBytesBeforeReservation = budget.bytes;
        BudgetReport reservedBudget = budget;
        const std::uint64_t initialPendingBytes = symbolicFrameRecordBytes(pending.back());
        if (!chargeBudget(reservedBudget, limits, 0, initialPendingBytes)) {
            budget = reservedBudget;
            budget.bytes = retainedBytesBeforeReservation;
            result.reason = "completion resume stack exceeded proof budget";
            return result;
        }

        // Treat the whole resume segment as one transactional reservation.
        // No stronger template is published until every nonempty symbolic
        // branch is either covered by the selected next COMPLETE cut or has
        // been checked through FINISH.  Work/bytes/time spent while making
        // that reservation are still charged even when it cannot complete.
        while (!pending.empty()) {
            const std::uint64_t currentFrameBytes = symbolicFrameRecordBytes(pending.back());
            SymbolicFrame frame = std::move(pending.back());
            pending.pop_back();
            if (!chargeBudget(reservedBudget, limits, 1, 0)) {
                budget = reservedBudget;
                budget.bytes = retainedBytesBeforeReservation;
                result.reason = "completion resume exceeded proof budget";
                return result;
            }

            const ProgramPoint point = stepper.point(frame);
            bool isCut = false;
            const CompletionCutId cut = cutForPoint(bundle.profile, point, isCut);
            if (isCut && cut != checkpoint.cut) {
                if (!nextCut.has_value()) {
                    nextCut = cut;
                }
                if (cut == *nextCut) {
                    // This entire nonempty branch is covered by the selected
                    // stronger COMPLETE site; do not execute beyond the cut.
                    releaseBudgetBytes(reservedBudget, currentFrameBytes);
                    continue;
                }
                // A different registered cut is not the template we are
                // proposing.  Keep this branch detailed instead of silently
                // dropping it or inventing a second completion in one root.
            }

            std::string instructionWorkError;
            const std::optional<std::uint64_t> nativeWork =
                nativeInternalWorkAt(bundle, frame, instructionWorkError);
            if (!nativeWork.has_value()) {
                budget = reservedBudget;
                budget.bytes = retainedBytesBeforeReservation;
                result.reason = instructionWorkError;
                return result;
            }
            if (*nativeWork != 0 && !chargeBudget(reservedBudget, limits, *nativeWork, 0)) {
                budget = reservedBudget;
                budget.bytes = retainedBytesBeforeReservation;
                result.reason = "completion resume NATIVE internal work exceeded proof budget";
                return result;
            }

            SymbolicStepResult step = stepper.step(frame);
            if (!step.accepted) {
                budget = reservedBudget;
                budget.bytes = retainedBytesBeforeReservation;
                result.reason = "completion resume symbolic step failed at " +
                                frame.routineId + ":" + std::to_string(frame.pc) + ": " + step.reason;
                return result;
            }
            const std::uint64_t producedBytes = symbolicFrameVectorBytes(step.frames);
            if (producedBytes != 0 &&
                !chargeBudget(reservedBudget, limits, step.frames.size(), producedBytes)) {
                budget = reservedBudget;
                budget.bytes = retainedBytesBeforeReservation;
                result.reason = "completion resume child materialization exceeded proof budget";
                return result;
            }
            if (step.finished) {
                releaseBudgetBytes(reservedBudget, producedBytes);
                releaseBudgetBytes(reservedBudget, currentFrameBytes);
                continue;
            }
            for (auto child = step.frames.rbegin(); child != step.frames.rend(); ++child) {
                const std::uint64_t childBytes = symbolicFrameRecordBytes(*child);
                if (!chargeBudget(reservedBudget, limits, 0, childBytes)) {
                    budget = reservedBudget;
                    budget.bytes = retainedBytesBeforeReservation;
                    result.reason = "completion resume stack exceeded proof budget";
                    return result;
                }
                pending.push_back(std::move(*child));
            }
            releaseBudgetBytes(reservedBudget, producedBytes);
            releaseBudgetBytes(reservedBudget, currentFrameBytes);
        }

        // Commit the reservation only after the whole required segment has
        // been accounted.  The ProofTemplate itself is published below.
        budget = reservedBudget;
        budget.bytes = retainedBytesBeforeReservation;

        advancedTemplate.elapsedTurn = checkpoint.elapsedTurn;
        advancedTemplate.rngPosition = checkpoint.source.rngPosition;
        advancedTemplate.selectedCommand = checkpoint.selectedCommand;
        advancedTemplate.coveredDomain = checkpoint.rootDomain;
        if (nextCut.has_value()) {
            advancedTemplate.kind = RootProofKind::Completion;
            advancedTemplate.cut = *nextCut;
        } else {
            advancedTemplate.kind = RootProofKind::FullyDetailed;
            advancedTemplate.cut = checkpoint.cut;
        }
        result.accepted = true;
        return result;
    }

    CheckResult ProofKernel::refineLeaf(
        const PartitionFamily &current,
        const Box &base,
        int rngPosition,
        std::uint32_t localCellId,
        const Predicate &predicate,
        std::uint32_t maxLeaves,
        PartitionFamily &refined) {
        CheckResult result;
        if (!predicateIsValid(predicate) || maxLeaves < 2) {
            result.reason = "refinement predicate or leaf limit is invalid";
            return result;
        }
        if (current.partitionVersion == std::numeric_limits<std::uint64_t>::max()) {
            result.reason = "partition_version overflow";
            return result;
        }

        std::size_t treeIndex = current.trees.size();
        for (std::size_t index = 0; index < current.trees.size(); ++index) {
            if (current.trees[index].rngPosition == rngPosition) {
                if (treeIndex != current.trees.size()) {
                    result.reason = "refinement position occurs more than once in PartitionFamily";
                    return result;
                }
                treeIndex = index;
            }
        }
        if (treeIndex == current.trees.size()) {
            result.reason = "refinement position is missing from PartitionFamily";
            return result;
        }

        const PredicatePartition &oldTree = current.trees[treeIndex];
        const CheckedPartition checked = validatePartition(oldTree, base, maxLeaves);
        if (!checked.check.accepted) {
            result.reason = "refinement source partition is invalid: " + checked.check.reason;
            return result;
        }
        if (checked.leaves.size() >= maxLeaves) {
            result.reason = "refinement would exceed max_leaves";
            return result;
        }

        const Box *leafBox = leafDomain(checked, localCellId);
        if (leafBox == nullptr) {
            result.reason = "refinement local_cell_id is not a leaf at the requested position";
            return result;
        }
        auto [trueDomain, falseDomain] = split(*leafBox, predicate);
        if (trueDomain.empty() || falseDomain.empty()) {
            result.reason = "refinement does not split the requested leaf into two nonempty children";
            return result;
        }

        int leafNodeIndex = -1;
        std::uint32_t maximumId = 0;
        for (int index = 0; index < static_cast<int>(oldTree.nodes.size()); ++index) {
            const PartitionNode &node = oldTree.nodes[index];
            if (node.leaf) {
                maximumId = std::max(maximumId, node.localCellId);
                if (node.localCellId == localCellId) {
                    leafNodeIndex = index;
                }
            }
        }
        if (leafNodeIndex < 0 || maximumId > std::numeric_limits<std::uint32_t>::max() - 2u) {
            result.reason = leafNodeIndex < 0
                ? "refinement leaf node is missing"
                : "local_cell_id space exhausted";
            return result;
        }

        refined = current;
        refined.partitionVersion = current.partitionVersion + 1;
        PredicatePartition &newTree = refined.trees[treeIndex];
        const int trueChild = static_cast<int>(newTree.nodes.size());
        const int falseChild = trueChild + 1;
        PartitionNode branch;
        branch.leaf = false;
        branch.localCellId = 0;
        branch.predicate = predicate;
        branch.trueChild = trueChild;
        branch.falseChild = falseChild;
        newTree.nodes[leafNodeIndex] = branch;

        PartitionNode trueLeaf;
        trueLeaf.leaf = true;
        trueLeaf.localCellId = maximumId + 1u;
        PartitionNode falseLeaf;
        falseLeaf.leaf = true;
        falseLeaf.localCellId = maximumId + 2u;
        newTree.nodes.push_back(trueLeaf);
        newTree.nodes.push_back(falseLeaf);

        const CheckedPartition refinedCheck = validatePartition(newTree, base, maxLeaves);
        if (!refinedCheck.check.accepted || refinedCheck.leaves.size() != checked.leaves.size() + 1) {
            result.reason = refinedCheck.check.accepted
                ? "refinement did not increase the checked leaf count by one"
                : "refined partition is invalid: " + refinedCheck.check.reason;
            refined = {};
            return result;
        }
        result.accepted = true;
        return result;
    }

    CheckResult ProofKernel::bindCheckedCache(
        CheckedKernelCache &cache,
        const Problem &problem) {
        CheckResult result;
        if (!cache.bound) {
            cache.bound = true;
            cache.problemKey = problem;
            result.accepted = true;
            return result;
        }
        if (!sameProblemKey(cache.problemKey, problem)) {
            result.reason = "checked_cache belongs to a different problem_key";
            return result;
        }
        result.accepted = true;
        return result;
    }

    CheckResult ProofKernel::rememberCheckedTemplate(
        CheckedKernelCache &cache,
        const Problem &problem,
        const ProofTemplate &proofTemplate,
        const ProofBudget &limits,
        BudgetReport &budget,
        bool &changed) {
        changed = false;
        CheckResult result = bindCheckedCache(cache, problem);
        if (!result.accepted) {
            return result;
        }
        if (proofTemplate.elapsedTurn < 0 ||
            proofTemplate.rngPosition < problem.s0.position ||
            proofTemplate.coveredDomain.empty()) {
            result.accepted = false;
            result.reason = "checked_cache template has an invalid root identity/domain";
            return result;
        }
        auto rank = [](const ProofTemplate &value) {
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
            return -1;
        };
        const int newRank = rank(proofTemplate);
        if (newRank < 0) {
            result.accepted = false;
            result.reason = "checked_cache template has an invalid proof strategy";
            return result;
        }
        auto keyOf = [](const ProofTemplate &value) {
            return std::tuple{value.elapsedTurn, value.rngPosition, value.selectedCommand};
        };
        const auto key = keyOf(proofTemplate);
        std::size_t lo = 0;
        std::size_t hi = cache.templates.size();
        while (lo < hi) {
            const std::size_t mid = lo + (hi - lo) / 2;
            if (keyOf(cache.templates[mid]) < key) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (!chargeBudget(budget, limits, 1, 0)) {
            result.accepted = false;
            result.reason = "checked_cache lookup exceeded proof budget";
            return result;
        }

        std::size_t insertAt = lo;
        for (; insertAt < cache.templates.size() && keyOf(cache.templates[insertAt]) == key; ++insertAt) {
            if (!chargeBudget(budget, limits, 1, 0)) {
                result.accepted = false;
                result.reason = "checked_cache key scan exceeded proof budget";
                return result;
            }
            ProofTemplate &existing = cache.templates[insertAt];
            if (contains(existing.coveredDomain, proofTemplate.coveredDomain) &&
                rank(existing) >= newRank) {
                result.accepted = true;
                return result;
            }
            if (existing.coveredDomain == proofTemplate.coveredDomain && newRank > rank(existing)) {
                existing = proofTemplate;
                changed = true;
                result.accepted = true;
                return result;
            }
        }
        if (cache.templates.size() >= limits.maxModelTerms ||
            !chargeBudget(budget, limits, 1, sizeof(ProofTemplate))) {
            result.accepted = false;
            result.reason = "checked_cache exceeded proof budget";
            return result;
        }
        cache.templates.insert(cache.templates.begin() + static_cast<std::ptrdiff_t>(insertAt), proofTemplate);
        changed = true;
        result.accepted = true;
        return result;
    }

    CheckResult ProofKernel::rememberCheckedSnapshot(
        CheckedKernelCache &cache,
        const Problem &problem,
        const CheckedSnapshot &snapshot,
        const ProofBudget &limits,
        BudgetReport &budget) {
        CheckResult result = bindCheckedCache(cache, problem);
        if (!result.accepted) {
            return result;
        }
        if (!snapshot.check.accepted) {
            result.accepted = false;
            result.reason = "unchecked snapshot cannot be inserted into checked_cache";
            return result;
        }
        for (const CheckedRootRecord &record: snapshot.coverage) {
            // The registered TurnEntry COMPLETE is the generator's default
            // proof when no stronger cached template matches a required root.
            // Caching one identical default template per checked root only
            // duplicates information already reconstructible from the
            // immutable RuleBundle and prevents a repaired snapshot from being
            // rebuilt inside the shared byte budget.  Retain only proofs that
            // are stronger than that default; Support/coverage stay entirely
            // in the checked snapshot and are never pruned by this cache.
            if (record.proofKind == RootProofKind::Completion &&
                record.verifiedCut == CompletionCutId::TurnEntry) {
                continue;
            }
            ProofTemplate proofTemplate;
            proofTemplate.elapsedTurn = record.elapsedTurn;
            proofTemplate.rngPosition = record.source.rngPosition;
            proofTemplate.selectedCommand = record.selectedCommand;
            proofTemplate.coveredDomain = record.rootDomain;
            proofTemplate.kind = record.proofKind;
            proofTemplate.cut = record.verifiedCut;
            bool changed = false;
            result = rememberCheckedTemplate(
                cache,
                problem,
                proofTemplate,
                limits,
                budget,
                changed);
            if (!result.accepted) {
                return result;
            }
        }
        result.accepted = true;
        return result;
    }

    namespace {
        struct MaxPlusResult {
            bool accepted = false;
            std::string reason;
            std::vector<std::vector<MaxPlusValue>> values;
            MaxPlusValue rootBound;
            std::uint64_t chargedBytes = 0;
        };

        MaxPlusValue maxValue(const MaxPlusValue &a, const MaxPlusValue &b) noexcept {
            if (a.isNegativeInfinity()) {
                return b;
            }
            if (b.isNegativeInfinity()) {
                return a;
            }
            return MaxPlusValue::finiteValue(std::max(a.finite, b.finite));
        }

        MaxPlusResult computeMaxPlus(
            const RuleBundle &bundle,
            const CheckedSnapshot &snapshot,
            int horizon,
            int q,
            int u,
            int v,
            int w,
            const ProofBudget &limits,
            BudgetReport &budget) {
            MaxPlusResult result;
            if (q <= 0 || u < 0 || v < 0 || w < 0) {
                result.reason = "max-plus coefficients violate the nonnegative integer profile";
                return result;
            }
            if (!snapshot.check.accepted || snapshot.support.size() != static_cast<std::size_t>(horizon) + 1 ||
                snapshot.edgesByElapsedTurn.size() != static_cast<std::size_t>(horizon)) {
                result.reason = "checked snapshot has inconsistent layer dimensions";
                return result;
            }

            ScopedBudgetBytes retainedValues(budget);

            if (snapshot.partitions.trees.empty()) {
                result.reason = "max-plus snapshot has no hotel partitions";
                return result;
            }
            const int firstHotelPosition = snapshot.partitions.trees.front().rngPosition;
            auto hotelPartitionAt = [&](int position) -> const PredicatePartition * {
                if (position < firstHotelPosition) {
                    return nullptr;
                }
                const std::uint64_t rawIndex =
                    static_cast<std::uint64_t>(position - firstHotelPosition);
                if (rawIndex >= snapshot.partitions.trees.size()) {
                    return nullptr;
                }
                const PredicatePartition &partition =
                    snapshot.partitions.trees[static_cast<std::size_t>(rawIndex)];
                return partition.rngPosition == position ? &partition : nullptr;
            };

            result.values.resize(static_cast<std::size_t>(horizon) + 1);
            result.values[0].assign(
                snapshot.support[horizon].size(),
                MaxPlusValue::negativeInfinity());
            const std::uint64_t initialBytes =
                result.values[0].size() * sizeof(MaxPlusValue);
            if (!chargeBudget(
                    budget,
                    limits,
                    result.values[0].size(),
                    initialBytes)) {
                result.reason = "max-plus B[0] exceeded proof budget";
                return result;
            }
            retainedValues.add(initialBytes);
            result.chargedBytes += initialBytes;

            for (int remaining = 1; remaining <= horizon; ++remaining) {
                const int elapsedTurn = horizon - remaining;
                const std::vector<CellKey> &sources = snapshot.support[elapsedTurn];
                const std::vector<CellKey> &nextSources = snapshot.support[elapsedTurn + 1];
                const std::vector<MaxPlusValue> &previous = result.values[remaining - 1];
                if (previous.size() != nextSources.size()) {
                    result.reason = "max-plus previous layer does not match Support";
                    return result;
                }

                // COMPLETE targets are intervals of RNG positions containing
                // every leaf of Partition[p].  Build the per-position maximum
                // once for this layer, then answer each COMPLETE interval with
                // a range-maximum query.  Re-scanning every CellKey for every
                // completion edge is both unnecessary and contrary to the
                // bounded range-table construction required by section 15.
                struct PositionMaximum {
                    int rngPosition = 0;
                    MaxPlusValue value = MaxPlusValue::negativeInfinity();
                    bool complete = false;
                };
                ScopedBudgetBytes rangeTableBytes(budget);
                std::vector<PositionMaximum> positionMaximums;
                const std::uint64_t positionReservationBytes =
                    nextSources.size() * sizeof(PositionMaximum);
                if (positionReservationBytes != 0 &&
                    !chargeBudget(budget, limits, 0, positionReservationBytes)) {
                    result.reason = "max-plus per-position maximum table exceeded proof budget";
                    return result;
                }
                rangeTableBytes.add(positionReservationBytes);
                positionMaximums.reserve(nextSources.size());

                for (std::size_t begin = 0; begin < nextSources.size();) {
                    const int position = nextSources[begin].rngPosition;
                    std::size_t end = begin + 1;
                    while (end < nextSources.size() && nextSources[end].rngPosition == position) {
                        ++end;
                    }
                    const PredicatePartition *partition = hotelPartitionAt(position);
                    if (partition == nullptr) {
                        result.reason = "max-plus position maximum references a missing partition";
                        return result;
                    }
                    std::size_t leafCount = 0;
                    std::uint64_t validationWork = 0;
                    for (const PartitionNode &node: partition->nodes) {
                        ++validationWork;
                        if (node.leaf) {
                            ++leafCount;
                        }
                    }
                    MaxPlusValue positionBest = MaxPlusValue::negativeInfinity();
                    for (std::size_t index = begin; index < end; ++index) {
                        positionBest = maxValue(positionBest, previous[index]);
                        ++validationWork;
                    }
                    if (!chargeBudget(budget, limits, validationWork, 0)) {
                        result.reason = "max-plus per-position maximum construction exceeded proof budget";
                        return result;
                    }
                    positionMaximums.push_back({position, positionBest, leafCount == end - begin});
                    begin = end;
                }

                std::size_t rangeLevels = 0;
                for (std::size_t n = positionMaximums.size(); n != 0; n >>= 1) {
                    ++rangeLevels;
                }
                const std::size_t positionSpan = positionMaximums.empty()
                    ? 0
                    : static_cast<std::size_t>(
                        positionMaximums.back().rngPosition - positionMaximums.front().rngPosition + 1);
                const std::uint64_t sparseStorage =
                    rangeLevels * positionMaximums.size() * sizeof(MaxPlusValue) +
                    (positionMaximums.size() + 1) * sizeof(std::uint8_t) +
                    (positionMaximums.size() + 1) * sizeof(std::uint32_t) +
                    positionSpan * sizeof(std::size_t);
                if (sparseStorage != 0 &&
                    !chargeBudget(budget, limits, 0, sparseStorage)) {
                    result.reason = "max-plus sparse range-maximum table exceeded proof budget";
                    return result;
                }
                rangeTableBytes.add(sparseStorage);
                std::vector<MaxPlusValue> sparseRangeMaximums(
                    rangeLevels * positionMaximums.size(),
                    MaxPlusValue::negativeInfinity());
                std::vector<std::uint8_t> floorLog2(positionMaximums.size() + 1, 0);
                std::vector<std::uint32_t> incompletePrefix(positionMaximums.size() + 1, 0);
                std::vector<std::size_t> positionIndex(
                    positionSpan,
                    positionMaximums.size());
                std::uint64_t rangeBuildWork = 0;
                if (!positionMaximums.empty()) {
                    const std::size_t width = positionMaximums.size();
                    for (std::size_t index = 0; index < width; ++index) {
                        sparseRangeMaximums[index] = positionMaximums[index].value;
                        positionIndex[static_cast<std::size_t>(
                            positionMaximums[index].rngPosition - positionMaximums.front().rngPosition)] = index;
                        incompletePrefix[index + 1] =
                            incompletePrefix[index] + (positionMaximums[index].complete ? 0u : 1u);
                        ++rangeBuildWork;
                    }
                    for (std::size_t length = 2; length <= width; ++length) {
                        floorLog2[length] = static_cast<std::uint8_t>(floorLog2[length / 2] + 1);
                        ++rangeBuildWork;
                    }
                    for (std::size_t level = 1; level < rangeLevels; ++level) {
                        const std::size_t span = std::size_t{1} << level;
                        const std::size_t half = span >> 1;
                        for (std::size_t index = 0; index + span <= width; ++index) {
                            sparseRangeMaximums[level * width + index] = maxValue(
                                sparseRangeMaximums[(level - 1) * width + index],
                                sparseRangeMaximums[(level - 1) * width + index + half]);
                            ++rangeBuildWork;
                        }
                    }
                }
                if (rangeBuildWork != 0 && !chargeBudget(budget, limits, rangeBuildWork, 0)) {
                    result.reason = "max-plus sparse range-maximum construction exceeded proof budget";
                    return result;
                }

                auto completionRangeMaximum = [&](int firstPosition,
                                                  int lastPosition,
                                                  MaxPlusValue &maximum) -> bool {
                    maximum = MaxPlusValue::negativeInfinity();
                    if (firstPosition > lastPosition || positionMaximums.empty()) {
                        result.reason = "COMPLETE range has no checked per-position maximum";
                        return false;
                    }
                    const int basePosition = positionMaximums.front().rngPosition;
                    if (firstPosition < basePosition || lastPosition < basePosition ||
                        static_cast<std::uint64_t>(lastPosition - basePosition) >= positionIndex.size()) {
                        result.reason = "COMPLETE range is missing a checked RNG position in next-layer Support";
                        return false;
                    }
                    const std::size_t left = positionIndex[static_cast<std::size_t>(firstPosition - basePosition)];
                    const std::size_t right = positionIndex[static_cast<std::size_t>(lastPosition - basePosition)];
                    const std::size_t width = positionMaximums.size();
                    const std::size_t count = static_cast<std::size_t>(lastPosition - firstPosition + 1);
                    if (left >= width || right >= width || right < left || right - left + 1 != count) {
                        result.reason = "COMPLETE range is missing a checked RNG position in next-layer Support";
                        return false;
                    }
                    if (incompletePrefix[right + 1] != incompletePrefix[left]) {
                        result.reason = "max-plus COMPLETE support does not contain every partition leaf in queried range";
                        return false;
                    }
                    const std::size_t level = floorLog2[count];
                    const std::size_t span = std::size_t{1} << level;
                    maximum = maxValue(
                        sparseRangeMaximums[level * width + left],
                        sparseRangeMaximums[level * width + right - span + 1]);
                    return true;
                };

                std::vector<MaxPlusValue> current(
                    sources.size(),
                    MaxPlusValue::negativeInfinity());
                for (std::size_t sourceIndex = 0; sourceIndex < sources.size(); ++sourceIndex) {
                    const CellKey &source = sources[sourceIndex];
                    MaxPlusValue best = MaxPlusValue::negativeInfinity();
                    const std::vector<CheckedEdge> &layerEdges =
                        snapshot.edgesByElapsedTurn[elapsedTurn];
                    std::size_t edgeLo = 0;
                    std::size_t edgeHi = layerEdges.size();
                    while (edgeLo < edgeHi) {
                        const std::size_t mid = edgeLo + (edgeHi - edgeLo) / 2;
                        if (layerEdges[mid].source < source) {
                            edgeLo = mid + 1;
                        } else {
                            edgeHi = mid;
                        }
                    }

                    struct CompletionAggregate {
                        int selectedCommand = 0;
                        bool mayReachGoal = false;
                        bool hasContinuingOutput = false;
                        int firstOutputPosition = 0;
                        int lastOutputPosition = -1;
                        CompletionWeightTerm weight;
                        bool hasWeight = false;
                    };
                    ScopedBudgetBytes aggregateBytes(budget);
                    std::vector<CompletionAggregate> completionAggregates;
                    std::uint64_t aggregateWork = 0;
                    for (std::size_t edgeIndex = edgeLo;
                         edgeIndex < layerEdges.size() && layerEdges[edgeIndex].source == source;
                         ++edgeIndex) {
                        const CheckedEdge &edge = layerEdges[edgeIndex];
                        if (edge.kind != CheckedEdgeKind::Completion ||
                            !hasSuccessDpDestination(edge)) {
                            continue;
                        }
                        std::size_t aggregateIndex = 0;
                        while (aggregateIndex < completionAggregates.size() &&
                               completionAggregates[aggregateIndex].selectedCommand != edge.selectedCommand) {
                            ++aggregateIndex;
                            ++aggregateWork;
                        }
                        if (aggregateIndex == completionAggregates.size()) {
                            if (!chargeBudget(budget, limits, 0, sizeof(CompletionAggregate))) {
                                result.reason = "max-plus completion aggregate storage exceeded proof budget";
                                return result;
                            }
                            aggregateBytes.add(sizeof(CompletionAggregate));
                            CompletionAggregate aggregate;
                            aggregate.selectedCommand = edge.selectedCommand;
                            completionAggregates.push_back(aggregate);
                        }
                        CompletionAggregate &aggregate = completionAggregates[aggregateIndex];
                        aggregate.mayReachGoal = aggregate.mayReachGoal || edge.mayReachGoal;
                        if (edge.hasContinuingOutput) {
                            if (!aggregate.hasContinuingOutput) {
                                aggregate.firstOutputPosition = edge.firstOutputPosition;
                                aggregate.lastOutputPosition = edge.lastOutputPosition;
                                aggregate.hasContinuingOutput = true;
                            } else {
                                aggregate.firstOutputPosition = std::min(
                                    aggregate.firstOutputPosition,
                                    edge.firstOutputPosition);
                                aggregate.lastOutputPosition = std::max(
                                    aggregate.lastOutputPosition,
                                    edge.lastOutputPosition);
                            }
                        }
                        const std::vector<CompletionWeightTerm> *terms =
                            checkedEdgeWeightTerms(bundle, edge);
                        if (terms == nullptr) {
                            result.reason = "max-plus completion edge has invalid registered weight terms";
                            return result;
                        }
                        for (const CompletionWeightTerm &term: *terms) {
                            includeDominatingCompletionTerm(aggregate.weight, aggregate.hasWeight, term);
                            ++aggregateWork;
                        }
                    }
                    if (aggregateWork != 0 && !chargeBudget(budget, limits, aggregateWork, 0)) {
                        result.reason = "max-plus completion aggregation exceeded proof budget";
                        return result;
                    }

                    for (std::size_t edgeIndex = edgeLo;
                         edgeIndex < layerEdges.size() && layerEdges[edgeIndex].source == source;
                         ++edgeIndex) {
                        const CheckedEdge &edge = layerEdges[edgeIndex];

                        if (edge.kind == CheckedEdgeKind::Completion) {
                            continue;
                        }

                        if (hasSuccessDpDestination(edge)) {
                            bool duplicateDpTerm = false;
                            std::uint64_t dedupWork = 0;
                            for (std::size_t previousIndex = edgeLo;
                                 previousIndex < edgeIndex;
                                 ++previousIndex) {
                                const CheckedEdge &previous = layerEdges[previousIndex];
                                if (!hasSuccessDpDestination(previous)) {
                                    continue;
                                }
                                ++dedupWork;
                                if (sameSuccessDpTerm(previous, edge)) {
                                    duplicateDpTerm = true;
                                    break;
                                }
                            }
                            if (dedupWork != 0 && !chargeBudget(budget, limits, dedupWork, 0)) {
                                result.reason = "max-plus DP-term deduplication exceeded proof budget";
                                return result;
                            }
                            if (duplicateDpTerm) {
                                continue;
                            }
                        }

                        MaxPlusValue continuation = edge.mayReachGoal
                            ? MaxPlusValue::finiteValue(0)
                            : MaxPlusValue::negativeInfinity();
                        if (edge.hasContinuingOutput) {
                            bool sawTarget = false;
                            if (edge.kind == CheckedEdgeKind::Completion) {
                                if (!edge.targets.empty() ||
                                    edge.firstOutputPosition > edge.lastOutputPosition) {
                                    result.reason = "COMPLETE edge has an invalid virtual target representation";
                                    return result;
                                }
                                MaxPlusValue rangeMaximum;
                                if (!completionRangeMaximum(
                                        edge.firstOutputPosition,
                                        edge.lastOutputPosition,
                                        rangeMaximum)) {
                                    return result;
                                }
                                continuation = maxValue(continuation, rangeMaximum);
                                sawTarget = true;
                            } else {
                                if (edge.targets.empty()) {
                                    result.reason = "detailed continuing edge has no checked targets";
                                    return result;
                                }
                                for (const CellKey &target: edge.targets) {
                                    std::size_t lo = 0;
                                    std::size_t hi = nextSources.size();
                                    while (lo < hi) {
                                        const std::size_t mid = lo + (hi - lo) / 2;
                                        if (nextSources[mid] < target) {
                                            lo = mid + 1;
                                        } else {
                                            hi = mid;
                                        }
                                    }
                                    if (lo >= nextSources.size() || !(nextSources[lo] == target)) {
                                        result.reason = "checked detailed edge target is missing from next-layer Support";
                                        return result;
                                    }
                                    continuation = maxValue(continuation, previous[lo]);
                                    sawTarget = true;
                                    if (!chargeBudget(budget, limits, 1, 0)) {
                                        result.reason = "max-plus target scan exceeded proof budget";
                                        return result;
                                    }
                                }
                            }
                            if (!sawTarget) {
                                result.reason = "continuing edge has no checked or virtual target";
                                return result;
                            }
                        }

                        if (continuation.isNegativeInfinity()) {
                            continue;
                        }

                        std::int64_t weight = 0;
                        if (!completionWeight(edge, q, u, v, w, weight)) {
                            result.reason = "max-plus completion weight overflow or missing term";
                            return result;
                        }

                        std::int64_t candidate = 0;
                        if (!checkedAdd(weight, continuation.finite, candidate)) {
                            result.reason = "max-plus addition overflow";
                            return result;
                        }
                        best = maxValue(best, MaxPlusValue::finiteValue(candidate));

                        if (!chargeBudget(budget, limits, 1, 0)) {
                            result.reason = "max-plus edge evaluation exceeded proof budget";
                            return result;
                        }
                    }

                    for (const CompletionAggregate &aggregate: completionAggregates) {
                        if (!aggregate.hasWeight) {
                            result.reason = "max-plus completion aggregate has no checked weight";
                            return result;
                        }
                        MaxPlusValue continuation = aggregate.mayReachGoal
                            ? MaxPlusValue::finiteValue(0)
                            : MaxPlusValue::negativeInfinity();
                        if (aggregate.hasContinuingOutput) {
                            MaxPlusValue rangeMaximum;
                            if (!completionRangeMaximum(
                                    aggregate.firstOutputPosition,
                                    aggregate.lastOutputPosition,
                                    rangeMaximum)) {
                                return result;
                            }
                            continuation = maxValue(continuation, rangeMaximum);
                        }
                        if (continuation.isNegativeInfinity()) {
                            continue;
                        }
                        std::int64_t weight = 0;
                        if (!completionTermWeight(aggregate.weight, q, u, v, w, weight)) {
                            result.reason = "max-plus aggregated completion weight overflow";
                            return result;
                        }
                        std::int64_t candidate = 0;
                        if (!checkedAdd(weight, continuation.finite, candidate)) {
                            result.reason = "max-plus aggregated completion addition overflow";
                            return result;
                        }
                        best = maxValue(best, MaxPlusValue::finiteValue(candidate));
                        if (!chargeBudget(budget, limits, 1, 0)) {
                            result.reason = "max-plus aggregated completion evaluation exceeded proof budget";
                            return result;
                        }
                    }

                    current[sourceIndex] = best;
                }

                if (!chargeBudget(
                        budget,
                        limits,
                        current.size(),
                        current.size() * sizeof(MaxPlusValue))) {
                    result.reason = "max-plus layer exceeded proof budget";
                    return result;
                }
                retainedValues.add(current.size() * sizeof(MaxPlusValue));
                result.chargedBytes += current.size() * sizeof(MaxPlusValue);
                result.values[remaining] = std::move(current);
            }

            const auto rootIt = std::lower_bound(
                snapshot.support[0].begin(),
                snapshot.support[0].end(),
                snapshot.root);
            if (rootIt == snapshot.support[0].end() || !(*rootIt == snapshot.root)) {
                result.reason = "root CellKey is missing from Support[0]";
                return result;
            }
            const std::size_t rootIndex = static_cast<std::size_t>(rootIt - snapshot.support[0].begin());
            if (rootIndex >= result.values[horizon].size()) {
                result.reason = "root max-plus index is outside B[H]";
                return result;
            }

            result.rootBound = result.values[horizon][rootIndex];
            result.accepted = true;
            retainedValues.disarm();
            return result;
        }

        bool falseInequality(
            const Problem &problem,
            const MaxPlusValue &rootBound,
            int q,
            int u,
            int v,
            int w,
            std::int64_t &delta,
            std::string &error) {
            if (rootBound.isNegativeInfinity()) {
                // This is the separate "no abstract success path" sufficient
                // condition.  Do not encode -infinity as an integer or feed it
                // through ordinary arithmetic.
                delta = 0;
                return true;
            }

            std::int64_t left = 0;
            if (!checkedMultiply(q, problem.s0.players[1].hp, left)) {
                error = "Q*E0 overflow";
                return false;
            }

            std::int64_t right = rootBound.finite;
            const std::pair<int, int> potentialTerms[] = {
                {u, problem.s0.players[0].hp},
                {v, problem.s0.players[0].mp},
                {w, problem.s0.players[0].medicinal_herbs_count},
            };
            for (const auto &[coefficient, resource]: potentialTerms) {
                std::int64_t product = 0;
                std::int64_t sum = 0;
                if (!checkedMultiply(coefficient, resource, product) || !checkedAdd(right, product, sum)) {
                    error = "B_H(q0)+Phi(S0) overflow";
                    return false;
                }
                right = sum;
            }

            if (right == std::numeric_limits<std::int64_t>::min()) {
                error = "false-proof delta overflow";
                return false;
            }
            if (!checkedAdd(left, -right, delta)) {
                error = "false-proof delta overflow";
                return false;
            }
            return delta >= 1;
        }
    } // namespace

    FalseCheckResult ProofKernel::tryFalseZeroPrice(
        const RuleBundle &bundle,
        const Problem &problem,
        int horizon,
        const PartitionFamily &partitions,
        const ProofBudget &limits,
        BudgetReport &budget,
        const std::vector<ProofTemplate> *generationTemplates,
        std::uint64_t coverageVersion) {
        FalseCheckResult result;
        CheckedSnapshot snapshot = rebuildSupport(
            bundle,
            problem,
            horizon,
            partitions,
            limits,
            budget,
            nullptr,
            generationTemplates,
            coverageVersion);
        if (!snapshot.check.accepted) {
            result.check.reason = snapshot.check.reason;
            return result;
        }
        return tryFalseZeroPriceOnSnapshot(
            bundle,
            problem,
            horizon,
            std::move(snapshot),
            limits,
            budget);
    }

    FalseCheckResult ProofKernel::tryFalseZeroPriceOnSnapshot(
        const RuleBundle &bundle,
        const Problem &problem,
        int horizon,
        CheckedSnapshot snapshot,
        const ProofBudget &limits,
        BudgetReport &budget,
        bool structuralNoPathOnly) {
        FalseCheckResult result;
        if (!snapshot.check.accepted || snapshot.partitions.partitionVersion == 0 ||
            snapshot.coverageVersion == 0) {
            result.check.reason = "zero-price check requires a checked snapshot with live versions";
            return result;
        }

        // The zero-price bound is an optional improvement for this snapshot.
        // Exhausting its own evaluation cap must not destroy a perfectly valid
        // checked model or prevent the witness iterator from continuing.
        if (!structuralNoPathOnly && budget.priceEvaluations >= limits.maxPriceEvaluations) {
            result.check.accepted = true;
            result.snapshot = std::move(snapshot);
            return result;
        }
        if (!structuralNoPathOnly) {
            ++budget.priceEvaluations;
        }

        constexpr int q = 256;
        constexpr int u = 0;
        constexpr int v = 0;
        constexpr int w = 0;
        MaxPlusResult maxPlus = computeMaxPlus(bundle, snapshot, horizon, q, u, v, w, limits, budget);
        if (!maxPlus.accepted) {
            result.check.reason = maxPlus.reason;
            return result;
        }
        std::int64_t delta = 0;
        bool candidateFalse = maxPlus.rootBound.isNegativeInfinity();
        if (!structuralNoPathOnly && !candidateFalse) {
            std::string inequalityError;
            candidateFalse = falseInequality(
                problem,
                maxPlus.rootBound,
                q,
                u,
                v,
                w,
                delta,
                inequalityError);
            if (!inequalityError.empty()) {
                result.check.reason = inequalityError;
                return result;
            }
        }

        result.check.accepted = true;
        if (!candidateFalse) {
            releaseBudgetBytes(budget, maxPlus.chargedBytes);
            result.snapshot = std::move(snapshot);
            return result;
        }

        std::string proofMaterializationError;
        if (!materializeDefaultProofRecords(
                bundle,
                snapshot,
                limits,
                budget,
                proofMaterializationError)) {
            releaseBudgetBytes(budget, maxPlus.chargedBytes);
            releaseBudgetBytes(budget, snapshotAccountedBytes(snapshot));
            result.check.accepted = false;
            result.check.reason = proofMaterializationError;
            return result;
        }

        FalseCertificate certificate;
        certificate.problem = problem;
        certificate.horizon = horizon;
        certificate.partitions = std::move(snapshot.partitions);
        certificate.coverageVersion = snapshot.coverageVersion;
        certificate.proofs = std::move(snapshot.proofs);
        certificate.coverage = std::move(snapshot.coverage);
        certificate.q = q;
        certificate.u = u;
        certificate.v = v;
        certificate.w = w;
        certificate.bByRemainingTurns = std::move(maxPlus.values);
        certificate.rootBound = maxPlus.rootBound;
        certificate.delta = delta;

        // The certificate takes ownership of partitions/proofs/coverage and B.
        // Edges, Support and completion checkpoints are generation-only and
        // are released before the independent verifier builds its own copy.
        releaseBudgetBytes(budget, snapshotAccountedBytes(snapshot));

        const CheckResult independent = verifyFalseCertificate(bundle, certificate, limits, budget);
        if (!independent.accepted) {
            result.check.accepted = false;
            result.check.reason = "independent FALSE verification failed: " + independent.reason;
            return result;
        }

        result.provedFalse = true;
        result.certificate = std::move(certificate);
        return result;
    }

    CheckResult ProofKernel::checkTrivialFalse(
        const RuleBundle &bundle,
        const Problem &problem,
        int horizon,
        bool &provedFalse) {
        CheckResult result;
        provedFalse = false;

        const std::string inputError = ExactReplay::validateProblem(bundle, problem, horizon);
        if (!inputError.empty()) {
            result.reason = inputError;
            return result;
        }

        if (problem.s0.players[1].hp == 0) {
            result.accepted = true;
            return result;
        }
        if (problem.s0.players[0].hp == 0 || horizon == 0) {
            provedFalse = true;
        }

        result.accepted = true;
        return result;
    }

    CheckResult ProofKernel::verifyFalseCertificate(
        const RuleBundle &bundle,
        const FalseCertificate &certificate,
        const ProofBudget &limits,
        BudgetReport &budget) {
        CheckResult result;
        if (!bundle.registered || certificate.problem.ruleId != bundle.id) {
            result.reason = "certificate rule_id is not the registered bundle";
            return result;
        }
        if (certificate.horizon < 0 || certificate.q <= 0 ||
            certificate.u < 0 || certificate.v < 0 || certificate.w < 0) {
            result.reason = "certificate has invalid horizon or coefficients";
            return result;
        }

        CheckedSnapshot snapshot = rebuildSupport(
            bundle,
            certificate.problem,
            certificate.horizon,
            certificate.partitions,
            limits,
            budget,
            &certificate.proofs,
            nullptr,
            certificate.coverageVersion);
        if (!snapshot.check.accepted) {
            result.reason = snapshot.check.reason;
            return result;
        }
        ScopedBudgetBytes verifierSnapshotBytes(budget);
        verifierSnapshotBytes.add(snapshotAccountedBytes(snapshot));
        if (snapshot.coverageVersion != certificate.coverageVersion ||
            snapshot.proofs != certificate.proofs ||
            snapshot.coverage != certificate.coverage) {
            result.reason = "certificate coverage does not match kernel-rebuilt required roots";
            return result;
        }

        MaxPlusResult maxPlus = computeMaxPlus(
            bundle,
            snapshot,
            certificate.horizon,
            certificate.q,
            certificate.u,
            certificate.v,
            certificate.w,
            limits,
            budget);
        if (!maxPlus.accepted) {
            result.reason = maxPlus.reason;
            return result;
        }
        ScopedBudgetBytes verifierMaxPlusBytes(budget);
        verifierMaxPlusBytes.add(maxPlus.chargedBytes);
        if (maxPlus.values != certificate.bByRemainingTurns || maxPlus.rootBound != certificate.rootBound) {
            result.reason = "certificate B table or root bound differs from independent recomputation";
            return result;
        }

        std::int64_t delta = 0;
        std::string inequalityError;
        const bool provedFalse = falseInequality(
            certificate.problem,
            maxPlus.rootBound,
            certificate.q,
            certificate.u,
            certificate.v,
            certificate.w,
            delta,
            inequalityError);
        if (!inequalityError.empty()) {
            result.reason = inequalityError;
            return result;
        }
        if (!provedFalse || delta != certificate.delta) {
            result.reason = "certificate does not satisfy the checked max-plus FALSE inequality";
            return result;
        }

        result.accepted = true;
        return result;
    }

    CheckResult ProofKernel::selfCheck(const RuleBundle &bundle) {
        CheckResult result;
        if (!bundle.registered) {
            result.reason = "bundle is not registered";
            return result;
        }

        auto expectRegistrationReject = [&](RuleBundle candidate, const char *label) -> bool {
            RuleRegistry registry;
            std::string error;
            if (registry.registerNew(std::move(candidate), error)) {
                result.reason = std::string("registration self-check accepted ") + label;
                return false;
            }
            return true;
        };

        {
            RuleRegistry registry;
            RuleBundle candidate = makeYo2BeBundleCandidate();
            candidate.bounds.rMax = 1;
            std::string error;
            if (!registry.registerNew(candidate, error)) {
                result.reason = "registration self-check rejected harmless submitted bounds: " + error;
                return result;
            }
            const RuleBundle *registered = registry.lookup(candidate.id);
            if (registered == nullptr || registered->bounds.rMax != bundle.bounds.rMax ||
                registered->bounds.rMax == 1) {
                result.reason = "handwritten Rmax bypassed kernel recomputation";
                return result;
            }
            if (registry.registerNew(makeYo2BeBundleCandidate(), error)) {
                result.reason = "registry allowed overwrite of an existing rule_id";
                return result;
            }
        }

        {
            RuleBundle changedProfile = makeYo2BeBundleCandidate();
            --changedProfile.profile.commandProfiles.front().turnEntryCompletionTerms.front().enemyDamageUpper;
            if (!expectRegistrationReject(std::move(changedProfile), "same-version completion bound mutation")) {
                return result;
            }

            RuleBundle changedNative = makeYo2BeBundleCandidate();
            changedNative.nativeContracts.front().maxRngReads = 1;
            if (!expectRegistrationReject(std::move(changedNative), "same-version native RNG-bound mutation")) {
                return result;
            }

            RuleBundle changedProgram = makeYo2BeBundleCandidate();
            changedProgram.program.routines.front().instructions.front().label += "-mutated";
            if (!expectRegistrationReject(std::move(changedProgram), "same-version RuleProgram mutation")) {
                return result;
            }

            RuleBundle changedEffects = makeYo2BeBundleCandidate();
            bool changedDeclaredEffects = false;
            for (Routine &routine: changedEffects.program.routines) {
                if (!routine.declaredEffects.stateReads.empty()) {
                    routine.declaredEffects.stateReads.pop_back();
                    changedDeclaredEffects = true;
                    break;
                }
            }
            if (!changedDeclaredEffects || !expectRegistrationReject(
                    std::move(changedEffects),
                    "same-version declared routine effects mutation")) {
                if (!changedDeclaredEffects) {
                    result.reason = "registration self-check could not locate a declared state read";
                }
                return result;
            }

            RuleBundle changedCallBinding = makeYo2BeBundleCandidate();
            bool changedTypedCall = false;
            for (Routine &routine: changedCallBinding.program.routines) {
                for (Instruction &instruction: routine.instructions) {
                    if (instruction.opcode == Opcode::Call && !instruction.call.arguments.empty()) {
                        instruction.call.arguments.clear();
                        changedTypedCall = true;
                        break;
                    }
                }
                if (changedTypedCall) {
                    break;
                }
            }
            if (!changedTypedCall || !expectRegistrationReject(
                    std::move(changedCallBinding),
                    "same-version typed CALL argument mutation")) {
                if (!changedTypedCall) {
                    result.reason = "registration self-check could not locate a CALL with typed arguments";
                }
                return result;
            }

            RuleBundle changedResultType = makeYo2BeBundleCandidate();
            bool changedTypedResult = false;
            for (Routine &routine: changedResultType.program.routines) {
                if (routine.result.present) {
                    routine.result.type = ScalarType::Boolean;
                    changedTypedResult = true;
                    break;
                }
            }
            if (!changedTypedResult || !expectRegistrationReject(
                    std::move(changedResultType),
                    "same-version typed routine result mutation")) {
                if (!changedTypedResult) {
                    result.reason = "registration self-check could not locate a typed routine result";
                }
                return result;
            }

            RuleBundle changedFleeLegality = makeYo2BeBundleCandidate();
            bool foundFleeProfile = false;
            for (CommandProfile &profile: changedFleeLegality.profile.commandProfiles) {
                if (profile.command == BattleEmulator::FLEE_ALLY) {
                    profile.selectableParalysisMask = kParalysisMaskAll;
                    foundFleeProfile = true;
                    break;
                }
            }
            if (!foundFleeProfile || !expectRegistrationReject(
                    std::move(changedFleeLegality),
                    "same-version FLEE paralysis-legality mutation")) {
                if (!foundFleeProfile) {
                    result.reason = "registration self-check could not locate FLEE command profile";
                }
                return result;
            }

            RuleBundle changedFleeGate = makeYo2BeBundleCandidate();
            bool foundFleeGate = false;
            for (Routine &routine: changedFleeGate.program.routines) {
                if (routine.id != "ally-slot") {
                    continue;
                }
                for (Instruction &instruction: routine.instructions) {
                    if (instruction.opcode == Opcode::Branch &&
                        instruction.label.find("FLEE_ALLY") != std::string::npos &&
                        !instruction.branchCondition.any.empty() &&
                        !instruction.branchCondition.any.front().all.empty()) {
                        instruction.branchCondition.any.front().all.pop_back();
                        foundFleeGate = true;
                        break;
                    }
                }
            }
            if (!foundFleeGate || !expectRegistrationReject(
                    std::move(changedFleeGate),
                    "same-version FLEE execution-gate mutation")) {
                if (!foundFleeGate) {
                    result.reason = "registration self-check could not locate FLEE execution gate";
                }
                return result;
            }

            RuleBundle changedFleeRuleDeclaration = makeYo2BeBundleCandidate();
            changedFleeRuleDeclaration.program.explicitRuleChanges.clear();
            if (!expectRegistrationReject(
                    std::move(changedFleeRuleDeclaration),
                    "same-version FLEE explicit-rule declaration mutation")) {
                return result;
            }

            RuleBundle cyclic = makeYo2BeBundleCandidate();
            cyclic.id = {"yo2_be.registration-cycle-test", 1};
            cyclic.program.routines.front().instructions.front().successors = {0};
            if (!expectRegistrationReject(std::move(cyclic), "cyclic routine CFG")) {
                return result;
            }

            RuleBundle missingCall = makeYo2BeBundleCandidate();
            missingCall.id = {"yo2_be.registration-call-test", 1};
            bool changed = false;
            for (Routine &routine: missingCall.program.routines) {
                for (Instruction &instruction: routine.instructions) {
                    if (instruction.opcode == Opcode::Call) {
                        instruction.callTarget = "missing-routine";
                        changed = true;
                        break;
                    }
                }
                if (changed) {
                    break;
                }
            }
            if (!changed || !expectRegistrationReject(std::move(missingCall), "unresolved CALL target")) {
                if (!changed) {
                    result.reason = "registration self-check could not locate a CALL instruction";
                }
                return result;
            }

            RuleBundle missingNativeBound = makeYo2BeBundleCandidate();
            missingNativeBound.id = {"yo2_be.registration-native-test", 1};
            missingNativeBound.nativeContracts.front().maxRngReads = -1;
            if (!expectRegistrationReject(std::move(missingNativeBound), "native without finite RNG bound")) {
                return result;
            }
        }

        RawState initialState;
        initialState.players[0] = BasePlayers[0];
        initialState.players[1] = BasePlayers[1];

        {
            RawState paralyzed = initialState;
            paralyzed.players[0].paralysis = true;
            paralyzed.players[0].paralysisTurns = 1;
            if (ExactReplay::isSelectable(bundle, paralyzed, BattleEmulator::FLEE_ALLY)) {
                result.reason = "FLEE self-check allowed selection while paralyzed";
                return result;
            }

            RawState sleeping = initialState;
            sleeping.players[0].sleeping = true;
            if (ExactReplay::isSelectable(bundle, sleeping, BattleEmulator::FLEE_ALLY)) {
                result.reason = "FLEE self-check allowed selection while sleeping";
                return result;
            }

            const CommandProfile *fleeProfile = nullptr;
            for (const CommandProfile &profile: bundle.profile.commandProfiles) {
                if (profile.command == BattleEmulator::FLEE_ALLY) {
                    fleeProfile = &profile;
                    break;
                }
            }
            if (fleeProfile == nullptr || fleeProfile->selectableParalysisMask != 0x0001) {
                result.reason = "registered FLEE profile does not require clear paralysis at selection";
                return result;
            }

            Problem inactiveProblem;
            inactiveProblem.ruleId = bundle.id;
            inactiveProblem.seed = 0x13;
            inactiveProblem.s0 = initialState;
            inactiveProblem.s0.position = 1;
            inactiveProblem.s0.nowState = 0;
            inactiveProblem.s0.players[0].inactive = true;
            inactiveProblem.startTurn = 0;
            const ReplayResult fleeReplay = ExactReplay::replay(
                bundle, inactiveProblem, {BattleEmulator::FLEE_ALLY}, true);
            const ReplayResult statusReference = ExactReplay::replay(
                bundle, inactiveProblem, {BattleEmulator::ATTACK_ALLY}, true);
            if (!fleeReplay.supported || !fleeReplay.valid ||
                !statusReference.supported || !statusReference.valid ||
                !sameRawState(fleeReplay.finalState, statusReference.finalState)) {
                result.reason = "FLEE execution adapter does not preserve the normal inactive-status transition";
                return result;
            }
        }

        const Box base = baseBox(bundle, initialState);
        if (base.empty()) {
            result.reason = "base box is empty";
            return result;
        }

        std::string alphaError;
        const auto singleton = alphaSingleton(initialState, alphaError);
        if (!singleton || !contains(base, *singleton)) {
            result.reason = alphaError.empty() ? "alpha(S0) outside BaseBox" : alphaError;
            return result;
        }

        PredicatePartition testPartition;
        testPartition.rngPosition = 1;
        testPartition.root = 0;
        testPartition.nodes = {
            {false, 0, {PredicateKind::ResourceLe, ResourceAxis::EnemyHp, ModeAxis::Charge, 227, 0}, 1, 2},
            {true, 7, {}},
            {true, 8, {}},
        };

        const CheckedPartition checked = validatePartition(testPartition, base, 4);
        if (!checked.check.accepted) {
            result.reason = "partition self-check failed: " + checked.check.reason;
            return result;
        }

        Problem proofProblem;
        proofProblem.ruleId = bundle.id;
        proofProblem.seed = 0x13;
        proofProblem.s0 = initialState;
        proofProblem.s0.position = 1;
        proofProblem.s0.nowState = 0;
        proofProblem.startTurn = 0;

        Problem constrainedProblem = proofProblem;
        constrainedProblem.constraints.actions.push_back({0, {bundle.profile.heroCommands.front()}});
        if (sameProblemKey(proofProblem, constrainedProblem)) {
            result.reason = "problem_key ignores action constraints";
            return result;
        }

        CheckedKernelCache cache;
        if (!bindCheckedCache(cache, proofProblem).accepted) {
            result.reason = "checked_cache could not bind its initial problem_key";
            return result;
        }
        Problem otherProblem = proofProblem;
        ++otherProblem.seed;
        if (bindCheckedCache(cache, otherProblem).accepted) {
            result.reason = "checked_cache accepted a different problem_key";
            return result;
        }

        ProofBudget proofBudget;
        BudgetReport generationBudget;
        std::string familyError;
        PartitionFamily proofFamily = makeInitialFamily(
            bundle,
            proofProblem.s0,
            1,
            proofBudget.maxLeavesPerPosition,
            familyError);
        if (!familyError.empty()) {
            result.reason = "proof-record self-check family failed: " + familyError;
            return result;
        }

        std::string twoTurnFamilyError;
        const PartitionFamily twoTurnFamily = makeInitialFamily(
            bundle,
            proofProblem.s0,
            2,
            proofBudget.maxLeavesPerPosition,
            twoTurnFamilyError);
        if (!twoTurnFamilyError.empty()) {
            result.reason = "two-turn partition family self-check failed: " + twoTurnFamilyError;
            return result;
        }
        PartitionFamily refinedFamily;
        Predicate splitPredicate;
        splitPredicate.kind = PredicateKind::ResourceLe;
        splitPredicate.resource = ResourceAxis::EnemyHp;
        splitPredicate.threshold = 227;
        const CheckResult refinedCheck = refineLeaf(
            twoTurnFamily,
            base,
            100,
            0,
            splitPredicate,
            proofBudget.maxLeavesPerPosition,
            refinedFamily);
        if (!refinedCheck.accepted || refinedFamily.partitionVersion == twoTurnFamily.partitionVersion) {
            result.reason = "per-position partition refinement self-check failed";
            return result;
        }
        for (std::size_t index = 0; index < twoTurnFamily.trees.size(); ++index) {
            if (twoTurnFamily.trees[index].rngPosition != 100 &&
                twoTurnFamily.trees[index] != refinedFamily.trees[index]) {
                result.reason = "refining Partition[100] changed another RNG-position partition";
                return result;
            }
        }
        if (validateFamily(
                refinedFamily,
                base,
                proofProblem.s0.position,
                proofProblem.s0.position + 2 * bundle.bounds.rMax,
                1).accepted) {
            result.reason = "partition validator accepted a position with more than K leaves";
            return result;
        }

        {
            BudgetReport reservationBuildBudget;
            CheckedSnapshot reservationSnapshot = rebuildSupport(
                bundle,
                proofProblem,
                1,
                proofFamily,
                proofBudget,
                reservationBuildBudget,
                nullptr,
                nullptr,
                1);
            if (!reservationSnapshot.check.accepted || reservationSnapshot.coverage.empty()) {
                result.reason = "completion-reservation self-check could not build a checked root";
                return result;
            }
            const CheckedRootRecord &reservationRoot = reservationSnapshot.coverage.front();
            SymbolicStepper reservationStepper(bundle, proofProblem);
            const CompletionCheckpoint checkpoint{
                reservationRoot.elapsedTurn,
                reservationRoot.source,
                reservationRoot.selectedCommand,
                CompletionCutId::TurnEntry,
                reservationRoot.rootDomain,
                reservationStepper.makeRootFrame(
                    reservationRoot.elapsedTurn,
                    reservationRoot.selectedCommand,
                    reservationRoot.source.rngPosition,
                    reservationRoot.rootDomain),
            };

            BudgetReport normalResumeBudget;
            ProofTemplate advanced;
            const CheckResult normalAdvance = advanceCompletionCheckpoint(
                bundle,
                proofProblem,
                checkpoint,
                proofBudget,
                normalResumeBudget,
                advanced);
            if (!normalAdvance.accepted ||
                (advanced.kind == RootProofKind::Completion && advanced.cut == checkpoint.cut)) {
                result.reason = "completion-reservation self-check did not advance with ordinary budget";
                return result;
            }

            if (advanced.kind != RootProofKind::Completion ||
                advanced.cut == CompletionCutId::TurnEntry) {
                result.reason = "completion-reservation self-check did not reach a registered partial COMPLETE cut";
                return result;
            }

            {
                const std::vector<ProofTemplate> advancedTemplates{advanced};
                ProofBudget partialProofBudget = proofBudget;
                partialProofBudget.maxDetailedTermsPerAction = 64;
                partialProofBudget.maxCompletionTermsPerAction = 64;
                BudgetReport partialBudget;
                FalseCheckResult partialFalse = tryFalseZeroPrice(
                    bundle,
                    proofProblem,
                    1,
                    proofFamily,
                    partialProofBudget,
                    partialBudget,
                    &advancedTemplates,
                    1);
                if (!partialFalse.check.accepted || !partialFalse.provedFalse) {
                    result.reason = "partial COMPLETE self-check could not build an independently verified H=1 certificate: " + partialFalse.check.reason;
                    return result;
                }

                auto partialProof = std::find_if(
                    partialFalse.certificate.proofs.begin(),
                    partialFalse.certificate.proofs.end(),
                    [](const RootProofRecord &proof) {
                        return proof.kind == RootProofKind::Completion &&
                               proof.cut != CompletionCutId::TurnEntry;
                    });
                if (partialProof == partialFalse.certificate.proofs.end()) {
                    result.reason = "partial COMPLETE self-check certificate did not retain the advanced cut";
                    return result;
                }
                const std::size_t partialIndex = static_cast<std::size_t>(
                    partialProof - partialFalse.certificate.proofs.begin());

                FalseCertificate partialMissingCase = partialFalse.certificate;
                if (partialMissingCase.proofs[partialIndex].completionCases.empty()) {
                    result.reason = "partial COMPLETE self-check unexpectedly has no completion cases";
                    return result;
                }
                partialMissingCase.proofs[partialIndex].completionCases.pop_back();
                BudgetReport partialMissingCaseBudget;
                if (verifyFalseCertificate(
                        bundle,
                        partialMissingCase,
                        partialProofBudget,
                        partialMissingCaseBudget).accepted) {
                    result.reason = "verifier accepted a partial COMPLETE node with a missing case_id";
                    return result;
                }

                FalseCertificate partialWrongPc = partialFalse.certificate;
                ++partialWrongPc.proofs[partialIndex].expectedPc.instructionIndex;
                BudgetReport partialWrongPcBudget;
                if (verifyFalseCertificate(
                        bundle,
                        partialWrongPc,
                        partialProofBudget,
                        partialWrongPcBudget).accepted) {
                    result.reason = "verifier accepted a partial COMPLETE node at an unregistered pc";
                    return result;
                }

                if (partialProof->detailedNodes.empty()) {
                    result.reason = "partial COMPLETE self-check did not serialize its detailed proof-node tree";
                    return result;
                }

                std::size_t branchingNodeIndex = partialProof->detailedNodes.size();
                for (std::size_t index = 0; index < partialProof->detailedNodes.size(); ++index) {
                    if (partialProof->detailedNodes[index].kind == DetailedProofNodeKind::Branch &&
                        partialProof->detailedNodes[index].children.size() > 1) {
                        branchingNodeIndex = index;
                        break;
                    }
                }
                if (branchingNodeIndex == partialProof->detailedNodes.size()) {
                    result.reason = "partial COMPLETE self-check could not locate a nontrivial branch proof node";
                    return result;
                }

                FalseCertificate missingBranchChild = partialFalse.certificate;
                missingBranchChild.proofs[partialIndex]
                    .detailedNodes[branchingNodeIndex]
                    .children.pop_back();
                BudgetReport missingBranchBudget;
                if (verifyFalseCertificate(
                        bundle,
                        missingBranchChild,
                        partialProofBudget,
                        missingBranchBudget).accepted) {
                    result.reason = "verifier accepted a detailed proof with a missing nonempty branch child";
                    return result;
                }

                FalseCertificate changedBranchDomain = partialFalse.certificate;
                const std::uint32_t branchChild = changedBranchDomain.proofs[partialIndex]
                    .detailedNodes[branchingNodeIndex]
                    .children.front();
                if (branchChild >= changedBranchDomain.proofs[partialIndex].detailedNodes.size()) {
                    result.reason = "partial COMPLETE self-check branch child index is invalid";
                    return result;
                }
                ++changedBranchDomain.proofs[partialIndex]
                    .detailedNodes[branchChild]
                    .claimedFrame.inputDomain.enemyHp.lo;
                BudgetReport changedBranchDomainBudget;
                if (verifyFalseCertificate(
                        bundle,
                        changedBranchDomain,
                        partialProofBudget,
                        changedBranchDomainBudget).accepted) {
                    result.reason = "verifier accepted a detailed proof with a tampered branch-domain boundary";
                    return result;
                }

                std::size_t rngNodeIndex = partialProof->detailedNodes.size();
                for (std::size_t index = 0; index < partialProof->detailedNodes.size(); ++index) {
                    const DetailedProofNode &node = partialProof->detailedNodes[index];
                    if ((node.kind == DetailedProofNodeKind::Rng ||
                         node.kind == DetailedProofNodeKind::RngSkip) &&
                        !node.children.empty()) {
                        rngNodeIndex = index;
                        break;
                    }
                }
                if (rngNodeIndex == partialProof->detailedNodes.size()) {
                    result.reason = "partial COMPLETE self-check could not locate an RNG proof node";
                    return result;
                }
                FalseCertificate changedRngPayload = partialFalse.certificate;
                const std::uint32_t rngChild = changedRngPayload.proofs[partialIndex]
                    .detailedNodes[rngNodeIndex]
                    .children.front();
                if (rngChild >= changedRngPayload.proofs[partialIndex].detailedNodes.size()) {
                    result.reason = "partial COMPLETE self-check RNG child index is invalid";
                    return result;
                }
                ++changedRngPayload.proofs[partialIndex]
                    .detailedNodes[rngChild]
                    .claimedFrame.rngPosition;
                BudgetReport changedRngBudget;
                if (verifyFalseCertificate(
                        bundle,
                        changedRngPayload,
                        partialProofBudget,
                        changedRngBudget).accepted) {
                    result.reason = "verifier accepted a detailed proof with a tampered RNG-position payload";
                    return result;
                }
            }

            ProofBudget tinyResumeLimit = proofBudget;
            tinyResumeLimit.maxWork = 1;
            BudgetReport tinyResumeBudget;
            ProofTemplate unpublished;
            unpublished.elapsedTurn = -999;
            const CheckResult tinyAdvance = advanceCompletionCheckpoint(
                bundle,
                proofProblem,
                checkpoint,
                tinyResumeLimit,
                tinyResumeBudget,
                unpublished);
            if (tinyAdvance.accepted || unpublished.elapsedTurn != -999 || tinyResumeBudget.bytes != 0) {
                result.reason = "completion-reservation self-check published a template without reserved work";
                return result;
            }
        }

        FalseCheckResult falseCheck = tryFalseZeroPrice(
            bundle,
            proofProblem,
            1,
            proofFamily,
            proofBudget,
            generationBudget);
        if (!falseCheck.check.accepted || !falseCheck.provedFalse || falseCheck.certificate.proofs.empty()) {
            result.reason = "proof-record self-check could not build the H=1 certificate";
            return result;
        }

        {
            BudgetReport independentBudget;
            if (!verifyFalseCertificate(
                    bundle,
                    falseCheck.certificate,
                    proofBudget,
                    independentBudget).accepted ||
                independentBudget.bytes != 0) {
                result.reason = "independent verifier leaked temporary proof bytes";
                return result;
            }
        }

        {
            BudgetReport capBudget;
            CheckedSnapshot capSnapshot = rebuildSupport(
                bundle,
                proofProblem,
                1,
                proofFamily,
                proofBudget,
                capBudget,
                nullptr,
                nullptr,
                1);
            if (!capSnapshot.check.accepted) {
                result.reason = "price-cap self-check could not build a checked snapshot";
                return result;
            }
            const std::uint64_t retainedSnapshotBytes = capBudget.bytes;
            ProofBudget noPriceBudget = proofBudget;
            noPriceBudget.maxPriceEvaluations = 0;
            FalseCheckResult skippedPrice = tryFalseZeroPriceOnSnapshot(
                bundle,
                proofProblem,
                1,
                std::move(capSnapshot),
                noPriceBudget,
                capBudget);
            if (!skippedPrice.check.accepted || skippedPrice.provedFalse ||
                !skippedPrice.snapshot.check.accepted || capBudget.priceEvaluations != 0 ||
                capBudget.bytes != retainedSnapshotBytes) {
                result.reason = "price-cap self-check did not preserve the checked snapshot";
                return result;
            }
            releaseBudgetBytes(capBudget, snapshotAccountedBytes(skippedPrice.snapshot));
            if (capBudget.bytes != 0) {
                result.reason = "price-cap self-check leaked checked-snapshot bytes";
                return result;
            }
        }

        {
            ProofBudget expiredBudgetLimit = proofBudget;
            expiredBudgetLimit.hasDeadline = true;
            expiredBudgetLimit.deadline =
                std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
            BudgetReport expiredBudget;
            CheckedSnapshot expiredSnapshot = rebuildSupport(
                bundle,
                proofProblem,
                1,
                proofFamily,
                expiredBudgetLimit,
                expiredBudget,
                nullptr,
                nullptr,
                1);
            if (expiredSnapshot.check.accepted || expiredBudget.bytes != 0) {
                result.reason = "expired-deadline rebuild self-check did not roll back temporary bytes";
                return result;
            }
            const ReplayResult interruptedReplay = ExactReplay::replay(
                bundle,
                proofProblem,
                {BattleEmulator::ATTACK_ALLY},
                true,
                &expiredBudgetLimit);
            if (!interruptedReplay.interrupted || interruptedReplay.valid) {
                result.reason = "expired-deadline exact replay self-check did not interrupt";
                return result;
            }
        }

        {
            ProofBudget noReplayWork = proofBudget;
            noReplayWork.maxWork = 0;
            BudgetReport replayBudget;
            const ReplayResult blockedReplay = ExactReplay::replay(
                bundle,
                proofProblem,
                {BattleEmulator::ATTACK_ALLY},
                true,
                &noReplayWork,
                &replayBudget);
            if (!blockedReplay.interrupted || replayBudget.work != 0 ||
                replayBudget.bytes != 0 || blockedReplay.accountedBytes != 0) {
                result.reason = "exact replay work-budget self-check did not reject before the first turn";
                return result;
            }

            ProofBudget noReplayBytes = proofBudget;
            noReplayBytes.maxWork = 1;
            noReplayBytes.maxBytes = 1;
            replayBudget = {};
            const ReplayResult byteBlockedReplay = ExactReplay::replay(
                bundle,
                proofProblem,
                {BattleEmulator::ATTACK_ALLY},
                true,
                &noReplayBytes,
                &replayBudget);
            if (!byteBlockedReplay.interrupted || replayBudget.work != 1 ||
                replayBudget.bytes != 0 || byteBlockedReplay.accountedBytes != 0) {
                result.reason = "exact replay byte-budget self-check retained an uncommitted replay record";
                return result;
            }

            ProofBudget oneReplayTurn = proofBudget;
            oneReplayTurn.maxWork = 1;
            replayBudget = {};
            ReplayResult checkedReplay = ExactReplay::replay(
                bundle,
                proofProblem,
                {BattleEmulator::ATTACK_ALLY},
                true,
                &oneReplayTurn,
                &replayBudget);
            const std::uint64_t expectedReplayBytes =
                checkedReplay.turns.size() * (sizeof(ReplayTurn) + sizeof(int));
            if (checkedReplay.interrupted || !checkedReplay.supported || !checkedReplay.valid ||
                checkedReplay.turns.size() != 1 || replayBudget.work != 1 ||
                replayBudget.bytes != expectedReplayBytes ||
                checkedReplay.accountedBytes != expectedReplayBytes) {
                result.reason = "exact replay shared-budget accounting self-check failed";
                return result;
            }
            releaseBudgetBytes(replayBudget, checkedReplay.accountedBytes);
            checkedReplay.accountedBytes = 0;
            if (replayBudget.bytes != 0 || replayBudget.work != 1) {
                result.reason = "exact replay byte release changed cumulative work or leaked bytes";
                return result;
            }
        }

        {
            ProofBudget tinyByteLimit = proofBudget;
            tinyByteLimit.maxBytes = 1;
            BudgetReport tinyByteBudget;
            CheckedSnapshot tinySnapshot = rebuildSupport(
                bundle,
                proofProblem,
                1,
                proofFamily,
                tinyByteLimit,
                tinyByteBudget,
                nullptr,
                nullptr,
                1);
            if (tinySnapshot.check.accepted || tinyByteBudget.bytes != 0) {
                result.reason = "failed rebuild retained bytes after maxBytes rejection";
                return result;
            }
        }

        FalseCertificate missingRoot = falseCheck.certificate;
        missingRoot.proofs.erase(missingRoot.proofs.begin());
        BudgetReport missingRootBudget;
        if (verifyFalseCertificate(bundle, missingRoot, proofBudget, missingRootBudget).accepted) {
            result.reason = "verifier accepted a certificate with a missing required root";
            return result;
        }

        FalseCertificate missingCase = falseCheck.certificate;
        missingCase.proofs.front().completionCases.pop_back();
        BudgetReport missingCaseBudget;
        if (verifyFalseCertificate(bundle, missingCase, proofBudget, missingCaseBudget).accepted) {
            result.reason = "verifier accepted a COMPLETE node with a missing case_id";
            return result;
        }

        FalseCertificate wrongPc = falseCheck.certificate;
        ++wrongPc.proofs.front().expectedPc.instructionIndex;
        BudgetReport wrongPcBudget;
        if (verifyFalseCertificate(bundle, wrongPc, proofBudget, wrongPcBudget).accepted) {
            result.reason = "verifier accepted a COMPLETE node at an unregistered pc";
            return result;
        }

        FalseCertificate stalePartition = falseCheck.certificate;
        ++stalePartition.proofs.front().partitionVersion;
        BudgetReport stalePartitionBudget;
        if (verifyFalseCertificate(bundle, stalePartition, proofBudget, stalePartitionBudget).accepted) {
            result.reason = "verifier accepted a stale partition_version root";
            return result;
        }

        FalseCertificate staleCoverage = falseCheck.certificate;
        ++staleCoverage.coverageVersion;
        BudgetReport staleCoverageBudget;
        if (verifyFalseCertificate(bundle, staleCoverage, proofBudget, staleCoverageBudget).accepted) {
            result.reason = "verifier accepted a stale coverage_version";
            return result;
        }

        FalseCertificate changedRootBound = falseCheck.certificate;
        if (changedRootBound.rootBound.isNegativeInfinity()) {
            changedRootBound.rootBound = MaxPlusValue::finiteValue(0);
        } else {
            ++changedRootBound.rootBound.finite;
        }
        BudgetReport changedRootBudget;
        if (verifyFalseCertificate(bundle, changedRootBound, proofBudget, changedRootBudget).accepted) {
            result.reason = "verifier accepted a tampered tagged max-plus root bound";
            return result;
        }

        FalseCertificate changedB = falseCheck.certificate;
        bool changedBEntry = false;
        for (auto &layer: changedB.bByRemainingTurns) {
            if (layer.empty()) {
                continue;
            }
            if (layer.front().isNegativeInfinity()) {
                layer.front() = MaxPlusValue::finiteValue(0);
            } else {
                ++layer.front().finite;
            }
            changedBEntry = true;
            break;
        }
        BudgetReport changedBBudget;
        if (!changedBEntry || verifyFalseCertificate(bundle, changedB, proofBudget, changedBBudget).accepted) {
            result.reason = changedBEntry
                ? "verifier accepted a tampered tagged max-plus B table"
                : "self-check certificate unexpectedly has no B-table entries";
            return result;
        }

        FalseCertificate invalidPrice = falseCheck.certificate;
        invalidPrice.q = 0;
        BudgetReport invalidPriceBudget;
        if (verifyFalseCertificate(bundle, invalidPrice, proofBudget, invalidPriceBudget).accepted) {
            result.reason = "verifier accepted an invalid integer proof price";
            return result;
        }

        result.accepted = true;
        return result;
    }
} // namespace d20proof
