#include <fmt/color.h>
#include <fmt/core.h>

#include <filesystem>
#include <string>
#include <vector>

#include "cgrep/version.hpp"
#include "format_util.hpp"
#include "walk.hpp"

namespace fs = std::filesystem;

int main(int argc, char *argv[]) {
  cgrep::WalkOptions options;
  std::vector<fs::path> roots;

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
    } else if (arg == "--max-depth") {
      options.max_depth = static_cast<std::uintmax_t>(std::stoul(argv[++i]));
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

  cgrep::WalkStats total;
  std::uintmax_t bytes = 0;

  for (const fs::path &root : roots) {
    const cgrep::WalkStats s = cgrep::walk(
        root, options,
        [&bytes](const fs::directory_entry &entry) {
          std::error_code ec;
          const std::uintmax_t size = entry.file_size(ec);
          if (!ec) {
            bytes += size;
          }
          fmt::print("{}\n", entry.path().string());
        },
        [](const fs::path &path, const std::error_code &ec) {
          fmt::print(stderr, "cgrep: {}: {}\n", path.string(), ec.message());
        });

    total.files += s.files;
    total.dirs += s.dirs;
    total.skipped_hidden += s.skipped_hidden;
    total.skipped_special += s.skipped_special;
    total.skipped_loops += s.skipped_loops;
    total.errors += s.errors;
  }

  fmt::print(stderr,
             "\n{} files in {} dirs, {}\n"
             "skipped: {} hidden, {} special, {} loops | {} errors\n",
             total.files, total.dirs, cgrep::format_bytes(bytes),
             total.skipped_hidden, total.skipped_special, total.skipped_loops,
             total.errors);
  return total.errors > 0 ? 1 : 0;
}