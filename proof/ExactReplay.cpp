#include "ExactReplay.h"
#include "RuleProgram.h"

#include "../BattleEmulator.h"
#include "../BattleInitialPlayers.h"
#include "../BattleResult.h"
#include "../lcg.h"

#include <sstream>
#include <limits>

namespace d20proof {
    namespace {
        constexpr int kGeneCapacity = 350;


        bool fixedStatsMatch(const Player &actual, const Player &expected) noexcept {
            return actual.maxHp == expected.maxHp && actual.atk == expected.atk &&
                   actual.defaultATK == expected.defaultATK && actual.def == expected.def &&
                   actual.defaultDEF == expected.defaultDEF && actual.speed == expected.speed &&
                   actual.defaultSpeed == expected.defaultSpeed && actual.HealPower == expected.HealPower &&
                   actual.maxMp == expected.maxMp;
        }

        std::uint16_t chargeModeBit(const Player &hero) noexcept {
            if (!hero.specialCharge) {
                return 0x0001;
            }
            if (hero.specialChargeTurn < 0 || hero.specialChargeTurn > 6) {
                return 0;
            }
            return static_cast<std::uint16_t>(1u << (hero.specialChargeTurn + 1));
        }

        std::uint16_t paralysisModeBit(const Player &hero) noexcept {
            if (!hero.paralysis) {
                return 0x0001;
            }
            if (hero.paralysisTurns < -2 || hero.paralysisTurns > 4) {
                return 0;
            }
            return static_cast<std::uint16_t>(1u << (hero.paralysisTurns + 3));
        }

        std::uint16_t acroModeBit(const Player &hero) noexcept {
            if (!hero.acrobaticStar) {
                return 0x0001;
            }
            if (hero.acrobaticStarTurn < 1 || hero.acrobaticStarTurn > 6) {
                return 0;
            }
            return static_cast<std::uint16_t>(1u << hero.acrobaticStarTurn);
        }

        int turnFromNowState(std::uint64_t nowState) noexcept {
            return static_cast<int>((nowState >> 12) & 0xfffffULL);
        }

        std::string validateProfileModes(const RawState &state) {
            const Player &hero = state.players[0];
            const Player &enemy = state.players[1];
            if (hero.sleeping || enemy.sleeping) {
                return "sleeping state is unsupported";
            }
            if (hero.paralysis && (hero.paralysisTurns < -2 || hero.paralysisTurns > 4)) {
                return "active paralysis timer outside proof mode range";
            }
            if (hero.specialCharge && (hero.specialChargeTurn < 0 || hero.specialChargeTurn > 6)) {
                return "active charge timer outside proof mode range";
            }
            if (hero.acrobaticStar && (hero.acrobaticStarTurn < 1 || hero.acrobaticStarTurn > 6)) {
                return "active acrobat timer outside proof mode range";
            }
            if (enemy.rage && (enemy.rageTurns < 1 || enemy.rageTurns > 4)) {
                return "active rage timer outside proof mode range";
            }
            const int cameraMode = static_cast<int>((state.nowState >> 8) & 0xfULL);
            if (cameraMode < 0 || cameraMode > 5) {
                return "camera mode outside proof mode range";
            }
            return {};
        }

        bool battleIsTerminal(const RawState &state) noexcept {
            return state.players[0].hp == 0 || state.players[1].hp == 0;
        }

        std::string validateConstraintState(
            const RuleBundle &bundle,
            const RawState &state,
            int expectedTurn) {
            if (state.position < 1 || state.position >= ExactReplay::kRngTapeSize) {
                return "observation RNG position outside registered tape";
            }
            if (turnFromNowState(state.nowState) != expectedTurn) {
                return "observation turn disagrees with NowState turn field";
            }
            const Player &hero = state.players[0];
            const Player &enemy = state.players[1];
            if (!fixedStatsMatch(hero, BasePlayers[0]) || !fixedStatsMatch(enemy, BasePlayers[1])) {
                return "observation fixed ability values do not match yo2_be profile";
            }
            if (hero.hp < 0 || hero.hp > hero.maxHp || enemy.hp < 0 || enemy.hp > enemy.maxHp ||
                hero.mp < 0 || hero.mp > bundle.profile.heroMaxMp ||
                hero.medicinal_herbs_count < 0 ||
                hero.medicinal_herbs_count > bundle.profile.heroInitialHerbs) {
                return "observation resource value outside profile";
            }
            if (const std::string modeError = validateProfileModes(state); !modeError.empty()) {
                return "observation " + modeError;
            }
            return {};
        }

