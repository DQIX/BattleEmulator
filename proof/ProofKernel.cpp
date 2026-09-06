#include "ProofKernel.h"

#include "../BattleEmulator.h"
#include "../BattleInitialPlayers.h"
#include "SymbolicStepper.h"

#include <algorithm>
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



        bool chargeBudget(
            BudgetReport &budget,
            const ProofBudget &limits,
            std::uint64_t work,
            std::uint64_t bytes) {
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

        Box selectableDomain(const RuleBundle &bundle, Box domain, int command) {
            const CommandProfile *profile = lookupCommandProfile(bundle.profile, command);
            if (profile == nullptr) {
                return {};
            }
            domain.mp.lo = std::max<std::int64_t>(domain.mp.lo, profile->minimumMp);
            domain.herb.lo = std::max<std::int64_t>(domain.herb.lo, profile->minimumHerbs);
            domain.chargeMask &= profile->selectableChargeMask;
            domain.acroMask &= profile->selectableAcroMask;
            domain.paralysisMask &= profile->selectableParalysisMask;
            return domain;
        }

        std::vector<CompletionWeightTerm> fullTurnCompletionTerms(
            const RuleBundle &bundle,
            int command) {
            const CommandProfile *profile = lookupCommandProfile(bundle.profile, command);
            if (profile == nullptr) {
                return {};
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
        std::uint64_t coverageVersion) {
        CheckedSnapshot snapshot;
        snapshot.partitions = partitions;
        snapshot.coverageVersion = coverageVersion;
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
        const CheckResult familyCheck = validateFamily(
            partitions,
            base,
            problem.s0.position,
            static_cast<int>(lastPosition),
            limits.maxLeavesPerPosition);
        if (!familyCheck.accepted) {
            snapshot.check.reason = familyCheck.reason;
            return snapshot;
        }

        std::map<int, CheckedPartition> checkedPartitions;
        for (const PredicatePartition &partition: partitions.trees) {
            CheckedPartition checked = validatePartition(partition, base, limits.maxLeavesPerPosition);
            if (!checked.check.accepted) {
                snapshot.check.reason = checked.check.reason;
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
            checkedPartitions.emplace(partition.rngPosition, std::move(checked));
        }

        std::vector<bool> submittedProofUsed(
            submittedProofs == nullptr ? 0 : submittedProofs->size(),
            false);

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
                                        const Box &rootDomain)
            -> std::optional<RootProofRecord> {
            if (submittedProofs != nullptr) {
                std::optional<std::size_t> match;
                for (std::size_t index = 0; index < submittedProofs->size(); ++index) {
                    const RootProofRecord &proof = (*submittedProofs)[index];
                    if (proof.elapsedTurn != elapsedTurn || !(proof.source == source) ||
                        proof.selectedCommand != command ||
                        proof.partitionVersion != partitions.partitionVersion ||
                        proof.coverageVersion != snapshot.coverageVersion) {
                        continue;
                    }
                    if (match.has_value()) {
                        snapshot.check.reason = "submitted proof contains duplicate required root records";
                        return std::nullopt;
                    }
                    match = index;
                }
                if (!match.has_value()) {
                    snapshot.check.reason = "submitted proof is missing a kernel-required root";
                    return std::nullopt;
                }
                submittedProofUsed[*match] = true;
                return (*submittedProofs)[*match];
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
                for (const ProofTemplate &candidate: *generationTemplates) {
                    if (candidate.elapsedTurn != elapsedTurn ||
                        candidate.rngPosition != source.rngPosition ||
                        candidate.selectedCommand != command ||
                        !contains(candidate.coveredDomain, rootDomain)) {
                        continue;
                    }
                    if (bestTemplate == nullptr || rank(candidate) > rank(*bestTemplate)) {
                        bestTemplate = &candidate;
                    }
                }
            }
            if (bestTemplate != nullptr) {
                ++budget.proofTemplateReuseHits;
                return generatedProof(
                    elapsedTurn,
                    source,
                    command,
                    bestTemplate->kind,
                    bestTemplate->cut);
            }
            return generatedProof(
                elapsedTurn,
                source,
                command,
                RootProofKind::Completion,
                CompletionCutId::TurnEntry);
        };

        auto verifyTurnEntryProof = [&](
            const RootProofRecord &proof,
            int elapsedTurn,
            const CellKey &source,
            int command,
            const Box &rootDomain,
            CheckedRootRecord &record,
            CheckedEdge &edge) -> bool {
            const CompletionSite *site = lookupCompletionSite(bundle.profile, CompletionCutId::TurnEntry);
            if (proof.kind != RootProofKind::Completion || site == nullptr ||
                proof.cut != CompletionCutId::TurnEntry || proof.expectedPc != site->pc) {
                snapshot.check.reason = "COMPLETE cut_id or expected_pc is not the registered turn-entry site";
                return false;
            }
            if (proof.elapsedTurn != elapsedTurn || !(proof.source == source) ||
                proof.selectedCommand != command ||
                proof.partitionVersion != partitions.partitionVersion ||
                proof.coverageVersion != snapshot.coverageVersion) {
                snapshot.check.reason = "COMPLETE root identity differs from the kernel-required root";
                return false;
            }

            const std::vector<CompletionWeightTerm> terms = fullTurnCompletionTerms(bundle, command);
            if (terms.empty() || terms.size() > limits.maxCompletionTermsPerAction ||
                proof.completionCases.size() != terms.size()) {
                snapshot.check.reason = "turn-entry COMPLETE does not contain every required case_id";
                return false;
            }
            std::vector<bool> seenCases(terms.size(), false);
            for (const CompletionProofCase &proofCase: proof.completionCases) {
                if (proofCase.caseId >= terms.size() ||
                    proofCase.targetKind != CompletionTargetKind::AllLeavesInCheckedRange ||
                    seenCases[proofCase.caseId]) {
                    snapshot.check.reason = "turn-entry COMPLETE has an invalid, duplicate, or unsupported case";
                    return false;
                }
                seenCases[proofCase.caseId] = true;
            }

            const PcStaticBounds *remaining = lookupPcBounds(
                bundle.bounds,
                site->pc.routineId,
                site->pc.instructionIndex);
            if (remaining == nullptr) {
                snapshot.check.reason = "registered COMPLETE site has no kernel-computed pc bounds";
                return false;
            }

            record.proofKind = proof.kind;
            record.elapsedTurn = elapsedTurn;
            record.source = source;
            record.selectedCommand = command;
            record.rootDomain = rootDomain;
            record.partitionVersion = partitions.partitionVersion;
            record.coverageVersion = snapshot.coverageVersion;
            record.verifiedPc = site->pc;
            record.verifiedCut = proof.cut;

            edge.kind = CheckedEdgeKind::Completion;
            edge.completionCut = proof.cut;
            edge.elapsedTurn = elapsedTurn;
            edge.source = source;
            edge.selectedCommand = command;
            edge.rootDomain = rootDomain;
            edge.weightTerms = terms;

            const int envelopeLastPosition = problem.s0.position +
                                             (elapsedTurn + 1) * bundle.bounds.rMax;
            edge.firstOutputPosition = source.rngPosition;
            edge.lastOutputPosition = source.rngPosition + remaining->maximumRngReads;
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
                                       std::vector<CompletionCheckpoint> &checkpoints) -> bool {
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

            SymbolicStepper stepper(bundle, problem);
            std::vector<SymbolicFrame> pending;
            pending.push_back(stepper.makeRootFrame(elapsedTurn, command, source.rngPosition, rootDomain));
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
                            const auto partitionIt = checkedPartitions.find(position);
                            if (partitionIt == checkedPartitions.end()) {
                                snapshot.check.reason = "COMPLETE output references missing partition";
                                return false;
                            }
                            for (const auto &[leafId, leafBox]: partitionIt->second.leaves) {
                                (void) leafBox;
                                edge.targets.push_back({position, leafId});
                            }
                        }
                    } else {
                        if (firstPosition != lastPosition) {
                            snapshot.check.reason = "detailed output has non-singleton RNG position";
                            return false;
                        }
                        const auto partitionIt = checkedPartitions.find(firstPosition);
                        if (partitionIt == checkedPartitions.end()) {
                            snapshot.check.reason = "detailed output references missing partition";
                            return false;
                        }
                        for (const auto &[leafId, leafBox]: partitionIt->second.leaves) {
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
                std::sort(edge.targets.begin(), edge.targets.end());
                edge.targets.erase(std::unique(edge.targets.begin(), edge.targets.end()), edge.targets.end());
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
                        if (!chargeBudget(
                                budget,
                                limits,
                                classified.size(),
                                classified.size() * sizeof(OutputLeafClassification))) {
                            snapshot.check.reason = "detailed output classification exceeded proof budget";
                            return false;
                        }
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
                            edges.push_back(std::move(edge));
                        }
                    }
                }
                return true;
            };

            while (!pending.empty()) {
                SymbolicFrame frame = std::move(pending.back());
                pending.pop_back();
                if (!chargeBudget(budget, limits, 1, sizeof(SymbolicFrame))) {
                    snapshot.check.reason = "detailed verify_root exceeded proof budget";
                    return false;
                }

                if (site != nullptr && stepper.point(frame) == site->pc) {
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

                    checkpoints.push_back({elapsedTurn, source, command, proof.cut, rootDomain, frame});
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
                    continue;
                }

                SymbolicStepResult step = stepper.step(frame);
                if (!step.accepted) {
                    snapshot.check.reason = "verify_root symbolic step failed at " + frame.routineId + ":" +
                                            std::to_string(frame.pc) + ": " + step.reason;
                    return false;
                }
                if (step.finished) {
                    for (const SymbolicFrame &finished: step.frames) {
                        if (!addExactFinishedOutput(finished)) {
                            return false;
                        }
                    }
                    continue;
                }
                for (auto child = step.frames.rbegin(); child != step.frames.rend(); ++child) {
                    pending.push_back(std::move(*child));
                }
            }

            if (!reachedCut) {
                snapshot.check.reason = "submitted partial COMPLETE pc is unreachable from this required root";
                return false;
            }
            if (edges.empty()) {
                snapshot.check.reason = "detailed verify_root produced no checked outputs";
                return false;
            }
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
        std::vector<std::set<CellKey>> supportSets(static_cast<std::size_t>(horizon) + 1);
        supportSets[0].insert(snapshot.root);
        snapshot.edgesByElapsedTurn.resize(static_cast<std::size_t>(horizon));
        std::uint64_t storedModelTerms = 0;

        for (int elapsedTurn = 0; elapsedTurn < horizon; ++elapsedTurn) {
            for (const CellKey &source: supportSets[elapsedTurn]) {
                const auto checkedIt = checkedPartitions.find(source.rngPosition);
                if (checkedIt == checkedPartitions.end()) {
                    snapshot.check.reason = "Support references an undefined RNG position";
                    return snapshot;
                }
                const Box *cell = leafDomain(checkedIt->second, source.localCellId);
                if (cell == nullptr) {
                    snapshot.check.reason = "Support references an undefined local cell";
                    return snapshot;
                }

                for (int command: bundle.profile.heroCommands) {
                    const int absoluteTurn = problem.startTurn + elapsedTurn;
                    if (!commandAllowedByConstraints(problem, absoluteTurn, command)) {
                        continue;
                    }
                    const Box rootDomain = selectableDomain(bundle, *cell, command);
                    if (rootDomain.empty()) {
                        continue;
                    }

                    const std::optional<RootProofRecord> proof = acquireRequiredProof(
                        elapsedTurn,
                        source,
                        command,
                        rootDomain);
                    if (!proof.has_value()) {
                        return snapshot;
                    }

                    CheckedRootRecord record;
                    std::vector<CheckedEdge> checkedEdges;
                    std::vector<CompletionCheckpoint> checkpoints;
                    if (proof->kind == RootProofKind::Completion &&
                        proof->cut == CompletionCutId::TurnEntry) {
                        CheckedEdge edge;
                        if (!verifyTurnEntryProof(
                                *proof,
                                elapsedTurn,
                                source,
                                command,
                                rootDomain,
                                record,
                                edge)) {
                            return snapshot;
                        }
                        SymbolicStepper stepper(bundle, problem);
                        checkpoints.push_back({
                            elapsedTurn,
                            source,
                            command,
                            CompletionCutId::TurnEntry,
                            rootDomain,
                            stepper.makeRootFrame(
                                elapsedTurn,
                                command,
                                source.rngPosition,
                                rootDomain),
                        });
                        const Box output = fullTurnOutputEnvelope(bundle, rootDomain, edge.weightTerms);
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
                                const auto outputPartition = checkedPartitions.find(outputPosition);
                                if (outputPartition == checkedPartitions.end()) {
                                    snapshot.check.reason = "COMPLETE output references missing partition";
                                    return snapshot;
                                }
                                for (const auto &[leafId, leafBox]: outputPartition->second.leaves) {
                                    (void) leafBox;
                                    edge.targets.push_back({outputPosition, leafId});
                                }
                            }
                            std::sort(edge.targets.begin(), edge.targets.end());
                            edge.targets.erase(
                                std::unique(edge.targets.begin(), edge.targets.end()),
                                edge.targets.end());
                        }
                        checkedEdges.push_back(std::move(edge));
                    } else if (!verifyDetailedProof(
                                   *proof,
                                   elapsedTurn,
                                   source,
                                   command,
                                   rootDomain,
                                   record,
                                   checkedEdges,
                                   checkpoints)) {
                        return snapshot;
                    }

                    std::size_t edgeTargets = 0;
                    std::size_t edgeTerms = 0;
                    std::size_t detailedTerms = 0;
                    std::size_t completionTerms = 0;
                    for (const CheckedEdge &edge: checkedEdges) {
                        edgeTargets += edge.targets.size();
                        edgeTerms += edge.weightTerms.size();
                        if (edge.kind == CheckedEdgeKind::Detailed) {
                            detailedTerms += edge.weightTerms.size();
                        } else {
                            completionTerms += edge.weightTerms.size();
                        }
                        for (const CellKey &target: edge.targets) {
                            supportSets[elapsedTurn + 1].insert(target);
                        }
                    }
                    if (detailedTerms > limits.maxDetailedTermsPerAction) {
                        snapshot.check.reason = "checked detailed term limit J exceeded for one CellKey/action";
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

                    if (!chargeBudget(
                            budget,
                            limits,
                            1 + proof->completionCases.size() + edgeTerms + edgeTargets,
                            sizeof(RootProofRecord) +
                                proof->completionCases.size() * sizeof(CompletionProofCase) +
                                sizeof(CheckedRootRecord) +
                                checkedEdges.size() * sizeof(CheckedEdge) +
                                edgeTerms * sizeof(CompletionWeightTerm) +
                                edgeTargets * sizeof(CellKey) +
                                checkpoints.size() * sizeof(CompletionCheckpoint))) {
                        snapshot.check.reason = "root coverage exceeded proof budget";
                        return snapshot;
                    }

                    snapshot.proofs.push_back(*proof);
                    snapshot.coverage.push_back(std::move(record));
                    snapshot.completionCheckpoints.insert(
                        snapshot.completionCheckpoints.end(),
                        std::make_move_iterator(checkpoints.begin()),
                        std::make_move_iterator(checkpoints.end()));
                    snapshot.edgesByElapsedTurn[elapsedTurn].insert(
                        snapshot.edgesByElapsedTurn[elapsedTurn].end(),
                        std::make_move_iterator(checkedEdges.begin()),
                        std::make_move_iterator(checkedEdges.end()));
                }
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

        snapshot.support.resize(static_cast<std::size_t>(horizon) + 1);
        for (int elapsedTurn = 0; elapsedTurn <= horizon; ++elapsedTurn) {
            snapshot.support[elapsedTurn].assign(
                supportSets[elapsedTurn].begin(),
                supportSets[elapsedTurn].end());
            if (!chargeBudget(
                    budget,
                    limits,
                    snapshot.support[elapsedTurn].size(),
                    snapshot.support[elapsedTurn].size() * sizeof(CellKey))) {
                snapshot.check.reason = "Support materialization exceeded proof budget";
                return snapshot;
            }
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
        std::uint64_t completionCases = 0;
        for (const RootProofRecord &proof: snapshot.proofs) {
            completionCases += proof.completionCases.size();
        }
        budget.supportCells = supportCells;
        budget.detailedEdges = detailedEdges;
        budget.completionEdges = completionEdges;
        budget.proofRoots = snapshot.proofs.size();
        budget.completionCases = completionCases;

        snapshot.check.accepted = true;
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

        SymbolicStepper stepper(bundle, problem);
        std::vector<SymbolicFrame> pending;
        pending.push_back(checkpoint.frame);
        std::optional<CompletionCutId> nextCut;

        while (!pending.empty() && !nextCut.has_value()) {
            SymbolicFrame frame = std::move(pending.back());
            pending.pop_back();
            if (!chargeBudget(budget, limits, 1, sizeof(SymbolicFrame))) {
                result.reason = "completion resume exceeded proof budget";
                return result;
            }

            const ProgramPoint point = stepper.point(frame);
            bool isCut = false;
            const CompletionCutId cut = cutForPoint(bundle.profile, point, isCut);
            if (isCut && cut != checkpoint.cut) {
                nextCut = cut;
                break;
            }

            SymbolicStepResult step = stepper.step(frame);
            if (!step.accepted) {
                result.reason = "completion resume symbolic step failed at " +
                                frame.routineId + ":" + std::to_string(frame.pc) + ": " + step.reason;
                return result;
            }
            if (step.finished) {
                continue;
            }
            for (auto child = step.frames.rbegin(); child != step.frames.rend(); ++child) {
                pending.push_back(std::move(*child));
            }
        }

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
        for (ProofTemplate &existing: cache.templates) {
            if (existing.elapsedTurn != proofTemplate.elapsedTurn ||
                existing.rngPosition != proofTemplate.rngPosition ||
                existing.selectedCommand != proofTemplate.selectedCommand) {
                continue;
            }
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
        cache.templates.push_back(proofTemplate);
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

            result.values.resize(static_cast<std::size_t>(horizon) + 1);
            result.values[0].assign(
                snapshot.support[horizon].size(),
                MaxPlusValue::negativeInfinity());

            for (int remaining = 1; remaining <= horizon; ++remaining) {
                const int elapsedTurn = horizon - remaining;
                const std::vector<CellKey> &sources = snapshot.support[elapsedTurn];
                const std::vector<CellKey> &nextSources = snapshot.support[elapsedTurn + 1];
                const std::vector<MaxPlusValue> &previous = result.values[remaining - 1];
                if (previous.size() != nextSources.size()) {
                    result.reason = "max-plus previous layer does not match Support";
                    return result;
                }

                std::map<CellKey, MaxPlusValue> continuationByCell;
                for (std::size_t index = 0; index < nextSources.size(); ++index) {
                    continuationByCell.emplace(nextSources[index], previous[index]);
                }

                std::vector<MaxPlusValue> current(
                    sources.size(),
                    MaxPlusValue::negativeInfinity());
                for (std::size_t sourceIndex = 0; sourceIndex < sources.size(); ++sourceIndex) {
                    const CellKey &source = sources[sourceIndex];
                    MaxPlusValue best = MaxPlusValue::negativeInfinity();

                    for (const CheckedEdge &edge: snapshot.edgesByElapsedTurn[elapsedTurn]) {
                        if (!(edge.source == source)) {
                            continue;
                        }

                        MaxPlusValue continuation = edge.mayReachGoal
                            ? MaxPlusValue::finiteValue(0)
                            : MaxPlusValue::negativeInfinity();
                        if (edge.hasContinuingOutput) {
                            if (edge.targets.empty()) {
                                result.reason = "continuing edge has no checked targets";
                                return result;
                            }
                            for (const CellKey &target: edge.targets) {
                                const auto continuationIt = continuationByCell.find(target);
                                if (continuationIt == continuationByCell.end()) {
                                    result.reason = "checked edge target is missing from next-layer Support";
                                    return result;
                                }
                                continuation = maxValue(continuation, continuationIt->second);
                                if (!chargeBudget(budget, limits, 1, 0)) {
                                    result.reason = "max-plus target scan exceeded proof budget";
                                    return result;
                                }
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
        if (budget.priceEvaluations >= limits.maxPriceEvaluations) {
            result.check.reason = "price evaluation budget exhausted";
            return result;
        }
        ++budget.priceEvaluations;
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

        constexpr int q = 256;
        constexpr int u = 0;
        constexpr int v = 0;
        constexpr int w = 0;
        MaxPlusResult maxPlus = computeMaxPlus(snapshot, horizon, q, u, v, w, limits, budget);
        if (!maxPlus.accepted) {
            result.check.reason = maxPlus.reason;
            return result;
        }

        std::int64_t delta = 0;
        std::string inequalityError;
        const bool candidateFalse = falseInequality(
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

        result.check.accepted = true;
        if (!candidateFalse) {
            result.snapshot = std::move(snapshot);
            return result;
        }

        FalseCertificate certificate;
        certificate.problem = problem;
        certificate.horizon = horizon;
        certificate.partitions = partitions;
        certificate.coverageVersion = snapshot.coverageVersion;
        certificate.proofs = snapshot.proofs;
        certificate.coverage = snapshot.coverage;
        certificate.q = q;
        certificate.u = u;
        certificate.v = v;
        certificate.w = w;
        certificate.bByRemainingTurns = maxPlus.values;
        certificate.rootBound = maxPlus.rootBound;
        certificate.delta = delta;

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
        if (snapshot.coverageVersion != certificate.coverageVersion ||
            snapshot.proofs != certificate.proofs ||
            snapshot.coverage != certificate.coverage) {
            result.reason = "certificate coverage does not match kernel-rebuilt required roots";
            return result;
        }

        MaxPlusResult maxPlus = computeMaxPlus(
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
