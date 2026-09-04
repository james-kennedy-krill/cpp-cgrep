#pragma once

#include <cstdint>
#include <string>

namespace cgrep {
// "1.5 GiB", "912 KiB", "37 B" -- for --stats output.
std::string format_bytes(std::uintmax_t bytes);
} // namespace cgrep
