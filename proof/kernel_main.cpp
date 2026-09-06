#include "ProofKernel.h"
#include "RuleProgram.h"

#include <iostream>
#include <string>

int main(int argc, char **argv) {
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

    if (argc != 2 || std::string(argv[1]) != "--self-check") {
        std::cerr << "usage: " << argv[0] << " --self-check\n";
        return 1;
    }

    const d20proof::CheckResult check = d20proof::ProofKernel::selfCheck(*bundle);
    if (!check.accepted) {
        std::cerr << "KERNEL_SELF_CHECK_FAILED " << check.reason << '\n';
        return 3;
    }

    std::cout << "KERNEL_SELF_CHECK_OK"
            << " rule=yo2_be.d20:1"
            << " Rmax=" << bundle->bounds.rMax
            << " maxSteps=" << bundle->bounds.maximumInstructionSteps
            << " callDepth=" << bundle->bounds.maximumCallDepth
            << '\n';
    return 0;
}
