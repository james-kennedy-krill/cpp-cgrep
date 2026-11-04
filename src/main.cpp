#include <fmt/core.h>

#include <filesystem>
#include <string>
#include <vector>

#include "cgrep/version.hpp"
#include "file_reader.hpp"
#include "format_util.hpp"
#include "walk.hpp"

namespace fs = std::filesystem;

namespace {

struct Totals {
  std::uintmax_t files = 0;
  std::uintmax_t binaries = 0;
  std::uintmax_t lines = 0;
  std::uintmax_t bytes = 0;
  std::uintmax_t mapped = 0;
  std::uintmax_t read_errors = 0;
};

} // namespace

int main(int argc, char *argv[]) {
  cgrep::WalkOptions options;
  std::vector<fs::path> roots;
  bool show_stats = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--version") {
      fmt::print("{} {}\n", cgrep::kName, cgrep::kVersion);
      return 0;
    }
    if (arg == "--hidden") {
      options.include_hidden = true;
    } else if (arg == "--follow") {
      options.follow_symlinks = true;
    } else if (arg == "--stats") {
      show_stats = true;
    } else if (arg.starts_with("--")) {
      fmt::print(stderr, "cgrep: unknown flag '{}'\n", arg);
      return 2;
    } else {
      roots.emplace_back(arg);
    }
  }
  if (roots.empty()) {
    roots.emplace_back(".");
  }

  cgrep::FileReader reader; // one buffer, reused for every file
  Totals totals;
  cgrep::WalkStats walk_totals;

  for (const fs::path &root : roots) {
    const cgrep::WalkStats s = cgrep::walk(
        root, options,
        [&](const fs::directory_entry &entry) {
          std::error_code ec;
          const auto text = reader.read(entry.path(), ec);
          if (!text) {
            fmt::print(stderr, "cgrep: {}: {}\n", entry.path().string(),
                       ec.message());
            ++totals.read_errors;
            return;
          }

          ++totals.files;
          totals.bytes += text->size();
          if (reader.last_was_mapped()) {
            ++totals.mapped;
          }

          if (cgrep::looks_binary(*text)) {
            ++totals.binaries;
            fmt::print("{}: binary file (skipped)\n", entry.path().string());
            return;
          }

          std::uintmax_t lines = 0;
          std::string_view rest = *text;
          std::string_view line;
          while (cgrep::next_line(rest, line)) {
            ++lines;
          }

          totals.lines += lines;
          fmt::print("{}: {} lines\n", entry.path().string(), lines);
        },
        [](const fs::path &path, const std::error_code &ec) {
          fmt::print(stderr, "cgrep: {}: {}\n", path.string(), ec.message());
        });

    walk_totals.files += s.files;
    walk_totals.dirs += s.dirs;
    walk_totals.skipped_hidden += s.skipped_hidden;
    walk_totals.skipped_special += s.skipped_special;
    walk_totals.skipped_loops += s.skipped_loops;
    walk_totals.errors += s.errors;
  }

  if (show_stats) {
    fmt::print(stderr,
               "\n{} files read ({} mapped, {} buffered), {}\n"
               "{} lines, {} binary files skipped\n"
               "walk: {} dirs, {} hidden, {} special, {} loops, {} errors, {} "
               "read errors\n",
               totals.files, totals.mapped, totals.files - totals.mapped,
               cgrep::format_bytes(totals.bytes), totals.lines, totals.binaries,
               walk_totals.dirs, walk_totals.skipped_hidden,
               walk_totals.skipped_special, walk_totals.skipped_loops,
               walk_totals.errors, totals.read_errors);
  }
  return (walk_totals.errors > 0 || totals.read_errors > 0) ? 1 : 0;
}