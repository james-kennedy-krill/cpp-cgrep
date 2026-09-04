#include <fmt/core.h>

#include "cgrep/version.hpp"
#include "format_util.hpp"

int main(int argc, char *argv[]) {
  if (argc > 1) {
    for (int i = 1; i < argc; ++i) {
      const std::string flag = argv[i];
      if (flag == "--version") {
        fmt::print("{} {}\n", cgrep::kName, cgrep::kVersion);
      }
    }
  }

  fmt::print(
      "hello from a real build system -- {} of nothing searched so far\n",
      cgrep::format_bytes(0));
  return 0;
}