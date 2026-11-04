#include <doctest/doctest.h>

#include <unistd.h>

#include <fstream>
#include <string>
#include <vector>

#include "file_reader.hpp"

namespace fs = std::filesystem;

namespace {
class TempDir {
public:
  TempDir()
      : root_(fs::temp_directory_path() /
              ("cgrep_fr_" + std::to_string(::getpid()))) {
    fs::create_directories(root_);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(root_, ec);
  }
  TempDir(const TempDir &) = delete;
  TempDir &operator=(const TempDir &) = delete;
  const fs::path &root() const { return root_; }
  fs::path write(const std::string &name, const std::string &body) const {
    const fs::path p = root_ / name;
    std::ofstream out(p, std::ios::binary);
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
    return p;
  }

private:
  fs::path root_;
};

std::vector<std::string> lines_of(std::string_view text) {
  std::vector<std::string> out;
  std::string_view rest = text, line;
  while (cgrep::next_line(rest, line)) {
    out.emplace_back(line);
  }
  return out;
}
} // namespace

TEST_CASE("next_line splits without a trailing newline") {
  CHECK(lines_of("a\nb\nc") == std::vector<std::string>{"a", "b", "c"});
}
TEST_CASE("next_line handles a trailing newline") {
  CHECK(lines_of("a\nb\n") == std::vector<std::string>{"a", "b"});
}
TEST_CASE("next_line preserves empty lines") {
  CHECK(lines_of("a\n\nb\n") == std::vector<std::string>{"a", "", "b"});
}
TEST_CASE("next_line strips CRLF") {
  CHECK(lines_of("a\r\nb\r\n") == std::vector<std::string>{"a", "b"});
}
TEST_CASE("next_line on empty input yields nothing") {
  CHECK(lines_of("").empty());
}
TEST_CASE("next_line does not copy") {
  const std::string text = "hello\nworld\n";
  std::string_view rest(text), line;
  REQUIRE(cgrep::next_line(rest, line));
  CHECK(line.data() == text.data()); // points INTO text
  CHECK(rest.data() == text.data() + 6);
}

TEST_CASE("looks_binary detects NUL bytes") {
  CHECK(!cgrep::looks_binary("plain text\nmore text\n"));
  CHECK(cgrep::looks_binary(std::string_view("ELF\0\1\2", 6)));
  CHECK(!cgrep::looks_binary(""));
}
TEST_CASE("looks_binary only probes the first 8 KiB") {
  std::string s(9000, 'a');
  s[8500] = '\0';
  CHECK(!cgrep::looks_binary(s));
}

TEST_CASE("FileReader reads a small file through its buffer") {
  TempDir t;
  const auto p = t.write("small.txt", "one\ntwo\n");
  cgrep::FileReader reader;
  std::error_code ec;
  const auto text = reader.read(p, ec);
  REQUIRE(text.has_value());
  CHECK(!ec);
  CHECK(*text == "one\ntwo\n");
  CHECK(!reader.last_was_mapped());
}

TEST_CASE("FileReader maps a large file") {
  TempDir t;
  const std::string body(cgrep::kMmapThreshold + 1024, 'x');
  const auto p = t.write("big.txt", body);
  cgrep::FileReader reader;
  std::error_code ec;
  const auto text = reader.read(p, ec);
  REQUIRE(text.has_value());
  CHECK(text->size() == body.size());
  CHECK(reader.last_was_mapped());
}

TEST_CASE("FileReader handles an empty file") {
  TempDir t;
  const auto p = t.write("empty.txt", "");
  cgrep::FileReader reader;
  std::error_code ec;
  const auto text = reader.read(p, ec);
  REQUIRE(text.has_value());
  CHECK(text->empty());
}

TEST_CASE("FileReader reports a missing file") {
  TempDir t;
  cgrep::FileReader reader;
  std::error_code ec;
  CHECK(!reader.read(t.root() / "nope.txt", ec).has_value());
  CHECK(ec == std::errc::no_such_file_or_directory);
}

TEST_CASE("FileReader reuses its buffer across calls") {
  TempDir t;
  const auto a = t.write("a.txt", "aaaa\n");
  const auto b = t.write("b.txt", "bb\n");
  cgrep::FileReader reader;
  std::error_code ec;
  const auto first = reader.read(a, ec);
  REQUIRE(first.has_value());
  CHECK(*first == "aaaa\n");
  const auto second = reader.read(b, ec);
  REQUIRE(second.has_value());
  CHECK(*second == "bb\n"); // the first view is now dangling -- by design
}

TEST_CASE("MappedFile is move-only and releases on move") {
  TempDir t;
  const std::string body(cgrep::kMmapThreshold + 10, 'z');
  const auto p = t.write("m.txt", body);
  std::error_code ec;
  auto first = cgrep::MappedFile::open(p, ec);
  REQUIRE(first.has_value());
  CHECK(first->size() == body.size());

  cgrep::MappedFile moved = std::move(*first);
  CHECK(moved.size() == body.size());
  CHECK(first->size() == 0); // moved-from: empty, still destructible
}