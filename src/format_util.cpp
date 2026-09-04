#include "format_util.hpp"

#include <array>
#include <fmt/format.h>

namespace cgrep {
std::string format_bytes(std::uintmax_t bytes) {
  constexpr std::array<const char *, 5> kUnits{"B", "KiB", "MiB", "GiB", "TiB"};

  if (bytes < 1024) {
    return fmt::format("{} {}", bytes, kUnits[0]);
  }

  double value = static_cast<double>(bytes);
  std::size_t unit = 0;
  while (value >= 1024.0 && unit + 1 < kUnits.size()) {
    value /= 1024.0;
    ++unit;
  }
  return fmt::format("{:.1f} {}", value, kUnits[unit]);
}

} // namespace cgrep
