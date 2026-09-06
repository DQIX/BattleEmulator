#pragma once

#include "ExactReplay.h"
#include "RuleProgram.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace d20proof {
    enum class ResourceExpressionKind : std::uint8_t {
        InputOffset,
        Constant,
    };

    struct ResourceExpression {
        ResourceExpressionKind kind = ResourceExpressionKind::InputOffset;
        ResourceAxis source = ResourceAxis::EnemyHp;
        std::int64_t value = 0;

        bool operator==(const ResourceExpression &) const = default;
    };

    enum class ControlExpressionKind : std::uint8_t {
        Constant,
        ModeMap,
    };

    struct ControlExpression {
        ControlExpressionKind kind = ControlExpressionKind::Constant;
        ModeAxis source = ModeAxis::Charge;
        std::int64_t constant = 0;
        std::array<std::int64_t, 8> table{};

        bool operator==(const ControlExpression &) const = default;
    };

    struct ReturnAddress {
        std::string routineId;
        int pc = -1;
        std::array<double, static_cast<std::size_t>(ScalarSlot::Count)> callerLocalScalars{};
        std::array<bool, static_cast<std::size_t>(ScalarSlot::Count)> callerLocalDefined{};
        ScalarSlot resultSlot = ScalarSlot::None;

        bool operator==(const ReturnAddress &) const = default;
    };

    enum ActionProgress : std::uint8_t {
        ActionProgressNone = 0,
        ActionProgressAllyDone = 1u << 0,
        ActionProgressEnemyDone = 1u << 1,
    };

    struct SymbolicFrame {
        std::string routineId;
        int pc = 0;
        std::vector<ReturnAddress> callStack;
        int rngPosition = 1;
        int selectedCommand = 0;
        int elapsedTurn = 0;
        std::array<ResourceExpression, 4> resources{};
        std::array<ControlExpression, static_cast<std::size_t>(StateField::Count)> controls{};
        std::array<double, static_cast<std::size_t>(ScalarSlot::Count)> scalars{};
        std::array<bool, static_cast<std::size_t>(ScalarSlot::Count)> scalarDefined{};
        std::uint8_t actionProgress = ActionProgressNone;
        Box inputDomain;

        bool operator==(const SymbolicFrame &) const = default;
    };

    struct SymbolicStepResult {
        bool accepted = false;
        bool finished = false;
        std::string reason;
        std::vector<SymbolicFrame> frames;
    };

    struct OutputLeafClassification {
        std::uint32_t localCellId = 0;
        SymbolicFrame frame;
    };

    class SymbolicStepper {
    public:
        SymbolicStepper(const RuleBundle &bundle, const Problem &problem);

        [[nodiscard]] SymbolicFrame makeRootFrame(
            int elapsedTurn,
            int selectedCommand,
            int rngPosition,
            const Box &inputDomain) const;

        [[nodiscard]] SymbolicStepResult step(const SymbolicFrame &frame) const;

        [[nodiscard]] bool outputBox(
            const SymbolicFrame &frame,
            Box &output,
            std::string &error) const;

        [[nodiscard]] bool splitOutputPredicate(
            const SymbolicFrame &frame,
            const Predicate &predicate,
            std::vector<SymbolicFrame> &trueFrames,
            std::vector<SymbolicFrame> &falseFrames,
            std::string &error) const;

        [[nodiscard]] bool classifyOutput(
            const SymbolicFrame &frame,
            const PredicatePartition &partition,
            std::vector<OutputLeafClassification> &leaves,
            std::string &error) const;

        [[nodiscard]] bool remainingRngBound(
            const SymbolicFrame &frame,
            int &remaining,
            std::string &error) const;

        [[nodiscard]] ProgramPoint point(const SymbolicFrame &frame) const {
            return {frame.routineId, frame.pc};
        }

    private:
        const RuleBundle &bundle_;
        const Problem &problem_;
        std::array<std::uint32_t, ExactReplay::kRngTapeSize> rngTape_{};
    };
} // namespace d20proof
