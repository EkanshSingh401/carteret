// carteret/mapped_file.hpp -- read-only mmap of a session file.
#pragma once

#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace carteret {

class MappedFile {
public:
    explicit MappedFile(const std::string& path) {
        fd_ = ::open(path.c_str(), O_RDONLY);
        if (fd_ < 0) throw std::runtime_error("open failed: " + path);
        struct stat st{};
        if (::fstat(fd_, &st) != 0) { ::close(fd_); throw std::runtime_error("fstat failed"); }
        size_ = static_cast<std::size_t>(st.st_size);
        if (size_ == 0) { ::close(fd_); throw std::runtime_error("empty file: " + path); }
        void* p = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
        if (p == MAP_FAILED) { ::close(fd_); throw std::runtime_error("mmap failed"); }
        data_ = static_cast<const unsigned char*>(p);
        // Sequential replay. Measure whether this actually helps on your box --
        // on a warm page cache it often does nothing, and saying so is better
        // than cargo-culting the call.
        ::madvise(const_cast<void*>(p), size_, MADV_SEQUENTIAL);
    }

    ~MappedFile() {
        if (data_) ::munmap(const_cast<unsigned char*>(data_), size_);
        if (fd_ >= 0) ::close(fd_);
    }

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    [[nodiscard]] std::span<const unsigned char> bytes() const noexcept { return {data_, size_}; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

private:
    int fd_ = -1;
    const unsigned char* data_ = nullptr;
    std::size_t size_ = 0;
};

}  // namespace carteret