        bool checkBoundaryObservation(
            const Problem &problem,
            int absoluteTurn,
            const RawState &state,
            std::string &reason) {
            const BoundaryObservation *observation = boundaryObservationAt(problem, absoluteTurn);
            if (observation == nullptr) {
                return true;
            }
            if (!sameRawState(observation->expectedState, state)) {
                std::ostringstream message;
                message << "boundary observation mismatch at absolute turn " << absoluteTurn;
                reason = message.str();
                return false;
            }
            return true;
        }

        ReplayTurn makeReplayTurnBefore(const RawState &state, int command) {
            ReplayTurn turn;
            turn.selectedCommand = command;
            turn.positionBefore = state.position;
            turn.heroHpBefore = state.players[0].hp;
            turn.enemyHpBefore = state.players[1].hp;
            turn.stateBefore = state;
            return turn;
        }

        void finishReplayTurn(ReplayTurn &turn, const RawState &state) {
            turn.positionAfter = state.position;
            turn.heroHpAfter = state.players[0].hp;
            turn.enemyHpAfter = state.players[1].hp;
            turn.stateAfter = state;
        }

        void runMainOneTurn(
            RawState &state,
            int command,
            std::uint64_t seed,
            BattleResult &battleResult) {
            int32_t gene[kGeneCapacity] = {};
            gene[0] = command;
            gene[1] = -1;
            BattleEmulator::Main(
                &state.position,
                1,
                gene,
                state.players,
                &battleResult,
                seed,
                nullptr,
                nullptr,
                -1,
                &state.nowState,
                true,
                false);
        }

        bool isStatusReplacementAction(int action) noexcept {
            return action == BattleEmulator::PARALYSIS ||
                   action == BattleEmulator::CURE_PARALYSIS ||
                   action == BattleEmulator::INACTIVE_ALLY;
        }

        bool runRegisteredTurn(
            RawState &state,
            int command,
            std::uint64_t seed,
            std::string &error) {
            const RawState before = state;
            BattleResult firstPass;
            runMainOneTurn(state, command, seed, firstPass);

            if (command != BattleEmulator::FLEE_ALLY) {
                return true;
            }
            if (firstPass.position <= 0) {
                error = "FLEE exact replay produced no action record";
                return false;
            }

            const bool allyFirst = firstPass.initiative[0];
            bool needsStatusReplay = false;
            if (allyFirst) {
                // Selection-time paralysis/sleep is rejected before this point.
                // Inactive is deliberately an execution-time gate, not a FLEE
                // selection restriction in the registered yo2_be profile.
                needsStatusReplay = before.players[0].inactive;
            } else if (state.players[0].hp > 0 && state.players[1].hp > 0) {
                // With enemy initiative, Main's optimized FLEE path leaves any
                // newly-applied inability flag untouched.  Under the registered
                // rule that flag must instead enter the normal ally-status path.
                needsStatusReplay = state.players[0].paralysis || state.players[0].inactive;
            }

            if (state.players[0].sleeping || state.players[1].sleeping) {
                error = "yo2_be v1 exact replay reached unsupported sleeping state";
                return false;
            }
            if (!needsStatusReplay) {
                return true;
            }

            // Do not change BattleEmulator::Main's production shortcut.  Replay
            // the same raw turn from the same RNG position with a neutral ATTACK
            // prepared command.  In exactly the states covered here Main's own
            // status routine replaces that command with PARALYSIS,
            // CURE_PARALYSIS, or INACTIVE_ALLY before callAttackFun, so ATTACK
            // itself is never executed.  This is the registered FLEE adapter.
            state = before;
            lcg::init(seed, true);
            BattleResult correctedPass;
            runMainOneTurn(state, BattleEmulator::ATTACK_ALLY, seed, correctedPass);

            if (correctedPass.position <= 0 || correctedPass.initiative[0] != allyFirst) {
                error = "FLEE status adapter changed initiative or lost the action record";
                return false;
            }
            int executedAllyAction = -1;
            for (int index = 0; index < correctedPass.position; ++index) {
                if (!correctedPass.isEnemy[index]) {
                    executedAllyAction = correctedPass.actions[index];
                    break;
                }
            }
            if (!isStatusReplacementAction(executedAllyAction)) {
                error = "FLEE status adapter failed to enter the registered inability transition";
                return false;
            }
            if (state.players[0].sleeping || state.players[1].sleeping) {
                error = "yo2_be v1 corrected FLEE replay reached unsupported sleeping state";
                return false;
            }
            return true;
        }
    } // namespace

