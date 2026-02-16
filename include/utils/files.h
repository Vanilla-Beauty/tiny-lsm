#pragma once

#include "mmap_file.h"
#include "std_file.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tiny_lsm {

class Cursor;

class FileObj {
private:
  std::unique_ptr<StdFile> m_file;

public:
  FileObj();
  ~FileObj();

  // 移动构造函数和赋值运算符
  FileObj(FileObj &&other) noexcept;
  FileObj &operator=(FileObj &&other) noexcept;

  // 文件操作方法
  size_t size() const;
  void del_file();

// TODO: Windows下的文件截断实现目前有问题搁置
#ifndef _WIN32
  bool truncate(size_t offset);
#endif

  void close();

  // 文件创建和打开
  static FileObj create_and_write(const std::string &path,
                                  std::vector<uint8_t> buf);
  static FileObj open(const std::string &path, bool create = false);

  // 读取方法
  std::vector<uint8_t> read_to_slice(size_t offset, size_t length);
  uint8_t read_uint8(size_t offset);
  uint16_t read_uint16(size_t offset);
  uint32_t read_uint32(size_t offset);
  uint64_t read_uint64(size_t offset);

  // 写入方法
  bool write(size_t offset, std::vector<uint8_t> &buf);
  bool write_int(size_t offset, int value);
  bool write_uint8(size_t offset, uint8_t value);
  bool write_uint16(size_t offset, uint16_t value);
  bool write_uint32(size_t offset, uint32_t value);
  bool write_uint64(size_t offset, uint64_t value);

  // 追加写入方法
  bool append(std::vector<uint8_t> &buf);
  bool append_int(int value);
  bool append_uint8(uint8_t value);
  bool append_uint16(uint16_t value);
  bool append_uint32(uint32_t value);
  bool append_uint64(uint64_t value);

  // 同步文件
  bool sync();

  // 获取游标
  Cursor get_cursor(FileObj &file_obj);
};

} // namespace tiny_lsm
