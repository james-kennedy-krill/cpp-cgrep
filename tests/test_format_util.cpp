#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "format_util.hpp"

using cgrep::format_bytes;

TEST_CASE("format_bytes uses plain bytes below 1 KiB") {
  CHECK(format_bytes(0) == "0 B");
  CHECK(format_bytes(37) == "37 B");
  CHECK(format_bytes(1023) == "1023 B");
}

TEST_CASE("format_bytes steps up through binary units") {
  CHECK(format_bytes(1024) == "1.0 KiB");
  CHECK(format_bytes(1536) == "1.5 KiB");
  CHECK(format_bytes(1024ULL * 1024) == "1.0 MiB");
  CHECK(format_bytes(1024ULL * 1024 * 1024) == "1.0 GiB");
  CHECK(format_bytes(1024ULL * 1024 * 1024 * 1024) == "1.0 TiB");
}

TEST_CASE("format_bytes saturates at the largest unit") {
  CHECK(format_bytes(5ULL * 1024 * 1024 * 1024 * 1024 * 1024) == "5120.0 TiB");
}
