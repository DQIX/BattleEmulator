#include "SymbolicStepper.h"

#include "../BattleEmulator.h"
#include "../lcg.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string_view>

namespace d20proof {
    namespace {
        constexpr std::uint64_t kLcgMultiplier = 0x5d588b656c078965ULL;
        constexpr std::uint64_t kLcgIncrement = 0x269ec3ULL;

        constexpr std::size_t resourceIndex(ResourceAxis axis) noexcept {
            return static_cast<std::size_t>(axis);
        }

        constexpr std::size_t stateIndex(StateField field) noexcept {
            return static_cast<std::size_t>(field);
        }

        constexpr std::size_t scalarIndex(ScalarSlot slot) noexcept {
            return static_cast<std::size_t>(slot);
        }

        const Routine *findRoutine(const RuleProgram &program, std::string_view id) {
            for (const Routine &routine: program.routines) {
                if (routine.id == id) {
                    return &routine;
                }
            }
            return nullptr;
        }

        const NativeContract *findNative(const RuleBundle &bundle, std::string_view id) {
            for (const NativeContract &contract: bundle.nativeContracts) {
                if (contract.id == id) {
                    return &contract;
                }
            }
            return nullptr;
        }

        bool safeAdd(std::int64_t a, std::int64_t b, std::int64_t &out) noexcept {
            if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
                (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b)) {
                return false;
            }
            out = a + b;
            return true;
        }

        bool compareNumber(double left, CompareOp op, double right) noexcept {
            switch (op) {
                case CompareOp::Eq:
                    return left == right;
                case CompareOp::Ne:
                    return left != right;
                case CompareOp::Lt:
                    return left < right;
                case CompareOp::Le:
                    return left <= right;
                case CompareOp::Gt:
                    return left > right;
                case CompareOp::Ge:
                    return left >= right;
            }
            return false;
        }

        CompareOp reverseComparison(CompareOp op) noexcept {
            switch (op) {
                case CompareOp::Eq:
                    return CompareOp::Eq;
                case CompareOp::Ne:
                    return CompareOp::Ne;
                case CompareOp::Lt:
                    return CompareOp::Gt;
                case CompareOp::Le:
                    return CompareOp::Ge;
                case CompareOp::Gt:
                    return CompareOp::Lt;
                case CompareOp::Ge:
                    return CompareOp::Le;
            }
            return CompareOp::Eq;
        }

        bool toInt64Floor(double value, std::int64_t &out) noexcept {
            if (!std::isfinite(value)) {
                return false;
            }
            const double floored = std::floor(value);
            if (floored < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
                floored > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
                return false;
            }
            out = static_cast<std::int64_t>(floored);
            return true;
        }

        bool toInt64Ceil(double value, std::int64_t &out) noexcept {
            if (!std::isfinite(value)) {
                return false;
            }
            const double ceiled = std::ceil(value);
            if (ceiled < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
                ceiled > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
                return false;
            }
            out = static_cast<std::int64_t>(ceiled);
            return true;
        }

        bool exactInteger(double value, std::int64_t &out) noexcept {
            if (!toInt64Floor(value, out)) {
                return false;
            }
            return static_cast<double>(out) == value;
        }

        StateField stateFieldForMode(ModeAxis axis) noexcept {
            switch (axis) {
                case ModeAxis::Charge:
                    return StateField::Charge;
                case ModeAxis::Paralysis:
                    return StateField::Paralysis;
                case ModeAxis::Acro:
                    return StateField::Acro;
                case ModeAxis::Rage:
                    return StateField::Rage;
                case ModeAxis::Inactive:
                    return StateField::Inactive;
                case ModeAxis::Camera:
                    return StateField::Camera;
            }
            return StateField::None;
        }

        ControlExpression constantControl(std::int64_t value) {
            ControlExpression expression;
            expression.kind = ControlExpressionKind::Constant;
            expression.constant = value;
            return expression;
        }

        ControlExpression modeMap(ModeAxis source, std::initializer_list<std::int64_t> values) {
            ControlExpression expression;
            expression.kind = ControlExpressionKind::ModeMap;
            expression.source = source;
            std::size_t index = 0;
            for (std::int64_t value: values) {
                if (index >= expression.table.size()) {
                    break;
                }
                expression.table[index++] = value;
            }
            return expression;
        }

        ControlExpression rootControl(
            StateField field,
            const Problem &problem,
            int elapsedTurn,
            int selectedCommand) {
            switch (field) {
                case StateField::LogicalTurn:
                    return constantControl(static_cast<std::int64_t>(problem.startTurn) + elapsedTurn);
                case StateField::RawChargeTimer:
                    return modeMap(ModeAxis::Charge, {0, 0, 1, 2, 3, 4, 5, 6});
                case StateField::Charge:
                    return modeMap(ModeAxis::Charge, {0, 1, 2, 3, 4, 5, 6, 7});
                case StateField::RawParalysisTimer:
                    return modeMap(ModeAxis::Paralysis, {0, -2, -1, 0, 1, 2, 3, 4});
                case StateField::Paralysis:
                    return modeMap(ModeAxis::Paralysis, {0, 1, 2, 3, 4, 5, 6, 7});
                case StateField::ParalysisLevel:
                    return constantControl(problem.s0.players[0].paralysisLevel);
                case StateField::CurrentAction:
                    return constantControl(selectedCommand);
                case StateField::Acro:
                    return modeMap(ModeAxis::Acro, {0, 1, 2, 3, 4, 5, 6});
                case StateField::Rage:
                    return modeMap(ModeAxis::Rage, {0, 1, 2, 3, 4});
                case StateField::Inactive:
                    return modeMap(ModeAxis::Inactive, {0, 1});
                case StateField::RawRageTimer:
                    return modeMap(ModeAxis::Rage, {0, 1, 2, 3, 4});
                case StateField::RawAcroTimer:
                    return modeMap(ModeAxis::Acro, {0, 1, 2, 3, 4, 5, 6});
                case StateField::Camera:
                    return modeMap(ModeAxis::Camera, {0, 1, 2, 3, 4, 5});
                case StateField::None:
                case StateField::Count:
                    return constantControl(0);
            }
            return constantControl(0);
        }

        bool adjustControl(ControlExpression &expression, std::int64_t delta) noexcept {
            if (expression.kind == ControlExpressionKind::Constant) {
                return safeAdd(expression.constant, delta, expression.constant);
            }
            for (std::int64_t &value: expression.table) {
                if (!safeAdd(value, delta, value)) {
                    return false;
                }
            }
            return true;
        }

        bool scalarIsIntegral(ScalarSlot slot) noexcept {
            switch (slot) {
                case ScalarSlot::Damage:
                case ScalarSlot::Amount:
                case ScalarSlot::Duration:
                case ScalarSlot::CriticalFlag:
                case ScalarSlot::DodgeFlag:
                case ScalarSlot::ShieldFlag:
                case ScalarSlot::DefenceHalfFlag:
                case ScalarSlot::PreemptiveFlag:
                case ScalarSlot::ActionSlot0:
                case ScalarSlot::ActionSlot1:
                case ScalarSlot::ActionCount:
                    return true;
                case ScalarSlot::Random0:
                case ScalarSlot::Random1:
                case ScalarSlot::SpeedHero:
                case ScalarSlot::SpeedEnemy:
                case ScalarSlot::None:
                case ScalarSlot::Count:
                    return false;
            }
            return false;
        }

        double normalizeScalar(ScalarSlot slot, double value) noexcept {
            if (scalarIsIntegral(slot)) {
                return static_cast<double>(static_cast<std::int64_t>(value));
            }
            return value;
        }