    bool ExactReplay::isSelectable(
        const RuleBundle &bundle,
        const RawState &state,
        int command) noexcept {
        const CommandProfile *profile = lookupCommandProfile(bundle.profile, command);
        if (profile == nullptr) {
            return false;
        }

        const Player &hero = state.players[0];
        const std::uint16_t charge = chargeModeBit(hero);
        const std::uint16_t acro = acroModeBit(hero);
        const std::uint16_t paralysis = paralysisModeBit(hero);
        if (command == BattleEmulator::FLEE_ALLY && hero.sleeping) {
            return false;
        }
        return hero.mp >= profile->minimumMp &&
               hero.medicinal_herbs_count >= profile->minimumHerbs &&
               charge != 0 && (charge & profile->selectableChargeMask) != 0 &&
               acro != 0 && (acro & profile->selectableAcroMask) != 0 &&
               paralysis != 0 && (paralysis & profile->selectableParalysisMask) != 0;
    }

    std::string ExactReplay::validateProblem(
        const RuleBundle &bundle,
        const Problem &problem,
        int remainingTurns) {
        if (!bundle.registered || problem.ruleId != bundle.id) {
            return "problem rule_id is not the registered bundle";
        }
        if (problem.seed == 0) {
            return "seed 0 is unsupported by registered LCG";
        }
        if (remainingTurns < 0) {
            return "negative horizon";
        }
        if (problem.s0.position < 1 || problem.s0.position >= kRngTapeSize) {
            return "RNG position outside registered tape";
        }
        if (turnFromNowState(problem.s0.nowState) != problem.startTurn) {
            return "startTurn disagrees with NowState turn field";
        }
        if (problem.startTurn < 0 ||
            static_cast<long long>(problem.startTurn) + remainingTurns > 0xfffffLL) {
            return "turn counter outside registered NowState field";
        }

        int previousActionTurn = -1;
        for (const ActionConstraint &constraint: problem.constraints.actions) {
            if (constraint.absoluteTurn < problem.startTurn ||
                constraint.absoluteTurn <= previousActionTurn ||
                constraint.absoluteTurn > 0xfffff ||
                constraint.allowedCommands.empty() ||
                !std::is_sorted(constraint.allowedCommands.begin(), constraint.allowedCommands.end()) ||
                std::adjacent_find(
                    constraint.allowedCommands.begin(),
                    constraint.allowedCommands.end()) != constraint.allowedCommands.end()) {
                return "action constraints are not in canonical strictly-ordered form";
            }
            for (int command: constraint.allowedCommands) {
                if (lookupCommandProfile(bundle.profile, command) == nullptr) {
                    return "action constraint contains a command outside the registered profile";
                }
            }
            previousActionTurn = constraint.absoluteTurn;
        }

        int previousObservationTurn = -1;
        for (const BoundaryObservation &observation: problem.constraints.observations) {
            if (observation.absoluteTurn < problem.startTurn ||
                observation.absoluteTurn <= previousObservationTurn ||
                observation.absoluteTurn > 0xfffff) {
                return "boundary observations are not in canonical strictly-ordered form";
            }
            if (const std::string observationError = validateConstraintState(
                    bundle,
                    observation.expectedState,
                    observation.absoluteTurn);
                !observationError.empty()) {
                return observationError;
            }
            previousObservationTurn = observation.absoluteTurn;
        }

        if (const BoundaryObservation *rootObservation = boundaryObservationAt(problem, problem.startTurn);
            rootObservation != nullptr && !sameRawState(rootObservation->expectedState, problem.s0)) {
            return "root state disagrees with its boundary observation";
        }

        const Player &hero = problem.s0.players[0];
        const Player &enemy = problem.s0.players[1];
        if (!fixedStatsMatch(hero, BasePlayers[0]) || !fixedStatsMatch(enemy, BasePlayers[1])) {
            return "fixed ability values do not match yo2_be profile";
        }
        if (hero.hp < 0 || hero.hp > hero.maxHp || enemy.hp < 0 || enemy.hp > enemy.maxHp) {
            return "HP outside profile";
        }
        if (hero.mp < 0 || hero.mp > bundle.profile.heroMaxMp) {
            return "MP outside profile";
        }
        if (hero.medicinal_herbs_count < 0 ||
            hero.medicinal_herbs_count > bundle.profile.heroInitialHerbs) {
            return "herb count outside profile";
        }
        if (const std::string modeError = validateProfileModes(problem.s0); !modeError.empty()) {
            return modeError;
        }

        const long long chargeLower =
            static_cast<long long>(std::min(hero.specialChargeTurn, 0)) - remainingTurns;
        const long long acroLower =
            static_cast<long long>(std::min(hero.acrobaticStarTurn, 0)) - remainingTurns;
        const long long paralysisLevelUpper =
            static_cast<long long>(std::max(hero.paralysisLevel, 0)) + remainingTurns;
        if (chargeLower < std::numeric_limits<int>::min() ||
            acroLower < std::numeric_limits<int>::min() ||
            paralysisLevelUpper > std::numeric_limits<int>::max()) {
            return "raw timer arithmetic may overflow within horizon";
        }

        const long long lastPosition = static_cast<long long>(problem.s0.position) +
                                       static_cast<long long>(remainingTurns) * bundle.bounds.rMax;
        if (lastPosition > kRngTapeSize - 1) {
            return "requested horizon cannot be enclosed inside registered RNG tape";
        }
        return {};
    }

