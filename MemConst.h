//
// Created by owner on 2026/01/23.
//

#ifndef NEWDIRECTORY_MEMCONST_H
#define NEWDIRECTORY_MEMCONST_H
#include <cstddef>

#include "ActionBruteForcer.h"
#include "ActionSearcher.h"

namespace MemConst {

    constexpr std::size_t NODE_COUNT =
        ActionBruteForcerConst::MAX_NODES;

    constexpr std::size_t NODE_BUF_COUNT = 2;

    constexpr std::size_t NODE_BUF_BYTES =
        sizeof(Node) * NODE_COUNT * NODE_BUF_COUNT;

    // SearchOutput
    constexpr std::size_t SEARCH_OUTPUT_BYTES =
        sizeof(const Node*) * NODE_COUNT
      + sizeof(int);

    // Pool 系
    constexpr std::size_t NODE_POOL_SIZE =
            ActionSearcher::MAX_LAYER * ActionSearcher::MAX_LAYER;

    constexpr std::size_t ACTION_POOL_SIZE =
        NODE_POOL_SIZE * ActionBruteForcerConst::CONST_MAX_DEPTH;

    constexpr std::size_t POOL_BYTES =
          sizeof(int) * ACTION_POOL_SIZE
        + sizeof(int) * NODE_POOL_SIZE
        + sizeof(int) * NODE_POOL_SIZE
        + sizeof(uint8_t) * NODE_POOL_SIZE;

    // 合計
    constexpr std::size_t TOTAL_BYTES =
          NODE_BUF_BYTES
        + SEARCH_OUTPUT_BYTES
        + POOL_BYTES;

    constexpr std::size_t GiB = 1024ull * 1024 * 1024;

    static_assert(
        MemConst::TOTAL_BYTES < 8 * GiB,
        "Memory usage exceeds 8GiB. Design is not safe."
    );

} // namespace MemConst


#endif //NEWDIRECTORY_MEMCONST_H