        bool setScalar(SymbolicFrame &frame, ScalarSlot slot, double value, std::string &error) {
            if (slot == ScalarSlot::None || slot == ScalarSlot::Count || !std::isfinite(value)) {
                error = "invalid scalar write";
                return false;
            }
            frame.scalars[scalarIndex(slot)] = normalizeScalar(slot, value);
            frame.scalarDefined[scalarIndex(slot)] = true;
            return true;
        }

        bool getScalar(const SymbolicFrame &frame, ScalarSlot slot, double &value, std::string &error) {
            if (slot == ScalarSlot::None || slot == ScalarSlot::Count ||
                !frame.scalarDefined[scalarIndex(slot)]) {
                error = "read of undefined scalar slot";
                return false;
            }
            value = frame.scalars[scalarIndex(slot)];
            return true;
        }

        bool controlConstantOnDomain(
            const ControlExpression &expression,
            const Box &domain,
            std::int64_t &value) {
            if (expression.kind == ControlExpressionKind::Constant) {
                value = expression.constant;
                return true;
            }
            const std::uint16_t active = modeMaskOf(domain, expression.source);
            bool found = false;
            std::int64_t common = 0;
            for (int bit = 0; bit < 8; ++bit) {
                if ((active & (1u << bit)) == 0) {
                    continue;
                }
                if (!found) {
                    common = expression.table[bit];
                    found = true;
                } else if (common != expression.table[bit]) {
                    return false;
                }
            }
            if (!found) {
                return false;
            }
            value = common;
            return true;
        }

        struct EvaluatedValue {
            enum class Kind : std::uint8_t {
                Concrete,
                Resource,
                ModeMap,
            } kind = Kind::Concrete;
            double concrete = 0.0;
            ResourceExpression resource;
            ModeAxis source = ModeAxis::Charge;
            std::array<double, 8> table{};
        };

        bool controlToValue(
            const ControlExpression &control,
            const Box &domain,
            EvaluatedValue &out) {
            if (control.kind == ControlExpressionKind::Constant) {
                out.kind = EvaluatedValue::Kind::Concrete;
                out.concrete = static_cast<double>(control.constant);
                return true;
            }
            out.kind = EvaluatedValue::Kind::ModeMap;
            out.source = control.source;
            const std::uint16_t active = modeMaskOf(domain, control.source);
            for (int bit = 0; bit < 8; ++bit) {
                if ((active & (1u << bit)) != 0) {
                    out.table[bit] = static_cast<double>(control.table[bit]);
                }
            }
            return true;
        }

        bool fixedSourceValue(
            const RuleBundle &bundle,
            FixedScalarSource source,
            double &value) noexcept {
            switch (source) {
                case FixedScalarSource::HeroAttack:
                    value = bundle.profile.heroAttack;
                    return true;
                case FixedScalarSource::HeroSpeed:
                    value = bundle.profile.heroSpeed;
                    return true;
                case FixedScalarSource::EnemySpeed:
                    value = bundle.profile.enemySpeed;
                    return true;
                case FixedScalarSource::None:
                    return false;
            }
            return false;
        }

        bool lookupMappedValue(
            const ValueRef &ref,
            double key,
            double &value) noexcept {
            std::int64_t integralKey = 0;
            if (!exactInteger(key, integralKey)) {
                return false;
            }
            for (std::size_t index = 0; index < ref.lookupKeys.size(); ++index) {
                if (ref.lookupKeys[index] == integralKey) {
                    value = ref.lookupValues[index];
                    return true;
                }
            }
            return false;
        }

        bool evaluateValueRef(
            const RuleBundle &bundle,
            const SymbolicFrame &frame,
            const ValueRef &ref,
            EvaluatedValue &out,
            std::string &error) {
            switch (ref.kind) {
                case ValueRefKind::Constant:
                    out.kind = EvaluatedValue::Kind::Concrete;
                    out.concrete = ref.constant;
                    return true;
                case ValueRefKind::Resource:
                    out.kind = EvaluatedValue::Kind::Resource;
                    out.resource = frame.resources[resourceIndex(ref.resource)];
                    return true;
                case ValueRefKind::StateField:
                    if (ref.state == StateField::None || ref.state == StateField::Count) {
                        error = "invalid state-field value reference";
                        return false;
                    }
                    return controlToValue(frame.controls[stateIndex(ref.state)], frame.inputDomain, out);
                case ValueRefKind::Scalar: {
                    double value = 0.0;
                    if (!getScalar(frame, ref.scalar, value, error)) {
                        return false;
                    }
                    out.kind = EvaluatedValue::Kind::Concrete;
                    out.concrete = value;
                    return true;
                }
                case ValueRefKind::ResourceMinusScalar: {
                    double scalar = 0.0;
                    if (!getScalar(frame, ref.scalar, scalar, error)) {
                        return false;
                    }
                    std::int64_t integral = 0;
                    if (!exactInteger(scalar, integral)) {
                        error = "resource expression subtracts a non-integer scalar";
                        return false;
                    }
                    out.kind = EvaluatedValue::Kind::Resource;
                    out.resource = frame.resources[resourceIndex(ref.resource)];
                    if (out.resource.kind == ResourceExpressionKind::Constant) {
                        if (!safeAdd(out.resource.value, -integral, out.resource.value)) {
                            error = "resource expression overflow";
                            return false;
                        }
                    } else if (!safeAdd(out.resource.value, -integral, out.resource.value)) {
                        error = "resource expression overflow";
                        return false;
                    }
                    return true;
                }
                case ValueRefKind::FixedSourceTimesScalar: {
                    double fixed = 0.0;
                    double scalar = 0.0;
                    if (!fixedSourceValue(bundle, ref.fixedSource, fixed) ||
                        !getScalar(frame, ref.scalar, scalar, error)) {
                        if (error.empty()) {
                            error = "invalid fixed-source scalar expression";
                        }
                        return false;
                    }
                    out.kind = EvaluatedValue::Kind::Concrete;
                    out.concrete = fixed * scalar;
                    return std::isfinite(out.concrete);
                }
                case ValueRefKind::StateIndexedLookup: {
                    if (ref.state == StateField::None || ref.state == StateField::Count) {
                        error = "lookup uses an invalid state field";
                        return false;
                    }
                    EvaluatedValue state;
                    if (!controlToValue(frame.controls[stateIndex(ref.state)], frame.inputDomain, state)) {
                        error = "failed to evaluate lookup state";
                        return false;
                    }
                    if (state.kind == EvaluatedValue::Kind::Concrete) {
                        double value = 0.0;
                        if (!lookupMappedValue(ref, state.concrete, value)) {
                            error = "state lookup key is outside registered table";
                            return false;
                        }
                        out.kind = EvaluatedValue::Kind::Concrete;
                        out.concrete = value;
                        return true;
                    }
                    out.kind = EvaluatedValue::Kind::ModeMap;
                    out.source = state.source;
                    const std::uint16_t active = modeMaskOf(frame.inputDomain, state.source);
                    for (int bit = 0; bit < 8; ++bit) {
                        if ((active & (1u << bit)) == 0) {
                            continue;
                        }
                        double value = 0.0;
                        if (!lookupMappedValue(ref, state.table[bit], value)) {
                            error = "state lookup key is outside registered table";
                            return false;
                        }
                        out.table[bit] = value;
                    }
                    return true;
                }
            }
            error = "unknown typed value reference";
            return false;
        }

        struct DomainSplit {
            bool accepted = false;
            std::string reason;
            std::vector<Box> trueDomains;
            std::vector<Box> falseDomains;
        };

        void addIfNonEmpty(std::vector<Box> &out, Box domain) {
            if (!domain.empty()) {
                out.push_back(std::move(domain));
            }
        }

        Box restrictResourceUpper(Box domain, ResourceAxis axis, std::int64_t upper) {
            Interval &interval = intervalOf(domain, axis);
            interval.hi = std::min(interval.hi, upper);
            return domain;
        }