    ReplayResult ExactReplay::replay(
        const RuleBundle &bundle,
        const Problem &problem,
        const std::vector<int> &commands,
        bool rejectCommandsAfterTerminal) {
        ReplayResult result;
        result.finalState = problem.s0;

        const std::string validationError = validateProblem(bundle, problem, static_cast<int>(commands.size()));
        if (!validationError.empty()) {
            result.reason = validationError;
            return result;
        }
        result.supported = true;

        if (!checkBoundaryObservation(
                problem,
                problem.startTurn,
                result.finalState,
                result.reason)) {
            return result;
        }

        if (result.finalState.players[1].hp == 0) {
            result.valid = commands.empty() || !rejectCommandsAfterTerminal;
            result.won = true;
            result.firstWinningTurn = 0;
            if (!result.valid) {
                result.reason = "commands continue after an already-won root";
            }
            return result;
        }
        if (result.finalState.players[0].hp == 0) {
            result.valid = commands.empty() || !rejectCommandsAfterTerminal;
            result.lost = true;
            if (!result.valid) {
                result.reason = "commands continue after an already-lost root";
            }
            return result;
        }

        lcg::init(problem.seed, true);

        for (std::size_t turnIndex = 0; turnIndex < commands.size(); ++turnIndex) {
            if (battleIsTerminal(result.finalState)) {
                if (rejectCommandsAfterTerminal) {
                    result.reason = "command requested after terminal battle state";
                    return result;
                }
                break;
            }

            const int command = commands[turnIndex];
            const int absoluteTurn = problem.startTurn + static_cast<int>(turnIndex);
            if (!commandAllowedByConstraints(problem, absoluteTurn, command)) {
                std::ostringstream message;
                message << "command " << command
                        << " violates the action constraint at absolute turn " << absoluteTurn;
                result.reason = message.str();
                return result;
            }
            if (!isSelectable(bundle, result.finalState, command)) {
                std::ostringstream message;
                message << "command " << command << " is not selectable at suffix turn " << turnIndex;
                result.reason = message.str();
                return result;
            }

            ReplayTurn turn = makeReplayTurnBefore(result.finalState, command);
            std::string turnError;
            if (!runRegisteredTurn(result.finalState, command, problem.seed, turnError)) {
                result.supported = false;
                result.reason = std::move(turnError);
                return result;
            }

            finishReplayTurn(turn, result.finalState);
            result.turns.push_back(turn);
            result.checkedCommands.push_back(command);

            if (!checkBoundaryObservation(
                    problem,
                    absoluteTurn + 1,
                    result.finalState,
                    result.reason)) {
                return result;
            }

            const int consumedRng = turn.positionAfter - turn.positionBefore;
            if (consumedRng < 0 || consumedRng > bundle.bounds.rMax) {
                result.supported = false;
                result.reason = "exact replay exceeded registered per-turn Rmax";
                return result;
            }

            if (result.finalState.position < 1 || result.finalState.position >= kRngTapeSize) {
                result.reason = "exact replay escaped registered RNG tape";
                return result;
            }

            if (result.finalState.players[1].hp == 0) {
                result.won = true;
                result.firstWinningTurn = static_cast<int>(turnIndex) + 1;
                if (rejectCommandsAfterTerminal && turnIndex + 1 != commands.size()) {
                    result.reason = "commands continue after first winning turn";
                    return result;
                }
                break;
            }

            if (result.finalState.players[0].hp == 0) {
                result.lost = true;
                if (rejectCommandsAfterTerminal && turnIndex + 1 != commands.size()) {
                    result.reason = "commands continue after loss";
                    return result;
                }
                break;
            }
        }

        result.valid = true;
        return result;
    }

