#include "CliCommon.h"
#include "RuleProgram.h"
#include "WorldlineSolver.h"

#include <cstdint>
#include <iostream>
#include <string_view>

int main(int argc, char **argv) {
    if (argc < 4) {
        std::cerr << "usage: " << argv[0]
                  << " <solve|minimum> <seed> <horizon> [candidate actions...]\n";
        return 1;
    }

    const std::string_view operation = argv[1];
    if (operation != "solve" && operation != "minimum") {
        std::cerr << "invalid operation; expected solve or minimum\n";
        return 1;
    }

    std::uint64_t seed = 0;
    int horizon = 0;
    if (!d20proof::parseU64(argv[2], seed) || !d20proof::parseInt(argv[3], horizon)) {
        std::cerr << "invalid seed/horizon\n";
        return 1;
    }

    bool actionsValid = false;
    const std::vector<int> actions = d20proof::parseActions(argc, argv, 4, actionsValid);
    if (!actionsValid) {
        std::cerr << "invalid action\n";
        return 1;
    }

    d20proof::RuleRegistry registry;
    std::string registrationError;
    if (!registry.registerNew(d20proof::makeYo2BeBundleCandidate(), registrationError)) {
        std::cerr << "RULE_REGISTRATION_ERROR " << registrationError << '\n';
        return 2;
    }

    const d20proof::RuleBundle *bundle = registry.lookup({"yo2_be.d20", 1});
    if (bundle == nullptr) {
        std::cerr << "RULE_REGISTRATION_ERROR registered rule not found\n";
        return 2;
    }

    d20proof::ProofBudget budget;
    // 8M is the specification's initial PoC tuning point.  The production
    // unknown-witness path is still bounded by the same 15s cumulative
    // deadline and all K/J/C/byte/candidate/repair limits, but uses the tuned
    // V ceiling established by measured runs.
    budget.maxWork = 512'000'000;
    // The 2M candidate-choice cap is likewise an explicitly adjustable PoC
    // cutoff.  Abstract-path duplication can consume it well before the 15s
    // deadline, so production keeps the semantic 2048 unique-candidate cap
    // but allows more choice scans to reach those candidates.
    budget.maxCandidateScans = 64'000'000;
    budget.maxCandidates = 4'096;
    d20proof::WorldlineSolver solver(*bundle);
    const d20proof::Problem problem = d20proof::initialProblem(seed);
    const d20proof::SolveResult result = operation == "minimum"
        ? solver.proveMinimum(problem, horizon, budget, actions)
        : solver.solveSuffix(problem, horizon, budget, actions);

    std::cout << d20proof::renderSolveResult(result) << '\n';
    return result.kind == d20proof::SolveKind::ModelError ? 3 : 0;
}