        Box restrictResourceLower(Box domain, ResourceAxis axis, std::int64_t lower) {
            Interval &interval = intervalOf(domain, axis);
            interval.lo = std::max(interval.lo, lower);
            return domain;
        }

        Box restrictResourceEqual(Box domain, ResourceAxis axis, std::int64_t value) {
            Interval &interval = intervalOf(domain, axis);
            interval.lo = std::max(interval.lo, value);
            interval.hi = std::min(interval.hi, value);
            return domain;
        }

        DomainSplit splitResourceComparison(
            const Box &domain,
            const ResourceExpression &expression,
            CompareOp op,
            double right) {
            DomainSplit result;
            result.accepted = true;

            if (expression.kind == ResourceExpressionKind::Constant) {
                const bool truth = compareNumber(static_cast<double>(expression.value), op, right);
                (truth ? result.trueDomains : result.falseDomains).push_back(domain);
                return result;
            }

            std::int64_t boundary = 0;
            std::int64_t inputBoundary = 0;
            switch (op) {
                case CompareOp::Lt:
                    if (!toInt64Ceil(right, boundary) || boundary == std::numeric_limits<std::int64_t>::min()) {
                        result.accepted = false;
                        result.reason = "resource comparison threshold overflow";
                        return result;
                    }
                    --boundary;
                    if (!safeAdd(boundary, -expression.value, inputBoundary)) {
                        result.accepted = false;
                        result.reason = "resource comparison threshold overflow";
                        return result;
                    }
                    addIfNonEmpty(result.trueDomains, restrictResourceUpper(domain, expression.source, inputBoundary));
                    if (inputBoundary != std::numeric_limits<std::int64_t>::max()) {
                        addIfNonEmpty(
                            result.falseDomains,
                            restrictResourceLower(domain, expression.source, inputBoundary + 1));
                    }
                    return result;
                case CompareOp::Le:
                    if (!toInt64Floor(right, boundary) ||
                        !safeAdd(boundary, -expression.value, inputBoundary)) {
                        result.accepted = false;
                        result.reason = "resource comparison threshold overflow";
                        return result;
                    }
                    addIfNonEmpty(result.trueDomains, restrictResourceUpper(domain, expression.source, inputBoundary));
                    if (inputBoundary != std::numeric_limits<std::int64_t>::max()) {
                        addIfNonEmpty(
                            result.falseDomains,
                            restrictResourceLower(domain, expression.source, inputBoundary + 1));
                    }
                    return result;
                case CompareOp::Gt:
                    if (!toInt64Floor(right, boundary) || boundary == std::numeric_limits<std::int64_t>::max()) {
                        result.accepted = false;
                        result.reason = "resource comparison threshold overflow";
                        return result;
                    }
                    ++boundary;
                    if (!safeAdd(boundary, -expression.value, inputBoundary)) {
                        result.accepted = false;
                        result.reason = "resource comparison threshold overflow";
                        return result;
                    }
                    addIfNonEmpty(result.trueDomains, restrictResourceLower(domain, expression.source, inputBoundary));
                    if (inputBoundary != std::numeric_limits<std::int64_t>::min()) {
                        addIfNonEmpty(
                            result.falseDomains,
                            restrictResourceUpper(domain, expression.source, inputBoundary - 1));
                    }
                    return result;
                case CompareOp::Ge:
                    if (!toInt64Ceil(right, boundary) ||
                        !safeAdd(boundary, -expression.value, inputBoundary)) {
                        result.accepted = false;
                        result.reason = "resource comparison threshold overflow";
                        return result;
                    }
                    addIfNonEmpty(result.trueDomains, restrictResourceLower(domain, expression.source, inputBoundary));
                    if (inputBoundary != std::numeric_limits<std::int64_t>::min()) {
                        addIfNonEmpty(
                            result.falseDomains,
                            restrictResourceUpper(domain, expression.source, inputBoundary - 1));
                    }
                    return result;
                case CompareOp::Eq:
                case CompareOp::Ne: {
                    std::int64_t exact = 0;
                    if (!exactInteger(right, exact)) {
                        (op == CompareOp::Ne ? result.trueDomains : result.falseDomains).push_back(domain);
                        return result;
                    }
                    if (!safeAdd(exact, -expression.value, inputBoundary)) {
                        result.accepted = false;
                        result.reason = "resource equality threshold overflow";
                        return result;
                    }
                    Box equal = restrictResourceEqual(domain, expression.source, inputBoundary);
                    std::vector<Box> unequal;
                    if (inputBoundary != std::numeric_limits<std::int64_t>::min()) {
                        addIfNonEmpty(
                            unequal,
                            restrictResourceUpper(domain, expression.source, inputBoundary - 1));
                    }
                    if (inputBoundary != std::numeric_limits<std::int64_t>::max()) {
                        addIfNonEmpty(
                            unequal,
                            restrictResourceLower(domain, expression.source, inputBoundary + 1));
                    }
                    if (op == CompareOp::Eq) {
                        addIfNonEmpty(result.trueDomains, equal);
                        result.falseDomains = std::move(unequal);
                    } else {
                        result.trueDomains = std::move(unequal);
                        addIfNonEmpty(result.falseDomains, equal);
                    }
                    return result;
                }
            }
            result.accepted = false;
            result.reason = "unknown resource comparison";
            return result;
        }

        DomainSplit splitModeMapComparison(
            const Box &domain,
            ModeAxis source,
            const std::array<double, 8> &table,
            CompareOp op,
            double right) {
            DomainSplit result;
            result.accepted = true;
            const std::uint16_t active = modeMaskOf(domain, source);
            std::uint16_t trueMask = 0;
            std::uint16_t falseMask = 0;
            for (int bit = 0; bit < 8; ++bit) {
                const auto bitMask = static_cast<std::uint16_t>(1u << bit);
                if ((active & bitMask) == 0) {
                    continue;
                }
                if (compareNumber(table[bit], op, right)) {
                    trueMask |= bitMask;
                } else {
                    falseMask |= bitMask;
                }
            }
            if (trueMask != 0) {
                Box trueDomain = domain;
                modeMaskOf(trueDomain, source) &= trueMask;
                result.trueDomains.push_back(std::move(trueDomain));
            }
            if (falseMask != 0) {
                Box falseDomain = domain;
                modeMaskOf(falseDomain, source) &= falseMask;
                result.falseDomains.push_back(std::move(falseDomain));
            }
            return result;
        }

