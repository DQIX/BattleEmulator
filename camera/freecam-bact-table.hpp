#pragma once

#include <array>
#include <cstdint>

namespace dq9::freecam::generated {
inline constexpr std::array<std::uint64_t, 16> kHasSpBact = {
    UINT64_C(0xfffffffffffffe00),
    UINT64_C(0xffffffffffffffff),
    UINT64_C(0xfbfffbffffffffff),
    UINT64_C(0xe0c00000000003fd),
    UINT64_C(0x0000000000000017),
    UINT64_C(0xfffe000000000000),
    UINT64_C(0xe0000001ffffffef),
    UINT64_C(0xfe000000b03fffff),
    UINT64_C(0x000000000024bc37),
    UINT64_C(0x000000000000007c),
    UINT64_C(0x0000040000000000),
    UINT64_C(0x0000000000000000),
    UINT64_C(0x000000180001f800),
    UINT64_C(0x0000000000000000),
    UINT64_C(0x0000000001830000),
    UINT64_C(0x0000000000000000)
};

[[nodiscard]] constexpr bool HasSpBact(std::uint16_t actionId) noexcept {
    return actionId < 1024 && ((kHasSpBact[actionId >> 6] >> (actionId & 63)) & UINT64_C(1)) != 0;
}
} // namespace dq9::freecam::generated
