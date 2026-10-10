#include "walk.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace cgrep {
namespace {

// Identifies a directory uniquely on this machine, regardless of the path used
// to reach it. Two paths with the same pair are the same directory.
struct DirId {
  dev_t device = 0;
  ino_t inode = 0;
  bool operator==(const DirId &other) const {
    return device == other.device && inode == other.inode;
  }
};

struct DirIdHash {
  std::size_t operator()(const DirId &id) const noexcept {
    const std::size_t a = std::hash<unsigned long long>{}(id.device);
    const std::size_t b = std::hash<unsigned long long>{}(id.inode);
    return a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2));
  }
};

bool stat_dir_id(const fs::path &p, DirId *out) {
  struct ::stat st{};
  if (::stat(p.c_str(), &st) != 0) {
    return false;
  }
  out->device = st.st_dev;
  out->inode = st.st_ino;
  return true;
}

// One level of the traversal: a directory's entries, already sorted, plus how
// far through them we are.
struct Frame {
  std::vector<fs::directory_entry> entries;
  std::size_t next = 0;
  std::size_t depth = 0;
};

// Reads one directory into a sorted vector. Returns false on failure.
bool read_sorted(const fs::path &dir, bool follow_symlinks,
                 std::vector<fs::directory_entry> *out, std::error_code *ec) {
  auto dir_options = fs::directory_options::skip_permission_denied;
  if (follow_symlinks) {
    dir_options |= fs::directory_options::follow_directory_symlink;
  }

  fs::directory_iterator it(dir, dir_options, *ec);
  if (*ec) {
    return false;
  }

  const fs::directory_iterator end;
  while (it != end) {
    out->push_back(*it);
    it.increment(*ec);
    if (*ec) {
      return false;
    }
  }

  std::sort(out->begin(), out->end(),
            [](const fs::directory_entry &a, const fs::directory_entry &b) {
              return a.path().filename() < b.path().filename();
            });
  return true;
}

} // namespace

bool is_hidden(const fs::path &p) {
  const std::string name = p.filename().string();
  return name.size() > 1 && name[0] == '.' && name != "..";
}

WalkStats walk(const fs::path &root, const WalkOptions &options,
               const FileCallback &on_file, const ErrorCallback &on_error) {
  WalkStats stats;
  std::error_code ec;

  const fs::file_status root_status = fs::status(root, ec);
  if (ec) {
    on_error(root, ec);
    ++stats.errors;
    return stats;
  }
  if (fs::is_regular_file(root_status)) {
    on_file(fs::directory_entry(root));
    ++stats.files;
    return stats;
  }
  if (!fs::is_directory(root_status)) {
    // /dev/null, a socket, a FIFO: exists, but there is nothing to search.
    on_error(root, std::make_error_code(std::errc::invalid_argument));
    ++stats.errors;
    return stats;
  }

  std::unordered_set<DirId, DirIdHash> visited;
  if (options.follow_symlinks) {
    DirId id;
    if (stat_dir_id(root, &id)) {
      visited.insert(id);
    }
  }

  std::vector<Frame> stack;
  {
    Frame first;
    first.depth = 0;
    if (!read_sorted(root, options.follow_symlinks, &first.entries, &ec)) {
      on_error(root, ec);
      ++stats.errors;
      return stats;
    }
    stack.push_back(std::move(first));
  }

  while (!stack.empty()) {
    Frame &frame = stack.back();
    if (frame.next >= frame.entries.size()) {
      stack.pop_back();
      continue;
    }

    const fs::directory_entry entry = frame.entries[frame.next++];

    if (!options.include_hidden && is_hidden(entry.path())) {
      ++stats.skipped_hidden;
      continue;
    }

    const bool is_dir = entry.is_directory(ec);
    if (ec) {
      on_error(entry.path(), ec);
      ++stats.errors;
      ec.clear();
      continue;
    }

    if (is_dir) {
      // is_directory() follows symlinks, so a link to a directory looks
      // like a directory here. recursive_directory_iterator refused to
      // descend without follow_directory_symlink; we must do it ourselves.
      if (!options.follow_symlinks && entry.is_symlink(ec)) {
        ++stats.skipped_special;
        ec.clear();
        continue;
      }
      ec.clear();

      const std::size_t child_depth = frame.depth + 1;
      if (options.max_depth > 0 && child_depth >= options.max_depth) {
        continue;
      }
      if (options.follow_symlinks) {
        DirId id;
        if (stat_dir_id(entry.path(), &id) && !visited.insert(id).second) {
          ++stats.skipped_loops; // already been here
          continue;
        }
      }

      Frame child;
      child.depth = child_depth;
      if (!read_sorted(entry.path(), options.follow_symlinks, &child.entries,
                       &ec)) {
        on_error(entry.path(), ec);
        ++stats.errors;
        ec.clear();
        continue;
      }
      ++stats.dirs;
      stack.push_back(std::move(child));
      continue;
    }

    if (entry.is_regular_file(ec) && !ec) {
      on_file(entry);
      ++stats.files;
    } else {
      ++stats.skipped_special;
      ec.clear();
    }
  }

  return stats;
}

} // namespace cgrep