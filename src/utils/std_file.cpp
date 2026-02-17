#include "utils/std_file.h"
#include "spdlog/spdlog.h"
#include <fstream>
#include <iostream>

#ifdef _WIN32
#include <io.h>
#include <windows.h>

#else
#include <unistd.h>
#endif

namespace tiny_lsm {

bool StdFile::open(const std::string &filename, bool create) {
  filename_ = filename;

  if (create) {
    file_.open(filename, std::ios::in | std::ios::out | std::ios::binary |
                             std::ios::trunc);
  } else {
    file_.open(filename, std::ios::in | std::ios::out | std::ios::binary);
  }

  if (!file_.is_open()) {
    // 获取具体的错误信息
    int err = errno;
    std::string error_msg = fmt::format("Failed to open file '{}': {} ({})",
                                        filename, strerror(err), err);

    // 同时输出到日志文件和标准错误
    spdlog::error(error_msg);
    std::cerr << "[ERROR] " << error_msg << std::endl;

    return false;
  }

  return true;
}

bool StdFile::create(const std::string &filename, std::vector<uint8_t> &buf) {
  if (!this->open(filename, true)) {
    throw std::runtime_error("Failed to open file for writing");
  }
  if (!buf.empty()) {
    write(0, buf.data(), buf.size());
  }

  return true;
}

void StdFile::close() {
  if (file_.is_open()) {
    sync();
    file_.close();
  }
}

size_t StdFile::size() {
  file_.seekg(0, std::ios::end);
  return file_.tellg();
}

std::vector<uint8_t> StdFile::read(size_t offset, size_t length) {
  std::vector<uint8_t> buf(length);
  file_.seekg(offset, std::ios::beg);
  if (!file_.read(reinterpret_cast<char *>(buf.data()), length)) {
    throw std::runtime_error("Failed to read from file");
  }
  return buf;
}

bool StdFile::write(size_t offset, const void *data, size_t size) {
  file_.seekg(offset, std::ios::beg);
  file_.write(static_cast<const char *>(data), size);
  // this->sync();
  return true;
}

bool StdFile::sync() {
  if (!file_.is_open()) {
    return false;
  }
  file_.flush();
  return file_.good();
}

bool StdFile::remove() {
  // 修复类型转换问题
  return std::remove((const char *)filename_.c_str()) == 0;
}

// TODO: Windows下的文件截断实现目前有问题搁置
#ifndef _WIN32
bool StdFile::truncate(size_t size) {
  if (file_.is_open())
    file_.close();
  int ret = ::truncate(filename_.c_str(), size);
  file_.open(filename_, std::ios::in | std::ios::out | std::ios::binary);
  return ret == 0 && file_.is_open();
}
#endif
} // namespace tiny_lsm