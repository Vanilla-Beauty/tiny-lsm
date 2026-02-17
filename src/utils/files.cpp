#include "utils/files.h"
#include "utils/cursor.h"
#include <cstring>
#include <stdexcept>

namespace tiny_lsm {
FileObj::FileObj() : m_file(std::make_unique<StdFile>()) {}

FileObj::~FileObj() = default;

// 实现移动语义
FileObj::FileObj(FileObj &&other) noexcept : m_file(std::move(other.m_file)) {}

FileObj &FileObj::operator=(FileObj &&other) noexcept {
  if (this != &other) {
    m_file = std::move(other.m_file);
  }
  return *this;
}

size_t FileObj::size() const { return m_file->size(); }

void FileObj::del_file() { m_file->remove(); }

#ifndef _WIN32
bool FileObj::truncate(size_t offset) {
  if (offset > m_file->size()) {
    throw std::out_of_range("Truncate offset beyond file size");
  }
  bool ok = m_file->truncate(offset);

  return ok;
}
#endif

FileObj FileObj::create_and_write(const std::string &path,
                                  std::vector<uint8_t> buf) {
  FileObj file_obj;
  if (!file_obj.m_file->create(path, buf)) {
    throw std::runtime_error("Failed to create or write file: " + path);
  }

  // 同步到磁盘
  file_obj.m_file->sync();

  return std::move(file_obj);
}

FileObj FileObj::open(const std::string &path, bool create) {
  FileObj file_obj;

  // 打开文件
  if (!file_obj.m_file->open(path, create)) {
    throw std::runtime_error("Failed to open file: " + path);
  }

  return std::move(file_obj);
}

std::vector<uint8_t> FileObj::read_to_slice(size_t offset, size_t length) {
  // 检查边界
  if (offset + length > m_file->size()) {
    throw std::out_of_range("Read beyond file size");
  }

  // 从w文件复制数据
  auto result = m_file->read(offset, length);

  return result;
}

uint8_t FileObj::read_uint8(size_t offset) {
  // 检查边界
  if (offset + sizeof(uint8_t) > m_file->size()) {
    throw std::out_of_range("Read beyond file size");
  }

  // 从w文件复制数据
  auto result = m_file->read(offset, sizeof(uint8_t));

  // 将数据转换为uint8_t
  return result[0];
}

uint16_t FileObj::read_uint16(size_t offset) {
  // 检查边界
  if (offset + sizeof(uint16_t) > m_file->size()) {
    throw std::out_of_range("Read beyond file size");
  }
  auto result = m_file->read(offset, sizeof(uint16_t));
  return *(uint16_t *)result.data();
}

uint32_t FileObj::read_uint32(size_t offset) {
  // 检查边界
  if (offset + sizeof(uint32_t) > m_file->size()) {
    throw std::out_of_range("Read beyond file size");
  }

  // 从w文件复制数据
  auto result = m_file->read(offset, sizeof(uint32_t));

  // 将数据转换为uint32_t
  return *(uint32_t *)result.data();
}

uint64_t FileObj::read_uint64(size_t offset) {
  // 检查边界
  if (offset + sizeof(uint64_t) > m_file->size()) {
    throw std::out_of_range("Read beyond file size");
  }

  // 从w文件复制数据
  auto result = m_file->read(offset, sizeof(uint64_t));

  // 将数据转换为uint64_t
  return *(uint64_t *)result.data();
}

// 写入到文件
bool FileObj::write(size_t offset, std::vector<uint8_t> &buf) {
  return m_file->write(offset, buf.data(), buf.size());
}

// 追加到文件
bool FileObj::append(std::vector<uint8_t> &buf) {
  // 获取文件大小
  size_t file_size = m_file->size();

  // 写入数据
  if (!m_file->write(file_size, buf.data(), buf.size())) {
    return false;
  }

  return true;
}

bool FileObj::write_int(size_t offset, int value) {
  return m_file->write(offset, reinterpret_cast<const uint8_t *>(&value),
                       sizeof(int));
}

bool FileObj::write_uint8(size_t offset, uint8_t value) {
  return m_file->write(offset, &value, sizeof(uint8_t));
}

bool FileObj::write_uint16(size_t offset, uint16_t value) {
  return m_file->write(offset, reinterpret_cast<const uint8_t *>(&value),
                       sizeof(uint16_t));
}

bool FileObj::write_uint32(size_t offset, uint32_t value) {
  return m_file->write(offset, reinterpret_cast<const uint8_t *>(&value),
                       sizeof(uint32_t));
}

bool FileObj::write_uint64(size_t offset, uint64_t value) {
  return m_file->write(offset, reinterpret_cast<const uint8_t *>(&value),
                       sizeof(uint64_t));
}

bool FileObj::append_int(int value) {
  size_t offset = m_file->size();
  return m_file->write(offset, reinterpret_cast<const uint8_t *>(&value),
                       sizeof(int));
}

bool FileObj::append_uint8(uint8_t value) {
  size_t offset = m_file->size();
  return m_file->write(offset, &value, sizeof(uint8_t));
}

bool FileObj::append_uint16(uint16_t value) {
  size_t offset = m_file->size();
  return m_file->write(offset, reinterpret_cast<const uint8_t *>(&value),
                       sizeof(uint16_t));
}

bool FileObj::append_uint32(uint32_t value) {
  size_t offset = m_file->size();
  return m_file->write(offset, reinterpret_cast<const uint8_t *>(&value),
                       sizeof(uint32_t));
}

bool FileObj::append_uint64(uint64_t value) {
  size_t offset = m_file->size();
  return m_file->write(offset, reinterpret_cast<const uint8_t *>(&value),
                       sizeof(uint64_t));
}

bool FileObj::sync() { return m_file->sync(); }

Cursor FileObj::get_cursor(FileObj &file_obj) {
  return Cursor(&file_obj, 0);
}

// 添加 close 函数
void FileObj::close() {
  m_file->close();
}

} // namespace tiny_lsm