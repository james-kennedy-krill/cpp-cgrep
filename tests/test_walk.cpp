#include <doctest/doctest.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "walk.hpp"

namespace fs = std::filesystem;

namespace {

// Builds a throwaway directory tree and removes it on destruction (RAII).
class TempTree {
public:
  TempTree() : root_(fs::temp_directory_path() / unique_name()) {
    fs::create_directories(root_);
  }
  ~TempTree() {
    std::error_code ec;
    fs::remove_all(root_, ec);
  }
  TempTree(const TempTree &) = delete;
  TempTree &operator=(const TempTree &) = delete;

  const fs::path &root() const { return root_; }

  void file(const std::string &rel, const std::string &body = "x") const {
    const fs::path p = root_ / rel;
    fs::create_directories(p.parent_path());
    std::ofstream out(p);
    out << body;
  }
  void dir(const std::string &rel) const {
    fs::create_directories(root_ / rel);
  }

private:
  static std::string unique_name() {
    static int counter = 0;
    return "cgrep_test_" + std::to_string(::getpid()) + "_" +
           std::to_string(counter++);
  }
  fs::path root_;
};

std::vector<std::string> collect(const fs::path &root,
                                 const cgrep::WalkOptions &opts,
                                 cgrep::WalkStats *stats_out = nullptr) {
  std::vector<std::string> found;
  const cgrep::WalkStats stats = cgrep::walk(
      root, opts,
      [&](const fs::directory_entry &e) {
        found.push_back(fs::relative(e.path(), root).generic_string());
      },
      [](const fs::path &, const std::error_code &) {});
  std::sort(found.begin(), found.end());
  if (stats_out != nullptr) {
    *stats_out = stats;
  }
  return found;
}

} // namespace

TEST_CASE("walk finds regular files recursively") {
  TempTree t;
  t.file("a.txt");
  t.file("sub/b.txt");
  t.file("sub/deep/c.txt");

  const auto found = collect(t.root(), {});
  CHECK(found ==
        std::vector<std::string>{"a.txt", "sub/b.txt", "sub/deep/c.txt"});
}

TEST_CASE("hidden files and directories are skipped by default") {
  TempTree t;
  t.file("visible.txt");
  t.file(".env");
  t.file(".git/config");

  SUBCASE("default skips them") {
    const auto found = collect(t.root(), {});
    CHECK(found == std::vector<std::string>{"visible.txt"});
  }
  SUBCASE("--hidden includes them") {
    cgrep::WalkOptions opts;
    opts.include_hidden = true;
    CHECK(collect(t.root(), opts).size() == 3);
  }
}

TEST_CASE("a single file as root is walked directly") {
  TempTree t;
  t.file("only.txt");
  CHECK(collect(t.root() / "only.txt", {}).size() == 1);
}

TEST_CASE("a missing root is an error, not a crash") {
  TempTree t;
  cgrep::WalkStats stats;
  const auto found = collect(t.root() / "nope", {}, &stats);
  CHECK(found.empty());
  CHECK(stats.errors == 1);
}

TEST_CASE("symlink loops do not hang the walk") {
  TempTree t;
  t.file("real.txt");
  t.dir("sub");
  std::error_code ec;
  fs::create_directory_symlink(t.root(), t.root() / "sub" / "loop", ec);
  if (ec) {
    return;
  } // some filesystems disallow it; skip rather than fail

  CHECK(collect(t.root(), {}) == std::vector<std::string>{"real.txt"});
}

TEST_CASE("FIFOs and sockets are skipped, not opened") {
  TempTree t;
  t.file("real.txt");
  const std::string fifo = (t.root() / "pipe").string();
  if (::mkfifo(fifo.c_str(), 0644) != 0) {
    return;
  }

  cgrep::WalkStats stats;
  CHECK(collect(t.root(), {}, &stats) == std::vector<std::string>{"real.txt"});
  CHECK(stats.skipped_special == 1);
}

TEST_CASE("max_depth limits recursion") {
  TempTree t;
  t.file("top.txt");
  t.file("one/a.txt");
  t.file("one/two/b.txt");

  cgrep::WalkOptions opts;
  opts.max_depth = 1;
  CHECK(collect(t.root(), opts) == std::vector<std::string>{"top.txt"});
}

TEST_CASE("is_hidden classifies names correctly") {
  CHECK(cgrep::is_hidden(".git"));
  CHECK(cgrep::is_hidden("/a/b/.env"));
  CHECK(!cgrep::is_hidden("visible.txt"));
  CHECK(!cgrep::is_hidden("/a/b/c"));
  CHECK(!cgrep::is_hidden(".."));
}

TEST_CASE("output is sorted within each directory") {
  TempTree t;
  t.file("zebra.txt");
  t.file("apple.txt");
  t.file("middle.txt");
  t.file("sub/z.txt");
  t.file("sub/a.txt");

  std::vector<std::string> order;
  cgrep::walk(
      t.root(), {},
      [&](const fs::directory_entry &e) {
        order.push_back(fs::relative(e.path(), t.root()).generic_string());
      },
      [](const fs::path &, const std::error_code &) {});

  // NOT sorted afterwards -- this is the order the walker emitted.
  CHECK(order == std::vector<std::string>{"apple.txt", "middle.txt",
                                          "sub/a.txt", "sub/z.txt",
                                          "zebra.txt"});
}

TEST_CASE("--follow detects a symlink loop instead of spinning") {
  TempTree t;
  t.file("real.txt");
  t.dir("sub");
  std::error_code ec;
  fs::create_directory_symlink(t.root(), t.root() / "sub" / "loop", ec);
  if (ec) {
    return;
  }

  cgrep::WalkOptions opts;
  opts.follow_symlinks = true;

  cgrep::WalkStats stats;
  const auto found = collect(t.root(), opts, &stats);
  CHECK(found == std::vector<std::string>{"real.txt"});
  CHECK(stats.skipped_loops == 1);
}

TEST_CASE("a character device root is rejected cleanly") {
  cgrep::WalkStats stats;
  const auto found = collect("/dev/null", {}, &stats);
  CHECK(found.empty());
  CHECK(stats.errors == 1);
}