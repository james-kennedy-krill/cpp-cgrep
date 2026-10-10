#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <system_error>

namespace cgrep {

struct WalkOptions {
  bool include_hidden = false;
  bool follow_symlinks = false;
  std::size_t max_depth = 0; // 0 == unlimited
};

struct WalkStats {
  std::uintmax_t files = 0;
  std::uintmax_t dirs = 0;
  std::uintmax_t skipped_hidden = 0;  // dotfiles and dotdirs
  std::uintmax_t skipped_special = 0; // FIFOs, sockets, devices, dangling links
  std::uintmax_t skipped_loops = 0;   // directories already visited (--follow)
  std::uintmax_t errors = 0;

  std::uintmax_t skipped() const {
    return skipped_hidden + skipped_special + skipped_loops;
  }
};

using FileCallback =
    std::function<void(const std::filesystem::directory_entry &)>;
using ErrorCallback =
    std::function<void(const std::filesystem::path &, const std::error_code &)>;

bool is_hidden(const std::filesystem::path &p);

// Walks `root`, calling `on_file` for every regular file, in sorted order
// within each directory. Never throws: failures go to `on_error`.
WalkStats walk(const std::filesystem::path &root, const WalkOptions &options,
               const FileCallback &on_file, const ErrorCallback &on_error);

} // namespace cgrep