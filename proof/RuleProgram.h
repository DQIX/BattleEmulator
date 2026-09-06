#pragma once

#include "ProofTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace d20proof {
    enum class Opcode : std::uint8_t {
        Step,
        Branch,
        Switch,
        Call,
        Return,
        Finish,
        ReadRng,
        SkipRng,
        Native,
        ResourceUpdate,
        ModeUpdate,
        ScalarUpdate,
        RecordAction,
    };

    enum class ScalarSlot : std::uint8_t {
        None,
        Damage,
        Amount,
        Duration,
        Random0,
        Random1,
        SpeedHero,
        SpeedEnemy,
        CriticalFlag,
        DodgeFlag,
        ShieldFlag,
        DefenceHalfFlag,
        PreemptiveFlag,
        ActionSlot0,
        ActionSlot1,
        ActionCount,
        Count,
    };

    enum class NativeOperation : std::uint8_t {
        None,
        PhysicalDamage,
        TypeC,
        TypeD,
    };

    enum class NativeImplementation : std::uint8_t {
        None,
        BattlePhysicalDamage,
        BattleTypeC,
        BattleTypeD,
    };

    enum class RngReadKind : std::uint8_t {
        None,
        Percent,
        IntRangeInclusive,
        FloatRange,
        PercentCameraRemaining,
    };

    struct RngReadOperand {
        RngReadKind kind = RngReadKind::None;
        ScalarSlot resultSlot = ScalarSlot::None;
        int intMin = 0;
        int intMax = 0;
        double floatMin = 0.0;
        double floatMax = 0.0;

        bool operator==(const RngReadOperand &) const = default;
    };

    enum class FixedScalarSource : std::uint8_t {
        None,
        HeroAttack,
        HeroSpeed,
        EnemySpeed,
    };

    enum class ScalarUpdateKind : std::uint8_t {
        None,
        SetConstant,
        MultiplyBySlot,
        SetFixedSourceTimesSlot,
        MultiplyRational,
        MultiplyByDefenceFlag,
    };

    struct ScalarUpdateOperand {
        ScalarUpdateKind kind = ScalarUpdateKind::None;
        ScalarSlot targetSlot = ScalarSlot::None;
        ScalarSlot sourceSlot = ScalarSlot::None;
        FixedScalarSource fixedSource = FixedScalarSource::None;
        int constant = 0;
        int numerator = 0;
        int denominator = 1;

        bool operator==(const ScalarUpdateOperand &) const = default;
    };

    enum class SwitchSourceKind : std::uint8_t {
        None,
        CurrentAction,
        WeightedScalarUpperBounds,
        ActionHistorySlot0,
        ActionHistorySlot1,
    };

    struct SwitchOperand {
        SwitchSourceKind kind = SwitchSourceKind::None;
        ScalarSlot sourceSlot = ScalarSlot::None;
        std::vector<int> upperInclusiveBounds;

        bool operator==(const SwitchOperand &) const = default;
    };

    enum class StateField : std::uint8_t {
        None,
        LogicalTurn,
        RawChargeTimer,
        Charge,
        RawParalysisTimer,
        Paralysis,
        ParalysisLevel,
        CurrentAction,
        Acro,
        Rage,
        Inactive,
        RawRageTimer,
        RawAcroTimer,
        Camera,
        Count,
    };

    enum class CompareOp : std::uint8_t {
        Eq,
        Ne,
        Lt,
        Le,
        Gt,
        Ge,
    };

    enum class ValueRefKind : std::uint8_t {
        Constant,
        Resource,
        StateField,
        Scalar,
        ResourceMinusScalar,
        FixedSourceTimesScalar,
        StateIndexedLookup,
    };

    struct ValueRef {
        ValueRefKind kind = ValueRefKind::Constant;
        ResourceAxis resource = ResourceAxis::EnemyHp;
        StateField state = StateField::None;
        ScalarSlot scalar = ScalarSlot::None;
        FixedScalarSource fixedSource = FixedScalarSource::None;
        double constant = 0.0;
        std::vector<int> lookupKeys;
        std::vector<double> lookupValues;

        bool operator==(const ValueRef &) const = default;
    };

    struct Comparison {
        ValueRef left;
        CompareOp op = CompareOp::Eq;
        ValueRef right;

        bool operator==(const Comparison &) const = default;
    };

    struct ConditionClause {
        std::vector<Comparison> all;

        bool operator==(const ConditionClause &) const = default;
    };

    struct BranchCondition {
        std::vector<ConditionClause> any;

        bool operator==(const BranchCondition &) const = default;
    };

    enum class ResourceUpdateKind : std::uint8_t {
        None,
        AddConstant,
        ClampSubtractSlot,
        ClampAddSlot,
        ClampAddQuarterSlot,
    };

    struct ResourceUpdateOperand {
        ResourceUpdateKind kind = ResourceUpdateKind::None;
        ResourceAxis target = ResourceAxis::EnemyHp;
        ScalarSlot sourceSlot = ScalarSlot::None;
        int constant = 0;
        int clampLo = 0;
        int clampHi = 0;

        bool operator==(const ResourceUpdateOperand &) const = default;
    };


    enum class StateWriteKind : std::uint8_t {
        None,
        SetConstant,
        SetFromSlot,
        Increment,
        Decrement,
        SetLogicalTurn,
        SetSelectedCommand,
    };

    struct StateWriteOperand {
        StateWriteKind kind = StateWriteKind::None;
        StateField target = StateField::None;
        ScalarSlot sourceSlot = ScalarSlot::None;
        int constant = 0;

        bool operator==(const StateWriteOperand &) const = default;
    };

    struct Instruction {
        Opcode opcode = Opcode::Step;
        std::string label;
        std::vector<int> successors;
        int rngReads = 0;
        int nativeWork = 0;
        std::vector<int> switchCaseValues;
        ResourceUpdateOperand resourceUpdate;
        std::vector<StateWriteOperand> stateWrites;
        ScalarSlot scalarResult = ScalarSlot::None;
        RngReadOperand rngRead;
        ScalarUpdateOperand scalarUpdate;
        std::string callTarget;
        std::string nativeId;
        SwitchOperand switchOperand;
        BranchCondition branchCondition;
        std::vector<double> nativeArguments;

        bool operator==(const Instruction &) const = default;
    };

    struct ScalarInitializer {
        ScalarSlot slot = ScalarSlot::None;
        double value = 0.0;

        bool operator==(const ScalarInitializer &) const = default;
    };

    struct Routine {
        std::string id;
        bool turnRoutine = false;
        std::vector<Instruction> instructions;
        std::vector<ScalarInitializer> localInitializers;

        bool operator==(const Routine &) const = default;
    };

    struct NativeArgumentDomain {
        double minimum = 0.0;
        double maximum = 0.0;
        bool integerOnly = false;

        bool operator==(const NativeArgumentDomain &) const = default;
    };

    struct NativeContract {
        std::string id;
        NativeOperation operation = NativeOperation::None;
        NativeImplementation implementation = NativeImplementation::None;
        std::uint8_t argumentCount = 0;
        std::vector<NativeArgumentDomain> argumentDomains;
        int maxRngReads = 0;
        int maxWork = 0;
        bool resourceIndependent = true;
        ScalarSlot resultSlot = ScalarSlot::None;
        int resultMin = 0;
        int resultMax = 0;

        bool operator==(const NativeContract &) const = default;
    };

    struct RuleProgram {
        std::uint32_t instructionFormatVersion = 1;
        std::string entryRoutine;
        std::vector<Routine> routines;
        std::vector<std::string> sourceCorrespondence;
        std::vector<std::string> explicitRuleChanges;

        bool operator==(const RuleProgram &) const = default;
    };

    struct PcStaticBounds {
        int maximumInstructionSteps = 0;
        int maximumRngReads = 0;
        int maximumCallDepth = 0;
        std::uint64_t maximumWork = 0;
    };

    struct RoutineStaticBounds {
        std::string routineId;
        std::vector<PcStaticBounds> pc;
    };

    struct StaticBounds {
        int maximumInstructionSteps = 0;
        int maximumCallDepth = 0;
        int rMax = 0;
        std::uint64_t registrationWork = 0;
        std::vector<RoutineStaticBounds> routines;
    };

    struct CompletionWeightTerm {
        int enemyDamageUpper = 0;
        int heroHpGainUpper = 0;
        int mpDelta = 0;
        int herbDelta = 0;

        bool operator==(const CompletionWeightTerm &) const = default;
    };

    struct CommandProfile {
        int command = 0;
        int minimumMp = 0;
        int minimumHerbs = 0;
        std::uint16_t selectableChargeMask = kChargeMaskAll;
        std::uint16_t selectableAcroMask = kAcroMaskAll;
        std::uint16_t selectableParalysisMask = kParalysisMaskAll;
        std::vector<CompletionWeightTerm> turnEntryCompletionTerms;

        bool operator==(const CommandProfile &) const = default;
    };

    enum class CompletionCutId : std::uint8_t {
        TurnEntry,
        AllyDoneEnemyPending,
        EnemyDoneAllyPending,
        ActionsDone,
    };

    struct ProgramPoint {
        std::string routineId;
        int instructionIndex = -1;

        bool operator==(const ProgramPoint &) const = default;
    };

    struct CompletionSite {
        CompletionCutId cut = CompletionCutId::TurnEntry;
        ProgramPoint pc;

        bool operator==(const CompletionSite &) const = default;
    };

    struct RuleProfile {
        std::vector<int> heroCommands;
        std::vector<CommandProfile> commandProfiles;
        int enemyMaxHp = 0;
        int heroMaxHp = 0;
        int heroMaxMp = 0;
        int heroInitialHerbs = 0;
        int heroAttack = 0;
        int heroSpeed = 0;
        int enemySpeed = 0;
        std::vector<CompletionSite> completionSites;
        bool sleepingSupported = false;

        bool operator==(const RuleProfile &) const = default;
    };

    struct NumericEnvironmentContract {
        std::uint32_t minimumCxxVersion = 202002;
        std::uint8_t intBytes = 4;
        std::uint8_t int32Bytes = 4;
        std::uint8_t uint64Bytes = 8;
        std::uint8_t doubleBytes = 8;
        std::uint8_t doubleRadix = 2;
        std::uint8_t doubleDigits = 53;
        bool requireIec559 = true;
        bool requireRoundToNearest = true;
        bool forbidFastMath = true;
        bool requireProofBuildContract = true;

        bool operator==(const NumericEnvironmentContract &) const = default;
    };

    struct ExactReplayContract {
        std::string entryPoint;
        int mode = -1;
        bool isSearch = true;
        bool calculateRngOnly = false;
        bool oneCommandPerMainCall = true;
        bool terminalAfterCameraTail = true;

        bool operator==(const ExactReplayContract &) const = default;
    };

    struct RuleBundle {
        RuleId id;
        RuleProgram program;
        std::vector<NativeContract> nativeContracts;
        RuleProfile profile;
        NumericEnvironmentContract numericEnvironment;
        ExactReplayContract exactReplay;
        StaticBounds bounds;
        bool registered = false;
    };

    struct RegistrationResult {
        bool accepted = false;
        std::string reason;
        StaticBounds bounds;
    };

    class RegistrationChecker {
    public:
        [[nodiscard]] static RegistrationResult check(
            const RuleProgram &program,
            const std::vector<NativeContract> &nativeContracts,
            const RuleProfile &profile);
    };

    [[nodiscard]] const PcStaticBounds *lookupPcBounds(
        const StaticBounds &bounds,
        const std::string &routineId,
        int pc) noexcept;

    [[nodiscard]] const CommandProfile *lookupCommandProfile(
        const RuleProfile &profile,
        int command) noexcept;

    [[nodiscard]] const CompletionSite *lookupCompletionSite(
        const RuleProfile &profile,
        CompletionCutId cut) noexcept;

    class RuleRegistry {
    public:
        [[nodiscard]] bool registerNew(RuleBundle candidate, std::string &error);

        [[nodiscard]] const RuleBundle *lookup(const RuleId &id) const noexcept;

    private:
        std::vector<RuleBundle> bundles_;
    };

    [[nodiscard]] RuleBundle makeYo2BeBundleCandidate();
} // namespace d20proof
