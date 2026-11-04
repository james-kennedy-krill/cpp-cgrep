#include "file_reader.hpp"

#include <fcntl.h>
#include <iterator>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <fstream>
#include <utility>

namespace fs = std::filesystem;

namespace cgrep {
namespace {

// Closes a file descriptor on scope exit. RAII for a C API.
class FdGuard {
public:
  explicit FdGuard(int fd) : fd_(fd) {}
  ~FdGuard() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }
  FdGuard(const FdGuard &) = delete;
  FdGuard &operator=(const FdGuard &) = delete;
  int get() const { return fd_; }

private:
  int fd_;
};

std::error_code last_errno() {
  return std::error_code(errno, std::generic_category());
}

} // namespace

std::optional<MappedFile> MappedFile::open(const fs::path &path,
                                           std::error_code &ec) {
  ec.clear();

  const FdGuard fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
  if (fd.get() < 0) {
    ec = last_errno();
    return std::nullopt;
  }

  struct ::stat st{};
  if (::fstat(fd.get(), &st) != 0) {
    ec = last_errno();
    return std::nullopt;
  }
  if (!S_ISREG(st.st_mode)) {
    ec = std::make_error_code(std::errc::invalid_argument);
    return std::nullopt;
  }
  if (st.st_size == 0) {
    return MappedFile(nullptr, 0); // mmap() rejects length 0
  }

  const auto length = static_cast<std::size_t>(st.st_size);
  void *addr = ::mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd.get(), 0);
  if (addr == MAP_FAILED) {
    ec = last_errno();
    return std::nullopt;
  }

  // Tell the kernel we will read front to back, so it reads ahead
  ::madvise(addr, length, MADV_SEQUENTIAL);

  return MappedFile(static_cast<const char *>(addr), length);
}

MappedFile::~MappedFile() {
  if (data_ != nullptr) {
    ::munmap(const_cast<char *>(data_), size_);
  }
}

MappedFile::MappedFile(MappedFile &&other) noexcept
    : data_(std::exchange(other.data_, nullptr)),
      size_(std::exchange(other.size_, 0)) {}

MappedFile &MappedFile::operator=(MappedFile &&other) noexcept {
  if (this != &other) {
    if (data_ != nullptr) {
      ::munmap(const_cast<char *>(data_), size_);
    }
    data_ = std::exchange(other.data_, nullptr);
    size_ = std::exchange(other.size_, 0);
  }
  return *this;
}

std::optional<std::string> read_file_to_string(const fs::path &path,
                                               std::error_code &ec) {
  ec.clear();
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    ec = last_errno();
    return std::nullopt;
  }
  // NOT `std::string out(iter, iter);` -- that parses as a function
  // declaration (the "most vexing parse"). assign() sidesteps it.
  std::string out;
  out.assign(std::istreambuf_iterator<char>(in),
             std::istreambuf_iterator<char>());
  if (in.bad()) {
    ec = std::make_error_code(std::errc::io_error);
    return std::nullopt;
  }
  return out;
}

std::optional<std::string_view> FileReader::read(const fs::path &path,
                                                 std::error_code &ec) {
  ec.clear();
  mapping_.reset(); // release any previous mapping first
  last_was_mapped_ = false;

  const FdGuard fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
  if (fd.get() < 0) {
    ec = last_errno();
    return std::nullopt;
  }

  struct ::stat st{};
  if (::fstat(fd.get(), &st) != 0) {
    ec = last_errno();
    return std::nullopt;
  }
  if (!S_ISREG(st.st_mode)) {
    ec = std::make_error_code(std::errc::invalid_argument);
    return std::nullopt;
  }

  const auto size = static_cast<std::size_t>(st.st_size);

  if (size >= kMmapThreshold) {
    mapping_ = MappedFile::open(path, ec);
    if (mapping_) {
      last_was_mapped_ = true;
      return mapping_->contents();
    }
    ec.clear(); // mapping failed; fall through to read()
  }

  buffer_.resize(size);
  std::size_t total = 0;
  while (total < size) {
    const ssize_t got = ::read(fd.get(), buffer_.data() + total, size - total);
    if (got < 0) {
      if (errno == EINTR) {
        continue;
      }
      ec = last_errno();
      return std::nullopt;
    }
    if (got == 0) {
      break;
    } // file shrank under us
    total += static_cast<std::size_t>(got);
  }
  return std::string_view(buffer_.data(), total);
}

bool looks_binary(std::string_view contents) {
  constexpr std::size_t kProbe = 8192;
  const std::size_t n = std::min(contents.size(), kProbe);
  return contents.substr(0, n).find('\0') != std::string_view::npos;
}

bool next_line(std::string_view &rest, std::string_view &line) {
  if (rest.empty()) {
    return false;
  }

  const std::size_t nl = rest.find('\n');
  if (nl == std::string_view::npos) {
    line = rest;
    rest = std::string_view();
  } else {
    line = rest.substr(0, nl);
    rest = rest.substr(nl + 1);
  }
  if (!line.empty() && line.back() == '\r') {
    line.remove_suffix(1);
  }
  return true;
}

} // namespace cgrep