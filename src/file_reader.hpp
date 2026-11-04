#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace cgrep {

// A read-only memory mapping of a file. Owns the mapping; unmaps on
// destruction. Move-only: two objects must never unmap the same address.
class MappedFile {
public:
  // Returns nullopt and sets 'ec' on failure. An empty file maps to an
  // empty view rather than an error.
  static std::optional<MappedFile> open(const std::filesystem::path &path,
                                        std::error_code &ec);

  ~MappedFile();

  MappedFile(MappedFile &&other) noexcept;
  MappedFile &operator=(MappedFile &&other) noexcept;

  MappedFile(const MappedFile &) = delete;
  MappedFile &operator=(const MappedFile &) = delete;

  // Valid only while this object is alive
  std::string_view contents() const { return std::string_view(data_, size_); }
  std::size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

private:
  MappedFile(const char *data, std::size_t size) : data_(data), size_(size) {}

  const char *data_ = nullptr;
  std::size_t size_ = 0;
};

// Fallback for anything mmap cannot handle (pipes, /proc, sysfs).
std::optional<std::string>
read_file_to_string(const std::filesystem::path &path, std::error_code &ec);

// A NUL byte in the first 8 KiB means "do not print this to a terminal".
bool looks_binary(std::string_view contents);

// Files at least this large are mapped; smaller ones are read into a buffer.
// Measured crossover on a warm page cache -- see the lessons's benchmark.
inline constexpr std::size_t kMmapThreshold = 256 * 1024;

// Read files, reusing one buffer across calls and mapping large ones.
// Cheap to construct; give each thread its own.
class FileReader {
public:
  // Returns a view of the file's bytes, valid until the NEXT call on this
  // reader or until the reader is destroyed. Never copy the view's data
  // without copying the bytes.
  std::optional<std::string_view> read(const std::filesystem::path &path,
                                       std::error_code &ec);

  // Which strategy the last successful read() used -- for --stats.
  bool last_was_mapped() const { return last_was_mapped_; }

private:
  std::string buffer_;                // reused for small files
  std::optional<MappedFile> mapping_; // keeps a large file's mapping alive
  bool last_was_mapped_ = false;
};

// Peels one line off the front of `rest`, without copying. The line excludes
// its terminator; a trailing "\r" is dropped. Returns false when exhausted.
bool next_line(std::string_view &rest, std::string_view &line);

} // namespace cgrep