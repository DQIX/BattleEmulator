//
// Created by Owner on 2024/02/07.
//

#include "debug.h"

#ifndef NDEBUG
namespace trace {
    namespace {
        bool traceEnabled = false;
    }

    void setEnabled(const bool value) noexcept {
        traceEnabled = value;
    }

    bool enabled() noexcept {
        return traceEnabled;
    }
}
#endif

