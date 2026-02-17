// ...existing code...
#include "utils/cursor.h"
#include "utils/files.h"

namespace tiny_lsm {

Cursor::Cursor(FileObj *file_obj, size_t offset)
    : m_file_obj(file_obj), m_offset(offset) {}

std::vector<uint8_t> Cursor::read(size_t length) {
  auto data = m_file_obj->read_to_slice(m_offset, length);
  m_offset += length;
  return data;
}

bool Cursor::write(const std::vector<uint8_t> &buf) {
  bool ok =
      m_file_obj->write(m_offset, const_cast<std::vector<uint8_t> &>(buf));
  if (ok)
    m_offset += buf.size();
  return ok;
}

uint8_t Cursor::read_uint8() {
  uint8_t val = m_file_obj->read_uint8(m_offset);
  m_offset += sizeof(uint8_t);
  return val;
}

uint16_t Cursor::read_uint16() {
  uint16_t val = m_file_obj->read_uint16(m_offset);
  m_offset += sizeof(uint16_t);
  return val;
}

uint32_t Cursor::read_uint32() {
  uint32_t val = m_file_obj->read_uint32(m_offset);
  m_offset += sizeof(uint32_t);
  return val;
}

uint64_t Cursor::read_uint64() {
  uint64_t val = m_file_obj->read_uint64(m_offset);
  m_offset += sizeof(uint64_t);
  return val;
}

bool Cursor::write_uint8(uint8_t value) {
  bool ok = m_file_obj->write_uint8(m_offset, value);
  if (ok)
    m_offset += sizeof(uint8_t);
  return ok;
}

bool Cursor::write_uint16(uint16_t value) {
  bool ok = m_file_obj->write_uint16(m_offset, value);
  if (ok)
    m_offset += sizeof(uint16_t);
  return ok;
}

bool Cursor::write_uint32(uint32_t value) {
  bool ok = m_file_obj->write_uint32(m_offset, value);
  if (ok)
    m_offset += sizeof(uint32_t);
  return ok;
}

bool Cursor::write_uint64(uint64_t value) {
  bool ok = m_file_obj->write_uint64(m_offset, value);
  if (ok)
    m_offset += sizeof(uint64_t);
  return ok;
}

size_t Cursor::offset() const { return m_offset; }

void Cursor::set_offset(size_t offset) { m_offset = offset; }

} // namespace tiny_lsm