    PrefixReceipt ExactReplay::replayPrefix(
        const RuleBundle &bundle,
        const Problem &problem,
        const std::vector<int> &prefix) {
        PrefixReceipt receipt;
        receipt.initialProblem = problem;
        receipt.prefix = prefix;

        const std::string validationError = validateProblem(bundle, problem, static_cast<int>(prefix.size()));
        if (!validationError.empty()) {
            receipt.failureKind = SolveKind::UnsupportedInput;
            receipt.reason = validationError;
            return receipt;
        }

        const ReplayResult replayedPrefix = replay(bundle, problem, prefix, true);
        if (!replayedPrefix.supported) {
            receipt.failureKind = SolveKind::ModelError;
            receipt.reason = replayedPrefix.reason;
            return receipt;
        }
        if (!replayedPrefix.valid) {
            receipt.failureKind = SolveKind::InvalidPrefix;
            receipt.reason = replayedPrefix.reason;
            return receipt;
        }

        receipt.replay = replayedPrefix;
        receipt.terminalState = replayedPrefix.finalState;
        receipt.terminalTurn = problem.startTurn + static_cast<int>(replayedPrefix.checkedCommands.size());
        if (turnFromNowState(receipt.terminalState.nowState) != receipt.terminalTurn) {
            receipt.failureKind = SolveKind::ModelError;
            receipt.reason = "prefix turn accounting disagrees with NowState";
            return receipt;
        }

        receipt.observationsChecked = true;
        receipt.valid = true;
        receipt.failureKind = SolveKind::Win;
        return receipt;
    }

    Problem ExactReplay::bindSuffixProblem(const Problem &initialProblem, const PrefixReceipt &receipt) {
        Problem suffixProblem = initialProblem;
        suffixProblem.s0 = receipt.terminalState;
        suffixProblem.startTurn = receipt.terminalTurn;
        std::erase_if(
            suffixProblem.constraints.actions,
            [&](const ActionConstraint &constraint) {
                return constraint.absoluteTurn < receipt.terminalTurn;
            });
        std::erase_if(
            suffixProblem.constraints.observations,
            [&](const BoundaryObservation &observation) {
                return observation.absoluteTurn < receipt.terminalTurn;
            });
        return suffixProblem;
    }
} // namespace d20proof