        DomainSplit splitComparison(
            const RuleBundle &bundle,
            const SymbolicFrame &frame,
            const Comparison &comparisonItem) {
            DomainSplit result;
            EvaluatedValue left;
            EvaluatedValue right;
            std::string error;
            if (!evaluateValueRef(bundle, frame, comparisonItem.left, left, error) ||
                !evaluateValueRef(bundle, frame, comparisonItem.right, right, error)) {
                result.reason = error;
                return result;
            }

            if (left.kind == EvaluatedValue::Kind::Concrete &&
                right.kind == EvaluatedValue::Kind::Concrete) {
                result.accepted = true;
                const bool truth = compareNumber(left.concrete, comparisonItem.op, right.concrete);
                (truth ? result.trueDomains : result.falseDomains).push_back(frame.inputDomain);
                return result;
            }
            if (left.kind == EvaluatedValue::Kind::Resource &&
                right.kind == EvaluatedValue::Kind::Concrete) {
                return splitResourceComparison(frame.inputDomain, left.resource, comparisonItem.op, right.concrete);
            }
            if (left.kind == EvaluatedValue::Kind::Concrete &&
                right.kind == EvaluatedValue::Kind::Resource) {
                return splitResourceComparison(
                    frame.inputDomain,
                    right.resource,
                    reverseComparison(comparisonItem.op),
                    left.concrete);
            }
            if (left.kind == EvaluatedValue::Kind::ModeMap &&
                right.kind == EvaluatedValue::Kind::Concrete) {
                return splitModeMapComparison(
                    frame.inputDomain,
                    left.source,
                    left.table,
                    comparisonItem.op,
                    right.concrete);
            }
            if (left.kind == EvaluatedValue::Kind::Concrete &&
                right.kind == EvaluatedValue::Kind::ModeMap) {
                return splitModeMapComparison(
                    frame.inputDomain,
                    right.source,
                    right.table,
                    reverseComparison(comparisonItem.op),
                    left.concrete);
            }
            if (left.kind == EvaluatedValue::Kind::Resource &&
                right.kind == EvaluatedValue::Kind::Resource) {
                if (left.resource.kind == ResourceExpressionKind::Constant) {
                    return splitResourceComparison(
                        frame.inputDomain,
                        right.resource,
                        reverseComparison(comparisonItem.op),
                        static_cast<double>(left.resource.value));
                }
                if (right.resource.kind == ResourceExpressionKind::Constant) {
                    return splitResourceComparison(
                        frame.inputDomain,
                        left.resource,
                        comparisonItem.op,
                        static_cast<double>(right.resource.value));
                }
                if (left.resource.source == right.resource.source) {
                    result.accepted = true;
                    const bool truth = compareNumber(
                        static_cast<double>(left.resource.value),
                        comparisonItem.op,
                        static_cast<double>(right.resource.value));
                    (truth ? result.trueDomains : result.falseDomains).push_back(frame.inputDomain);
                    return result;
                }
                result.reason = "comparison couples two resource coordinates";
                return result;
            }
            if (left.kind == EvaluatedValue::Kind::ModeMap &&
                right.kind == EvaluatedValue::Kind::ModeMap &&
                left.source == right.source) {
                std::array<double, 8> difference{};
                for (int bit = 0; bit < 8; ++bit) {
                    difference[bit] = left.table[bit] - right.table[bit];
                }
                return splitModeMapComparison(
                    frame.inputDomain,
                    left.source,
                    difference,
                    comparisonItem.op,
                    0.0);
            }
            result.reason = "comparison couples independent symbolic coordinates";
            return result;
        }

        DomainSplit splitClause(
            const RuleBundle &bundle,
            const SymbolicFrame &frame,
            const ConditionClause &conditionClause) {
            DomainSplit result;
            result.accepted = true;
            std::vector<Box> remaining{frame.inputDomain};
            for (const Comparison &item: conditionClause.all) {
                std::vector<Box> nextRemaining;
                for (const Box &candidate: remaining) {
                    SymbolicFrame local = frame;
                    local.inputDomain = candidate;
                    DomainSplit split = splitComparison(bundle, local, item);
                    if (!split.accepted) {
                        return split;
                    }
                    nextRemaining.insert(
                        nextRemaining.end(), split.trueDomains.begin(), split.trueDomains.end());
                    result.falseDomains.insert(
                        result.falseDomains.end(), split.falseDomains.begin(), split.falseDomains.end());
                }
                remaining = std::move(nextRemaining);
                if (remaining.empty()) {
                    break;
                }
            }
            result.trueDomains = std::move(remaining);
            return result;
        }

        DomainSplit splitCondition(
            const RuleBundle &bundle,
            const SymbolicFrame &frame,
            const BranchCondition &condition) {
            DomainSplit result;
            result.accepted = true;
            std::vector<Box> remainingFalse{frame.inputDomain};
            for (const ConditionClause &conditionClause: condition.any) {
                std::vector<Box> nextFalse;
                for (const Box &candidate: remainingFalse) {
                    SymbolicFrame local = frame;
                    local.inputDomain = candidate;
                    DomainSplit split = splitClause(bundle, local, conditionClause);
                    if (!split.accepted) {
                        return split;
                    }
                    result.trueDomains.insert(
                        result.trueDomains.end(), split.trueDomains.begin(), split.trueDomains.end());
                    nextFalse.insert(nextFalse.end(), split.falseDomains.begin(), split.falseDomains.end());
                }
                remainingFalse = std::move(nextFalse);
                if (remainingFalse.empty()) {
                    break;
                }
            }
            result.falseDomains = std::move(remainingFalse);
            return result;
        }

        bool applyRoutineInitializers(
            SymbolicFrame &frame,
            const Routine &routine,
            std::string &error) {
            for (const ScalarInitializer &initializer: routine.localInitializers) {
                if (!setScalar(frame, initializer.slot, initializer.value, error)) {
                    return false;
                }
            }
            return true;
        }

        bool integerScalar(
            const SymbolicFrame &frame,
            ScalarSlot slot,
            std::int64_t &value,
            std::string &error) {
            double scalar = 0.0;
            if (!getScalar(frame, slot, scalar, error) || !exactInteger(scalar, value)) {
                if (error.empty()) {
                    error = "expected integer scalar";
                }
                return false;
            }
            return true;
        }

        std::vector<std::pair<Box, std::int64_t>> splitByControlValue(
            const Box &domain,
            const ControlExpression &expression) {
            if (expression.kind == ControlExpressionKind::Constant) {
                return {{domain, expression.constant}};
            }
            std::map<std::int64_t, std::uint16_t> groupedMasks;
            const std::uint16_t active = modeMaskOf(domain, expression.source);
            for (int bit = 0; bit < 8; ++bit) {
                const auto bitMask = static_cast<std::uint16_t>(1u << bit);
                if ((active & bitMask) != 0) {
                    groupedMasks[expression.table[bit]] |= bitMask;
                }
            }
            std::vector<std::pair<Box, std::int64_t>> result;
            for (const auto &[value, mask]: groupedMasks) {
                Box child = domain;
                modeMaskOf(child, expression.source) &= mask;
                if (!child.empty()) {
                    result.emplace_back(std::move(child), value);
                }
            }
            return result;
        }

        bool setControlFromWrite(
            SymbolicFrame &frame,
            const StateWriteOperand &write,
            const Problem &problem,
            std::string &error) {
            if (write.target == StateField::None || write.target == StateField::Count) {
                error = "state write targets invalid field";
                return false;
            }
            ControlExpression &target = frame.controls[stateIndex(write.target)];
            switch (write.kind) {
                case StateWriteKind::SetConstant:
                    target = constantControl(write.constant);
                    return true;
                case StateWriteKind::SetFromSlot: {
                    std::int64_t value = 0;
                    if (!integerScalar(frame, write.sourceSlot, value, error)) {
                        return false;
                    }
                    target = constantControl(value);
                    return true;
                }
                case StateWriteKind::Increment:
                    if (!adjustControl(target, 1)) {
                        error = "state increment overflow";
                        return false;
                    }
                    return true;
                case StateWriteKind::Decrement:
                    if (!adjustControl(target, -1)) {
                        error = "state decrement overflow";
                        return false;
                    }
                    return true;
                case StateWriteKind::SetLogicalTurn:
                    target = constantControl(static_cast<std::int64_t>(problem.startTurn) + frame.elapsedTurn);
                    return true;
                case StateWriteKind::SetSelectedCommand:
                    target = constantControl(frame.selectedCommand);
                    return true;
                case StateWriteKind::None:
                    error = "state write has no operation";
                    return false;
            }
            error = "unknown state write";
            return false;
        }

