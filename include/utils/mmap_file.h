#pragma once

#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace tiny_lsm {

class MmapFile {
private:
  int fd_;               // 文件描述符
  void *mapped_data_;    // 映射的内存地址
  size_t file_size_;     // 文件大小
  std::string filename_; // 文件名

  // 获取映射的内存指针
  void *data() const { return mapped_data_; }

  // 创建文件并映射到内存
  bool create_and_map(const std::string &path, size_t size);

public:
  MmapFile() : fd_(-1), mapped_data_(nullptr), file_size_(0) {}
  ~MmapFile() { close(); }

  // 打开文件并映射到内存
  bool open(const std::string &filename, bool create = false);

  // 创建文件
  bool create(const std::string &filename, const std::vector<uint8_t> &buf);

  // 关闭文件
  void close();

  // 获取文件大小
  size_t size() const { return file_size_; }

  // 写入数据
  bool write(size_t offset, const void *data, size_t size);

  // 读取数据
  std::vector<uint8_t> read(size_t offset, size_t length);

  // 同步到磁盘
  bool sync();

  bool truncate(size_t size);

private:
  // 禁止拷贝
  MmapFile(const MmapFile &) = delete;
  MmapFile &operator=(const MmapFile &) = delete;
};
} // namespace tiny_lsm