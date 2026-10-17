/**
 * @file raw_tick_file.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Raw tick journal: header + append-only RawTickRecord array.
 */
#include "recorder/raw_tick_file.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <system_error>

namespace cts::recorder {
namespace {

bool ValidateHeader(RawTickFileHeader const& header) {
  return std::memcmp(header.magic, kRawTickFileMagic, sizeof(header.magic)) ==
             0 &&
         header.version == kRawTickFileVersion &&
         header.record_size == sizeof(RawTickRecord);
}

// Full write(2): retries EINTR and short writes
bool WriteAll(int fd, void const* data, std::size_t size) {
  auto const* p = static_cast<char const*>(data);
  while (size > 0) {
    ssize_t const n = ::write(fd, p, size);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    p += n;
    size -= static_cast<std::size_t>(n);
  }
  return true;
}

// Exactly `size` bytes; false on error or EOF before that (torn record)
bool ReadFull(int fd, void* data, std::size_t size) {
  auto* p = static_cast<char*>(data);
  while (size > 0) {
    ssize_t const n = ::read(fd, p, size);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (n == 0) {
      return false;
    }
    p += n;
    size -= static_cast<std::size_t>(n);
  }
  return true;
}

bool ReadHeaderAt(int fd, RawTickFileHeader& header) {
  auto* p = reinterpret_cast<char*>(&header);
  std::size_t left = sizeof(header);
  while (left > 0) {
    ssize_t const n = ::pread(fd, p, left, sizeof(header) - left);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (n == 0) {
      return false;
    }
    p += n;
    left -= static_cast<std::size_t>(n);
  }
  return true;
}

}  // namespace

bool RawTickFileWriter::Open(std::filesystem::path const& path,
                             std::string_view source) {
  Close();
  written_ = 0;
  last_seq_ = 0;

  if (path.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      return false;
    }
  }
  // O_RDWR (not O_WRONLY): reopening an existing file pread()s its header
  int const fd =
      ::open(path.c_str(), O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
  if (fd < 0) {
    return false;
  }

  struct stat st;
  if (::fstat(fd, &st) != 0) {
    ::close(fd);
    return false;
  }

  if (st.st_size == 0) {
    RawTickFileHeader header{};
    std::memcpy(header.magic, kRawTickFileMagic, sizeof(header.magic));
    header.version = kRawTickFileVersion;
    header.record_size = sizeof(RawTickRecord);
    header.created_epoch_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    std::strncpy(header.source, std::string(source).c_str(),
                 sizeof(header.source) - 1);
    if (!WriteAll(fd, &header, sizeof(header))) {
      ::close(fd);
      return false;
    }
  } else {
    // Existing file: validate, repair a torn tail, resume the seq counter
    std::int64_t const body =
        st.st_size - static_cast<std::int64_t>(sizeof(RawTickFileHeader));
    if (body < 0) {
      ::close(fd);
      return false;
    }
    std::int64_t const records =
        body / static_cast<std::int64_t>(sizeof(RawTickRecord));
    std::int64_t const valid =
        sizeof(RawTickFileHeader) +
        records * static_cast<std::int64_t>(sizeof(RawTickRecord));
    if (valid != st.st_size && ::ftruncate(fd, valid) != 0) {
      ::close(fd);
      return false;
    }
    RawTickFileHeader header;
    if (!ReadHeaderAt(fd, header) || !ValidateHeader(header)) {
      ::close(fd);
      return false;
    }
    if (records > 0) {
      // O(1) tail read: the seq counter resumes after the last record
      RawTickRecord last;
      auto* p = reinterpret_cast<char*>(&last);
      std::size_t left = sizeof(last);
      off_t const at = valid - static_cast<off_t>(sizeof(RawTickRecord));
      while (left > 0) {
        ssize_t const n = ::pread(fd, p, left, at + (sizeof(last) - left));
        if (n <= 0) {
          break;
        }
        p += n;
        left -= static_cast<std::size_t>(n);
      }
      if (left == 0) {
        last_seq_ = last.seq;
      }
    }
  }

  fd_ = fd;
  return true;
}

bool RawTickFileWriter::Write(RawTickRecord const& record) {
  if (fd_ < 0) {
    return false;
  }
  if (!WriteAll(fd_, &record, sizeof(record))) {
    return false;
  }
  ++written_;
  return true;
}

void RawTickFileWriter::Close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

bool RawTickFileReader::Open(std::filesystem::path const& path) {
  Close();
  read_count_ = 0;
  std::memset(source_, 0, sizeof(source_));

  int const fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return false;
  }
  // Sequential read (not pread): the file position must advance past the
  // header so Next() starts at the first record
  RawTickFileHeader header;
  if (!ReadFull(fd, &header, sizeof(header)) || !ValidateHeader(header)) {
    ::close(fd);
    return false;
  }
  std::memcpy(source_, header.source, sizeof(source_));
  fd_ = fd;
  return true;
}

bool RawTickFileReader::Next(RawTickRecord& out) {
  if (fd_ < 0) {
    return false;
  }
  // A short record is the torn tail of a crash: report it as clean EOF
  if (!ReadFull(fd_, &out, sizeof(out))) {
    return false;
  }
  ++read_count_;
  return true;
}

void RawTickFileReader::Close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

}  // namespace cts::recorder