        bool applyScalarUpdate(
            SymbolicFrame &frame,
            const RuleBundle &bundle,
            const ScalarUpdateOperand &update,
            std::string &error) {
            double current = 0.0;
            double source = 0.0;
            double fixed = 0.0;
            switch (update.kind) {
                case ScalarUpdateKind::SetConstant:
                    return setScalar(frame, update.targetSlot, update.constant, error);
                case ScalarUpdateKind::MultiplyBySlot:
                    if (!getScalar(frame, update.targetSlot, current, error) ||
                        !getScalar(frame, update.sourceSlot, source, error)) {
                        return false;
                    }
                    return setScalar(frame, update.targetSlot, current * source, error);
                case ScalarUpdateKind::SetFixedSourceTimesSlot:
                    if (!fixedSourceValue(bundle, update.fixedSource, fixed) ||
                        !getScalar(frame, update.sourceSlot, source, error)) {
                        if (error.empty()) {
                            error = "invalid fixed-source scalar update";
                        }
                        return false;
                    }
                    return setScalar(frame, update.targetSlot, fixed * source, error);
                case ScalarUpdateKind::MultiplyRational:
                    if (update.denominator <= 0 ||
                        !getScalar(frame, update.targetSlot, current, error)) {
                        if (error.empty()) {
                            error = "invalid rational scalar update";
                        }
                        return false;
                    }
                    return setScalar(
                        frame,
                        update.targetSlot,
                        current * static_cast<double>(update.numerator) /
                            static_cast<double>(update.denominator),
                        error);
                case ScalarUpdateKind::MultiplyByDefenceFlag:
                    if (!getScalar(frame, update.targetSlot, current, error) ||
                        !getScalar(frame, ScalarSlot::DefenceHalfFlag, source, error)) {
                        return false;
                    }
                    return setScalar(
                        frame,
                        update.targetSlot,
                        current * (source == 0.0 ? 1.0 : 0.5),
                        error);
                case ScalarUpdateKind::None:
                    error = "scalar update has no operation";
                    return false;
            }
            error = "unknown scalar update";
            return false;
        }

        bool shiftedResourceExpression(
            const ResourceExpression &input,
            std::int64_t delta,
            ResourceExpression &output,
            std::string &error) {
            output = input;
            if (!safeAdd(output.value, delta, output.value)) {
                error = "resource expression overflow";
                return false;
            }
            return true;
        }

        std::vector<SymbolicFrame> clampResource(
            const SymbolicFrame &frame,
            ResourceAxis target,
            std::int64_t delta,
            std::int64_t clampLo,
            std::int64_t clampHi,
            int nextPc,
            std::string &error) {
            if (clampLo > clampHi) {
                error = "invalid resource clamp";
                return {};
            }
            const ResourceExpression &current = frame.resources[resourceIndex(target)];
            ResourceExpression shifted;
            if (!shiftedResourceExpression(current, delta, shifted, error)) {
                return {};
            }
            if (shifted.kind == ResourceExpressionKind::Constant) {
                SymbolicFrame child = frame;
                child.pc = nextPc;
                child.resources[resourceIndex(target)] = {
                    ResourceExpressionKind::Constant,
                    target,
                    std::clamp(shifted.value, clampLo, clampHi),
                };
                return {std::move(child)};
            }

            std::int64_t lowBoundary = 0;
            std::int64_t highBoundary = 0;
            if (!safeAdd(clampLo, -shifted.value, lowBoundary) ||
                !safeAdd(clampHi, -shifted.value, highBoundary)) {
                error = "resource clamp threshold overflow";
                return {};
            }

            std::vector<SymbolicFrame> result;
            Box lowDomain = restrictResourceUpper(frame.inputDomain, shifted.source, lowBoundary);
            if (!lowDomain.empty()) {
                SymbolicFrame low = frame;
                low.pc = nextPc;
                low.inputDomain = std::move(lowDomain);
                low.resources[resourceIndex(target)] = {
                    ResourceExpressionKind::Constant,
                    target,
                    clampLo,
                };
                result.push_back(std::move(low));
            }

            if (lowBoundary != std::numeric_limits<std::int64_t>::max() &&
                highBoundary != std::numeric_limits<std::int64_t>::min() &&
                lowBoundary + 1 <= highBoundary - 1) {
                Box middleDomain = restrictResourceLower(
                    frame.inputDomain, shifted.source, lowBoundary + 1);
                middleDomain = restrictResourceUpper(
                    std::move(middleDomain), shifted.source, highBoundary - 1);
                if (!middleDomain.empty()) {
                    SymbolicFrame middle = frame;
                    middle.pc = nextPc;
                    middle.inputDomain = std::move(middleDomain);
                    middle.resources[resourceIndex(target)] = shifted;
                    result.push_back(std::move(middle));
                }
            }

            Box highDomain = restrictResourceLower(frame.inputDomain, shifted.source, highBoundary);
            if (!highDomain.empty()) {
                SymbolicFrame high = frame;
                high.pc = nextPc;
                high.inputDomain = std::move(highDomain);
                high.resources[resourceIndex(target)] = {
                    ResourceExpressionKind::Constant,
                    target,
                    clampHi,
                };
                result.push_back(std::move(high));
            }
            return result;
        }

        bool applyResourceUpdate(
            const SymbolicFrame &frame,
            const ResourceUpdateOperand &update,
            int nextPc,
            std::vector<SymbolicFrame> &children,
            std::string &error) {
            if (update.kind == ResourceUpdateKind::AddConstant) {
                SymbolicFrame child = frame;
                child.pc = nextPc;
                ResourceExpression shifted;
                if (!shiftedResourceExpression(
                        frame.resources[resourceIndex(update.target)],
                        update.constant,
                        shifted,
                        error)) {
                    return false;
                }
                child.resources[resourceIndex(update.target)] = shifted;
                children.push_back(std::move(child));
                return true;
            }

            std::int64_t amount = 0;
            if (!integerScalar(frame, update.sourceSlot, amount, error)) {
                return false;
            }
            std::int64_t delta = 0;
            switch (update.kind) {
                case ResourceUpdateKind::ClampSubtractSlot:
                    if (amount == std::numeric_limits<std::int64_t>::min()) {
                        error = "resource subtract overflow";
                        return false;
                    }
                    delta = -amount;
                    break;
                case ResourceUpdateKind::ClampAddSlot:
                    delta = amount;
                    break;
                case ResourceUpdateKind::ClampAddQuarterSlot:
                    if (amount < 0) {
                        error = "quarter resource update received negative amount";
                        return false;
                    }
                    delta = amount >> 2;
                    break;
                case ResourceUpdateKind::None:
                case ResourceUpdateKind::AddConstant:
                    error = "invalid resource update operation";
                    return false;
            }
            children = clampResource(
                frame,
                update.target,
                delta,
                update.clampLo,
                update.clampHi,
                nextPc,
                error);
            return error.empty();
        }

