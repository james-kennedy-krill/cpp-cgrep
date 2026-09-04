#include <doctest/doctest.h>

#include "cgrep/version.hpp"

TEST_CASE("version is populated from CMake") {
  CHECK(cgrep::kName == "cgrep");
  CHECK(!cgrep::kVersion.empty());
}