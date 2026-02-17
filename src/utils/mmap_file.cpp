#ifndef _WIN32
#include "utils/mmap_file.h"
#include <cstdint>
#include <errno.h>
#include <stdexcept>
#include <string.h>
#include <sys/stat.h>
#include <vector>

namespace tiny_lsm {

bool MmapFile::open(const std::string &filename, bool create) {
  filename_ = filename;

  // 打开或创建文件
  int flags = O_RDWR;
  if (create) {
    flags |= O_CREAT;
  }

  fd_ = ::open(filename.c_str(), flags, 0644);
  if (fd_ == -1) {
    return false;
  }

  // 获取文件大小
  struct stat st;
  if (fstat(fd_, &st) == -1) {
    close();
    return false;
  }
  file_size_ = st.st_size;

  // 映射文件
  if (file_size_ > 0) {
    mapped_data_ =
        mmap(nullptr, file_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (mapped_data_ == MAP_FAILED) {
      close();
      return false;
    }
  }

  return true;
}

bool MmapFile::create(const std::string &filename, const std::vector<uint8_t> &buf) {
  // 创建文件，设置大小并映射到内存
  if (!create_and_map(filename, buf.size())) {
    // throw std::runtime_error("Failed to create or wrte file: " + filename);
    return false;
  }

  // 写入数据
  memcpy(this->data(), buf.data(), buf.size());
  this->sync();
  return true;
}
void MmapFile::close() {
  if (mapped_data_ != nullptr && mapped_data_ != MAP_FAILED) {
    munmap(mapped_data_, file_size_);
    mapped_data_ = nullptr;
  }

  if (fd_ != -1) {
    ::close(fd_);
    fd_ = -1;
  }

  file_size_ = 0;
}

bool MmapFile::write(size_t offset, const void *data, size_t size) {
  // 调整文件大小以包含 offset + size
  size_t new_size = offset + size;
  if (ftruncate(fd_, new_size) == -1) {
    return false;
  }

  // 如果已经映射，先解除映射
  if (mapped_data_ != nullptr && mapped_data_ != MAP_FAILED) {
    munmap(mapped_data_, file_size_);
  }

  // 重新映射
  file_size_ = new_size;
  mapped_data_ =
      mmap(nullptr, new_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
  if (mapped_data_ == MAP_FAILED) {
    mapped_data_ = nullptr;
    return false;
  }

  // 写入数据
  memcpy(static_cast<uint8_t *>(mapped_data_) + offset, data, size);
  this->sync();
  return true;
}

std::vector<uint8_t> MmapFile::read(size_t offset, size_t length) {
  // 从映射的内存中复制数据
  // 创建结果vector
  // 创建结果vector
  std::vector<uint8_t> result(length);

  // 从映射的内存中复制数据
  const uint8_t *data = static_cast<const uint8_t *>(this->data());
  memcpy(result.data(), data + offset, length);

  return result;
}

bool MmapFile::sync() {
  if (mapped_data_ != nullptr && mapped_data_ != MAP_FAILED) {
    return msync(mapped_data_, file_size_, MS_SYNC) == 0;
  }
  return true;
}

// ********************* private *********************

bool MmapFile::create_and_map(const std::string &path, size_t size) {
  // 创建并打开文件
  fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
  if (fd_ == -1) {
    return false;
  }

  // 先调整文件大小
  if (ftruncate(fd_, size) == -1) {
    close();
    return false;
  }

  // 映射与文件大小相同的空间
  mapped_data_ =
      mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
  if (mapped_data_ == MAP_FAILED) {
    close();
    return false;
  }

  file_size_ = size;
  return true;
}

bool MmapFile::truncate(size_t size) {
  // 解除原有映射
  if (mapped_data_ != nullptr && mapped_data_ != MAP_FAILED) {
    munmap(mapped_data_, file_size_);
    mapped_data_ = nullptr;
  }

  // 调整文件大小
  if (ftruncate(fd_, size) == -1) {
    return false;
  }

  // 重新映射
  if (size > 0) {
    mapped_data_ =
        mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (mapped_data_ == MAP_FAILED) {
      mapped_data_ = nullptr;
      return false;
    }
  } else {
    mapped_data_ = nullptr;
  }

  file_size_ = size;
  return true;
}

} // namespace tiny_lsm
#endif