        int maximumModeIndex(ModeAxis axis) noexcept {
            const std::uint16_t mask = allMask(axis);
            for (int bit = 15; bit >= 0; --bit) {
                if ((mask & (1u << bit)) != 0) {
                    return bit;
                }
            }
            return -1;
        }
    } // namespace

    SymbolicStepper::SymbolicStepper(const RuleBundle &bundle, const Problem &problem)
        : bundle_(bundle), problem_(problem) {
        std::uint64_t state = problem.seed;
        for (std::size_t position = 1; position < rngTape_.size(); ++position) {
            state = state * kLcgMultiplier + kLcgIncrement;
            rngTape_[position] = static_cast<std::uint32_t>(state >> 32);
        }
        lcg::init(problem.seed, true);
    }

    SymbolicFrame SymbolicStepper::makeRootFrame(
        int elapsedTurn,
        int selectedCommand,
        int rngPosition,
        const Box &inputDomain) const {
        SymbolicFrame frame;
        frame.routineId = bundle_.program.entryRoutine;
        frame.pc = 0;
        frame.rngPosition = rngPosition;
        frame.selectedCommand = selectedCommand;
        frame.elapsedTurn = elapsedTurn;
        frame.inputDomain = inputDomain;
        for (int axis = 0; axis < 4; ++axis) {
            frame.resources[axis] = {
                ResourceExpressionKind::InputOffset,
                static_cast<ResourceAxis>(axis),
                0,
            };
        }
        for (int field = 0; field < static_cast<int>(StateField::Count); ++field) {
            frame.controls[field] = rootControl(
                static_cast<StateField>(field), problem_, elapsedTurn, selectedCommand);
        }
        if (const Routine *entry = findRoutine(bundle_.program, frame.routineId); entry != nullptr) {
            std::string ignored;
            (void) applyRoutineInitializers(frame, *entry, ignored);
        }
        return frame;
    }

    SymbolicStepResult SymbolicStepper::step(const SymbolicFrame &frame) const {
        SymbolicStepResult result;
        if (frame.inputDomain.empty()) {
            result.reason = "symbolic frame has an empty input domain";
            return result;
        }
        const Routine *routine = findRoutine(bundle_.program, frame.routineId);
        if (routine == nullptr || frame.pc < 0 || frame.pc >= static_cast<int>(routine->instructions.size())) {
            result.reason = "symbolic frame references an invalid pc";
            return result;
        }
        const Instruction &instruction = routine->instructions[frame.pc];

        auto ordinaryChild = [&](int pc) {
            SymbolicFrame child = frame;
            child.pc = pc;
            result.frames.push_back(std::move(child));
        };

        switch (instruction.opcode) {
            case Opcode::Step:
                result.reason = "untyped STEP reached symbolic verifier";
                return result;
            case Opcode::Branch: {
                if (instruction.successors.size() != 2) {
                    result.reason = "BRANCH has invalid successor count";
                    return result;
                }
                DomainSplit split = splitCondition(bundle_, frame, instruction.branchCondition);
                if (!split.accepted) {
                    result.reason = split.reason;
                    return result;
                }
                for (Box &domain: split.trueDomains) {
                    SymbolicFrame child = frame;
                    child.pc = instruction.successors[0];
                    child.inputDomain = std::move(domain);
                    result.frames.push_back(std::move(child));
                }
                for (Box &domain: split.falseDomains) {
                    SymbolicFrame child = frame;
                    child.pc = instruction.successors[1];
                    child.inputDomain = std::move(domain);
                    result.frames.push_back(std::move(child));
                }
                result.accepted = true;
                return result;
            }
            case Opcode::Switch: {
                if (instruction.switchCaseValues.size() != instruction.successors.size()) {
                    result.reason = "SWITCH case table mismatch";
                    return result;
                }
                auto appendCase = [&](SymbolicFrame child, std::int64_t value) -> bool {
                    auto found = std::find(
                        instruction.switchCaseValues.begin(),
                        instruction.switchCaseValues.end(),
                        static_cast<int>(value));
                    if (found == instruction.switchCaseValues.end()) {
                        result.reason = "SWITCH value is outside registered cases";
                        return false;
                    }
                    const std::size_t index = static_cast<std::size_t>(
                        found - instruction.switchCaseValues.begin());
                    child.pc = instruction.successors[index];
                    result.frames.push_back(std::move(child));
                    return true;
                };

                if (instruction.switchOperand.kind == SwitchSourceKind::CurrentAction) {
                    const ControlExpression &action = frame.controls[stateIndex(StateField::CurrentAction)];
                    for (auto &[domain, value]: splitByControlValue(frame.inputDomain, action)) {
                        SymbolicFrame child = frame;
                        child.inputDomain = std::move(domain);
                        if (!appendCase(std::move(child), value)) {
                            return result;
                        }
                    }
                } else if (instruction.switchOperand.kind == SwitchSourceKind::WeightedScalarUpperBounds) {
                    double scalar = 0.0;
                    if (!getScalar(frame, instruction.switchOperand.sourceSlot, scalar, result.reason)) {
                        return result;
                    }
                    std::int64_t value = 0;
                    if (!exactInteger(scalar, value)) {
                        result.reason = "weighted SWITCH uses non-integer scalar";
                        return result;
                    }
                    std::size_t selected = instruction.switchOperand.upperInclusiveBounds.size();
                    for (std::size_t index = 0;
                         index < instruction.switchOperand.upperInclusiveBounds.size(); ++index) {
                        if (value <= instruction.switchOperand.upperInclusiveBounds[index]) {
                            selected = index;
                            break;
                        }
                    }
                    if (selected >= instruction.successors.size()) {
                        result.reason = "weighted SWITCH value exceeds registered range";
                        return result;
                    }
                    ordinaryChild(instruction.successors[selected]);
                } else {
                    ScalarSlot sourceSlot = ScalarSlot::None;
                    if (instruction.switchOperand.kind == SwitchSourceKind::ActionHistorySlot0) {
                        sourceSlot = ScalarSlot::ActionSlot0;
                    } else if (instruction.switchOperand.kind == SwitchSourceKind::ActionHistorySlot1) {
                        sourceSlot = ScalarSlot::ActionSlot1;
                    } else {
                        result.reason = "SWITCH has unsupported typed source";
                        return result;
                    }
                    std::int64_t value = 0;
                    if (!integerScalar(frame, sourceSlot, value, result.reason) ||
                        !appendCase(frame, value)) {
                        return result;
                    }
                }
                result.accepted = true;
                return result;
            }
            case Opcode::Call: {
                if (instruction.successors.size() != 1) {
                    result.reason = "CALL has invalid return pc";
                    return result;
                }
                const Routine *callee = findRoutine(bundle_.program, instruction.callTarget);
                if (callee == nullptr) {
                    result.reason = "CALL target is missing";
                    return result;
                }
                SymbolicFrame child = frame;
                child.callStack.push_back({frame.routineId, instruction.successors.front()});
                child.routineId = callee->id;
                child.pc = 0;
                if (!applyRoutineInitializers(child, *callee, result.reason)) {
                    return result;
                }
                result.frames.push_back(std::move(child));
                result.accepted = true;
                return result;
            }
            case Opcode::Return: {
                if (frame.callStack.empty()) {
                    result.reason = "RETURN reached with empty call stack";
                    return result;
                }
                SymbolicFrame child = frame;
                if (frame.routineId == "ally-slot") {
                    child.actionProgress |= ActionProgressAllyDone;
                } else if (frame.routineId == "enemy-slot") {
                    child.actionProgress |= ActionProgressEnemyDone;
                }
                const ReturnAddress address = child.callStack.back();
                child.callStack.pop_back();
                child.routineId = address.routineId;
                child.pc = address.pc;
                result.frames.push_back(std::move(child));
                result.accepted = true;
                return result;
            }
            case Opcode::Finish:
                if (!frame.callStack.empty() || !routine->turnRoutine) {
                    result.reason = "FINISH reached outside top-level TURN";
                    return result;
                }
                result.accepted = true;
                result.finished = true;
                result.frames.push_back(frame);
                return result;
            case Opcode::ReadRng: {
                if (instruction.successors.size() != 1) {
                    result.reason = "READ_RNG has invalid successor count";
                    return result;
                }
                auto readAt = [&](SymbolicFrame child, int maximum) -> bool {
                    if (child.rngPosition <= 0 ||
                        child.rngPosition >= static_cast<int>(rngTape_.size()) || maximum < 0) {
                        result.reason = "READ_RNG position or maximum is out of range";
                        return false;
                    }
                    const std::uint32_t top = rngTape_[child.rngPosition++];
                    double value = 0.0;
                    switch (instruction.rngRead.kind) {
                        case RngReadKind::Percent:
                            value = static_cast<double>(
                                (static_cast<std::uint64_t>(top) * instruction.rngRead.intMax) >> 32);
                            break;
                        case RngReadKind::IntRangeInclusive: {
                            const int width = instruction.rngRead.intMax - instruction.rngRead.intMin + 1;
                            value = instruction.rngRead.intMin + static_cast<int>(
                                (static_cast<std::uint64_t>(top) * width) >> 32);
                            break;
                        }
                        case RngReadKind::FloatRange:
                            value = instruction.rngRead.floatMin +
                                    static_cast<double>(top) *
                                        ((instruction.rngRead.floatMax - instruction.rngRead.floatMin) /
                                         4294967296.0);
                            break;
                        case RngReadKind::PercentCameraRemaining:
                            value = static_cast<double>(
                                (static_cast<std::uint64_t>(top) * maximum) >> 32);
                            break;
                        case RngReadKind::None:
                            result.reason = "READ_RNG has no operation";
                            return false;
                    }
                    if (!setScalar(child, instruction.rngRead.resultSlot, value, result.reason)) {
                        return false;
                    }
                    child.pc = instruction.successors.front();
                    result.frames.push_back(std::move(child));
                    return true;
                };

                if (instruction.rngRead.kind != RngReadKind::PercentCameraRemaining) {
                    if (!readAt(frame, instruction.rngRead.intMax)) {
                        return result;
                    }
                } else {
                    const ControlExpression &camera = frame.controls[stateIndex(StateField::Camera)];
                    for (auto &[domain, counter]: splitByControlValue(frame.inputDomain, camera)) {
                        if (counter < 0 || counter > 5) {
                            result.reason = "camera counter outside 0..5";
                            return result;
                        }
                        SymbolicFrame child = frame;
                        child.inputDomain = std::move(domain);
                        if (!readAt(std::move(child), 5 - static_cast<int>(counter))) {
                            return result;
                        }
                    }
                }
                result.accepted = true;
                return result;
            }
            case Opcode::SkipRng: {
                if (instruction.successors.size() != 1 || instruction.rngReads < 0 ||
                    frame.rngPosition > static_cast<int>(rngTape_.size()) - instruction.rngReads) {
                    result.reason = "SKIP_RNG exceeds fixed RNG tape";
                    return result;
                }
                SymbolicFrame child = frame;
                child.rngPosition += instruction.rngReads;
                child.pc = instruction.successors.front();
                result.frames.push_back(std::move(child));
                result.accepted = true;
                return result;
            }
            case Opcode::Native: {
                if (instruction.successors.size() != 1) {
                    result.reason = "NATIVE has invalid successor count";
                    return result;
                }
                const NativeContract *contract = findNative(bundle_, instruction.nativeId);
                if (contract == nullptr || !contract->resourceIndependent) {
                    result.reason = "NATIVE contract missing or resource-dependent";
                    return result;
                }
                SymbolicFrame child = frame;
                if (child.rngPosition <= 0 || contract->maxRngReads < 0 ||
                    child.rngPosition > static_cast<int>(rngTape_.size()) - contract->maxRngReads) {
                    result.reason = "NATIVE cannot be invoked inside its bounded RNG cursor";
                    return result;
                }

                const int positionBefore = child.rngPosition;
                int nativePosition = positionBefore;
                int nativeResult = 0;
                switch (contract->implementation) {
                    case NativeImplementation::BattlePhysicalDamage:
                        if (instruction.nativeArguments.size() != 2) {
                            result.reason = "physical NATIVE argument mismatch";
                            return result;
                        }
                        nativeResult = BattleEmulator::proofNativePhysicalDamage(
                            &nativePosition,
                            static_cast<int>(instruction.nativeArguments[0]),
                            static_cast<int>(instruction.nativeArguments[1]));
                        break;
                    case NativeImplementation::BattleTypeC:
                        if (instruction.nativeArguments.size() != 3) {
                            result.reason = "type-C NATIVE argument mismatch";
                            return result;
                        }
                        nativeResult = BattleEmulator::proofNativeTypeC(
                            &nativePosition,
                            instruction.nativeArguments[0],
                            instruction.nativeArguments[1],
                            instruction.nativeArguments[2]);
                        break;
                    case NativeImplementation::BattleTypeD:
                        if (instruction.nativeArguments.size() != 2) {
                            result.reason = "type-D NATIVE argument mismatch";
                            return result;
                        }
                        nativeResult = BattleEmulator::proofNativeTypeD(
                            &nativePosition,
                            instruction.nativeArguments[0],
                            instruction.nativeArguments[1]);
                        break;
                    case NativeImplementation::None:
                        result.reason = "NATIVE has no fixed implementation";
                        return result;
                }

                const int consumed = nativePosition - positionBefore;
                if (consumed < 0 || consumed > contract->maxRngReads ||
                    nativePosition >= static_cast<int>(rngTape_.size())) {
                    result.reason = "NATIVE implementation violated its registered RNG bound";
                    return result;
                }
                child.rngPosition = nativePosition;
                const double computed = static_cast<double>(nativeResult);
                if (computed < contract->resultMin || computed > contract->resultMax) {
                    result.reason = "NATIVE result violates registered range";
                    return result;
                }
                if (!setScalar(child, instruction.scalarResult, computed, result.reason)) {
                    return result;
                }
                child.pc = instruction.successors.front();
                result.frames.push_back(std::move(child));
                result.accepted = true;
                return result;
            }
            case Opcode::ResourceUpdate: {
                if (instruction.successors.size() != 1 ||
                    !applyResourceUpdate(
                        frame,
                        instruction.resourceUpdate,
                        instruction.successors.front(),
                        result.frames,
                        result.reason)) {
                    if (result.reason.empty()) {
                        result.reason = "RESOURCE_UPDATE failed";
                    }
                    return result;
                }
                result.accepted = true;
                return result;
            }
            case Opcode::ModeUpdate: {
                if (instruction.successors.size() != 1) {
                    result.reason = "MODE_UPDATE has invalid successor count";
                    return result;
                }
                SymbolicFrame child = frame;
                for (const StateWriteOperand &write: instruction.stateWrites) {
                    if (!setControlFromWrite(child, write, problem_, result.reason)) {
                        return result;
                    }
                }
                child.pc = instruction.successors.front();
                result.frames.push_back(std::move(child));
                result.accepted = true;
                return result;
            }
            case Opcode::ScalarUpdate: {
                if (instruction.successors.size() != 1) {
                    result.reason = "SCALAR_UPDATE has invalid successor count";
                    return result;
                }
                SymbolicFrame child = frame;
                if (!applyScalarUpdate(child, bundle_, instruction.scalarUpdate, result.reason)) {
                    return result;
                }
                child.pc = instruction.successors.front();
                result.frames.push_back(std::move(child));
                result.accepted = true;
                return result;
            }
            case Opcode::RecordAction: {
                if (instruction.successors.size() != 1) {
                    result.reason = "RECORD_ACTION has invalid successor count";
                    return result;
                }
                const ControlExpression &action = frame.controls[stateIndex(StateField::CurrentAction)];
                for (auto &[domain, actionValue]: splitByControlValue(frame.inputDomain, action)) {
                    SymbolicFrame child = frame;
                    child.inputDomain = std::move(domain);
                    std::int64_t count = 0;
                    if (!integerScalar(child, ScalarSlot::ActionCount, count, result.reason) ||
                        count < 0 || count >= 2) {
                        if (result.reason.empty()) {
                            result.reason = "action history exceeds two slots";
                        }
                        return result;
                    }
                    const ScalarSlot target = count == 0 ? ScalarSlot::ActionSlot0 : ScalarSlot::ActionSlot1;
                    if (!setScalar(child, target, static_cast<double>(actionValue), result.reason) ||
                        !setScalar(child, ScalarSlot::ActionCount, static_cast<double>(count + 1), result.reason)) {
                        return result;
                    }
                    child.pc = instruction.successors.front();
                    result.frames.push_back(std::move(child));
                }
                result.accepted = true;
                return result;
            }
        }
        result.reason = "unknown opcode";
        return result;
    }

    bool SymbolicStepper::outputBox(
        const SymbolicFrame &frame,
        Box &output,
        std::string &error) const {
        output = {};
        for (int axisIndex = 0; axisIndex < 4; ++axisIndex) {
            const ResourceAxis axis = static_cast<ResourceAxis>(axisIndex);
            const ResourceExpression &expression = frame.resources[axisIndex];
            Interval image;
            if (expression.kind == ResourceExpressionKind::Constant) {
                image = {expression.value, expression.value};
            } else {
                if (expression.source != axis) {
                    error = "resource expression changed its source coordinate";
                    return false;
                }
                const Interval &input = intervalOf(frame.inputDomain, expression.source);
                if (!safeAdd(input.lo, expression.value, image.lo) ||
                    !safeAdd(input.hi, expression.value, image.hi)) {
                    error = "resource output image overflow";
                    return false;
                }
            }
            intervalOf(output, axis) = image;
        }

        for (int modeIndex = 0; modeIndex < 6; ++modeIndex) {
            const ModeAxis axis = static_cast<ModeAxis>(modeIndex);
            const StateField stateField = stateFieldForMode(axis);
            const ControlExpression &expression = frame.controls[stateIndex(stateField)];
            std::uint16_t outputMask = 0;
            auto addValue = [&](std::int64_t value) -> bool {
                const int maximum = maximumModeIndex(axis);
                if (value < 0 || value > maximum) {
                    error = "mode output is outside registered enum";
                    return false;
                }
                outputMask |= static_cast<std::uint16_t>(1u << value);
                return true;
            };
            if (expression.kind == ControlExpressionKind::Constant) {
                if (!addValue(expression.constant)) {
                    return false;
                }
            } else {
                const std::uint16_t active = modeMaskOf(frame.inputDomain, expression.source);
                for (int bit = 0; bit < 8; ++bit) {
                    if ((active & (1u << bit)) != 0 && !addValue(expression.table[bit])) {
                        return false;
                    }
                }
            }
            modeMaskOf(output, axis) = outputMask;
        }
        if (output.empty()) {
            error = "symbolic output box is empty";
            return false;
        }
        return true;
    }

    bool SymbolicStepper::splitOutputPredicate(
        const SymbolicFrame &frame,
        const Predicate &predicate,
        std::vector<SymbolicFrame> &trueFrames,
        std::vector<SymbolicFrame> &falseFrames,
        std::string &error) const {
        trueFrames.clear();
        falseFrames.clear();

        auto appendDomains = [&](const DomainSplit &split) {
            for (const Box &domain: split.trueDomains) {
                SymbolicFrame child = frame;
                child.inputDomain = domain;
                trueFrames.push_back(std::move(child));
            }
            for (const Box &domain: split.falseDomains) {
                SymbolicFrame child = frame;
                child.inputDomain = domain;
                falseFrames.push_back(std::move(child));
            }
        };

        if (predicate.kind == PredicateKind::ResourceLe) {
            const ResourceExpression &expression = frame.resources[resourceIndex(predicate.resource)];
            DomainSplit split = splitResourceComparison(
                frame.inputDomain,
                expression,
                CompareOp::Le,
                static_cast<double>(predicate.threshold));
            if (!split.accepted) {
                error = split.reason;
                return false;
            }
            appendDomains(split);
            return true;
        }

        const StateField field = stateFieldForMode(predicate.mode);
        if (field == StateField::None) {
            error = "output predicate references an invalid mode";
            return false;
        }
        const ControlExpression &expression = frame.controls[stateIndex(field)];
        if (expression.kind == ControlExpressionKind::Constant) {
            if (expression.constant < 0 || expression.constant >= 16) {
                error = "output mode predicate saw value outside bit domain";
                return false;
            }
            const bool truth =
                (predicate.mask & static_cast<std::uint16_t>(1u << expression.constant)) != 0;
            (truth ? trueFrames : falseFrames).push_back(frame);
            return true;
        }

        std::uint16_t trueInputMask = 0;
        std::uint16_t falseInputMask = 0;
        const std::uint16_t active = modeMaskOf(frame.inputDomain, expression.source);
        for (int bit = 0; bit < 8; ++bit) {
            const auto inputBit = static_cast<std::uint16_t>(1u << bit);
            if ((active & inputBit) == 0) {
                continue;
            }
            const std::int64_t outputValue = expression.table[bit];
            if (outputValue < 0 || outputValue >= 16) {
                error = "output mode map produced value outside bit domain";
                return false;
            }
            const auto outputBit = static_cast<std::uint16_t>(1u << outputValue);
            if ((predicate.mask & outputBit) != 0) {
                trueInputMask |= inputBit;
            } else {
                falseInputMask |= inputBit;
            }
        }
        if (trueInputMask != 0) {
            SymbolicFrame child = frame;
            modeMaskOf(child.inputDomain, expression.source) &= trueInputMask;
            if (!child.inputDomain.empty()) {
                trueFrames.push_back(std::move(child));
            }
        }
        if (falseInputMask != 0) {
            SymbolicFrame child = frame;
            modeMaskOf(child.inputDomain, expression.source) &= falseInputMask;
            if (!child.inputDomain.empty()) {
                falseFrames.push_back(std::move(child));
            }
        }
        return true;
    }

    bool SymbolicStepper::classifyOutput(
        const SymbolicFrame &frame,
        const PredicatePartition &partition,
        std::vector<OutputLeafClassification> &leaves,
        std::string &error) const {
        leaves.clear();
        if (partition.root < 0 || partition.root >= static_cast<int>(partition.nodes.size())) {
            error = "output classification has invalid partition root";
            return false;
        }

        struct WorkItem {
            int node = -1;
            SymbolicFrame frame;
        };
        std::vector<WorkItem> pending;
        pending.push_back({partition.root, frame});
        std::uint64_t work = 0;
        const std::uint64_t hardLimit =
            static_cast<std::uint64_t>(partition.nodes.size()) * 16u + 16u;

        while (!pending.empty()) {
            WorkItem item = std::move(pending.back());
            pending.pop_back();
            if (++work > hardLimit) {
                error = "output classification exceeded finite partition bound";
                return false;
            }
            if (item.node < 0 || item.node >= static_cast<int>(partition.nodes.size())) {
                error = "output classification escaped partition";
                return false;
            }
            const PartitionNode &node = partition.nodes[item.node];
            if (node.leaf) {
                leaves.push_back({node.localCellId, std::move(item.frame)});
                continue;
            }

            std::vector<SymbolicFrame> trueFrames;
            std::vector<SymbolicFrame> falseFrames;
            if (!splitOutputPredicate(
                    item.frame,
                    node.predicate,
                    trueFrames,
                    falseFrames,
                    error)) {
                return false;
            }
            // LIFO: push false first so the registered true branch is processed first.
            for (auto it = falseFrames.rbegin(); it != falseFrames.rend(); ++it) {
                pending.push_back({node.falseChild, std::move(*it)});
            }
            for (auto it = trueFrames.rbegin(); it != trueFrames.rend(); ++it) {
                pending.push_back({node.trueChild, std::move(*it)});
            }
        }
        if (leaves.empty()) {
            error = "nonempty symbolic output classified to no partition leaf";
            return false;
        }
        return true;
    }

    bool SymbolicStepper::remainingRngBound(
        const SymbolicFrame &frame,
        int &remaining,
        std::string &error) const {
        remaining = 0;
        auto addPc = [&](const std::string &routineId, int pc) -> bool {
            const PcStaticBounds *bounds = lookupPcBounds(bundle_.bounds, routineId, pc);
            if (bounds == nullptr || bounds->maximumRngReads < 0 ||
                remaining > std::numeric_limits<int>::max() - bounds->maximumRngReads) {
                error = "missing or overflowing static RNG bound for resume point";
                return false;
            }
            remaining += bounds->maximumRngReads;
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
} // namespace d20proof
