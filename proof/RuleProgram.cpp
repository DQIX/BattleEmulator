#include "RuleProgram.h"

#include "ExactReplay.h"
#include "../BattleEmulator.h"
#include "../BattleInitialPlayers.h"

#include <algorithm>
#include <cfenv>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace d20proof {
    namespace {
        constexpr std::string_view kNativePrefix = "native:";

        using RoutineIndex = std::unordered_map<std::string, const Routine *>;
        using CallGraph = std::unordered_map<std::string, std::vector<std::string> >;

        struct RoutineAnalysis {
            bool accepted = false;
            std::string reason;
        };

        using NativeIndex = std::unordered_map<std::string, const NativeContract *>;

        RegistrationResult rejected(std::string reason) {
            RegistrationResult result;
            result.reason = std::move(reason);
            return result;
        }

        RegistrationResult validateRuntimeContracts(const RuleBundle &bundle) {
            const NumericEnvironmentContract &numeric = bundle.numericEnvironment;
            if (__cplusplus < static_cast<long>(numeric.minimumCxxVersion)) {
                return rejected("runtime C++ language version is below the registered numeric contract");
            }
            if (sizeof(int) != numeric.intBytes ||
                sizeof(std::int32_t) != numeric.int32Bytes ||
                sizeof(std::uint64_t) != numeric.uint64Bytes ||
                sizeof(double) != numeric.doubleBytes) {
                return rejected("runtime integer/double widths differ from the registered numeric contract");
            }
            if (std::numeric_limits<double>::radix != numeric.doubleRadix ||
                std::numeric_limits<double>::digits != numeric.doubleDigits) {
                return rejected("runtime double radix/precision differs from the registered numeric contract");
            }
            if (numeric.requireIec559 && !std::numeric_limits<double>::is_iec559) {
                return rejected("runtime double is not IEC 559 as required by the registered contract");
            }
            if (numeric.requireRoundToNearest &&
                std::numeric_limits<double>::round_style != std::round_to_nearest) {
                return rejected("runtime double rounding mode contract is not round-to-nearest");
            }
            if (numeric.requireRoundToNearest && std::fegetround() != FE_TONEAREST) {
                return rejected("runtime floating-point environment is not FE_TONEAREST");
            }
#if defined(__FAST_MATH__)
            if (numeric.forbidFastMath) {
                return rejected("fast-math is forbidden by the registered numeric contract");
            }
#endif
#if !defined(D20_PROOF_NUMERIC_CONTRACT_V1)
            if (numeric.requireProofBuildContract) {
                return rejected("proof target was not built with D20_PROOF_NUMERIC_CONTRACT_V1");
            }
#endif
            const ExactReplayContract &replay = bundle.exactReplay;
            if (replay.entryPoint != "d20proof::ExactReplay::replay" || replay.mode != -1 ||
                !replay.isSearch || replay.calculateRngOnly ||
                !replay.oneCommandPerMainCall || !replay.terminalAfterCameraTail) {
                return rejected("exact replay contract differs from the registered authoritative entry");
            }
            RegistrationResult accepted;
            accepted.accepted = true;
            return accepted;
        }

        ValueRef constantValue(double value) {
            ValueRef ref;
            ref.kind = ValueRefKind::Constant;
            ref.constant = value;
            return ref;
        }

        ValueRef resourceValue(ResourceAxis resource) {
            ValueRef ref;
            ref.kind = ValueRefKind::Resource;
            ref.resource = resource;
            return ref;
        }

        ValueRef stateValue(StateField state) {
            ValueRef ref;
            ref.kind = ValueRefKind::StateField;
            ref.state = state;
            return ref;
        }

        ValueRef scalarValue(ScalarSlot scalar) {
            ValueRef ref;
            ref.kind = ValueRefKind::Scalar;
            ref.scalar = scalar;
            return ref;
        }

        ValueRef resourceMinusScalar(ResourceAxis resource, ScalarSlot scalar) {
            ValueRef ref;
            ref.kind = ValueRefKind::ResourceMinusScalar;
            ref.resource = resource;
            ref.scalar = scalar;
            return ref;
        }

        ValueRef fixedSourceTimesScalar(FixedScalarSource source, ScalarSlot scalar) {
            ValueRef ref;
            ref.kind = ValueRefKind::FixedSourceTimesScalar;
            ref.fixedSource = source;
            ref.scalar = scalar;
            return ref;
        }

        ValueRef stateIndexedLookup(
            StateField state,
            std::vector<int> keys,
            std::vector<double> values) {
            ValueRef ref;
            ref.kind = ValueRefKind::StateIndexedLookup;
            ref.state = state;
            ref.lookupKeys = std::move(keys);
            ref.lookupValues = std::move(values);
            return ref;
        }

        Comparison comparison(ValueRef left, CompareOp op, ValueRef right) {
            return {std::move(left), op, std::move(right)};
        }

        ConditionClause clause(std::initializer_list<Comparison> all) {
            return {std::vector<Comparison>(all)};
        }

        BranchCondition anyOf(std::initializer_list<ConditionClause> clauses) {
            return {std::vector<ConditionClause>(clauses)};
        }

        BranchCondition allOf(std::initializer_list<Comparison> comparisons) {
            return anyOf({clause(comparisons)});
        }

        bool isTerminalOpcode(Opcode opcode) noexcept {
            return opcode == Opcode::Return || opcode == Opcode::Finish;
        }

        const Routine *findRoutine(const RoutineIndex &index, std::string_view id) {
            const auto found = index.find(std::string(id));
            return found == index.end() ? nullptr : found->second;
        }

        bool validValueRef(const ValueRef &ref) {
            switch (ref.kind) {
                case ValueRefKind::Constant:
                    return std::isfinite(ref.constant);
                case ValueRefKind::Resource:
                    return ref.scalar == ScalarSlot::None && ref.state == StateField::None;
                case ValueRefKind::StateField:
                    return ref.state != StateField::None;
                case ValueRefKind::Scalar:
                    return ref.scalar != ScalarSlot::None;
                case ValueRefKind::ResourceMinusScalar:
                    return ref.scalar != ScalarSlot::None;
                case ValueRefKind::FixedSourceTimesScalar:
                    return ref.fixedSource != FixedScalarSource::None && ref.scalar != ScalarSlot::None;
                case ValueRefKind::StateIndexedLookup: {
                    if (ref.state == StateField::None || ref.lookupKeys.empty() ||
                        ref.lookupKeys.size() != ref.lookupValues.size()) {
                        return false;
                    }
                    std::set<int> keys;
                    for (std::size_t index = 0; index < ref.lookupKeys.size(); ++index) {
                        if (!keys.insert(ref.lookupKeys[index]).second ||
                            !std::isfinite(ref.lookupValues[index])) {
                            return false;
                        }
                    }
                    return true;
                }
            }
            return false;
        }

        std::size_t nativeArgumentCount(NativeOperation operation) noexcept {
            switch (operation) {
                case NativeOperation::PhysicalDamage:
                    return 2;
                case NativeOperation::TypeC:
                    return 3;
                case NativeOperation::TypeD:
                    return 2;
                case NativeOperation::None:
                    return 0;
            }
            return 0;
        }

        bool validNativeArguments(const NativeContract &contract, const Instruction &instruction) {
            if (contract.operation == NativeOperation::None ||
                contract.implementation == NativeImplementation::None ||
                contract.argumentCount != nativeArgumentCount(contract.operation) ||
                instruction.nativeArguments.size() != contract.argumentCount ||
                contract.argumentDomains.size() != contract.argumentCount) {
                return false;
            }
            for (std::size_t index = 0; index < instruction.nativeArguments.size(); ++index) {
                const double argument = instruction.nativeArguments[index];
                const NativeArgumentDomain &domain = contract.argumentDomains[index];
                if (!std::isfinite(argument) || !std::isfinite(domain.minimum) ||
                    !std::isfinite(domain.maximum) || domain.minimum > domain.maximum ||
                    argument < domain.minimum || argument > domain.maximum ||
                    (domain.integerOnly && std::floor(argument) != argument)) {
                    return false;
                }
            }
            return true;
        }

        RegistrationResult validateProfile(const RuleProfile &profile) {
            if (profile.heroCommands.size() != 8) {
                return rejected("yo2_be profile must register exactly eight hero commands");
            }
            if (profile.commandProfiles.size() != profile.heroCommands.size()) {
                return rejected("every registered hero command must have one command profile");
            }

            const std::set<int> uniqueCommands(profile.heroCommands.begin(), profile.heroCommands.end());
            if (uniqueCommands.size() != profile.heroCommands.size()) {
                return rejected("duplicate hero command");
            }
            for (std::size_t index = 0; index < profile.heroCommands.size(); ++index) {
                const CommandProfile &command = profile.commandProfiles[index];
                if (command.command != profile.heroCommands[index]) {
                    return rejected("command profile order must match hero command order");
                }
                if (command.minimumMp < 0 || command.minimumHerbs < 0 ||
                    command.turnEntryCompletionTerms.empty()) {
                    return rejected("invalid command legality or missing turn-entry completion bound");
                }
                if (command.selectableChargeMask == 0 ||
                    (command.selectableChargeMask & ~kChargeMaskAll) != 0 ||
                    command.selectableAcroMask == 0 ||
                    (command.selectableAcroMask & ~kAcroMaskAll) != 0 ||
                    command.selectableParalysisMask == 0 ||
                    (command.selectableParalysisMask & ~kParalysisMaskAll) != 0) {
                    return rejected("command profile has invalid selectable mode mask");
                }
                for (const CompletionWeightTerm &term: command.turnEntryCompletionTerms) {
                    if (term.enemyDamageUpper < 0 || term.heroHpGainUpper < 0 ||
                        term.mpDelta > 0 || term.herbDelta > 0) {
                        return rejected("yo2_be completion term violates registered resource directions");
                    }
                }
            }
            if (profile.sleepingSupported) {
                return rejected("yo2_be v1 must not claim sleeping support");
            }
            if (profile.completionSites.size() != 4) {
                return rejected("yo2_be v1 must register exactly four COMPLETE cut sites");
            }
            std::set<CompletionCutId> completionCuts;
            for (const CompletionSite &site: profile.completionSites) {
                if (site.pc.routineId.empty() || site.pc.instructionIndex < 0 ||
                    !completionCuts.insert(site.cut).second) {
                    return rejected("invalid or duplicate COMPLETE cut site");
                }
            }

            RegistrationResult result;
            result.accepted = true;
            return result;
        }

        RegistrationResult buildRoutineIndex(const RuleProgram &program, RoutineIndex &index) {
            int turnRoutineCount = 0;

            for (const Routine &routine: program.routines) {
                if (routine.id.empty() || routine.instructions.empty()) {
                    return rejected("empty routine");
                }
                std::set<ScalarSlot> initializedSlots;
                for (const ScalarInitializer &initializer: routine.localInitializers) {
                    if (initializer.slot == ScalarSlot::None || !std::isfinite(initializer.value) ||
                        !initializedSlots.insert(initializer.slot).second) {
                        return rejected("routine has an invalid or duplicate typed scalar initializer");
                    }
                }
                if (!index.emplace(routine.id, &routine).second) {
                    return rejected("duplicate routine id");
                }
                if (routine.turnRoutine) {
                    ++turnRoutineCount;
                }
            }

            const Routine *entry = findRoutine(index, program.entryRoutine);
            if (turnRoutineCount != 1 || entry == nullptr || !entry->turnRoutine) {
                return rejected("RuleProgram must have one TURN entry");
            }

            RegistrationResult result;
            result.accepted = true;
            return result;
        }

        RegistrationResult validateNativeContracts(
            const std::vector<NativeContract> &contracts,
            NativeIndex &nativeIndex) {
            for (const NativeContract &contract: contracts) {
                if (contract.id.empty() || contract.maxRngReads < 0 || contract.maxWork < 0 ||
                    contract.operation == NativeOperation::None ||
                    contract.implementation == NativeImplementation::None ||
                    contract.argumentCount != nativeArgumentCount(contract.operation) ||
                    contract.argumentDomains.size() != contract.argumentCount ||
                    !contract.resourceIndependent || contract.resultSlot == ScalarSlot::None ||
                    contract.resultMin > contract.resultMax) {
                    return rejected("invalid native contract");
                }
                for (const NativeArgumentDomain &domain: contract.argumentDomains) {
                    if (!std::isfinite(domain.minimum) || !std::isfinite(domain.maximum) ||
                        domain.minimum > domain.maximum) {
                        return rejected("native contract has an invalid argument domain");
                    }
                }
                const bool implementationMatchesOperation =
                    (contract.operation == NativeOperation::PhysicalDamage &&
                     contract.implementation == NativeImplementation::BattlePhysicalDamage) ||
                    (contract.operation == NativeOperation::TypeC &&
                     contract.implementation == NativeImplementation::BattleTypeC) ||
                    (contract.operation == NativeOperation::TypeD &&
                     contract.implementation == NativeImplementation::BattleTypeD);
                if (!implementationMatchesOperation) {
                    return rejected("native operation does not match its fixed implementation");
                }
                if (!nativeIndex.emplace(contract.id, &contract).second) {
                    return rejected("duplicate native id");
                }
            }

            RegistrationResult result;
            result.accepted = true;
            return result;
        }

        RoutineAnalysis analyzeRoutine(
            const Routine &routine,
            const RoutineIndex &routineIndex,
            const NativeIndex &nativeIndex,
            CallGraph &callGraph) {
            for (const Instruction &instruction: routine.instructions) {
                if (instruction.rngReads < 0 || instruction.nativeWork < 0) {
                    return {false, "negative work bound"};
                }

                for (int successor: instruction.successors) {
                    if (successor < 0 || successor >= static_cast<int>(routine.instructions.size())) {
                        return {false, "unresolved local successor"};
                    }
                }

                if (!isTerminalOpcode(instruction.opcode) && instruction.opcode != Opcode::Call &&
                    instruction.successors.empty()) {
                    return {false, "nonterminal instruction has no successor"};
                }

                if (instruction.opcode == Opcode::Call) {
                    if (instruction.callTarget.empty() ||
                        findRoutine(routineIndex, instruction.callTarget) == nullptr) {
                        return {false, "CALL references missing routine"};
                    }
                    callGraph[routine.id].push_back(instruction.callTarget);
                } else if (!instruction.callTarget.empty()) {
                    return {false, "typed CALL target attached to a different opcode"};
                }

                if (instruction.opcode == Opcode::Native) {
                    if (instruction.nativeId.empty()) {
                        return {false, "NATIVE is missing its typed native id"};
                    }
                    const auto nativeIt = nativeIndex.find(instruction.nativeId);
                    if (nativeIt == nativeIndex.end()) {
                        return {false, "NATIVE references missing contract"};
                    }
                    if (instruction.scalarResult == ScalarSlot::None ||
                        instruction.scalarResult != nativeIt->second->resultSlot) {
                        return {false, "NATIVE result slot does not match its registered contract"};
                    }
                    if (!validNativeArguments(*nativeIt->second, instruction)) {
                        return {false, "NATIVE is missing concrete typed arguments"};
                    }
                } else if (instruction.scalarResult != ScalarSlot::None) {
                    return {false, "typed scalar result attached to a non-NATIVE opcode"};
                } else if (!instruction.nativeArguments.empty()) {
                    return {false, "typed NATIVE arguments attached to a different opcode"};
                } else if (!instruction.nativeId.empty()) {
                    return {false, "typed NATIVE id attached to a different opcode"};
                }

                if (instruction.opcode == Opcode::ScalarUpdate) {
                    const ScalarUpdateOperand &update = instruction.scalarUpdate;
                    if (update.kind == ScalarUpdateKind::None || update.targetSlot == ScalarSlot::None) {
                        return {false, "SCALAR_UPDATE is missing typed operands"};
                    }
                    if ((update.kind == ScalarUpdateKind::MultiplyBySlot ||
                         update.kind == ScalarUpdateKind::SetFixedSourceTimesSlot) &&
                        update.sourceSlot == ScalarSlot::None) {
                        return {false, "SCALAR_UPDATE is missing its source slot"};
                    }
                    if (update.kind == ScalarUpdateKind::SetFixedSourceTimesSlot &&
                        update.fixedSource == FixedScalarSource::None) {
                        return {false, "SCALAR_UPDATE is missing its fixed source"};
                    }
                    if (update.kind == ScalarUpdateKind::MultiplyRational && update.denominator <= 0) {
                        return {false, "SCALAR_UPDATE has an invalid rational denominator"};
                    }
                } else if (instruction.scalarUpdate.kind != ScalarUpdateKind::None) {
                    return {false, "typed scalar update attached to a different opcode"};
                }

                if (instruction.opcode == Opcode::ReadRng) {
                    const RngReadOperand &read = instruction.rngRead;
                    if (read.kind == RngReadKind::None || read.resultSlot == ScalarSlot::None) {
                        return {false, "READ_RNG is missing typed operands"};
                    }
                    switch (read.kind) {
                        case RngReadKind::Percent:
                            if (read.intMin != 0 || read.intMax <= 0) {
                                return {false, "READ_RNG percent has an invalid maximum"};
                            }
                            break;
                        case RngReadKind::IntRangeInclusive:
                            if (read.intMin > read.intMax) {
                                return {false, "READ_RNG integer range is reversed"};
                            }
                            break;
                        case RngReadKind::FloatRange:
                            if (!std::isfinite(read.floatMin) || !std::isfinite(read.floatMax) ||
                                !(read.floatMin < read.floatMax)) {
                                return {false, "READ_RNG floating range is invalid"};
                            }
                            break;
                        case RngReadKind::PercentCameraRemaining:
                            break;
                        case RngReadKind::None:
                            return {false, "READ_RNG has no operation"};
                    }
                } else if (instruction.rngRead.kind != RngReadKind::None) {
                    return {false, "typed RNG operand attached to a different opcode"};
                }

                if (instruction.opcode == Opcode::Branch) {
                    if (instruction.branchCondition.any.empty()) {
                        return {false, "BRANCH is missing its typed condition"};
                    }
                    for (const ConditionClause &conditionClause: instruction.branchCondition.any) {
                        if (conditionClause.all.empty()) {
                            return {false, "BRANCH contains an empty condition clause"};
                        }
                        for (const Comparison &item: conditionClause.all) {
                            if (!validValueRef(item.left) || !validValueRef(item.right)) {
                                return {false, "BRANCH contains an invalid typed value reference"};
                            }
                        }
                    }
                } else if (!instruction.branchCondition.any.empty()) {
                    return {false, "typed BRANCH condition attached to a different opcode"};
                }
            }

            std::vector<int> visitState(routine.instructions.size(), 0);

            std::function < bool(int) > hasCycle = [&](int pc) {
                if (visitState[pc] == 1) {
                    return true;
                }
                if (visitState[pc] == 2) {
                    return false;
                }

                visitState[pc] = 1;
                for (int successor: routine.instructions[pc].successors) {
                    if (hasCycle(successor)) {
                        return true;
                    }
                }
                visitState[pc] = 2;
                return false;
            };

            if (hasCycle(0)) {
                return {false, "cyclic routine CFG"};
            }

            for (int state: visitState) {
                if (state == 0) {
                    return {false, "unreachable instruction in routine CFG"};
                }
            }

            for (const Instruction &instruction: routine.instructions) {
                if (instruction.opcode == Opcode::Call && instruction.successors.size() != 1) {
                    return {false, "CALL must have exactly one return_pc"};
                }
                if (isTerminalOpcode(instruction.opcode) && !instruction.successors.empty()) {
                    return {false, "terminal instruction must not have successors"};
                }
                if (instruction.opcode == Opcode::Finish && !routine.turnRoutine) {
                    return {false, "subroutine terminates with FINISH"};
                }
                if (instruction.opcode == Opcode::Return && routine.turnRoutine) {
                    return {false, "TURN routine terminates with RETURN"};
                }
                const bool rngOpcode = instruction.opcode == Opcode::ReadRng ||
                                       instruction.opcode == Opcode::SkipRng;
                if (!rngOpcode && instruction.rngReads != 0) {
                    return {false, "RNG cost is attached to a non-RNG opcode"};
                }
                if (instruction.opcode == Opcode::Branch && instruction.successors.size() != 2) {
                    return {false, "BRANCH must have exactly two successors"};
                }
                if (instruction.opcode == Opcode::Switch) {
                    if (instruction.successors.empty() ||
                        instruction.successors.size() != instruction.switchCaseValues.size()) {
                        return {false, "SWITCH must enumerate every case and successor"};
                    }
                    if (instruction.switchOperand.kind == SwitchSourceKind::None) {
                        return {false, "SWITCH is missing a typed source"};
                    }
                    const std::set<int> uniqueCases(
                        instruction.switchCaseValues.begin(),
                        instruction.switchCaseValues.end());
                    if (uniqueCases.size() != instruction.switchCaseValues.size()) {
                        return {false, "SWITCH contains duplicate case values"};
                    }
                    if (instruction.switchOperand.kind == SwitchSourceKind::WeightedScalarUpperBounds) {
                        if (instruction.switchOperand.sourceSlot == ScalarSlot::None ||
                            instruction.switchOperand.upperInclusiveBounds.size() !=
                                instruction.switchCaseValues.size()) {
                            return {false, "weighted SWITCH has invalid typed thresholds"};
                        }
                        int previous = 0;
                        for (int threshold: instruction.switchOperand.upperInclusiveBounds) {
                            if (threshold <= previous) {
                                return {false, "weighted SWITCH thresholds are not strictly increasing"};
                            }
                            previous = threshold;
                        }
                    } else if (!instruction.switchOperand.upperInclusiveBounds.empty() ||
                               instruction.switchOperand.sourceSlot != ScalarSlot::None) {
                        return {false, "non-weighted SWITCH has unexpected weighted operands"};
                    }
                } else if (!instruction.switchCaseValues.empty()) {
                    return {false, "switch case values attached to a non-SWITCH opcode"};
                } else if (instruction.switchOperand.kind != SwitchSourceKind::None) {
                    return {false, "typed SWITCH source attached to a different opcode"};
                }

                if (instruction.opcode == Opcode::ResourceUpdate) {
                    const ResourceUpdateOperand &update = instruction.resourceUpdate;
                    if (update.kind == ResourceUpdateKind::None) {
                        return {false, "RESOURCE_UPDATE is missing typed operands"};
                    }
                    if (update.clampLo > update.clampHi) {
                        return {false, "RESOURCE_UPDATE has an invalid clamp interval"};
                    }
                    if (update.kind == ResourceUpdateKind::AddConstant) {
                        if (update.sourceSlot != ScalarSlot::None) {
                            return {false, "constant RESOURCE_UPDATE unexpectedly reads a scalar slot"};
                        }
                    } else if (update.sourceSlot == ScalarSlot::None) {
                        return {false, "slot-based RESOURCE_UPDATE is missing its typed source slot"};
                    }
                } else if (instruction.resourceUpdate.kind != ResourceUpdateKind::None) {
                    return {false, "typed resource operands attached to a non-RESOURCE_UPDATE opcode"};
                }

                if (instruction.opcode == Opcode::ModeUpdate) {
                    if (instruction.stateWrites.empty()) {
                        return {false, "MODE_UPDATE is missing typed writes"};
                    }
                    std::set<StateField> writtenFields;
                    for (const StateWriteOperand &write: instruction.stateWrites) {
                        if (write.kind == StateWriteKind::None || write.target == StateField::None) {
                            return {false, "MODE_UPDATE contains an incomplete typed write"};
                        }
                        if (!writtenFields.insert(write.target).second) {
                            return {false, "MODE_UPDATE writes the same typed field twice"};
                        }
                        if (write.kind == StateWriteKind::SetFromSlot) {
                            if (write.sourceSlot == ScalarSlot::None) {
                                return {false, "slot-based MODE_UPDATE is missing its source slot"};
                            }
                        } else if (write.sourceSlot != ScalarSlot::None) {
                            return {false, "MODE_UPDATE has an unexpected source slot"};
                        }
                        if (write.kind == StateWriteKind::SetLogicalTurn &&
                            write.target != StateField::LogicalTurn) {
                            return {false, "SetLogicalTurn targets the wrong typed field"};
                        }
                        if (write.kind == StateWriteKind::SetSelectedCommand &&
                            write.target != StateField::CurrentAction) {
                            return {false, "SetSelectedCommand targets the wrong typed field"};
                        }
                    }
                } else if (!instruction.stateWrites.empty()) {
                    return {false, "typed state writes attached to a non-MODE_UPDATE opcode"};
                }

                if (instruction.opcode == Opcode::Step) {
                    return {false, "untyped STEP is not permitted in registered RuleProgram"};
                }
            }

            RoutineAnalysis result;
            result.accepted = true;
            return result;
        }

        RegistrationResult validateAcyclicCallGraph(
            const RuleProgram &program,
            const CallGraph &callGraph,
            int &maximumCallDepth) {
            std::unordered_map<std::string, int> visitState;
            std::unordered_map<std::string, int> depthMemo;

            std::function < bool(const std::string &) > hasCycle = [&](const std::string &routineId) {
                if (visitState[routineId] == 1) {
                    return true;
                }
                if (visitState[routineId] == 2) {
                    return false;
                }

                visitState[routineId] = 1;
                if (const auto found = callGraph.find(routineId); found != callGraph.end()) {
                    for (const std::string &callee: found->second) {
                        if (hasCycle(callee)) {
                            return true;
                        }
                    }
                }
                visitState[routineId] = 2;
                return false;
            };

            for (const Routine &routine: program.routines) {
                if (hasCycle(routine.id)) {
                    return rejected("recursive call graph");
                }
            }

            std::function < int(const std::string &) > depth = [&](const std::string &routineId) {
                if (const auto memo = depthMemo.find(routineId); memo != depthMemo.end()) {
                    return memo->second;
                }

                int childDepth = 0;
                if (const auto found = callGraph.find(routineId); found != callGraph.end()) {
                    for (const std::string &callee: found->second) {
                        childDepth = std::max(childDepth, depth(callee));
                    }
                }

                const int result = childDepth + 1;
                depthMemo.emplace(routineId, result);
                return result;
            };

            maximumCallDepth = depth(program.entryRoutine);

            RegistrationResult result;
            result.accepted = true;
            return result;
        }

        class StaticBoundComputer {
        public:
            StaticBoundComputer(const RoutineIndex &routines, const NativeIndex &natives)
                : routines_(routines), natives_(natives) {
            }

            RegistrationResult run(const RuleProgram &program) {
                for (const Routine &routine: program.routines) {
                    if (!computeRoutine(routine.id)) {
                        return rejected(error_);
                    }
                }

                const auto turnIt = routineBounds_.find(program.entryRoutine);
                if (turnIt == routineBounds_.end() || turnIt->second.empty()) {
                    return rejected("missing computed TURN bounds");
                }

                RegistrationResult result;
                const PcStaticBounds &turn = turnIt->second.front();
                result.bounds.maximumInstructionSteps = turn.maximumInstructionSteps;
                result.bounds.maximumCallDepth = turn.maximumCallDepth;
                result.bounds.rMax = turn.maximumRngReads;
                result.bounds.registrationWork = turn.maximumWork;

                for (const Routine &routine: program.routines) {
                    RoutineStaticBounds stored;
                    stored.routineId = routine.id;
                    stored.pc = routineBounds_.at(routine.id);
                    result.bounds.routines.push_back(std::move(stored));
                }

                result.accepted = true;
                return result;
            }

        private:
            bool computeRoutine(const std::string &routineId) {
                if (routineBounds_.contains(routineId)) {
                    return true;
                }
                if (!computing_.insert(routineId).second) {
                    error_ = "recursive call graph while computing static bounds";
                    return false;
                }

                const Routine *routine = findRoutine(routines_, routineId);
                if (routine == nullptr) {
                    error_ = "missing routine while computing static bounds";
                    return false;
                }

                std::vector<PcStaticBounds> pcBounds(routine->instructions.size());
                std::vector<bool> computed(routine->instructions.size(), false);

                std::function<bool(int)> computePc = [&](int pc) {
                    if (computed[pc]) {
                        return true;
                    }

                    const Instruction &instruction = routine->instructions[pc];
                    PcStaticBounds value;

                    if (isTerminalOpcode(instruction.opcode)) {
                        value.maximumInstructionSteps = 1;
                        value.maximumWork = 1;
                    } else if (instruction.opcode == Opcode::Call) {
                        const std::string &calleeId = instruction.callTarget;
                        if (!computeRoutine(calleeId)) {
                            return false;
                        }

                        const int returnPc = instruction.successors.front();
                        if (!computePc(returnPc)) {
                            return false;
                        }

                        const PcStaticBounds &callee = routineBounds_.at(calleeId).front();
                        if (!combineCall(callee, pcBounds[returnPc], value)) {
                            return false;
                        }
                    } else {
                        PcStaticBounds continuation;
                        for (int successor: instruction.successors) {
                            if (!computePc(successor)) {
                                return false;
                            }
                            continuation.maximumInstructionSteps = std::max(
                                continuation.maximumInstructionSteps,
                                pcBounds[successor].maximumInstructionSteps);
                            continuation.maximumRngReads = std::max(
                                continuation.maximumRngReads,
                                pcBounds[successor].maximumRngReads);
                            continuation.maximumCallDepth = std::max(
                                continuation.maximumCallDepth,
                                pcBounds[successor].maximumCallDepth);
                            continuation.maximumWork = std::max(
                                continuation.maximumWork,
                                pcBounds[successor].maximumWork);
                        }

                        if (!combineOrdinary(instruction, continuation, value)) {
                            return false;
                        }
                    }

                    pcBounds[pc] = value;
                    computed[pc] = true;
                    return true;
                };

                if (!computePc(0)) {
                    return false;
                }

                routineBounds_.emplace(routineId, std::move(pcBounds));
                computing_.erase(routineId);
                return true;
            }

            bool combineCall(
                const PcStaticBounds &callee,
                const PcStaticBounds &continuation,
                PcStaticBounds &out) {
                const long long steps = 1LL + callee.maximumInstructionSteps +
                                        continuation.maximumInstructionSteps;
                const long long rng = static_cast<long long>(callee.maximumRngReads) +
                                      continuation.maximumRngReads;
                if (steps > std::numeric_limits<int>::max() || rng > std::numeric_limits<int>::max()) {
                    error_ = "static CALL bound overflow";
                    return false;
                }
                if (callee.maximumWork > std::numeric_limits<std::uint64_t>::max() - 1 ||
                    continuation.maximumWork >
                        std::numeric_limits<std::uint64_t>::max() - 1 - callee.maximumWork) {
                    error_ = "static CALL work overflow";
                    return false;
                }

                out.maximumInstructionSteps = static_cast<int>(steps);
                out.maximumRngReads = static_cast<int>(rng);
                out.maximumCallDepth = std::max(1 + callee.maximumCallDepth, continuation.maximumCallDepth);
                out.maximumWork = 1 + callee.maximumWork + continuation.maximumWork;
                return true;
            }

            bool combineOrdinary(
                const Instruction &instruction,
                const PcStaticBounds &continuation,
                PcStaticBounds &out) {
                int rngCost = 0;
                std::uint64_t internalWork = 0;

                if (instruction.opcode == Opcode::ReadRng || instruction.opcode == Opcode::SkipRng) {
                    rngCost = instruction.rngReads;
                } else if (instruction.opcode == Opcode::Native) {
                    const auto nativeIt = natives_.find(instruction.nativeId);
                    if (nativeIt == natives_.end()) {
                        error_ = "missing native contract while computing bounds";
                        return false;
                    }
                    rngCost = nativeIt->second->maxRngReads;
                    internalWork = static_cast<std::uint64_t>(nativeIt->second->maxWork);
                }

                if (continuation.maximumInstructionSteps == std::numeric_limits<int>::max() ||
                    continuation.maximumRngReads > std::numeric_limits<int>::max() - rngCost) {
                    error_ = "static instruction bound overflow";
                    return false;
                }
                if (continuation.maximumWork >
                    std::numeric_limits<std::uint64_t>::max() - 1 - internalWork) {
                    error_ = "static instruction work overflow";
                    return false;
                }

                out.maximumInstructionSteps = 1 + continuation.maximumInstructionSteps;
                out.maximumRngReads = rngCost + continuation.maximumRngReads;
                out.maximumCallDepth = continuation.maximumCallDepth;
                out.maximumWork = 1 + internalWork + continuation.maximumWork;
                return true;
            }

            const RoutineIndex &routines_;
            const NativeIndex &natives_;
            std::unordered_map<std::string, std::vector<PcStaticBounds>> routineBounds_;
            std::unordered_set<std::string> computing_;
            std::string error_;
        };

        class RuleRoutineBuilder {
        public:
            explicit RuleRoutineBuilder(std::string id)
                : id_(std::move(id)) {
            }

            int add(Opcode opcode, std::string label, int rngReads = 0, int nativeWork = 1) {
                const int index = static_cast<int>(instructions_.size());
                instructions_.push_back({opcode, std::move(label), {}, rngReads, nativeWork});
                return index;
            }

            int addResourceUpdate(std::string label, ResourceUpdateOperand operand) {
                const int index = add(Opcode::ResourceUpdate, std::move(label));
                instructions_.at(index).resourceUpdate = operand;
                return index;
            }

            int addStateUpdate(std::string label, std::vector<StateWriteOperand> writes) {
                const int index = add(Opcode::ModeUpdate, std::move(label));
                instructions_.at(index).stateWrites = std::move(writes);
                return index;
            }

            int addNative(
                std::string label,
                ScalarSlot resultSlot,
                std::vector<double> arguments) {
                const int index = add(Opcode::Native, std::move(label));
                instructions_.at(index).scalarResult = resultSlot;
                instructions_.at(index).nativeArguments = std::move(arguments);
                const std::string &storedLabel = instructions_.at(index).label;
                if (storedLabel.starts_with(kNativePrefix)) {
                    instructions_.at(index).nativeId = storedLabel.substr(kNativePrefix.size());
                }
                return index;
            }

            int addCall(std::string label, std::string target) {
                const int index = add(Opcode::Call, std::move(label));
                instructions_.at(index).callTarget = std::move(target);
                return index;
            }

            int addRngRead(std::string label, RngReadOperand operand) {
                const int index = add(Opcode::ReadRng, std::move(label), 1);
                instructions_.at(index).rngRead = operand;
                return index;
            }

            int addScalarUpdate(std::string label, ScalarUpdateOperand operand) {
                const int index = add(Opcode::ScalarUpdate, std::move(label));
                instructions_.at(index).scalarUpdate = operand;
                return index;
            }

            int addBranch(std::string label, BranchCondition condition) {
                const int index = add(Opcode::Branch, std::move(label));
                instructions_.at(index).branchCondition = std::move(condition);
                return index;
            }

            void next(int from, int to) {
                instructions_.at(from).successors = {to};
            }

            void branch(int from, int truePc, int falsePc) {
                instructions_.at(from).successors = {truePc, falsePc};
            }

            void cases(int from, std::vector<int> values, std::vector<int> successors) {
                instructions_.at(from).switchCaseValues = std::move(values);
                instructions_.at(from).successors = std::move(successors);
            }

            void switchSource(int instructionIndex, SwitchOperand operand) {
                instructions_.at(instructionIndex).switchOperand = std::move(operand);
            }

            void initialize(ScalarSlot slot, double value) {
                initializers_.push_back({slot, value});
            }

            Routine finish(bool turnRoutine = false) && {
                return {
                    std::move(id_),
                    turnRoutine,
                    std::move(instructions_),
                    std::move(initializers_),
                };
            }

        private:
            std::string id_;
            std::vector<Instruction> instructions_;
            std::vector<ScalarInitializer> initializers_;
        };

        bool isTurnTransientScalar(ScalarSlot slot) noexcept {
            switch (slot) {
                case ScalarSlot::DefenceHalfFlag:
                case ScalarSlot::PreemptiveFlag:
                case ScalarSlot::ActionSlot0:
                case ScalarSlot::ActionSlot1:
                case ScalarSlot::ActionCount:
                    return true;
                default:
                    return false;
            }
        }

        ScalarType scalarInterfaceType(ScalarSlot slot) {
            switch (slot) {
                case ScalarSlot::Damage:
                case ScalarSlot::Amount:
                    return ScalarType::Integer;
                case ScalarSlot::Random0:
                case ScalarSlot::Random1:
                case ScalarSlot::SpeedHero:
                case ScalarSlot::SpeedEnemy:
                    return ScalarType::Real;
                case ScalarSlot::Duration:
                case ScalarSlot::ActionCount:
                    return ScalarType::Integer;
                case ScalarSlot::CriticalFlag:
                case ScalarSlot::DodgeFlag:
                case ScalarSlot::ShieldFlag:
                case ScalarSlot::DefenceHalfFlag:
                case ScalarSlot::PreemptiveFlag:
                    return ScalarType::Boolean;
                case ScalarSlot::ActionSlot0:
                case ScalarSlot::ActionSlot1:
                    return ScalarType::Action;
                case ScalarSlot::None:
                case ScalarSlot::Count:
                    break;
            }
            return ScalarType::Real;
        }

        struct EffectSets {
            std::set<ResourceAxis> resourceReads;
            std::set<ResourceAxis> resourceWrites;
            std::set<StateField> stateReads;
            std::set<StateField> stateWrites;
            std::set<ScalarSlot> transientReads;
            std::set<ScalarSlot> transientWrites;
        };

        void mergeEffects(EffectSets &target, const EffectSets &source) {
            target.resourceReads.insert(source.resourceReads.begin(), source.resourceReads.end());
            target.resourceWrites.insert(source.resourceWrites.begin(), source.resourceWrites.end());
            target.stateReads.insert(source.stateReads.begin(), source.stateReads.end());
            target.stateWrites.insert(source.stateWrites.begin(), source.stateWrites.end());
            target.transientReads.insert(source.transientReads.begin(), source.transientReads.end());
            target.transientWrites.insert(source.transientWrites.begin(), source.transientWrites.end());
        }

        void addScalarEffectRead(EffectSets &effects, ScalarSlot slot) {
            if (slot != ScalarSlot::None && isTurnTransientScalar(slot)) {
                effects.transientReads.insert(slot);
            }
        }

        void addScalarEffectWrite(EffectSets &effects, ScalarSlot slot) {
            if (slot != ScalarSlot::None && isTurnTransientScalar(slot)) {
                effects.transientWrites.insert(slot);
            }
        }

        void addValueRefEffects(EffectSets &effects, const ValueRef &ref) {
            switch (ref.kind) {
                case ValueRefKind::Resource:
                    effects.resourceReads.insert(ref.resource);
                    break;
                case ValueRefKind::StateField:
                case ValueRefKind::StateIndexedLookup:
                    effects.stateReads.insert(ref.state);
                    break;
                case ValueRefKind::Scalar:
                case ValueRefKind::FixedSourceTimesScalar:
                    addScalarEffectRead(effects, ref.scalar);
                    break;
                case ValueRefKind::ResourceMinusScalar:
                    effects.resourceReads.insert(ref.resource);
                    addScalarEffectRead(effects, ref.scalar);
                    break;
                case ValueRefKind::Constant:
                    break;
            }
        }

        EffectSets directInstructionEffects(const Instruction &instruction) {
            EffectSets effects;
            if (instruction.opcode == Opcode::Branch) {
                for (const ConditionClause &conditionClause: instruction.branchCondition.any) {
                    for (const Comparison &item: conditionClause.all) {
                        addValueRefEffects(effects, item.left);
                        addValueRefEffects(effects, item.right);
                    }
                }
            }
            if (instruction.opcode == Opcode::Switch) {
                switch (instruction.switchOperand.kind) {
                    case SwitchSourceKind::CurrentAction:
                        effects.stateReads.insert(StateField::CurrentAction);
                        break;
                    case SwitchSourceKind::ActionHistorySlot0:
                        effects.transientReads.insert(ScalarSlot::ActionSlot0);
                        break;
                    case SwitchSourceKind::ActionHistorySlot1:
                        effects.transientReads.insert(ScalarSlot::ActionSlot1);
                        break;
                    case SwitchSourceKind::WeightedScalarUpperBounds:
                        addScalarEffectRead(effects, instruction.switchOperand.sourceSlot);
                        break;
                    case SwitchSourceKind::None:
                        break;
                }
            }
            if (instruction.opcode == Opcode::ResourceUpdate) {
                effects.resourceReads.insert(instruction.resourceUpdate.target);
                effects.resourceWrites.insert(instruction.resourceUpdate.target);
                addScalarEffectRead(effects, instruction.resourceUpdate.sourceSlot);
            }
            if (instruction.opcode == Opcode::ModeUpdate) {
                for (const StateWriteOperand &write: instruction.stateWrites) {
                    if (write.kind == StateWriteKind::Increment || write.kind == StateWriteKind::Decrement) {
                        effects.stateReads.insert(write.target);
                    }
                    effects.stateWrites.insert(write.target);
                    addScalarEffectRead(effects, write.sourceSlot);
                }
            }
            if (instruction.opcode == Opcode::ScalarUpdate) {
                const ScalarUpdateOperand &update = instruction.scalarUpdate;
                if (update.kind == ScalarUpdateKind::MultiplyBySlot ||
                    update.kind == ScalarUpdateKind::MultiplyRational ||
                    update.kind == ScalarUpdateKind::MultiplyByDefenceFlag) {
                    addScalarEffectRead(effects, update.targetSlot);
                }
                if (update.kind == ScalarUpdateKind::MultiplyBySlot ||
                    update.kind == ScalarUpdateKind::SetFixedSourceTimesSlot) {
                    addScalarEffectRead(effects, update.sourceSlot);
                }
                if (update.kind == ScalarUpdateKind::MultiplyByDefenceFlag) {
                    effects.transientReads.insert(ScalarSlot::DefenceHalfFlag);
                }
                addScalarEffectWrite(effects, update.targetSlot);
            }
            if (instruction.opcode == Opcode::ReadRng) {
                if (instruction.rngRead.kind == RngReadKind::PercentCameraRemaining) {
                    effects.stateReads.insert(StateField::Camera);
                }
                addScalarEffectWrite(effects, instruction.rngRead.resultSlot);
            }
            if (instruction.opcode == Opcode::Native) {
                addScalarEffectWrite(effects, instruction.scalarResult);
            }
            if (instruction.opcode == Opcode::Call) {
                for (ScalarSlot argument: instruction.call.arguments) {
                    addScalarEffectRead(effects, argument);
                }
                addScalarEffectWrite(effects, instruction.call.resultSlot);
            }
            if (instruction.opcode == Opcode::RecordAction) {
                effects.stateReads.insert(StateField::CurrentAction);
                effects.transientReads.insert(ScalarSlot::ActionCount);
                effects.transientReads.insert(ScalarSlot::ActionSlot0);
                effects.transientReads.insert(ScalarSlot::ActionSlot1);
                effects.transientWrites.insert(ScalarSlot::ActionCount);
                effects.transientWrites.insert(ScalarSlot::ActionSlot0);
                effects.transientWrites.insert(ScalarSlot::ActionSlot1);
            }
            return effects;
        }

        RoutineEffects materializeEffects(const EffectSets &sets) {
            RoutineEffects effects;
            effects.resourceReads.assign(sets.resourceReads.begin(), sets.resourceReads.end());
            effects.resourceWrites.assign(sets.resourceWrites.begin(), sets.resourceWrites.end());
            effects.stateReads.assign(sets.stateReads.begin(), sets.stateReads.end());
            effects.stateWrites.assign(sets.stateWrites.begin(), sets.stateWrites.end());
            effects.transientReads.assign(sets.transientReads.begin(), sets.transientReads.end());
            effects.transientWrites.assign(sets.transientWrites.begin(), sets.transientWrites.end());
            return effects;
        }

        bool computeRoutineEffectMap(
            const RuleProgram &program,
            std::unordered_map<std::string, RoutineEffects> &out,
            std::string &error) {
            std::unordered_map<std::string, const Routine *> routines;
            for (const Routine &routine: program.routines) {
                routines.emplace(routine.id, &routine);
            }
            std::set<std::string> visiting;
            std::function<bool(const std::string &)> compute = [&](const std::string &id) {
                if (out.contains(id)) {
                    return true;
                }
                if (!visiting.insert(id).second) {
                    error = "recursive call graph while computing declared effects";
                    return false;
                }
                const auto found = routines.find(id);
                if (found == routines.end()) {
                    error = "missing routine while computing declared effects";
                    return false;
                }
                EffectSets effects;
                for (const ScalarInitializer &initializer: found->second->localInitializers) {
                    addScalarEffectWrite(effects, initializer.slot);
                }
                for (const Instruction &instruction: found->second->instructions) {
                    mergeEffects(effects, directInstructionEffects(instruction));
                    if (instruction.opcode == Opcode::Call) {
                        if (!compute(instruction.callTarget)) {
                            return false;
                        }
                        const RoutineEffects &callee = out.at(instruction.callTarget);
                        EffectSets calleeSets;
                        calleeSets.resourceReads.insert(callee.resourceReads.begin(), callee.resourceReads.end());
                        calleeSets.resourceWrites.insert(callee.resourceWrites.begin(), callee.resourceWrites.end());
                        calleeSets.stateReads.insert(callee.stateReads.begin(), callee.stateReads.end());
                        calleeSets.stateWrites.insert(callee.stateWrites.begin(), callee.stateWrites.end());
                        calleeSets.transientReads.insert(callee.transientReads.begin(), callee.transientReads.end());
                        calleeSets.transientWrites.insert(callee.transientWrites.begin(), callee.transientWrites.end());
                        mergeEffects(effects, calleeSets);
                    }
                }
                out.emplace(id, materializeEffects(effects));
                visiting.erase(id);
                return true;
            };
            for (const Routine &routine: program.routines) {
                if (!compute(routine.id)) {
                    return false;
                }
            }
            return true;
        }

        bool hasId(const std::set<std::string_view> &ids, const std::string &id) {
            return ids.contains(id);
        }

        bool configureYo2RoutineContracts(RuleProgram &program, std::string &error) {
            const std::set<std::string_view> damageResults = {
                "ally-action", "enemy-action", "ally-attack", "ally-dragon-slash",
                "ally-defence", "ally-herb", "ally-heal", "ally-crack", "ally-acro",
                "ally-paralysis", "ally-cure-paralysis", "ally-inactive",
                "enemy-victimiser", "enemy-hp-hoover", "enemy-crack", "enemy-attack",
                "enemy-manazashi", "enemy-puff-puff", "enemy-inactive", "acro-dodge", "counter",
            };
            const std::set<std::string_view> damageParameters = {
                "rage-crossing", "process7a8", "apply-ally-result", "apply-enemy-result",
            };
            std::unordered_map<std::string, Routine *> routines;
            for (Routine &routine: program.routines) {
                routines.emplace(routine.id, &routine);
                if (hasId(damageParameters, routine.id)) {
                    routine.parameters = {{ScalarSlot::Damage, scalarInterfaceType(ScalarSlot::Damage)}};
                }
                if (hasId(damageResults, routine.id)) {
                    routine.result = {true, ScalarSlot::Damage, scalarInterfaceType(ScalarSlot::Damage)};
                }
            }
            for (Routine &routine: program.routines) {
                for (Instruction &instruction: routine.instructions) {
                    if (instruction.opcode != Opcode::Call) {
                        continue;
                    }
                    const auto calleeIt = routines.find(instruction.callTarget);
                    if (calleeIt == routines.end()) {
                        error = "assembler cannot bind typed CALL to missing routine";
                        return false;
                    }
                    instruction.call.arguments.clear();
                    for (const RoutineParameter &parameter: calleeIt->second->parameters) {
                        instruction.call.arguments.push_back(parameter.slot);
                    }
                    instruction.call.resultSlot = calleeIt->second->result.present
                        ? calleeIt->second->result.slot
                        : ScalarSlot::None;
                }
            }
            std::unordered_map<std::string, RoutineEffects> effects;
            if (!computeRoutineEffectMap(program, effects, error)) {
                return false;
            }
            for (Routine &routine: program.routines) {
                routine.declaredEffects = effects.at(routine.id);
            }
            return true;
        }

        void addValueRefScalarReads(std::set<ScalarSlot> &reads, const ValueRef &ref) {
            switch (ref.kind) {
                case ValueRefKind::Scalar:
                case ValueRefKind::ResourceMinusScalar:
                case ValueRefKind::FixedSourceTimesScalar:
                    if (ref.scalar != ScalarSlot::None) {
                        reads.insert(ref.scalar);
                    }
                    break;
                default:
                    break;
            }
        }

        void directScalarUse(
            const Instruction &instruction,
            std::set<ScalarSlot> &reads,
            std::set<ScalarSlot> &writes) {
            if (instruction.opcode == Opcode::Branch) {
                for (const ConditionClause &conditionClause: instruction.branchCondition.any) {
                    for (const Comparison &item: conditionClause.all) {
                        addValueRefScalarReads(reads, item.left);
                        addValueRefScalarReads(reads, item.right);
                    }
                }
            }
            if (instruction.opcode == Opcode::Switch) {
                switch (instruction.switchOperand.kind) {
                    case SwitchSourceKind::WeightedScalarUpperBounds:
                        if (instruction.switchOperand.sourceSlot != ScalarSlot::None) {
                            reads.insert(instruction.switchOperand.sourceSlot);
                        }
                        break;
                    case SwitchSourceKind::ActionHistorySlot0:
                        reads.insert(ScalarSlot::ActionSlot0);
                        break;
                    case SwitchSourceKind::ActionHistorySlot1:
                        reads.insert(ScalarSlot::ActionSlot1);
                        break;
                    default:
                        break;
                }
            }
            if (instruction.opcode == Opcode::ResourceUpdate &&
                instruction.resourceUpdate.sourceSlot != ScalarSlot::None) {
                reads.insert(instruction.resourceUpdate.sourceSlot);
            }
            if (instruction.opcode == Opcode::ModeUpdate) {
                for (const StateWriteOperand &write: instruction.stateWrites) {
                    if (write.sourceSlot != ScalarSlot::None) {
                        reads.insert(write.sourceSlot);
                    }
                }
            }
            if (instruction.opcode == Opcode::ScalarUpdate) {
                const ScalarUpdateOperand &update = instruction.scalarUpdate;
                if (update.kind == ScalarUpdateKind::MultiplyBySlot ||
                    update.kind == ScalarUpdateKind::MultiplyRational ||
                    update.kind == ScalarUpdateKind::MultiplyByDefenceFlag) {
                    reads.insert(update.targetSlot);
                }
                if (update.kind == ScalarUpdateKind::MultiplyBySlot ||
                    update.kind == ScalarUpdateKind::SetFixedSourceTimesSlot) {
                    reads.insert(update.sourceSlot);
                }
                if (update.kind == ScalarUpdateKind::MultiplyByDefenceFlag) {
                    reads.insert(ScalarSlot::DefenceHalfFlag);
                }
                writes.insert(update.targetSlot);
            }
            if (instruction.opcode == Opcode::ReadRng) {
                writes.insert(instruction.rngRead.resultSlot);
            }
            if (instruction.opcode == Opcode::Native) {
                writes.insert(instruction.scalarResult);
            }
            if (instruction.opcode == Opcode::Call) {
                for (ScalarSlot argument: instruction.call.arguments) {
                    reads.insert(argument);
                }
                if (instruction.call.resultSlot != ScalarSlot::None) {
                    writes.insert(instruction.call.resultSlot);
                }
            }
            if (instruction.opcode == Opcode::RecordAction) {
                reads.insert(ScalarSlot::ActionCount);
                reads.insert(ScalarSlot::ActionSlot0);
                reads.insert(ScalarSlot::ActionSlot1);
                writes.insert(ScalarSlot::ActionCount);
                writes.insert(ScalarSlot::ActionSlot0);
                writes.insert(ScalarSlot::ActionSlot1);
            }
            reads.erase(ScalarSlot::None);
            writes.erase(ScalarSlot::None);
        }

        bool canonicalEffectVector(const std::vector<ResourceAxis> &values) {
            return std::is_sorted(values.begin(), values.end()) &&
                   std::adjacent_find(values.begin(), values.end()) == values.end();
        }

        bool canonicalEffectVector(const std::vector<StateField> &values) {
            return std::is_sorted(values.begin(), values.end()) &&
                   std::adjacent_find(values.begin(), values.end()) == values.end() &&
                   std::none_of(values.begin(), values.end(), [](StateField value) {
                       return value == StateField::None || value == StateField::Count;
                   });
        }

        bool canonicalEffectVector(const std::vector<ScalarSlot> &values) {
            return std::is_sorted(values.begin(), values.end()) &&
                   std::adjacent_find(values.begin(), values.end()) == values.end() &&
                   std::all_of(values.begin(), values.end(), [](ScalarSlot value) {
                       return value != ScalarSlot::None && value != ScalarSlot::Count &&
                              isTurnTransientScalar(value);
                   });
        }

        struct ScalarContractSummary {
            std::set<ScalarSlot> requiredTransient;
        };

        RegistrationResult validateRoutineContracts(
            const RuleProgram &program,
            const RoutineIndex &routineIndex) {
            std::unordered_map<std::string, RoutineEffects> computedEffects;
            std::string effectError;
            if (!computeRoutineEffectMap(program, computedEffects, effectError)) {
                return rejected(effectError);
            }

            for (const Routine &routine: program.routines) {
                std::set<ScalarSlot> parameterSlots;
                for (const RoutineParameter &parameter: routine.parameters) {
                    if (parameter.slot == ScalarSlot::None || parameter.slot == ScalarSlot::Count ||
                        isTurnTransientScalar(parameter.slot) ||
                        parameter.type != scalarInterfaceType(parameter.slot) ||
                        !parameterSlots.insert(parameter.slot).second) {
                        return rejected("routine has an invalid or duplicate typed parameter");
                    }
                }
                if (routine.result.present) {
                    if (routine.result.slot == ScalarSlot::None || routine.result.slot == ScalarSlot::Count ||
                        isTurnTransientScalar(routine.result.slot) ||
                        routine.result.type != scalarInterfaceType(routine.result.slot)) {
                        return rejected("routine has an invalid typed result");
                    }
                } else if (routine.result.slot != ScalarSlot::None) {
                    return rejected("void routine carries a result slot");
                }
                for (const ScalarInitializer &initializer: routine.localInitializers) {
                    if (!isTurnTransientScalar(initializer.slot) && parameterSlots.contains(initializer.slot)) {
                        return rejected("routine initializer overwrites a typed parameter at entry");
                    }
                }

                const RoutineEffects &declared = routine.declaredEffects;
                if (!canonicalEffectVector(declared.resourceReads) ||
                    !canonicalEffectVector(declared.resourceWrites) ||
                    !canonicalEffectVector(declared.stateReads) ||
                    !canonicalEffectVector(declared.stateWrites) ||
                    !canonicalEffectVector(declared.transientReads) ||
                    !canonicalEffectVector(declared.transientWrites)) {
                    return rejected("routine has a non-canonical declared read/write set");
                }
                if (declared != computedEffects.at(routine.id)) {
                    return rejected("routine declared reads/writes differ from kernel recomputation");
                }

                for (const Instruction &instruction: routine.instructions) {
                    if (instruction.opcode == Opcode::Call) {
                        const Routine *callee = findRoutine(routineIndex, instruction.callTarget);
                        if (callee == nullptr || instruction.call.arguments.size() != callee->parameters.size()) {
                            return rejected("CALL typed argument count does not match callee signature");
                        }
                        for (std::size_t index = 0; index < callee->parameters.size(); ++index) {
                            const ScalarSlot argument = instruction.call.arguments[index];
                            if (argument == ScalarSlot::None || argument == ScalarSlot::Count ||
                                scalarInterfaceType(argument) != callee->parameters[index].type) {
                                return rejected("CALL typed argument does not match callee parameter type");
                            }
                        }
                        if (callee->result.present) {
                            if (instruction.call.resultSlot == ScalarSlot::None ||
                                instruction.call.resultSlot == ScalarSlot::Count ||
                                scalarInterfaceType(instruction.call.resultSlot) != callee->result.type) {
                                return rejected("CALL result slot does not match callee result type");
                            }
                        } else if (instruction.call.resultSlot != ScalarSlot::None) {
                            return rejected("CALL stores a result from a void callee");
                        }
                    } else if (!instruction.call.arguments.empty() ||
                               instruction.call.resultSlot != ScalarSlot::None) {
                        return rejected("typed CALL operands attached to a non-CALL opcode");
                    }
                }
            }

            std::unordered_map<std::string, ScalarContractSummary> summaries;
            std::set<std::string> visiting;
            std::function<RegistrationResult(const std::string &)> analyze = [&](const std::string &routineId) {
                if (summaries.contains(routineId)) {
                    RegistrationResult ok;
                    ok.accepted = true;
                    return ok;
                }
                if (!visiting.insert(routineId).second) {
                    return rejected("recursive call graph while checking scalar contracts");
                }
                const Routine *routine = findRoutine(routineIndex, routineId);
                if (routine == nullptr) {
                    return rejected("missing routine while checking scalar contracts");
                }
                for (const Instruction &instruction: routine->instructions) {
                    if (instruction.opcode == Opcode::Call) {
                        RegistrationResult child = analyze(instruction.callTarget);
                        if (!child.accepted) {
                            return child;
                        }
                    }
                }

                const std::size_t n = routine->instructions.size();
                std::vector<std::vector<int>> predecessors(n);
                std::vector<int> indegree(n, 0);
                for (std::size_t pc = 0; pc < n; ++pc) {
                    for (int successor: routine->instructions[pc].successors) {
                        predecessors[successor].push_back(static_cast<int>(pc));
                        ++indegree[successor];
                    }
                }
                std::vector<int> queue;
                queue.reserve(n);
                for (std::size_t pc = 0; pc < n; ++pc) {
                    if (indegree[pc] == 0) {
                        queue.push_back(static_cast<int>(pc));
                    }
                }
                std::vector<int> order;
                for (std::size_t head = 0; head < queue.size(); ++head) {
                    const int pc = queue[head];
                    order.push_back(pc);
                    for (int successor: routine->instructions[pc].successors) {
                        if (--indegree[successor] == 0) {
                            queue.push_back(successor);
                        }
                    }
                }
                if (order.size() != n || order.front() != 0) {
                    return rejected("routine scalar analysis requires one reachable acyclic entry");
                }

                std::set<ScalarSlot> requiredTransient;
                for (;;) {
                    std::vector<std::set<ScalarSlot>> out(n);
                    std::set<ScalarSlot> entryDefined = requiredTransient;
                    for (const RoutineParameter &parameter: routine->parameters) {
                        entryDefined.insert(parameter.slot);
                    }
                    for (const ScalarInitializer &initializer: routine->localInitializers) {
                        entryDefined.insert(initializer.slot);
                    }
                    std::set<ScalarSlot> newlyRequired;
                    for (int pc: order) {
                        std::set<ScalarSlot> defined;
                        if (pc == 0) {
                            defined = entryDefined;
                        } else {
                            bool first = true;
                            for (int predecessor: predecessors[pc]) {
                                if (first) {
                                    defined = out[predecessor];
                                    first = false;
                                } else {
                                    std::set<ScalarSlot> intersection;
                                    std::set_intersection(
                                        defined.begin(), defined.end(),
                                        out[predecessor].begin(), out[predecessor].end(),
                                        std::inserter(intersection, intersection.end()));
                                    defined = std::move(intersection);
                                }
                            }
                        }

                        std::set<ScalarSlot> reads;
                        std::set<ScalarSlot> writes;
                        const Instruction &instruction = routine->instructions[pc];
                        directScalarUse(instruction, reads, writes);
                        if (instruction.opcode == Opcode::Call) {
                            const ScalarContractSummary &callee = summaries.at(instruction.callTarget);
                            reads.insert(callee.requiredTransient.begin(), callee.requiredTransient.end());
                            const Routine *calleeRoutine = findRoutine(routineIndex, instruction.callTarget);
                            writes.insert(
                                calleeRoutine->declaredEffects.transientWrites.begin(),
                                calleeRoutine->declaredEffects.transientWrites.end());
                        }
                        for (ScalarSlot read: reads) {
                            if (defined.contains(read)) {
                                continue;
                            }
                            if (isTurnTransientScalar(read)) {
                                newlyRequired.insert(read);
                                defined.insert(read);
                            } else {
                                return rejected("routine reads an uninitialized local scalar slot");
                            }
                        }
                        defined.insert(writes.begin(), writes.end());
                        out[pc] = std::move(defined);

                        if (instruction.opcode == Opcode::Return && routine->result.present &&
                            !out[pc].contains(routine->result.slot)) {
                            return rejected("routine RETURN does not define its typed result on every path");
                        }
                    }
                    bool changed = false;
                    for (ScalarSlot slot: newlyRequired) {
                        changed |= requiredTransient.insert(slot).second;
                    }
                    if (!changed) {
                        break;
                    }
                }

                if (routine->turnRoutine && !requiredTransient.empty()) {
                    return rejected("TURN entry requires an uninitialized shared transient slot");
                }
                summaries.emplace(routineId, ScalarContractSummary{std::move(requiredTransient)});
                visiting.erase(routineId);
                RegistrationResult ok;
                ok.accepted = true;
                return ok;
            };

            for (const Routine &routine: program.routines) {
                RegistrationResult checked = analyze(routine.id);
                if (!checked.accepted) {
                    return checked;
                }
            }

            RegistrationResult result;
            result.accepted = true;
            return result;
        }

        Routine makeTurnRoutine() {
            RuleRoutineBuilder b("turn");
            b.initialize(ScalarSlot::ActionSlot0, 0.0);
            b.initialize(ScalarSlot::ActionSlot1, 0.0);
            b.initialize(ScalarSlot::ActionCount, 0.0);
            b.initialize(ScalarSlot::DefenceHalfFlag, 0.0);
            const int turnCounter = b.addStateUpdate(
                "turn:NowState turn field=current logical turn",
                {{StateWriteKind::SetLogicalTurn, StateField::LogicalTurn, ScalarSlot::None, 0}});
            const int chargeDecrement = b.addStateUpdate(
                "turn:specialChargeTurn-- raw",
                {{StateWriteKind::Decrement, StateField::RawChargeTimer, ScalarSlot::None, 0}});
            const int chargeExpired = b.addBranch(
                "turn:specialChargeTurn==-1",
                allOf({comparison(stateValue(StateField::RawChargeTimer), CompareOp::Eq, constantValue(-1))}));
            const int clearCharge = b.addStateUpdate(
                "turn:Charge=OFF",
                {{StateWriteKind::SetConstant, StateField::Charge, ScalarSlot::None, 0}});
            const int defenceReset = b.addScalarUpdate(
                "turn:hero.defence=1.0",
                {ScalarUpdateKind::SetConstant, ScalarSlot::DefenceHalfFlag, ScalarSlot::None,
                 FixedScalarSource::None, 0, 0, 1});
            const int prepare = b.addStateUpdate(
                "turn:prepared_command=selected command",
                {{StateWriteKind::SetSelectedCommand, StateField::CurrentAction, ScalarSlot::None, 0}});
            const int defenceStatus = b.addBranch(
                "turn:prepared DEFENCE and (Paralysis!=CLEAR or Inactive==ON)",
                anyOf({
                    clause({
                        comparison(stateValue(StateField::CurrentAction), CompareOp::Eq,
                                   constantValue(BattleEmulator::DEFENCE)),
                        comparison(stateValue(StateField::Paralysis), CompareOp::Ne, constantValue(0)),
                    }),
                    clause({
                        comparison(stateValue(StateField::CurrentAction), CompareOp::Eq,
                                   constantValue(BattleEmulator::DEFENCE)),
                        comparison(stateValue(StateField::Inactive), CompareOp::Eq, constantValue(1)),
                    }),
                }));
            const int replaceDefence = b.addStateUpdate(
                "turn:prepared DEFENCE->ATTACK_ALLY",
                {{StateWriteKind::SetConstant, StateField::CurrentAction, ScalarSlot::None,
                  BattleEmulator::ATTACK_ALLY}});
            const int defence = b.addBranch(
                "turn:prepared command==DEFENCE",
                allOf({comparison(stateValue(StateField::CurrentAction), CompareOp::Eq,
                                  constantValue(BattleEmulator::DEFENCE))}));
            const int halfDefence = b.addScalarUpdate(
                "turn:hero.defence=0.5",
                {ScalarUpdateKind::SetConstant, ScalarSlot::DefenceHalfFlag, ScalarSlot::None,
                 FixedScalarSource::None, 1, 0, 1});
            const int speedHero = b.addRngRead(
                "turn:hero speed floatRand(0.51,1.0)",
                {RngReadKind::FloatRange, ScalarSlot::SpeedHero, 0, 0, 0.51, 1.0});
            const int speedEnemy = b.addRngRead(
                "turn:enemy speed floatRand(0.51,1.0)",
                {RngReadKind::FloatRange, ScalarSlot::SpeedEnemy, 0, 0, 0.51, 1.0});
            const int initiativeExtra = b.add(Opcode::SkipRng, "turn:initiative-extra", 1);
            const int initiative = b.addBranch(
                "turn:hero speed>enemy speed",
                allOf({comparison(
                    fixedSourceTimesScalar(FixedScalarSource::HeroSpeed, ScalarSlot::SpeedHero),
                    CompareOp::Gt,
                    fixedSourceTimesScalar(FixedScalarSource::EnemySpeed, ScalarSlot::SpeedEnemy))}));
            const int allyFirst = b.addCall("call:ally-slot", "ally-slot");
            const int enemyAfterAlly = b.addCall("call:enemy-slot", "enemy-slot");
            const int enemyFirst = b.addCall("call:enemy-slot", "enemy-slot");
            const int allyAfterEnemy = b.addCall("call:ally-slot", "ally-slot");
            const int tail = b.addCall("call:turn-tail", "turn-tail");
            const int finish = b.add(Opcode::Finish, "turn:finish");

            b.next(turnCounter, chargeDecrement);
            b.next(chargeDecrement, chargeExpired);
            b.branch(chargeExpired, clearCharge, defenceReset);
            b.next(clearCharge, defenceReset);
            b.next(defenceReset, speedHero);
            b.next(speedHero, speedEnemy);
            b.next(speedEnemy, initiativeExtra);
            b.next(initiativeExtra, prepare);
            b.next(prepare, defenceStatus);
            b.branch(defenceStatus, replaceDefence, defence);
            b.next(replaceDefence, defence);
            b.branch(defence, halfDefence, initiative);
            b.next(halfDefence, initiative);
            b.branch(initiative, allyFirst, enemyFirst);
            b.next(allyFirst, enemyAfterAlly);
            b.next(enemyAfterAlly, tail);
            b.next(enemyFirst, allyAfterEnemy);
            b.next(allyAfterEnemy, tail);
            b.next(tail, finish);
            return std::move(b).finish(true);
        }

        Routine makeAllySlotRoutine() {
            RuleRoutineBuilder b("ally-slot");
            const int alive = b.addBranch(
                "ally-slot:both players alive",
                allOf({
                    comparison(resourceValue(ResourceAxis::HeroHp), CompareOp::Gt, constantValue(0)),
                    comparison(resourceValue(ResourceAxis::EnemyHp), CompareOp::Gt, constantValue(0)),
                }));
            const int flee = b.addBranch(
                "ally-slot:FLEE_ALLY and Paralysis==CLEAR and Inactive==OFF",
                allOf({
                    comparison(stateValue(StateField::CurrentAction), CompareOp::Eq,
                               constantValue(BattleEmulator::FLEE_ALLY)),
                    comparison(stateValue(StateField::Paralysis), CompareOp::Eq, constantValue(0)),
                    comparison(stateValue(StateField::Inactive), CompareOp::Eq, constantValue(0)),
                }));
            const int status = b.addCall("call:ally-status", "ally-status");
            const int action = b.addCall("call:ally-action", "ally-action");
            const int record = b.add(Opcode::RecordAction, "ally-slot:append executed action");
            const int apply = b.addCall("call:apply-ally-result", "apply-ally-result");
            const int post = b.addCall("call:ally-post", "ally-post");
            const int ret = b.add(Opcode::Return, "ally-slot:return");
            b.branch(alive, flee, ret);
            b.branch(flee, ret, status);
            b.next(status, action);
            b.next(action, record);
            b.next(record, apply);
            b.next(apply, post);
            b.next(post, ret);
            return std::move(b).finish();
        }

        Routine makeEnemySlotRoutine() {
            RuleRoutineBuilder b("enemy-slot");
            const int alive = b.addBranch(
                "enemy-slot:both players alive",
                allOf({
                    comparison(resourceValue(ResourceAxis::HeroHp), CompareOp::Gt, constantValue(0)),
                    comparison(resourceValue(ResourceAxis::EnemyHp), CompareOp::Gt, constantValue(0)),
                }));
            const int select = b.addCall("call:select-enemy", "select-enemy");
            const int action = b.addCall("call:enemy-action", "enemy-action");
            const int record = b.add(Opcode::RecordAction, "enemy-slot:append executed action");
            const int apply = b.addCall("call:apply-enemy-result", "apply-enemy-result");
            const int post = b.addCall("call:enemy-post", "enemy-post");
            const int ret = b.add(Opcode::Return, "enemy-slot:return");
            b.branch(alive, select, ret);
            b.next(select, action);
            b.next(action, record);
            b.next(record, apply);
            b.next(apply, post);
            b.next(post, ret);
            return std::move(b).finish();
        }

        Routine makeAllyStatusRoutine() {
            RuleRoutineBuilder b("ally-status");
            const int paralysis = b.addBranch(
                "ally-status:Paralysis!=CLEAR",
                allOf({comparison(stateValue(StateField::Paralysis), CompareOp::Ne, constantValue(0))}));
            const int clearSkip = b.add(Opcode::SkipRng, "ally-status:clear-paralysis-skip", 1);
            const int decrement = b.addStateUpdate(
                "ally-status:paralysisTurns-- raw",
                {{StateWriteKind::Decrement, StateField::RawParalysisTimer, ScalarSlot::None, 0}});
            const int releaseWindow = b.addBranch(
                "ally-status:paralysisTurns<=0",
                allOf({comparison(stateValue(StateField::RawParalysisTimer), CompareOp::Le, constantValue(0))}));
            const int releaseRoll = b.addRngRead(
                "ally-status:paralysis-release-percent",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 100, 0.0, 0.0});
            const int released = b.addBranch(
                "ally-status:release threshold satisfied",
                allOf({comparison(
                    scalarValue(ScalarSlot::Random0),
                    CompareOp::Le,
                    stateIndexedLookup(
                        StateField::RawParalysisTimer,
                        {0, -1, -2, -3},
                        {62, 75, 87, 99}))}));
            const int clearParalysis = b.addStateUpdate(
                "ally-status:Paralysis=CLEAR, level=0, action=CURE_PARALYSIS",
                {
                    {StateWriteKind::SetConstant, StateField::Paralysis, ScalarSlot::None, 0},
                    {StateWriteKind::SetConstant, StateField::ParalysisLevel, ScalarSlot::None, 0},
                    {
                        StateWriteKind::SetConstant,
                        StateField::CurrentAction,
                        ScalarSlot::None,
                        BattleEmulator::CURE_PARALYSIS,
                    },
                });
            const int retainParalysis = b.addStateUpdate(
                "ally-status:action=PARALYSIS",
                {{StateWriteKind::SetConstant, StateField::CurrentAction, ScalarSlot::None,
                  BattleEmulator::PARALYSIS}});
            const int positiveSkip = b.add(Opcode::SkipRng, "ally-status:positive-turn-skip", 1);
            const int inactive = b.addBranch(
                "ally-status:Inactive==ON",
                allOf({comparison(stateValue(StateField::Inactive), CompareOp::Eq, constantValue(1))}));
            const int clearInactive = b.addStateUpdate(
                "ally-status:Inactive=OFF",
                {{StateWriteKind::SetConstant, StateField::Inactive, ScalarSlot::None, 0}});
            const int statusAction = b.addBranch(
                "ally-status:action is CURE_PARALYSIS or PARALYSIS",
                anyOf({
                    clause({comparison(stateValue(StateField::CurrentAction), CompareOp::Eq,
                                       constantValue(BattleEmulator::CURE_PARALYSIS))}),
                    clause({comparison(stateValue(StateField::CurrentAction), CompareOp::Eq,
                                       constantValue(BattleEmulator::PARALYSIS))}),
                }));
            const int setInactiveAction = b.addStateUpdate(
                "ally-status:action=INACTIVE_ALLY",
                {{StateWriteKind::SetConstant, StateField::CurrentAction, ScalarSlot::None,
                  BattleEmulator::INACTIVE_ALLY}});
            const int ret = b.add(Opcode::Return, "ally-status:return action");

            b.branch(paralysis, decrement, clearSkip);
            b.next(clearSkip, inactive);
            b.next(decrement, releaseWindow);
            b.branch(releaseWindow, releaseRoll, positiveSkip);
            b.next(releaseRoll, released);
            b.branch(released, clearParalysis, retainParalysis);
            b.next(clearParalysis, inactive);
            b.next(retainParalysis, inactive);
            b.next(positiveSkip, retainParalysis);
            b.branch(inactive, clearInactive, ret);
            b.next(clearInactive, statusAction);
            b.branch(statusAction, ret, setInactiveAction);
            b.next(setInactiveAction, ret);
            return std::move(b).finish();
        }

        Routine makeDispatchRoutine(
            const std::string &id,
            const std::vector<std::pair<int, std::string>> &cases) {
            Routine routine;
            routine.id = id;

            Instruction dispatch;
            dispatch.opcode = Opcode::Switch;
            dispatch.label = id + ":switch";
            dispatch.switchOperand.kind = SwitchSourceKind::CurrentAction;
            for (std::size_t index = 0; index < cases.size(); ++index) {
                dispatch.successors.push_back(static_cast<int>(index) + 1);
                dispatch.switchCaseValues.push_back(cases[index].first);
            }
            routine.instructions.push_back(std::move(dispatch));

            const int returnPc = static_cast<int>(cases.size()) + 1;
            for (const auto &[action, callee]: cases) {
                (void) action;
                Instruction call;
                call.opcode = Opcode::Call;
                call.label = "call:" + callee;
                call.successors = {returnPc};
                call.nativeWork = 1;
                call.callTarget = callee;
                routine.instructions.push_back(std::move(call));
            }
            routine.instructions.push_back({Opcode::Return, id + ":return", {}, 0, 1});
            return routine;
        }

        Routine makeAllyActionRoutine() {
            return makeDispatchRoutine(
                "ally-action",
                {
                    {BattleEmulator::ATTACK_ALLY, "ally-attack"},
                    {BattleEmulator::DRAGON_SLASH, "ally-dragon-slash"},
                    {BattleEmulator::DEFENCE, "ally-defence"},
                    {BattleEmulator::MEDICINAL_HERBS, "ally-herb"},
                    {BattleEmulator::HEAL, "ally-heal"},
                    {BattleEmulator::CRACK_ALLY, "ally-crack"},
                    {BattleEmulator::ACROBATIC_STAR, "ally-acro"},
                    {BattleEmulator::PARALYSIS, "ally-paralysis"},
                    {BattleEmulator::CURE_PARALYSIS, "ally-cure-paralysis"},
                    {BattleEmulator::INACTIVE_ALLY, "ally-inactive"},
                });
        }

        Routine makeEnemyActionRoutine() {
            return makeDispatchRoutine(
                "enemy-action",
                {
                    {BattleEmulator::VICTIMISER, "enemy-victimiser"},
                    {BattleEmulator::HP_HOOVER, "enemy-hp-hoover"},
                    {BattleEmulator::CRACK_ENEMY, "enemy-crack"},
                    {BattleEmulator::ATTACK_ENEMY, "enemy-attack"},
                    {BattleEmulator::MANAZASHI, "enemy-manazashi"},
                    {BattleEmulator::PUFF_PUFF, "enemy-puff-puff"},
                    {BattleEmulator::INACTIVE_ENEMY, "enemy-inactive"},
                });
        }

        Routine makeRageCrossingRoutine() {
            RuleRoutineBuilder b("rage-crossing");
            const int afterHalf = b.addBranch(
                "rage:after<=227",
                allOf({comparison(resourceMinusScalar(ResourceAxis::EnemyHp, ScalarSlot::Damage),
                                  CompareOp::Le, constantValue(227))}));
            const int beforeHalf = b.addBranch(
                "rage:before>=228",
                allOf({comparison(resourceValue(ResourceAxis::EnemyHp), CompareOp::Ge, constantValue(228))}));
            const int rageOffHalf = b.addBranch(
                "rage:half-crossing:Rage==OFF",
                allOf({comparison(stateValue(StateField::Rage), CompareOp::Eq, constantValue(0))}));
            const int halfSkip = b.add(Opcode::SkipRng, "rage:half-crossing:skip-before-duration", 1);
            const int duration = b.addRngRead(
                "rage:duration:intRange(2,4)",
                {RngReadKind::IntRangeInclusive, ScalarSlot::Duration, 2, 4, 0.0, 0.0});
            const int enable = b.addStateUpdate(
                "rage:Rage=ON(duration)",
                {
                    {StateWriteKind::SetFromSlot, StateField::Rage, ScalarSlot::Duration, 0},
                    {StateWriteKind::SetFromSlot, StateField::RawRageTimer, ScalarSlot::Duration, 0},
                });
            const int halfAlreadyOnSkip = b.add(Opcode::SkipRng, "rage:half-crossing:already-on-skip", 1);
            const int afterQuarter = b.addBranch(
                "rage:after<=113",
                allOf({comparison(resourceMinusScalar(ResourceAxis::EnemyHp, ScalarSlot::Damage),
                                  CompareOp::Le, constantValue(113))}));
            const int beforeQuarter = b.addBranch(
                "rage:before>=114",
                allOf({comparison(resourceValue(ResourceAxis::EnemyHp), CompareOp::Ge, constantValue(114))}));
            const int rageOffQuarter = b.addBranch(
                "rage:quarter-crossing:Rage==OFF",
                allOf({comparison(stateValue(StateField::Rage), CompareOp::Eq, constantValue(0))}));
            const int quarterOffSkip = b.add(Opcode::SkipRng, "rage:quarter-crossing:off-skip", 2);
            const int quarterOnSkip = b.add(Opcode::SkipRng, "rage:quarter-crossing:on-skip", 1);
            const int ret = b.add(Opcode::Return, "rage:return");

            b.branch(afterHalf, beforeHalf, ret);
            b.branch(beforeHalf, rageOffHalf, afterQuarter);
            b.branch(rageOffHalf, halfSkip, halfAlreadyOnSkip);
            b.next(halfSkip, duration);
            b.next(duration, enable);
            b.next(enable, ret);
            b.next(halfAlreadyOnSkip, ret);
            b.branch(afterQuarter, beforeQuarter, ret);
            b.branch(beforeQuarter, rageOffQuarter, ret);
            b.branch(rageOffQuarter, quarterOffSkip, quarterOnSkip);
            b.next(quarterOffSkip, ret);
            b.next(quarterOnSkip, ret);
            return std::move(b).finish();
        }

        Routine makeProcess7A8Routine() {
            RuleRoutineBuilder b("process7a8");
            const int statusClear = b.addBranch(
                "7a8:defender not-paralysis and not-sleeping and not-inactive",
                allOf({
                    comparison(stateValue(StateField::Paralysis), CompareOp::Eq, constantValue(0)),
                    comparison(stateValue(StateField::Inactive), CompareOp::Eq, constantValue(0)),
                }));
            const int survives = b.addBranch(
                "7a8:defender.hp>damage",
                allOf({comparison(resourceValue(ResourceAxis::HeroHp), CompareOp::Gt,
                                  scalarValue(ScalarSlot::Damage))}));
            const int chargeOff = b.addBranch(
                "7a8:defender Charge==OFF",
                allOf({comparison(stateValue(StateField::Charge), CompareOp::Eq, constantValue(0))}));
            const int damageZero = b.addBranch(
                "7a8:damage==0",
                allOf({comparison(scalarValue(ScalarSlot::Damage), CompareOp::Eq, constantValue(0))}));
            const int zeroSkip = b.add(Opcode::SkipRng, "7a8:zero-damage-skip", 1);
            const int roll = b.addRngRead(
                "7a8:charge-percent",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 100, 0.0, 0.0});
            const int ret = b.add(Opcode::Return, "7a8:return");

            b.branch(statusClear, survives, ret);
            b.branch(survives, chargeOff, ret);
            b.branch(chargeOff, damageZero, ret);
            b.branch(damageZero, zeroSkip, roll);
            b.next(zeroSkip, ret);

            constexpr int damageThresholds[9] = {59, 53, 46, 40, 33, 27, 20, 14, 7};
            constexpr int percentThresholds[9] = {90, 90, 64, 32, 16, 8, 4, 2, 1};
            std::vector<int> damageTests;
            std::vector<int> percentTests;
            std::vector<int> enableCharge;
            for (int index = 0; index < 9; ++index) {
                damageTests.push_back(b.addBranch(
                    "7a8:damage>=proportionTable3[" + std::to_string(index) + "]",
                    allOf({comparison(scalarValue(ScalarSlot::Damage), CompareOp::Ge,
                                      constantValue(damageThresholds[index]))})));
                percentTests.push_back(b.addBranch(
                    "7a8:percent<proportionTable2[" + std::to_string(index) + "]",
                    allOf({comparison(scalarValue(ScalarSlot::Random0), CompareOp::Lt,
                                      constantValue(percentThresholds[index]))})));
                enableCharge.push_back(b.addStateUpdate(
                    "7a8:Charge=ON(6) at band " + std::to_string(index),
                    {
                        {StateWriteKind::SetConstant, StateField::Charge, ScalarSlot::None, 7},
                        {StateWriteKind::SetConstant, StateField::RawChargeTimer, ScalarSlot::None, 6},
                    }));
            }

            b.next(roll, damageTests.front());
            for (int index = 0; index < 9; ++index) {
                const int nextDamage = index + 1 < 9 ? damageTests[index + 1] : ret;
                b.branch(damageTests[index], percentTests[index], nextDamage);
                b.branch(percentTests[index], enableCharge[index], ret);
                b.next(enableCharge[index], ret);
            }
            return std::move(b).finish();
        }

        Routine makeAllyHerbRoutine() {
            RuleRoutineBuilder b("ally-herb");
            const int use = b.addResourceUpdate(
                "herb:I=I-1",
                {ResourceUpdateKind::AddConstant, ResourceAxis::Herb, ScalarSlot::None, -1, 0, 0});
            const int setup = b.add(Opcode::SkipRng, "herb:setup", 2);
            const int unrelated = b.add(Opcode::SkipRng, "herb:unrelated", 1);
            const int critical = b.add(Opcode::SkipRng, "herb:critical-slot", 1);
            const int evade = b.add(Opcode::SkipRng, "herb:evade-slot", 1);
            const int heal = b.addNative(
                "native:type-c-heal",
                ScalarSlot::Damage,
                {35.0, 35.0, 5.0});
            const int unknown = b.add(Opcode::SkipRng, "herb:post-heal-unknown", 1);
            const int chargeOff = b.addBranch(
                "herb:Charge==OFF",
                allOf({comparison(stateValue(StateField::Charge), CompareOp::Eq, constantValue(0))}));
            const int chargeSkip = b.add(Opcode::SkipRng, "herb:charge-precheck-skip", 1);
            const int chargeRoll = b.addRngRead(
                "herb:charge-percent",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 100, 0.0, 0.0});
            const int chargeSuccess = b.addBranch(
                "herb:charge-percent<1",
                allOf({comparison(scalarValue(ScalarSlot::Random0), CompareOp::Lt, constantValue(1))}));
            const int enableCharge = b.addStateUpdate(
                "herb:Charge=ON(6)",
                {
                    {StateWriteKind::SetConstant, StateField::Charge, ScalarSlot::None, 7},
                    {StateWriteKind::SetConstant, StateField::RawChargeTimer, ScalarSlot::None, 6},
                });
            const int ret = b.add(Opcode::Return, "herb:return");

            b.next(use, setup);
            b.next(setup, unrelated);
            b.next(unrelated, critical);
            b.next(critical, evade);
            b.next(evade, heal);
            b.next(heal, unknown);
            b.next(unknown, chargeOff);
            b.branch(chargeOff, chargeSkip, ret);
            b.next(chargeSkip, chargeRoll);
            b.next(chargeRoll, chargeSuccess);
            b.branch(chargeSuccess, enableCharge, ret);
            b.next(enableCharge, ret);
            return std::move(b).finish();
        }

        Routine makeAllyAcroRoutine() {
            RuleRoutineBuilder b("ally-acro");
            b.initialize(ScalarSlot::Damage, 0.0);
            const int acroOn = b.addStateUpdate(
                "acro:Acro=ON(6)",
                {
                    {StateWriteKind::SetConstant, StateField::Acro, ScalarSlot::None, 6},
                    {StateWriteKind::SetConstant, StateField::RawAcroTimer, ScalarSlot::None, 6},
                });
            const int chargeOff = b.addStateUpdate(
                "acro:Charge=OFF",
                {
                    {StateWriteKind::SetConstant, StateField::Charge, ScalarSlot::None, 0},
                    {StateWriteKind::SetConstant, StateField::RawChargeTimer, ScalarSlot::None, 0},
                });
            const int setup = b.add(Opcode::SkipRng, "acro:setup", 2);
            const int unrelated = b.add(Opcode::SkipRng, "acro:unrelated", 1);
            const int critical = b.add(Opcode::SkipRng, "acro:critical-slot", 1);
            const int evade = b.add(Opcode::SkipRng, "acro:evade-slot", 1);
            const int damage = b.addNative(
                "native:physical-damage",
                ScalarSlot::Damage,
                {BasePlayers[0].defaultATK, BasePlayers[0].def});
            const int discardDamage = b.addScalarUpdate(
                "acro:discard physical-damage result",
                {ScalarUpdateKind::SetConstant, ScalarSlot::Damage, ScalarSlot::None,
                 FixedScalarSource::None, 0, 0, 1});
            const int unknown = b.add(Opcode::SkipRng, "acro:post-damage-unknown", 1);
            const int ret = b.add(Opcode::Return, "acro:return");
            b.next(acroOn, chargeOff);
            b.next(chargeOff, setup);
            b.next(setup, unrelated);
            b.next(unrelated, critical);
            b.next(critical, evade);
            b.next(evade, damage);
            b.next(damage, discardDamage);
            b.next(discardDamage, unknown);
            b.next(unknown, ret);
            return std::move(b).finish();
        }

        Routine makePassiveAllyRoutine(
            const std::string &id,
            bool chargeRoll,
            int attack,
            int defence) {
            RuleRoutineBuilder b(id);
            b.initialize(ScalarSlot::Damage, 0.0);
            const int setup = b.add(Opcode::SkipRng, id + ":setup", 2);
            const int unrelated = b.add(Opcode::SkipRng, id + ":unrelated", 1);
            const int critical = b.add(Opcode::SkipRng, id + ":critical-slot", 1);
            const int evade = b.add(Opcode::SkipRng, id + ":evade-slot", 1);
            const int damage = b.addNative(
                "native:physical-damage",
                ScalarSlot::Damage,
                {static_cast<double>(attack), static_cast<double>(defence)});
            const int discardDamage = b.addScalarUpdate(
                id + ":discard physical-damage result",
                {ScalarUpdateKind::SetConstant, ScalarSlot::Damage, ScalarSlot::None,
                 FixedScalarSource::None, 0, 0, 1});
            const int unknown = b.add(Opcode::SkipRng, id + ":post-damage-unknown", 1);
            const int ret = b.add(Opcode::Return, id + ":return");
            b.next(setup, unrelated);
            b.next(unrelated, critical);
            b.next(critical, evade);
            b.next(evade, damage);
            b.next(damage, discardDamage);
            b.next(discardDamage, unknown);
            if (!chargeRoll) {
                b.next(unknown, ret);
                return std::move(b).finish();
            }

            const int chargeOff = b.addBranch(
                id + ":Charge==OFF",
                allOf({comparison(stateValue(StateField::Charge), CompareOp::Eq, constantValue(0))}));
            const int chargeSkip = b.add(Opcode::SkipRng, id + ":charge-precheck-skip", 1);
            const int chargeRollPc = b.addRngRead(
                id + ":charge-percent",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 100, 0.0, 0.0});
            const int success = b.addBranch(
                id + ":charge-percent<1",
                allOf({comparison(scalarValue(ScalarSlot::Random0), CompareOp::Lt, constantValue(1))}));
            const int enable = b.addStateUpdate(
                id + ":Charge=ON(6)",
                {
                    {StateWriteKind::SetConstant, StateField::Charge, ScalarSlot::None, 7},
                    {StateWriteKind::SetConstant, StateField::RawChargeTimer, ScalarSlot::None, 6},
                });
            b.next(unknown, chargeOff);
            b.branch(chargeOff, chargeSkip, ret);
            b.next(chargeSkip, chargeRollPc);
            b.next(chargeRollPc, success);
            b.branch(success, enable, ret);
            b.next(enable, ret);
            return std::move(b).finish();
        }

        Routine makeAllyAttackRoutine(const std::string &id) {
            RuleRoutineBuilder b(id);
            b.initialize(ScalarSlot::CriticalFlag, 0.0);
            b.initialize(ScalarSlot::DodgeFlag, 0.0);
            const int setup = b.add(Opcode::SkipRng, id + ":setup", 2);
            const int unrelated = b.add(Opcode::SkipRng, id + ":pre-critical-slot", 1);
            const int criticalRoll = b.addRngRead(
                id + ":critical-percent",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 0x2710, 0.0, 0.0});
            const bool dragonSlash = id == "ally-dragon-slash";
            const int criticalFlag = b.addBranch(
                id + ":critical-threshold",
                allOf({comparison(
                    scalarValue(ScalarSlot::Random0),
                    CompareOp::Lt,
                    constantValue(dragonSlash ? 100 : 200))}));
            const int markCritical = b.addScalarUpdate(
                id + ":critical=true",
                {ScalarUpdateKind::SetConstant, ScalarSlot::CriticalFlag, ScalarSlot::None,
                 FixedScalarSource::None, 1, 0, 1});
            const int dodgeEligible = b.addBranch(
                id + ":hero not-paralysis and not-inactive",
                allOf({
                    comparison(stateValue(StateField::Paralysis), CompareOp::Eq, constantValue(0)),
                    comparison(stateValue(StateField::Inactive), CompareOp::Eq, constantValue(0)),
                }));
            const int dodgeRoll = b.addRngRead(
                id + ":enemy-dodge-percent",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 100, 0.0, 0.0});
            const int dodged = b.addBranch(
                id + ":enemy-dodge-percent<2",
                allOf({comparison(scalarValue(ScalarSlot::Random0), CompareOp::Lt, constantValue(2))}));
            const int markDodge = b.addScalarUpdate(
                id + ":dodged=true",
                {ScalarUpdateKind::SetConstant, ScalarSlot::DodgeFlag, ScalarSlot::None,
                 FixedScalarSource::None, 1, 0, 1});
            const int shieldSkip = b.add(Opcode::SkipRng, id + ":enemy-shield-zero-percent", 1);
            const int evadeSlot = b.add(Opcode::SkipRng, id + ":evade-slot", 1);
            const int damage = b.addNative(
                "native:physical-damage",
                ScalarSlot::Damage,
                {BasePlayers[0].atk, BasePlayers[1].def});
            const int criticalScale = b.addBranch(
                id + ":critical?",
                allOf({comparison(scalarValue(ScalarSlot::CriticalFlag), CompareOp::Eq, constantValue(1))}));
            const int criticalRandom = b.addRngRead(
                id + ":critical-scale",
                {
                    RngReadKind::FloatRange,
                    ScalarSlot::Random0,
                    0,
                    0,
                    dragonSlash ? 1.5 : 0.95,
                    dragonSlash ? 2.0 : 1.05,
                });
            const int applyCritical = dragonSlash
                ? b.addScalarUpdate(
                    id + ":damage*=critical-scale",
                    {ScalarUpdateKind::MultiplyBySlot, ScalarSlot::Damage, ScalarSlot::Random0})
                : b.addScalarUpdate(
                    id + ":damage=hero.atk*critical-scale",
                    {
                        ScalarUpdateKind::SetFixedSourceTimesSlot,
                        ScalarSlot::Damage,
                        ScalarSlot::Random0,
                        FixedScalarSource::HeroAttack,
                    });
            const int hit = b.addBranch(
                id + ":not-dodged",
                allOf({comparison(scalarValue(ScalarSlot::DodgeFlag), CompareOp::Eq, constantValue(0))}));
            const int rage = b.addCall("call:rage-crossing", "rage-crossing");
            const int wakeSkip = b.add(Opcode::SkipRng, id + ":wake-check", 1);
            const int unknownSkip = b.add(Opcode::SkipRng, id + ":post-hit-unknown", 1);
            const int criticalPost = b.addBranch(
                id + ":critical-post?",
                allOf({comparison(scalarValue(ScalarSlot::CriticalFlag), CompareOp::Eq, constantValue(1))}));
            const int rageOff = b.addBranch(
                id + ":enemy Rage==OFF after crossing",
                allOf({comparison(stateValue(StateField::Rage), CompareOp::Eq, constantValue(0))}));
            const int criticalOffSkips = b.add(Opcode::SkipRng, id + ":critical-post-off-skips", 2);
            const int criticalOnSkip = b.add(Opcode::SkipRng, id + ":critical-post-on-skip", 1);
            const int targetAliveForCharge = b.addBranch(
                id + ":enemy.hp-damage>=0",
                allOf({comparison(resourceMinusScalar(ResourceAxis::EnemyHp, ScalarSlot::Damage),
                                  CompareOp::Ge, constantValue(0))}));
            const int chargeOff = b.addBranch(
                id + ":hero Charge==OFF",
                allOf({comparison(stateValue(StateField::Charge), CompareOp::Eq, constantValue(0))}));
            const int chargeRoll = b.addRngRead(
                id + ":hero-charge-percent",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 100, 0.0, 0.0});
            const int chargeSuccess = b.addBranch(
                id + ":hero-charge-percent<1",
                allOf({comparison(scalarValue(ScalarSlot::Random0), CompareOp::Lt, constantValue(1))}));
            const int enableCharge = b.addStateUpdate(
                id + ":hero Charge=ON(6)",
                {
                    {StateWriteKind::SetConstant, StateField::Charge, ScalarSlot::None, 7},
                    {StateWriteKind::SetConstant, StateField::RawChargeTimer, ScalarSlot::None, 6},
                });
            const int ret = b.add(Opcode::Return, id + ":return damage");

            b.next(setup, unrelated);
            b.next(unrelated, criticalRoll);
            b.next(criticalRoll, criticalFlag);
            b.branch(criticalFlag, markCritical, dodgeEligible);
            b.next(markCritical, dodgeEligible);
            b.branch(dodgeEligible, dodgeRoll, evadeSlot);
            b.next(dodgeRoll, dodged);
            b.branch(dodged, markDodge, shieldSkip);
            b.next(markDodge, evadeSlot);
            b.next(shieldSkip, evadeSlot);
            b.next(evadeSlot, damage);
            b.next(damage, criticalScale);
            b.branch(criticalScale, criticalRandom, hit);
            b.next(criticalRandom, applyCritical);
            b.next(applyCritical, hit);
            b.branch(hit, rage, criticalPost);
            b.next(rage, wakeSkip);
            b.next(wakeSkip, unknownSkip);
            b.next(unknownSkip, criticalPost);
            b.branch(criticalPost, rageOff, targetAliveForCharge);
            b.branch(rageOff, criticalOffSkips, criticalOnSkip);
            b.next(criticalOffSkips, targetAliveForCharge);
            b.next(criticalOnSkip, targetAliveForCharge);
            b.branch(targetAliveForCharge, chargeOff, ret);
            b.branch(chargeOff, chargeRoll, ret);
            b.next(chargeRoll, chargeSuccess);
            b.branch(chargeSuccess, enableCharge, ret);
            b.next(enableCharge, ret);
            return std::move(b).finish();
        }

        Routine makeAllyCrackRoutine() {
            RuleRoutineBuilder b("ally-crack");
            b.initialize(ScalarSlot::CriticalFlag, 0.0);
            const int pay = b.addResourceUpdate(
                "crack:M=M-3",
                {ResourceUpdateKind::AddConstant, ResourceAxis::Mp, ScalarSlot::None, -3, 0, 0});
            const int setup = b.add(Opcode::SkipRng, "crack:setup", 2);
            const int unrelated = b.add(Opcode::SkipRng, "crack:unrelated", 1);
            const int criticalRoll = b.addRngRead(
                "crack:critical-percent",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 0x2710, 0.0, 0.0});
            const int critical = b.addBranch(
                "crack:critical?",
                allOf({comparison(scalarValue(ScalarSlot::Random0), CompareOp::Lt, constantValue(100))}));
            const int markCritical = b.addScalarUpdate(
                "crack:critical=true",
                {ScalarUpdateKind::SetConstant, ScalarSlot::CriticalFlag, ScalarSlot::None,
                 FixedScalarSource::None, 1, 0, 1});
            const int shield = b.add(Opcode::SkipRng, "crack:shield-slot", 1);
            const int evade = b.add(Opcode::SkipRng, "crack:fake-evade-slot", 1);
            const int damage = b.addNative(
                "native:type-d-damage",
                ScalarSlot::Damage,
                {5.0, 30.0});
            const int criticalScaleGate = b.addBranch(
                "crack:critical-scale?",
                allOf({comparison(scalarValue(ScalarSlot::CriticalFlag), CompareOp::Eq, constantValue(1))}));
            const int criticalScale = b.addRngRead(
                "crack:critical-scale",
                {RngReadKind::FloatRange, ScalarSlot::Random0, 0, 0, 1.5, 2.0});
            const int applyCritical = b.addScalarUpdate(
                "crack:damage*=critical-scale",
                {ScalarUpdateKind::MultiplyBySlot, ScalarSlot::Damage, ScalarSlot::Random0});
            const int halfDamage = b.addScalarUpdate(
                "crack:damage*=0.5",
                {ScalarUpdateKind::MultiplyRational, ScalarSlot::Damage, ScalarSlot::None,
                 FixedScalarSource::None, 0, 1, 2});
            const int rage = b.addCall("call:rage-crossing", "rage-crossing");
            const int criticalPost = b.addBranch(
                "crack:critical-post?",
                allOf({comparison(scalarValue(ScalarSlot::CriticalFlag), CompareOp::Eq, constantValue(1))}));
            const int rageOff = b.addBranch(
                "crack:enemy Rage==OFF after crossing",
                allOf({comparison(stateValue(StateField::Rage), CompareOp::Eq, constantValue(0))}));
            const int offSkips = b.add(Opcode::SkipRng, "crack:critical-off-skips", 2);
            const int onSkip = b.add(Opcode::SkipRng, "crack:critical-on-skip", 1);
            const int fixedSkip = b.add(Opcode::SkipRng, "crack:fixed-post-skip", 1);
            const int chargeOff = b.addBranch(
                "crack:hero Charge==OFF",
                allOf({comparison(stateValue(StateField::Charge), CompareOp::Eq, constantValue(0))}));
            const int chargeRoll = b.addRngRead(
                "crack:hero-charge-percent",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 100, 0.0, 0.0});
            const int chargeSuccess = b.addBranch(
                "crack:hero-charge-percent<1",
                allOf({comparison(scalarValue(ScalarSlot::Random0), CompareOp::Lt, constantValue(1))}));
            const int enableCharge = b.addStateUpdate(
                "crack:hero Charge=ON(6)",
                {
                    {StateWriteKind::SetConstant, StateField::Charge, ScalarSlot::None, 7},
                    {StateWriteKind::SetConstant, StateField::RawChargeTimer, ScalarSlot::None, 6},
                });
            const int ret = b.add(Opcode::Return, "crack:return damage");

            b.next(pay, setup);
            b.next(setup, unrelated);
            b.next(unrelated, criticalRoll);
            b.next(criticalRoll, critical);
            b.branch(critical, markCritical, shield);
            b.next(markCritical, shield);
            b.next(shield, evade);
            b.next(evade, damage);
            b.next(damage, criticalScaleGate);
            b.branch(criticalScaleGate, criticalScale, halfDamage);
            b.next(criticalScale, applyCritical);
            b.next(applyCritical, halfDamage);
            b.next(halfDamage, rage);
            b.next(rage, criticalPost);
            b.branch(criticalPost, rageOff, fixedSkip);
            b.branch(rageOff, offSkips, onSkip);
            b.next(offSkips, fixedSkip);
            b.next(onSkip, fixedSkip);
            b.next(fixedSkip, chargeOff);
            b.branch(chargeOff, chargeRoll, ret);
            b.next(chargeRoll, chargeSuccess);
            b.branch(chargeSuccess, enableCharge, ret);
            b.next(enableCharge, ret);
            return std::move(b).finish();
        }

        Routine makeAllyHealRoutine() {
            RuleRoutineBuilder b("ally-heal");
            b.initialize(ScalarSlot::CriticalFlag, 0.0);
            const int setup = b.add(Opcode::SkipRng, "heal:setup", 2);
            const int unrelated = b.add(Opcode::SkipRng, "heal:unrelated", 1);
            const int criticalRoll = b.addRngRead(
                "heal:critical-percent",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 0x2710, 0.0, 0.0});
            const int critical = b.addBranch(
                "heal:critical?",
                allOf({comparison(scalarValue(ScalarSlot::Random0), CompareOp::Lt, constantValue(100))}));
            const int markCritical = b.addScalarUpdate(
                "heal:critical=true",
                {ScalarUpdateKind::SetConstant, ScalarSlot::CriticalFlag, ScalarSlot::None,
                 FixedScalarSource::None, 1, 0, 1});
            const int evade = b.add(Opcode::SkipRng, "heal:evade-slot", 1);
            const int amount = b.addNative(
                "native:type-d-heal",
                ScalarSlot::Damage,
                {5.0, 35.0});
            const int criticalScaleGate = b.addBranch(
                "heal:critical-scale?",
                allOf({comparison(scalarValue(ScalarSlot::CriticalFlag), CompareOp::Eq, constantValue(1))}));
            const int criticalScale = b.addRngRead(
                "heal:critical-scale",
                {RngReadKind::FloatRange, ScalarSlot::Random0, 0, 0, 1.5, 2.0});
            const int applyCritical = b.addScalarUpdate(
                "heal:amount*=critical-scale",
                {ScalarUpdateKind::MultiplyBySlot, ScalarSlot::Damage, ScalarSlot::Random0});
            const int unknown = b.add(Opcode::SkipRng, "heal:post-heal-unknown", 1);
            const int chargeOffSkip = b.addBranch(
                "heal:hero Charge==OFF for fixed skip",
                allOf({comparison(stateValue(StateField::Charge), CompareOp::Eq, constantValue(0))}));
            const int fixedChargeSkip = b.add(Opcode::SkipRng, "heal:charge-fixed-skip", 1);
            const int rageOff = b.addBranch(
                "heal:enemy Rage==OFF",
                allOf({comparison(stateValue(StateField::Rage), CompareOp::Eq, constantValue(0))}));
            const int rageSkip = b.add(Opcode::SkipRng, "heal:rage-off-skip", 1);
            const int fixedSkip = b.add(Opcode::SkipRng, "heal:fixed-post-skip", 1);
            const int criticalPost = b.addBranch(
                "heal:critical-post?",
                allOf({comparison(scalarValue(ScalarSlot::CriticalFlag), CompareOp::Eq, constantValue(1))}));
            const int criticalRageOff = b.addBranch(
                "heal:critical and enemy Rage==OFF",
                allOf({
                    comparison(scalarValue(ScalarSlot::CriticalFlag), CompareOp::Eq, constantValue(1)),
                    comparison(stateValue(StateField::Rage), CompareOp::Eq, constantValue(0)),
                }));
            const int criticalOffSkips = b.add(Opcode::SkipRng, "heal:critical-off-skips", 2);
            const int criticalOnSkip = b.add(Opcode::SkipRng, "heal:critical-on-skip", 1);
            const int statusClear = b.addBranch(
                "heal:hero not-paralysis and not-inactive",
                allOf({
                    comparison(stateValue(StateField::Paralysis), CompareOp::Eq, constantValue(0)),
                    comparison(stateValue(StateField::Inactive), CompareOp::Eq, constantValue(0)),
                }));
            const int chargeOffRoll = b.addBranch(
                "heal:hero Charge==OFF for roll",
                allOf({comparison(stateValue(StateField::Charge), CompareOp::Eq, constantValue(0))}));
            const int chargeRoll = b.addRngRead(
                "heal:hero-charge-percent",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 100, 0.0, 0.0});
            const int chargeSuccess = b.addBranch(
                "heal:hero-charge-percent<1",
                allOf({comparison(scalarValue(ScalarSlot::Random0), CompareOp::Lt, constantValue(1))}));
            const int enableCharge = b.addStateUpdate(
                "heal:hero Charge=ON(6)",
                {
                    {StateWriteKind::SetConstant, StateField::Charge, ScalarSlot::None, 7},
                    {StateWriteKind::SetConstant, StateField::RawChargeTimer, ScalarSlot::None, 6},
                });
            const int pay = b.addResourceUpdate(
                "heal:M=M-2",
                {ResourceUpdateKind::AddConstant, ResourceAxis::Mp, ScalarSlot::None, -2, 0, 0});
            const int ret = b.add(Opcode::Return, "heal:return amount");

            b.next(setup, unrelated);
            b.next(unrelated, criticalRoll);
            b.next(criticalRoll, critical);
            b.branch(critical, markCritical, evade);
            b.next(markCritical, evade);
            b.next(evade, amount);
            b.next(amount, criticalScaleGate);
            b.branch(criticalScaleGate, criticalScale, unknown);
            b.next(criticalScale, applyCritical);
            b.next(applyCritical, unknown);
            b.next(unknown, chargeOffSkip);
            b.branch(chargeOffSkip, fixedChargeSkip, rageOff);
            b.next(fixedChargeSkip, rageOff);
            b.branch(rageOff, rageSkip, fixedSkip);
            b.next(rageSkip, fixedSkip);
            b.next(fixedSkip, criticalPost);
            b.branch(criticalPost, criticalRageOff, statusClear);
            b.branch(criticalRageOff, criticalOffSkips, criticalOnSkip);
            b.next(criticalOffSkips, statusClear);
            b.next(criticalOnSkip, statusClear);
            b.branch(statusClear, chargeOffRoll, pay);
            b.branch(chargeOffRoll, chargeRoll, pay);
            b.next(chargeRoll, chargeSuccess);
            b.branch(chargeSuccess, enableCharge, pay);
            b.next(enableCharge, pay);
            b.next(pay, ret);
            return std::move(b).finish();
        }

        Routine makeAcroDodgeRoutine() {
            RuleRoutineBuilder b("acro-dodge");
            b.initialize(ScalarSlot::CriticalFlag, 0.0);
            b.initialize(ScalarSlot::Damage, 0.0);
            const int criticalRoll = b.addRngRead(
                "acro-dodge:critical-percent",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 0x2710, 0.0, 0.0});
            const int critical = b.addBranch(
                "acro-dodge:critical?",
                allOf({comparison(scalarValue(ScalarSlot::Random0), CompareOp::Lt, constantValue(200))}));
            const int markCritical = b.addScalarUpdate(
                "acro-dodge:critical=true",
                {ScalarUpdateKind::SetConstant, ScalarSlot::CriticalFlag, ScalarSlot::None,
                 FixedScalarSource::None, 1, 0, 1});
            const int evade = b.add(Opcode::SkipRng, "acro-dodge:evade-slot", 1);
            const int damage = b.addNative(
                "native:physical-damage",
                ScalarSlot::Damage,
                {BasePlayers[1].atk, BasePlayers[0].def});
            const int discardDamage = b.addScalarUpdate(
                "acro-dodge:return damage=0",
                {ScalarUpdateKind::SetConstant, ScalarSlot::Damage, ScalarSlot::None,
                 FixedScalarSource::None, 0, 0, 1});
            const int chargeOff = b.addBranch(
                "acro-dodge:defender Charge==OFF",
                allOf({comparison(stateValue(StateField::Charge), CompareOp::Eq, constantValue(0))}));
            const int chargeSkip = b.add(Opcode::SkipRng, "acro-dodge:charge-skip", 1);
            const int ret = b.add(Opcode::Return, "acro-dodge:return 0");
            b.next(criticalRoll, critical);
            b.branch(critical, markCritical, evade);
            b.next(markCritical, evade);
            b.next(evade, damage);
            b.next(damage, discardDamage);
            b.next(discardDamage, chargeOff);
            b.branch(chargeOff, chargeSkip, ret);
            b.next(chargeSkip, ret);
            return std::move(b).finish();
        }

        Routine makeCounterRoutine() {
            RuleRoutineBuilder b("counter");
            b.initialize(ScalarSlot::CriticalFlag, 0.0);
            b.initialize(ScalarSlot::DodgeFlag, 0.0);
            b.initialize(ScalarSlot::Damage, 0.0);
            const int criticalDisabled = b.add(Opcode::SkipRng, "counter:disabled-critical-slot", 1);
            const int criticalRoll = b.addRngRead(
                "counter:critical-percent",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 0x2710, 0.0, 0.0});
            const int critical = b.addBranch(
                "counter:critical?",
                allOf({comparison(scalarValue(ScalarSlot::Random0), CompareOp::Lt, constantValue(200))}));
            const int markCritical = b.addScalarUpdate(
                "counter:critical=true",
                {ScalarUpdateKind::SetConstant, ScalarSlot::CriticalFlag, ScalarSlot::None,
                 FixedScalarSource::None, 1, 0, 1});
            const int dodgeRoll = b.addRngRead(
                "counter:dodge-percent",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 100, 0.0, 0.0});
            const int dodged = b.addBranch(
                "counter:dodge-percent<2",
                allOf({comparison(scalarValue(ScalarSlot::Random0), CompareOp::Lt, constantValue(2))}));
            const int markDodge = b.addScalarUpdate(
                "counter:dodged=true",
                {ScalarUpdateKind::SetConstant, ScalarSlot::DodgeFlag, ScalarSlot::None,
                 FixedScalarSource::None, 1, 0, 1});
            const int shield = b.add(Opcode::SkipRng, "counter:shield-slot", 1);
            const int evade = b.add(Opcode::SkipRng, "counter:evade-slot", 1);
            const int damage = b.addNative(
                "native:physical-damage",
                ScalarSlot::Damage,
                {BasePlayers[0].atk, BasePlayers[1].def});
            const int criticalScaleGate = b.addBranch(
                "counter:critical-scale?",
                allOf({comparison(scalarValue(ScalarSlot::CriticalFlag), CompareOp::Eq, constantValue(1))}));
            const int criticalScale = b.addRngRead(
                "counter:critical-scale",
                {RngReadKind::FloatRange, ScalarSlot::Random0, 0, 0, 0.95, 1.05});
            const int applyCritical = b.addScalarUpdate(
                "counter:damage=hero.atk*critical-scale",
                {
                    ScalarUpdateKind::SetFixedSourceTimesSlot,
                    ScalarSlot::Damage,
                    ScalarSlot::Random0,
                    FixedScalarSource::HeroAttack,
                });
            const int hit = b.addBranch(
                "counter:not-dodged",
                allOf({comparison(scalarValue(ScalarSlot::DodgeFlag), CompareOp::Eq, constantValue(0))}));
            const int rage = b.addCall("call:rage-crossing", "rage-crossing");
            const int reduceEnemy = b.addResourceUpdate(
                "counter:E=max(0,E-damage)",
                {
                    ResourceUpdateKind::ClampSubtractSlot,
                    ResourceAxis::EnemyHp,
                    ScalarSlot::Damage,
                    0,
                    0,
                    456,
                });
            const int discardDamage = b.addScalarUpdate(
                "counter:return damage=0",
                {ScalarUpdateKind::SetConstant, ScalarSlot::Damage, ScalarSlot::None,
                 FixedScalarSource::None, 0, 0, 1});
            const int ret = b.add(Opcode::Return, "counter:return 0");

            b.next(criticalDisabled, criticalRoll);
            b.next(criticalRoll, critical);
            b.branch(critical, markCritical, dodgeRoll);
            b.next(markCritical, dodgeRoll);
            b.next(dodgeRoll, dodged);
            b.branch(dodged, markDodge, shield);
            b.next(markDodge, evade);
            b.next(shield, evade);
            b.next(evade, damage);
            b.next(damage, criticalScaleGate);
            b.branch(criticalScaleGate, criticalScale, hit);
            b.next(criticalScale, applyCritical);
            b.next(applyCritical, hit);
            b.branch(hit, rage, discardDamage);
            b.next(rage, reduceEnemy);
            b.next(reduceEnemy, discardDamage);
            b.next(discardDamage, ret);
            return std::move(b).finish();
        }

        Routine makeEnemyAttackRoutine(const std::string &id, bool hpHoover) {
            RuleRoutineBuilder b(id);
            b.initialize(ScalarSlot::DodgeFlag, 0.0);
            b.initialize(ScalarSlot::ShieldFlag, 0.0);
            const int setup = b.add(Opcode::SkipRng, id + ":setup", 2);
            const int acroEligible = b.addBranch(
                id + ":Acro active and hero not-paralysis and not-inactive",
                allOf({
                    comparison(stateValue(StateField::Acro), CompareOp::Ne, constantValue(0)),
                    comparison(stateValue(StateField::Paralysis), CompareOp::Eq, constantValue(0)),
                    comparison(stateValue(StateField::Inactive), CompareOp::Eq, constantValue(0)),
                }));
            const int acroRoll = b.addRngRead(
                id + ":acro-percent",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 100, 0.0, 0.0});
            const int acroDodge = b.addBranch(
                id + ":acro-percent<=49",
                allOf({comparison(scalarValue(ScalarSlot::Random0), CompareOp::Le, constantValue(49))}));
            const int acroCounter = b.addBranch(
                id + ":acro-percent<75",
                allOf({comparison(scalarValue(ScalarSlot::Random0), CompareOp::Lt, constantValue(75))}));
            const int callAcroDodge = b.addCall("call:acro-dodge", "acro-dodge");
            const int returnAcroDodge = b.add(Opcode::Return, id + ":return acro-dodge 0");
            const int callCounter = b.addCall("call:counter", "counter");
            const int returnCounter = b.add(Opcode::Return, id + ":return counter 0");
            const int acroSkip = b.add(Opcode::SkipRng, id + ":acro-ineligible-skip", 1);
            const int criticalSlot = b.add(Opcode::SkipRng, id + ":critical-slot", 1);
            const int statusClear = b.addBranch(
                id + ":hero not-paralysis and not-inactive",
                allOf({
                    comparison(stateValue(StateField::Paralysis), CompareOp::Eq, constantValue(0)),
                    comparison(stateValue(StateField::Inactive), CompareOp::Eq, constantValue(0)),
                }));
            const int acroOff = b.addBranch(
                id + ":Acro==OFF",
                allOf({comparison(stateValue(StateField::Acro), CompareOp::Eq, constantValue(0))}));
            const int dodgeRoll = b.addRngRead(
                id + ":dodge-percent",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 100, 0.0, 0.0});
            const int dodged = b.addBranch(
                id + ":dodge-percent<2",
                allOf({comparison(scalarValue(ScalarSlot::Random0), CompareOp::Lt, constantValue(2))}));
            const int markDodge = b.addScalarUpdate(
                id + ":dodged=true",
                {ScalarUpdateKind::SetConstant, ScalarSlot::DodgeFlag, ScalarSlot::None,
                 FixedScalarSource::None, 1, 0, 1});
            const int shieldRoll = b.addRngRead(
                id + ":shield-percent",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 100, 0.0, 0.0});
            const int shielded = b.addBranch(
                id + ":shield-percent<0.5",
                allOf({comparison(scalarValue(ScalarSlot::Random0), CompareOp::Lt, constantValue(0.5))}));
            const int markShield = b.addScalarUpdate(
                id + ":shielded=true",
                {ScalarUpdateKind::SetConstant, ScalarSlot::ShieldFlag, ScalarSlot::None,
                 FixedScalarSource::None, 1, 0, 1});
            const int evadeSlot = b.add(Opcode::SkipRng, id + ":evade-slot", 1);
            const int damage = b.addNative(
                "native:physical-damage",
                ScalarSlot::Damage,
                {BasePlayers[1].atk, BasePlayers[0].def});
            const int hit = b.addBranch(
                id + ":not-dodged-and-not-shielded",
                allOf({
                    comparison(scalarValue(ScalarSlot::DodgeFlag), CompareOp::Eq, constantValue(0)),
                    comparison(scalarValue(ScalarSlot::ShieldFlag), CompareOp::Eq, constantValue(0)),
                }));
            const int wakeSkip = b.add(Opcode::SkipRng, id + ":wake-check", 1);
            const int unknownSkip = b.add(Opcode::SkipRng, id + ":post-hit-unknown", 1);
            const int hooverHeal = hpHoover
                ? b.addResourceUpdate(
                    id + ":enemy heal damage/4",
                    {
                        ResourceUpdateKind::ClampAddQuarterSlot,
                        ResourceAxis::EnemyHp,
                        ScalarSlot::Damage,
                        0,
                        0,
                        456,
                    })
                : -1;
            const int zeroDamage = b.addScalarUpdate(
                id + ":damage=0",
                {ScalarUpdateKind::SetConstant, ScalarSlot::Damage, ScalarSlot::None,
                 FixedScalarSource::None, 0, 0, 1});
            const int defenceScale = b.addScalarUpdate(
                id + ":damage*=hero.defence",
                {ScalarUpdateKind::MultiplyByDefenceFlag, ScalarSlot::Damage});
            const int processCharge = b.addCall("call:process7a8", "process7a8");
            const int ret = b.add(Opcode::Return, id + ":return damage");

            b.next(setup, acroEligible);
            b.branch(acroEligible, acroRoll, acroSkip);
            b.next(acroRoll, acroDodge);
            b.branch(acroDodge, callAcroDodge, acroCounter);
            b.next(callAcroDodge, returnAcroDodge);
            b.branch(acroCounter, callCounter, criticalSlot);
            b.next(callCounter, returnCounter);
            b.next(acroSkip, criticalSlot);
            b.next(criticalSlot, statusClear);
            b.branch(statusClear, acroOff, evadeSlot);
            b.branch(acroOff, dodgeRoll, shieldRoll);
            b.next(dodgeRoll, dodged);
            b.branch(dodged, markDodge, shieldRoll);
            b.next(markDodge, evadeSlot);
            b.next(shieldRoll, shielded);
            b.branch(shielded, markShield, evadeSlot);
            b.next(markShield, evadeSlot);
            b.next(evadeSlot, damage);
            b.next(damage, hit);
            b.branch(hit, wakeSkip, zeroDamage);
            b.next(wakeSkip, unknownSkip);
            if (hpHoover) {
                b.next(unknownSkip, defenceScale);
                b.next(defenceScale, hooverHeal);
                b.next(hooverHeal, processCharge);
                b.next(zeroDamage, processCharge);
            } else {
                b.next(unknownSkip, defenceScale);
                b.next(zeroDamage, defenceScale);
                b.next(defenceScale, processCharge);
            }
            b.next(processCharge, ret);
            return std::move(b).finish();
        }

        Routine makeEnemyVictimiserRoutine() {
            RuleRoutineBuilder b("enemy-victimiser");
            b.initialize(ScalarSlot::DodgeFlag, 0.0);
            b.initialize(ScalarSlot::ShieldFlag, 0.0);
            const int setup = b.add(Opcode::SkipRng, "victimiser:setup", 2);
            const int acroSlot = b.add(Opcode::SkipRng, "victimiser:acro-never-triggers-slot", 1);
            const int criticalSlot = b.add(Opcode::SkipRng, "victimiser:critical-slot", 1);
            const int statusClear = b.addBranch(
                "victimiser:hero not-paralysis and not-inactive",
                allOf({
                    comparison(stateValue(StateField::Paralysis), CompareOp::Eq, constantValue(0)),
                    comparison(stateValue(StateField::Inactive), CompareOp::Eq, constantValue(0)),
                }));
            const int acroOff = b.addBranch(
                "victimiser:Acro==OFF",
                allOf({comparison(stateValue(StateField::Acro), CompareOp::Eq, constantValue(0))}));
            const int dodgeRoll = b.addRngRead(
                "victimiser:dodge-percent",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 100, 0.0, 0.0});
            const int dodged = b.addBranch(
                "victimiser:dodge-percent<2",
                allOf({comparison(scalarValue(ScalarSlot::Random0), CompareOp::Lt, constantValue(2))}));
            const int markDodge = b.addScalarUpdate(
                "victimiser:dodged=true",
                {ScalarUpdateKind::SetConstant, ScalarSlot::DodgeFlag, ScalarSlot::None,
                 FixedScalarSource::None, 1, 0, 1});
            const int shieldRoll = b.addRngRead(
                "victimiser:shield-percent",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 100, 0.0, 0.0});
            const int shielded = b.addBranch(
                "victimiser:shield-percent<0.5",
                allOf({comparison(scalarValue(ScalarSlot::Random0), CompareOp::Lt, constantValue(0.5))}));
            const int markShield = b.addScalarUpdate(
                "victimiser:shielded=true",
                {ScalarUpdateKind::SetConstant, ScalarSlot::ShieldFlag, ScalarSlot::None,
                 FixedScalarSource::None, 1, 0, 1});
            const int evadeSlot = b.add(Opcode::SkipRng, "victimiser:evade-slot", 1);
            const int damage = b.addNative(
                "native:physical-damage",
                ScalarSlot::Damage,
                {BasePlayers[1].atk, BasePlayers[0].def});
            const int scale = b.addScalarUpdate(
                "victimiser:damage*=1.5",
                {ScalarUpdateKind::MultiplyRational, ScalarSlot::Damage, ScalarSlot::None,
                 FixedScalarSource::None, 0, 3, 2});
            const int hit = b.addBranch(
                "victimiser:not-dodged-and-not-shielded",
                allOf({
                    comparison(scalarValue(ScalarSlot::DodgeFlag), CompareOp::Eq, constantValue(0)),
                    comparison(scalarValue(ScalarSlot::ShieldFlag), CompareOp::Eq, constantValue(0)),
                }));
            const int wakeSkip = b.add(Opcode::SkipRng, "victimiser:wake-check", 1);
            const int unknownSkip = b.add(Opcode::SkipRng, "victimiser:post-hit-unknown", 1);
            const int zeroDamage = b.addScalarUpdate(
                "victimiser:damage=0",
                {ScalarUpdateKind::SetConstant, ScalarSlot::Damage, ScalarSlot::None,
                 FixedScalarSource::None, 0, 0, 1});
            const int defenceScale = b.addScalarUpdate(
                "victimiser:damage*=hero.defence",
                {ScalarUpdateKind::MultiplyByDefenceFlag, ScalarSlot::Damage});
            const int processCharge = b.addCall("call:process7a8", "process7a8");
            const int ret = b.add(Opcode::Return, "victimiser:return damage");

            b.next(setup, acroSlot);
            b.next(acroSlot, criticalSlot);
            b.next(criticalSlot, statusClear);
            b.branch(statusClear, acroOff, evadeSlot);
            b.branch(acroOff, dodgeRoll, shieldRoll);
            b.next(dodgeRoll, dodged);
            b.branch(dodged, markDodge, shieldRoll);
            b.next(markDodge, evadeSlot);
            b.next(shieldRoll, shielded);
            b.branch(shielded, markShield, evadeSlot);
            b.next(markShield, evadeSlot);
            b.next(evadeSlot, damage);
            b.next(damage, scale);
            b.next(scale, hit);
            b.branch(hit, wakeSkip, zeroDamage);
            b.next(wakeSkip, unknownSkip);
            b.next(unknownSkip, defenceScale);
            b.next(zeroDamage, defenceScale);
            b.next(defenceScale, processCharge);
            b.next(processCharge, ret);
            return std::move(b).finish();
        }

        Routine makeEnemyManazashiRoutine() {
            RuleRoutineBuilder b("enemy-manazashi");
            const int setup = b.add(Opcode::SkipRng, "manazashi:setup", 2);
            const int critical = b.add(Opcode::SkipRng, "manazashi:critical-slot", 1);
            const int unrelated = b.add(Opcode::SkipRng, "manazashi:unrelated", 1);
            const int evade = b.add(Opcode::SkipRng, "manazashi:evade-slot", 1);
            const int damage = b.addNative(
                "native:type-d-damage",
                ScalarSlot::Damage,
                {4.0, 12.0});
            const int paralysisRoll = b.addRngRead(
                "manazashi:paralysis-percent",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 100, 0.0, 0.0});
            const int paralysis = b.addBranch(
                "manazashi:paralysis-percent<25",
                allOf({comparison(scalarValue(ScalarSlot::Random0), CompareOp::Lt, constantValue(25))}));
            const int setParalysis = b.addStateUpdate(
                "manazashi:Paralysis=ON(4), level++",
                {
                    {StateWriteKind::SetConstant, StateField::Paralysis, ScalarSlot::None, 7},
                    {StateWriteKind::SetConstant, StateField::RawParalysisTimer, ScalarSlot::None, 4},
                    {StateWriteKind::Increment, StateField::ParalysisLevel, ScalarSlot::None, 0},
                });
            const int unknown = b.add(Opcode::SkipRng, "manazashi:post-paralysis-unknown", 1);
            const int defenceScale = b.addScalarUpdate(
                "manazashi:damage*=hero.defence",
                {ScalarUpdateKind::MultiplyByDefenceFlag, ScalarSlot::Damage});
            const int processCharge = b.addCall("call:process7a8", "process7a8");
            const int ret = b.add(Opcode::Return, "manazashi:return damage");
            b.next(setup, critical);
            b.next(critical, unrelated);
            b.next(unrelated, evade);
            b.next(evade, damage);
            b.next(damage, paralysisRoll);
            b.next(paralysisRoll, paralysis);
            b.branch(paralysis, setParalysis, unknown);
            b.next(setParalysis, unknown);
            b.next(unknown, defenceScale);
            b.next(defenceScale, processCharge);
            b.next(processCharge, ret);
            return std::move(b).finish();
        }

        Routine makeEnemyCrackRoutine() {
            RuleRoutineBuilder b("enemy-crack");
            b.initialize(ScalarSlot::ShieldFlag, 0.0);
            const int setup = b.add(Opcode::SkipRng, "enemy-crack:setup", 2);
            const int unrelated = b.add(Opcode::SkipRng, "enemy-crack:unrelated", 1);
            const int critical = b.add(Opcode::SkipRng, "enemy-crack:critical-slot", 1);
            const int statusClear = b.addBranch(
                "enemy-crack:hero not-paralysis and not-inactive",
                allOf({
                    comparison(stateValue(StateField::Paralysis), CompareOp::Eq, constantValue(0)),
                    comparison(stateValue(StateField::Inactive), CompareOp::Eq, constantValue(0)),
                }));
            const int shieldRoll = b.addRngRead(
                "enemy-crack:shield-percent",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 100, 0.0, 0.0});
            const int shielded = b.addBranch(
                "enemy-crack:shield-percent<0.5",
                allOf({comparison(scalarValue(ScalarSlot::Random0), CompareOp::Lt, constantValue(0.5))}));
            const int markShield = b.addScalarUpdate(
                "enemy-crack:shielded=true",
                {ScalarUpdateKind::SetConstant, ScalarSlot::ShieldFlag, ScalarSlot::None,
                 FixedScalarSource::None, 1, 0, 1});
            const int evade = b.add(Opcode::SkipRng, "enemy-crack:evade-slot", 1);
            const int damage = b.addNative(
                "native:type-d-damage",
                ScalarSlot::Damage,
                {5.0, 17.0});
            const int hit = b.addBranch(
                "enemy-crack:not-shielded",
                allOf({comparison(scalarValue(ScalarSlot::ShieldFlag), CompareOp::Eq, constantValue(0))}));
            const int hitSkip = b.add(Opcode::SkipRng, "enemy-crack:hit-extra-skip", 1);
            const int zeroDamage = b.addScalarUpdate(
                "enemy-crack:damage=0",
                {ScalarUpdateKind::SetConstant, ScalarSlot::Damage, ScalarSlot::None,
                 FixedScalarSource::None, 0, 0, 1});
            const int defenceScale = b.addScalarUpdate(
                "enemy-crack:damage*=hero.defence",
                {ScalarUpdateKind::MultiplyByDefenceFlag, ScalarSlot::Damage});
            const int processCharge = b.addCall("call:process7a8", "process7a8");
            const int ret = b.add(Opcode::Return, "enemy-crack:return damage");
            b.next(setup, unrelated);
            b.next(unrelated, critical);
            b.next(critical, statusClear);
            b.branch(statusClear, shieldRoll, evade);
            b.next(shieldRoll, shielded);
            b.branch(shielded, markShield, evade);
            b.next(markShield, evade);
            b.next(evade, damage);
            b.next(damage, hit);
            b.branch(hit, hitSkip, zeroDamage);
            b.next(hitSkip, defenceScale);
            b.next(zeroDamage, defenceScale);
            b.next(defenceScale, processCharge);
            b.next(processCharge, ret);
            return std::move(b).finish();
        }

        Routine makeEnemyPuffPuffRoutine() {
            RuleRoutineBuilder b("enemy-puff-puff");
            b.initialize(ScalarSlot::Damage, 0.0);
            const int setup = b.add(Opcode::SkipRng, "puff:setup", 2);
            const int unrelated = b.add(Opcode::SkipRng, "puff:unrelated", 1);
            const int critical = b.add(Opcode::SkipRng, "puff:critical-slot", 1);
            const int inactiveRoll = b.addRngRead(
                "puff:inactive-percent",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 100, 0.0, 0.0});
            const int inactive = b.addBranch(
                "puff:inactive-percent<50",
                allOf({comparison(scalarValue(ScalarSlot::Random0), CompareOp::Lt, constantValue(50))}));
            const int setInactive = b.addStateUpdate(
                "puff:Inactive=ON",
                {{StateWriteKind::SetConstant, StateField::Inactive, ScalarSlot::None, 1}});
            const int paralysisEarlyReturn = b.addBranch(
                "puff:Inactive==OFF and Paralysis!=CLEAR",
                allOf({
                    comparison(stateValue(StateField::Inactive), CompareOp::Eq, constantValue(0)),
                    comparison(stateValue(StateField::Paralysis), CompareOp::Ne, constantValue(0)),
                }));
            const int inactiveNow = b.addBranch(
                "puff:Inactive==ON",
                allOf({comparison(stateValue(StateField::Inactive), CompareOp::Eq, constantValue(1))}));
            const int damage = b.addNative(
                "native:physical-damage",
                ScalarSlot::Damage,
                {BasePlayers[1].atk, BasePlayers[0].def});
            const int discardDamage = b.addScalarUpdate(
                "puff:discard physical-damage result",
                {ScalarUpdateKind::SetConstant, ScalarSlot::Damage, ScalarSlot::None,
                 FixedScalarSource::None, 0, 0, 1});
            const int unknown = b.add(Opcode::SkipRng, "puff:inactive-post-damage-unknown", 1);
            const int chargeOff = b.addBranch(
                "puff:defender Charge==OFF",
                allOf({comparison(stateValue(StateField::Charge), CompareOp::Eq, constantValue(0))}));
            const int chargeSkip = b.add(Opcode::SkipRng, "puff:defender-charge-skip", 1);
            const int ret = b.add(Opcode::Return, "puff:return 0");
            b.next(setup, unrelated);
            b.next(unrelated, critical);
            b.next(critical, inactiveRoll);
            b.next(inactiveRoll, inactive);
            b.branch(inactive, setInactive, paralysisEarlyReturn);
            b.next(setInactive, paralysisEarlyReturn);
            b.branch(paralysisEarlyReturn, ret, inactiveNow);
            b.branch(inactiveNow, damage, chargeOff);
            b.next(damage, discardDamage);
            b.next(discardDamage, unknown);
            b.next(unknown, ret);
            b.branch(chargeOff, chargeSkip, ret);
            b.next(chargeSkip, ret);
            return std::move(b).finish();
        }

        Routine makeSelectEnemyRoutine() {
            RuleRoutineBuilder b("select-enemy");
            const int mitoreRoll = b.addRngRead(
                "select-enemy:mitore-primary",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 100, 0.0, 0.0});
            const int mitoreGate = b.addBranch(
                "select-enemy:mitore-disabled",
                allOf({comparison(constantValue(0), CompareOp::Lt, constantValue(0))}));
            const int mitoreSecond = b.addRngRead(
                "select-enemy:mitore-secondary",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 100, 0.0, 0.0});
            const int mitoreHit = b.addBranch(
                "select-enemy:mitore-secondary<90",
                allOf({comparison(scalarValue(ScalarSlot::Random0), CompareOp::Lt, constantValue(90))}));
            const int setInactive = b.addStateUpdate(
                "select-enemy:action=INACTIVE_ENEMY",
                {{StateWriteKind::SetConstant, StateField::CurrentAction, ScalarSlot::None,
                  BattleEmulator::INACTIVE_ENEMY}});
            const int selectorRoll = b.addRngRead(
                "select-enemy:ProcessEnemyRandomAction2B",
                {RngReadKind::Percent, ScalarSlot::Random0, 0, 0x100, 0.0, 0.0});
            const int selectorSwitch = b.add(Opcode::Switch, "select-enemy:weighted-six-way");

            const std::vector<int> actions = {
                BattleEmulator::VICTIMISER,
                BattleEmulator::HP_HOOVER,
                BattleEmulator::CRACK_ENEMY,
                BattleEmulator::ATTACK_ENEMY,
                BattleEmulator::MANAZASHI,
                BattleEmulator::PUFF_PUFF,
            };
            std::vector<int> setAction;
            for (int action: actions) {
                setAction.push_back(b.addStateUpdate(
                    "select-enemy:action=" + std::to_string(action),
                    {{StateWriteKind::SetConstant, StateField::CurrentAction, ScalarSlot::None, action}}));
            }

            const int puffRestriction = b.addBranch(
                "select-enemy:action==PUFF_PUFF and hero Inactive",
                allOf({
                    comparison(stateValue(StateField::CurrentAction), CompareOp::Eq,
                               constantValue(BattleEmulator::PUFF_PUFF)),
                    comparison(stateValue(StateField::Inactive), CompareOp::Eq, constantValue(1)),
                }));
            const int replacePuff = b.addStateUpdate(
                "select-enemy:PUFF_PUFF->MANAZASHI",
                {{StateWriteKind::SetConstant, StateField::CurrentAction, ScalarSlot::None,
                  BattleEmulator::MANAZASHI}});
            const int victimiserRestriction = b.addBranch(
                "select-enemy:action==VICTIMISER and hero Paralysis==CLEAR",
                allOf({
                    comparison(stateValue(StateField::CurrentAction), CompareOp::Eq,
                               constantValue(BattleEmulator::VICTIMISER)),
                    comparison(stateValue(StateField::Paralysis), CompareOp::Eq, constantValue(0)),
                }));
            const int replaceVictimiser = b.addStateUpdate(
                "select-enemy:VICTIMISER->HP_HOOVER",
                {{StateWriteKind::SetConstant, StateField::CurrentAction, ScalarSlot::None,
                  BattleEmulator::HP_HOOVER}});
            const int manazashi = b.addBranch(
                "select-enemy:result action==MANAZASHI",
                allOf({comparison(stateValue(StateField::CurrentAction), CompareOp::Eq,
                                  constantValue(BattleEmulator::MANAZASHI))}));
            const int manazashiSkips = b.add(Opcode::SkipRng, "select-enemy:MANAZASHI target-range slots", 2);
            const int rageOff = b.addBranch(
                "select-enemy:enemy Rage==OFF",
                allOf({comparison(stateValue(StateField::Rage), CompareOp::Eq, constantValue(0))}));
            const int targetSkip = b.add(Opcode::SkipRng, "select-enemy:target-selection-skip", 1);
            const int finalSkip = b.add(Opcode::SkipRng, "select-enemy:final-slot", 1);
            const int ret = b.add(Opcode::Return, "select-enemy:return action");

            b.next(mitoreRoll, mitoreGate);
            b.branch(mitoreGate, mitoreSecond, selectorRoll);
            b.next(mitoreSecond, mitoreHit);
            b.branch(mitoreHit, setInactive, selectorRoll);
            b.next(setInactive, finalSkip);
            b.next(selectorRoll, selectorSwitch);
            b.cases(selectorSwitch, actions, setAction);
            b.switchSource(
                selectorSwitch,
                {
                    SwitchSourceKind::WeightedScalarUpperBounds,
                    ScalarSlot::Random0,
                    {42, 84, 127, 170, 212, 255},
                });
            for (int setter: setAction) {
                b.next(setter, puffRestriction);
            }
            b.branch(puffRestriction, replacePuff, victimiserRestriction);
            b.next(replacePuff, victimiserRestriction);
            b.branch(victimiserRestriction, replaceVictimiser, manazashi);
            b.next(replaceVictimiser, manazashi);
            b.branch(manazashi, manazashiSkips, rageOff);
            b.next(manazashiSkips, finalSkip);
            b.branch(rageOff, targetSkip, finalSkip);
            b.next(targetSkip, finalSkip);
            b.next(finalSkip, ret);
            return std::move(b).finish();
        }

        Routine makeApplyEnemyResultRoutine() {
            RuleRoutineBuilder b("apply-enemy-result");
            const int reduce = b.addResourceUpdate(
                "apply-enemy-result:A=max(0,A-damage)",
                {
                    ResourceUpdateKind::ClampSubtractSlot,
                    ResourceAxis::HeroHp,
                    ScalarSlot::Damage,
                    0,
                    0,
                    65,
                });
            const int ret = b.add(Opcode::Return, "apply-enemy-result:return");
            b.next(reduce, ret);
            return std::move(b).finish();
        }

        Routine makeApplyAllyResultRoutine() {
            RuleRoutineBuilder b("apply-ally-result");
            const int dispatch = b.add(Opcode::Switch, "apply-ally-result:executed-action");
            const int heal = b.addResourceUpdate(
                "apply-ally-result:A=min(65,A+amount)",
                {
                    ResourceUpdateKind::ClampAddSlot,
                    ResourceAxis::HeroHp,
                    ScalarSlot::Damage,
                    0,
                    0,
                    65,
                });
            const int damage = b.addResourceUpdate(
                "apply-ally-result:E=max(0,E-damage)",
                {
                    ResourceUpdateKind::ClampSubtractSlot,
                    ResourceAxis::EnemyHp,
                    ScalarSlot::Damage,
                    0,
                    0,
                    456,
                });
            const int ret = b.add(Opcode::Return, "apply-ally-result:return");

            const std::vector<int> actions = {
                BattleEmulator::ATTACK_ALLY,
                BattleEmulator::DRAGON_SLASH,
                BattleEmulator::DEFENCE,
                BattleEmulator::MEDICINAL_HERBS,
                BattleEmulator::HEAL,
                BattleEmulator::CRACK_ALLY,
                BattleEmulator::ACROBATIC_STAR,
                BattleEmulator::PARALYSIS,
                BattleEmulator::CURE_PARALYSIS,
                BattleEmulator::INACTIVE_ALLY,
            };
            std::vector<int> targets;
            for (int action: actions) {
                targets.push_back(
                    action == BattleEmulator::HEAL || action == BattleEmulator::MEDICINAL_HERBS
                        ? heal
                        : damage);
            }
            b.cases(dispatch, actions, targets);
            b.switchSource(dispatch, {SwitchSourceKind::CurrentAction});
            b.next(heal, ret);
            b.next(damage, ret);
            return std::move(b).finish();
        }

        Routine makeEnemyPostRoutineExact() {
            RuleRoutineBuilder b("enemy-post");
            const int alive = b.addBranch(
                "enemy-post:both players alive",
                allOf({
                    comparison(resourceValue(ResourceAxis::HeroHp), CompareOp::Gt, constantValue(0)),
                    comparison(resourceValue(ResourceAxis::EnemyHp), CompareOp::Gt, constantValue(0)),
                }));
            const int aliveSkip = b.add(Opcode::SkipRng, "enemy-post:alive-extra", 1);
            const int rageOn = b.addBranch(
                "enemy-post:Rage==ON",
                allOf({comparison(stateValue(StateField::Rage), CompareOp::Ne, constantValue(0))}));
            const int decrement = b.addStateUpdate(
                "enemy-post:rageTurns--",
                {{StateWriteKind::Decrement, StateField::RawRageTimer, ScalarSlot::None, 0}});
            const int expired = b.addBranch(
                "enemy-post:rageTurns<=0",
                allOf({comparison(stateValue(StateField::RawRageTimer), CompareOp::Le, constantValue(0))}));
            const int clear = b.addStateUpdate(
                "enemy-post:Rage=OFF",
                {{StateWriteKind::SetConstant, StateField::Rage, ScalarSlot::None, 0}});
            const int ret = b.add(Opcode::Return, "enemy-post:return");
            b.branch(alive, aliveSkip, ret);
            b.next(aliveSkip, rageOn);
            b.branch(rageOn, decrement, ret);
            b.next(decrement, expired);
            b.branch(expired, clear, ret);
            b.next(clear, ret);
            return std::move(b).finish();
        }

        Routine makeAllyPostRoutineExact() {
            RuleRoutineBuilder b("ally-post");
            const int alive = b.addBranch(
                "ally-post:both players alive",
                allOf({
                    comparison(resourceValue(ResourceAxis::HeroHp), CompareOp::Gt, constantValue(0)),
                    comparison(resourceValue(ResourceAxis::EnemyHp), CompareOp::Gt, constantValue(0)),
                }));
            const int aliveSkip = b.add(Opcode::SkipRng, "ally-post:alive-extra", 1);
            const int decrement = b.addStateUpdate(
                "ally-post:acroTurn-- raw even when Acro OFF",
                {{StateWriteKind::Decrement, StateField::RawAcroTimer, ScalarSlot::None, 0}});
            const int expire = b.addBranch(
                "ally-post:Acro==ON and acroTurn==0",
                allOf({
                    comparison(stateValue(StateField::Acro), CompareOp::Ne, constantValue(0)),
                    comparison(stateValue(StateField::RawAcroTimer), CompareOp::Eq, constantValue(0)),
                }));
            const int clear = b.addStateUpdate(
                "ally-post:Acro=OFF",
                {{StateWriteKind::SetConstant, StateField::Acro, ScalarSlot::None, 0}});
            const int expireSkip = b.add(Opcode::SkipRng, "ally-post:acro-expire-skip", 1);
            const int ret = b.add(Opcode::Return, "ally-post:return");
            b.branch(alive, aliveSkip, ret);
            b.next(aliveSkip, decrement);
            b.next(decrement, expire);
            b.branch(expire, clear, ret);
            b.next(clear, expireSkip);
            b.next(expireSkip, ret);
            return std::move(b).finish();
        }

        std::vector<int> cameraActionValues() {
            return {
                0,
                BattleEmulator::ATTACK_ALLY,
                BattleEmulator::DRAGON_SLASH,
                BattleEmulator::DEFENCE,
                BattleEmulator::MEDICINAL_HERBS,
                BattleEmulator::HEAL,
                BattleEmulator::CRACK_ALLY,
                BattleEmulator::ACROBATIC_STAR,
                BattleEmulator::PARALYSIS,
                BattleEmulator::CURE_PARALYSIS,
                BattleEmulator::INACTIVE_ALLY,
                BattleEmulator::VICTIMISER,
                BattleEmulator::HP_HOOVER,
                BattleEmulator::CRACK_ENEMY,
                BattleEmulator::ATTACK_ENEMY,
                BattleEmulator::MANAZASHI,
                BattleEmulator::PUFF_PUFF,
                BattleEmulator::INACTIVE_ENEMY,
            };
        }

        Routine makeCameraParam1Routine() {
            RuleRoutineBuilder b("camera-param1");
            const int firstSkip = b.add(Opcode::SkipRng, "camera-param1:first-skip", 1);
            const int counterZero = b.addBranch(
                "camera-param1:counter==0",
                allOf({comparison(stateValue(StateField::Camera), CompareOp::Eq, constantValue(0))}));
            const int zeroSkip = b.add(Opcode::SkipRng, "camera-param1:counter-zero-forced-skip", 1);
            const int nonzeroSkip = b.add(Opcode::SkipRng, "camera-param1:counter-nonzero-skip", 1);
            const int zeroReset = b.addStateUpdate(
                "camera-param1:Camera=0 from counter-zero",
                {{StateWriteKind::SetConstant, StateField::Camera, ScalarSlot::None, 0}});
            const int nonzeroReset = b.addStateUpdate(
                "camera-param1:Camera=0 from counter-nonzero",
                {{StateWriteKind::SetConstant, StateField::Camera, ScalarSlot::None, 0}});
            const int finalSkip = b.add(Opcode::SkipRng, "camera-param1:nonzero-final-skip", 1);
            const int ret = b.add(Opcode::Return, "camera-param1:return");
            b.next(firstSkip, counterZero);
            b.branch(counterZero, zeroSkip, nonzeroSkip);
            b.next(zeroSkip, zeroReset);
            b.next(zeroReset, ret);
            b.next(nonzeroSkip, nonzeroReset);
            b.next(nonzeroReset, finalSkip);
            b.next(finalSkip, ret);
            return std::move(b).finish();
        }

        Routine makeCameraParam0Routine() {
            RuleRoutineBuilder b("camera-param0");
            const int firstSkip = b.add(Opcode::SkipRng, "camera-param0:first-skip", 1);
            const int counterZero = b.addBranch(
                "camera-param0:counter==0",
                allOf({comparison(stateValue(StateField::Camera), CompareOp::Eq, constantValue(0))}));
            const int increment = b.addStateUpdate(
                "camera-param0:Camera=counter+1",
                {{StateWriteKind::Increment, StateField::Camera, ScalarSlot::None, 0}});
            const int roll = b.addRngRead(
                "camera-param0:percent(5-counter)",
                {RngReadKind::PercentCameraRemaining, ScalarSlot::Random0});
            const int resetGate = b.addBranch(
                "camera-param0:ret==0 or counter==5",
                anyOf({
                    clause({comparison(scalarValue(ScalarSlot::Random0), CompareOp::Eq, constantValue(0))}),
                    clause({comparison(stateValue(StateField::Camera), CompareOp::Eq, constantValue(5))}),
                }));
            const int reset = b.addStateUpdate(
                "camera-param0:Camera=0",
                {{StateWriteKind::SetConstant, StateField::Camera, ScalarSlot::None, 0}});
            const int resetSkip = b.add(Opcode::SkipRng, "camera-param0:reset-extra-skip", 1);
            const int ret = b.add(Opcode::Return, "camera-param0:return");
            b.next(firstSkip, counterZero);
            b.branch(counterZero, increment, roll);
            b.next(increment, ret);
            b.next(roll, resetGate);
            b.branch(resetGate, reset, increment);
            b.next(reset, resetSkip);
            b.next(resetSkip, ret);
            return std::move(b).finish();
        }

        Routine makeCameraFirstSlotRoutine() {
            RuleRoutineBuilder b("camera-slot0");
            const int dispatch = b.add(Opcode::Switch, "camera-slot0:action");
            const int attackAlly = b.addCall("call:camera-param1", "camera-param1");
            const int tracking = b.add(Opcode::SkipRng, "camera-slot0:tracking-camera", 1);
            const int clearPreemptive = b.addScalarUpdate(
                "camera-slot0:preemptive=false",
                {ScalarUpdateKind::SetConstant, ScalarSlot::PreemptiveFlag, ScalarSlot::None,
                 FixedScalarSource::None, 0, 0, 1});
            const int ret = b.add(Opcode::Return, "camera-slot0:return");

            const std::vector<int> values = cameraActionValues();
            std::vector<int> successors;
            successors.reserve(values.size());
            for (int action: values) {
                if (action == BattleEmulator::ATTACK_ALLY) {
                    successors.push_back(attackAlly);
                } else if (action == BattleEmulator::ATTACK_ENEMY || action == BattleEmulator::DRAGON_SLASH) {
                    successors.push_back(tracking);
                } else {
                    successors.push_back(clearPreemptive);
                }
            }
            b.cases(dispatch, values, successors);
            b.switchSource(dispatch, {SwitchSourceKind::ActionHistorySlot0});
            b.next(attackAlly, ret);
            b.next(tracking, clearPreemptive);
            b.next(clearPreemptive, ret);
            return std::move(b).finish();
        }

        Routine makeCameraSecondSlotRoutine() {
            RuleRoutineBuilder b("camera-slot1");
            const int dispatch = b.add(Opcode::Switch, "camera-slot1:action");
            const int attackAlly = b.addBranch(
                "camera-slot1:preemptive still true",
                allOf({comparison(scalarValue(ScalarSlot::PreemptiveFlag), CompareOp::Eq, constantValue(1))}));
            const int param1 = b.addCall("call:camera-param1", "camera-param1");
            const int param0 = b.addCall("call:camera-param0", "camera-param0");
            const int tracking = b.add(Opcode::SkipRng, "camera-slot1:tracking-camera", 1);
            const int ret = b.add(Opcode::Return, "camera-slot1:return");

            const std::vector<int> values = cameraActionValues();
            std::vector<int> successors;
            successors.reserve(values.size());
            for (int action: values) {
                if (action == BattleEmulator::ATTACK_ALLY) {
                    successors.push_back(attackAlly);
                } else if (action == BattleEmulator::ATTACK_ENEMY || action == BattleEmulator::DRAGON_SLASH) {
                    successors.push_back(tracking);
                } else {
                    successors.push_back(ret);
                }
            }
            b.cases(dispatch, values, successors);
            b.switchSource(dispatch, {SwitchSourceKind::ActionHistorySlot1});
            b.branch(attackAlly, param1, param0);
            b.next(param1, ret);
            b.next(param0, ret);
            b.next(tracking, ret);
            return std::move(b).finish();
        }

        Routine makeCameraTailRoutine() {
            RuleRoutineBuilder b("camera-tail");
            b.initialize(ScalarSlot::PreemptiveFlag, 1.0);
            const int slot0 = b.addCall("call:camera-slot0", "camera-slot0");
            const int slot1 = b.addCall("call:camera-slot1", "camera-slot1");
            const int ret = b.add(Opcode::Return, "camera-tail:return");
            b.next(slot0, slot1);
            b.next(slot1, ret);
            return std::move(b).finish();
        }

        Routine makeAllyPostRoutine() {
            return makeAllyPostRoutineExact();
        }

        Routine makeEnemyPostRoutine() {
            return makeEnemyPostRoutineExact();
        }

        Routine makeTurnTailRoutine() {
            RuleRoutineBuilder b("turn-tail");
            const int alive = b.addBranch(
                "turn-tail:both players alive",
                allOf({
                    comparison(resourceValue(ResourceAxis::HeroHp), CompareOp::Gt, constantValue(0)),
                    comparison(resourceValue(ResourceAxis::EnemyHp), CompareOp::Gt, constantValue(0)),
                }));
            const int aliveSkip = b.add(Opcode::SkipRng, "turn-tail:alive-extra", 1);
            const int camera = b.addCall("call:camera-tail", "camera-tail");
            const int ret = b.add(Opcode::Return, "turn-tail:return");
            b.branch(alive, aliveSkip, camera);
            b.next(aliveSkip, camera);
            b.next(camera, ret);
            return std::move(b).finish();
        }
    } // namespace

    RegistrationResult RegistrationChecker::check(
        const RuleProgram &program,
        const std::vector<NativeContract> &nativeContracts,
        const RuleProfile &profile) {
        if (program.instructionFormatVersion != 1) {
            return rejected("unsupported instruction format");
        }
        if (program.entryRoutine.empty() || program.routines.empty()) {
            return rejected("RuleProgram has no entry");
        }

        if (RegistrationResult profileCheck = validateProfile(profile); !profileCheck.accepted) {
            return profileCheck;
        }

        RoutineIndex routineIndex;
        if (RegistrationResult indexCheck = buildRoutineIndex(program, routineIndex); !indexCheck.accepted) {
            return indexCheck;
        }

        for (const CompletionSite &site: profile.completionSites) {
            const Routine *routine = findRoutine(routineIndex, site.pc.routineId);
            if (routine == nullptr || site.pc.instructionIndex >= static_cast<int>(routine->instructions.size())) {
                return rejected("COMPLETE cut site references an unresolved pc");
            }
            if (site.pc.routineId != program.entryRoutine) {
                return rejected("v1 COMPLETE cut sites must be TURN program points");
            }
            const Instruction &instruction = routine->instructions[site.pc.instructionIndex];
            switch (site.cut) {
                case CompletionCutId::TurnEntry:
                    if (site.pc.instructionIndex != 0) {
                        return rejected("turn-entry COMPLETE must be registered at TURN pc 0");
                    }
                    break;
                case CompletionCutId::AllyDoneEnemyPending:
                    if (instruction.opcode != Opcode::Call || instruction.callTarget != "enemy-slot") {
                        return rejected("ally-done COMPLETE must precede the typed enemy-slot CALL");
                    }
                    break;
                case CompletionCutId::EnemyDoneAllyPending:
                    if (instruction.opcode != Opcode::Call || instruction.callTarget != "ally-slot") {
                        return rejected("enemy-done COMPLETE must precede the typed ally-slot CALL");
                    }
                    break;
                case CompletionCutId::ActionsDone:
                    if (instruction.opcode != Opcode::Call || instruction.callTarget != "turn-tail") {
                        return rejected("actions-done COMPLETE must precede the typed turn-tail CALL");
                    }
                    break;
            }
        }

        NativeIndex nativeIndex;
        if (RegistrationResult nativeCheck = validateNativeContracts(nativeContracts, nativeIndex); !nativeCheck.
            accepted) {
            return nativeCheck;
        }

        CallGraph callGraph;
        for (const Routine &routine: program.routines) {
            RoutineAnalysis analysis = analyzeRoutine(routine, routineIndex, nativeIndex, callGraph);
            if (!analysis.accepted) {
                return rejected(analysis.reason);
            }
        }

        int maximumCallDepth = 0;
        if (RegistrationResult callCheck = validateAcyclicCallGraph(program, callGraph, maximumCallDepth);
            !callCheck.accepted) {
            return callCheck;
        }

        if (RegistrationResult contractCheck = validateRoutineContracts(program, routineIndex);
            !contractCheck.accepted) {
            return contractCheck;
        }

        RegistrationResult result = StaticBoundComputer(routineIndex, nativeIndex).run(program);
        if (!result.accepted) {
            return result;
        }

        if (result.bounds.rMax <= 0 || result.bounds.rMax >= ExactReplay::kRngTapeSize) {
            return rejected("invalid computed Rmax");
        }

        return result;
    }

    bool RuleRegistry::registerNew(RuleBundle candidate, std::string &error) {
        if (lookup(candidate.id) != nullptr) {
            error = "rule_id already registered";
            return false;
        }

        RegistrationResult runtimeContract = validateRuntimeContracts(candidate);
        if (!runtimeContract.accepted) {
            error = runtimeContract.reason;
            return false;
        }

        if (candidate.id == RuleId{"yo2_be.d20", 1}) {
            const RuleBundle canonical = makeYo2BeBundleCandidate();
            if (candidate.program != canonical.program ||
                candidate.nativeContracts != canonical.nativeContracts ||
                candidate.profile != canonical.profile ||
                candidate.numericEnvironment != canonical.numericEnvironment ||
                candidate.exactReplay != canonical.exactReplay) {
                error = "yo2_be.d20:1 content differs from the canonical immutable RuleBundle";
                return false;
            }
        }

        RegistrationResult registration =
                RegistrationChecker::check(candidate.program, candidate.nativeContracts, candidate.profile);
        if (!registration.accepted) {
            error = registration.reason;
            return false;
        }

        candidate.bounds = registration.bounds;
        candidate.registered = true;
        bundles_.push_back(std::move(candidate));
        return true;
    }

    const RuleBundle *RuleRegistry::lookup(const RuleId &id) const noexcept {
        for (const RuleBundle &bundle: bundles_) {
            if (bundle.id == id) {
                return &bundle;
            }
        }
        return nullptr;
    }

    const PcStaticBounds *lookupPcBounds(
        const StaticBounds &bounds,
        const std::string &routineId,
        int pc) noexcept {
        for (const RoutineStaticBounds &routine: bounds.routines) {
            if (routine.routineId != routineId) {
                continue;
            }
            if (pc < 0 || pc >= static_cast<int>(routine.pc.size())) {
                return nullptr;
            }
            return &routine.pc[pc];
        }
        return nullptr;
    }

    const CommandProfile *lookupCommandProfile(
        const RuleProfile &profile,
        int command) noexcept {
        for (const CommandProfile &entry: profile.commandProfiles) {
            if (entry.command == command) {
                return &entry;
            }
        }
        return nullptr;
    }

    const CompletionSite *lookupCompletionSite(
        const RuleProfile &profile,
        CompletionCutId cut) noexcept {
        for (const CompletionSite &site: profile.completionSites) {
            if (site.cut == cut) {
                return &site;
            }
        }
        return nullptr;
    }

    RuleBundle makeYo2BeBundleCandidate() {
        RuleBundle bundle;
        bundle.id = {"yo2_be.d20", 1};
        bundle.exactReplay = {
            "d20proof::ExactReplay::replay",
            -1,
            true,
            false,
            true,
            true,
        };

        bundle.profile.heroCommands = {
            BattleEmulator::ATTACK_ALLY,
            BattleEmulator::DRAGON_SLASH,
            BattleEmulator::DEFENCE,
            BattleEmulator::FLEE_ALLY,
            BattleEmulator::MEDICINAL_HERBS,
            BattleEmulator::HEAL,
            BattleEmulator::CRACK_ALLY,
            BattleEmulator::ACROBATIC_STAR,
        };
        bundle.profile.commandProfiles = {
            {
                BattleEmulator::ATTACK_ALLY,
                0,
                0,
                kChargeMaskAll,
                kAcroMaskAll,
                kParalysisMaskAll,
                {{128, 0, 0, 0}},
            },
            {
                BattleEmulator::DRAGON_SLASH,
                0,
                0,
                kChargeMaskAll,
                kAcroMaskAll,
                kParalysisMaskAll,
                {{98, 0, 0, 0}},
            },
            {
                BattleEmulator::DEFENCE,
                0,
                0,
                kChargeMaskAll,
                kAcroMaskAll,
                kParalysisMaskAll,
                {{64, 0, 0, 0}},
            },
            {
                BattleEmulator::FLEE_ALLY,
                0,
                0,
                kChargeMaskAll,
                kAcroMaskAll,
                0x0001,
                {{64, 0, 0, 0}},
            },
            {
                BattleEmulator::MEDICINAL_HERBS,
                0,
                1,
                kChargeMaskAll,
                kAcroMaskAll,
                kParalysisMaskAll,
                {{64, 0, 0, 0}, {64, 39, 0, -1}},
            },
            {
                BattleEmulator::HEAL,
                2,
                0,
                kChargeMaskAll,
                kAcroMaskAll,
                kParalysisMaskAll,
                {{64, 0, 0, 0}, {64, 65, -2, 0}},
            },
            {
                BattleEmulator::CRACK_ALLY,
                3,
                0,
                kChargeMaskAll,
                kAcroMaskAll,
                kParalysisMaskAll,
                {{64, 0, 0, 0}, {98, 0, -3, 0}},
            },
            {
                BattleEmulator::ACROBATIC_STAR,
                0,
                0,
                0x00fc,
                0x0001,
                kParalysisMaskAll,
                {{64, 0, 0, 0}},
            },
        };
        bundle.profile.enemyMaxHp = BasePlayers[1].maxHp;
        bundle.profile.heroMaxHp = BasePlayers[0].maxHp;
        bundle.profile.heroMaxMp = BasePlayers[0].maxMp;
        bundle.profile.heroInitialHerbs = BasePlayers[0].medicinal_herbs_count;
        bundle.profile.heroAttack = BasePlayers[0].atk;
        bundle.profile.heroSpeed = BasePlayers[0].speed;
        bundle.profile.enemySpeed = BasePlayers[1].speed;
        bundle.profile.completionSites = {
            {CompletionCutId::TurnEntry, {"turn", 0}},
            {CompletionCutId::AllyDoneEnemyPending, {"turn", 15}},
            {CompletionCutId::EnemyDoneAllyPending, {"turn", 17}},
            {CompletionCutId::ActionsDone, {"turn", 18}},
        };

        bundle.program.entryRoutine = "turn";
        bundle.program.routines = {
            makeTurnRoutine(),
            makeAllySlotRoutine(),
            makeEnemySlotRoutine(),
            makeAllyStatusRoutine(),
            makeAllyActionRoutine(),
            makeEnemyActionRoutine(),
            makeAllyAttackRoutine("ally-attack"),
            makeAllyAttackRoutine("ally-dragon-slash"),
            makePassiveAllyRoutine(
                "ally-defence",
                true,
                BasePlayers[0].defaultATK,
                BasePlayers[0].def),
            makeAllyHerbRoutine(),
            makeAllyHealRoutine(),
            makeAllyCrackRoutine(),
            makeAllyAcroRoutine(),
            makePassiveAllyRoutine(
                "ally-paralysis",
                false,
                BasePlayers[0].defaultATK,
                BasePlayers[0].def),
            makePassiveAllyRoutine(
                "ally-cure-paralysis",
                true,
                BasePlayers[0].defaultATK,
                BasePlayers[0].def),
            makePassiveAllyRoutine(
                "ally-inactive",
                false,
                BasePlayers[0].defaultATK,
                BasePlayers[0].def),
            makeEnemyVictimiserRoutine(),
            makeEnemyAttackRoutine("enemy-hp-hoover", true),
            makeEnemyCrackRoutine(),
            makeEnemyAttackRoutine("enemy-attack", false),
            makeEnemyManazashiRoutine(),
            makeEnemyPuffPuffRoutine(),
            makePassiveAllyRoutine(
                "enemy-inactive",
                false,
                BasePlayers[1].defaultATK,
                BasePlayers[1].def),
            makeAcroDodgeRoutine(),
            makeCounterRoutine(),
            makeRageCrossingRoutine(),
            makeProcess7A8Routine(),
            makeSelectEnemyRoutine(),
            makeApplyAllyResultRoutine(),
            makeApplyEnemyResultRoutine(),
            makeAllyPostRoutine(),
            makeEnemyPostRoutine(),
            makeCameraParam1Routine(),
            makeCameraParam0Routine(),
            makeCameraFirstSlotRoutine(),
            makeCameraSecondSlotRoutine(),
            makeCameraTailRoutine(),
            makeTurnTailRoutine(),
        };
        bundle.program.sourceCorrespondence = {
            "BattleEmulator.cpp:248-631 Main",
            "BattleEmulator.cpp:458-595 ally slot; registered FLEE correction gates skipTurn on clear paralysis and active status",
            "BattleEmulator.cpp:368-456 enemy slot",
            "BattleEmulator.cpp:676-1080 callAttackFun",
            "BattleEmulator.cpp:1127-1152 process7A8",
            "BattleEmulator.cpp:1225-1260 ProcessRage",
            "camera.cpp:10-78 camera",
        };
        bundle.program.explicitRuleChanges = {
            "FLEE is not selectable while sleeping or paralyzed; yo2_be v1 rejects sleeping inputs",
            "At ally execution, FLEE skips the ally slot only when Paralysis=CLEAR and Inactive=OFF",
            "Enemy-first paralysis/inactive routes through the normal ally status transition; cured paralysis does not restore FLEE",
            "ExactReplay adapts BattleEmulator::Main to the same corrected FLEE rule without changing production Main semantics",
        };

        bundle.nativeContracts = {
            {
                "physical-damage",
                NativeOperation::PhysicalDamage,
                NativeImplementation::BattlePhysicalDamage,
                2,
                {{0.0, 999.0, true}, {0.0, 999.0, true}},
                2,
                32,
                true,
                ScalarSlot::Damage,
                0,
                64,
            },
            {
                "type-c-heal",
                NativeOperation::TypeC,
                NativeImplementation::BattleTypeC,
                3,
                {{35.0, 35.0, false}, {35.0, 35.0, false}, {5.0, 5.0, false}},
                2,
                16,
                true,
                ScalarSlot::Damage,
                0,
                39,
            },
            {
                "type-d-heal",
                NativeOperation::TypeD,
                NativeImplementation::BattleTypeD,
                2,
                {{5.0, 5.0, false}, {35.0, 35.0, false}},
                1,
                16,
                true,
                ScalarSlot::Damage,
                0,
                65,
            },
            {
                "type-d-damage",
                NativeOperation::TypeD,
                NativeImplementation::BattleTypeD,
                2,
                {{4.0, 5.0, false}, {12.0, 30.0, false}},
                1,
                16,
                true,
                ScalarSlot::Damage,
                0,
                34,
            },
        };

        std::string contractError;
        if (!configureYo2RoutineContracts(bundle.program, contractError)) {
            bundle.program.entryRoutine.clear();
            bundle.program.sourceCorrespondence.push_back("RuleAssembler contract error: " + contractError);
        }

        return bundle;
    }
} // namespace d20proof
