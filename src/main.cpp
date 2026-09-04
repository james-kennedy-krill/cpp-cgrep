#include <fmt/core.h>

#include "cgrep/version.hpp"
#include "format_util.hpp"

int main() {
  fmt::print("{} {}\n", cgrep::kName, cgrep::kVersion);
  fmt::print(
      "hello from a real build system -- {} of nothing searched so far\n",
      cgrep::format_bytes(0));
  return 0;
}