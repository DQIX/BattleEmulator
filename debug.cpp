//
// Created by Owner on 2024/02/07.
//

#include "debug.h"

#ifdef DEBUG_TRACE_BOUNDARIES
namespace {
thread_local bool traceEnabled = false;
}

void battle_trace::setEnabled(const bool enabled) {
    traceEnabled = enabled;
}

bool battle_trace::enabled() {
    return traceEnabled;
}
#endif
