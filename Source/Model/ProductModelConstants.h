#pragma once

#include <cstddef>

namespace padflow {
inline constexpr std::size_t padBankCount = 4U;
inline constexpr std::size_t padsPerBank = 12U;
inline constexpr std::size_t legacyPadsPerBank = 16U;
inline constexpr std::size_t totalPadCount = padBankCount * padsPerBank;
inline constexpr std::size_t minimumLayersPerPad = 4U;
} // namespace padflow
