// src/wal/record.cpp

#include "../../include/wal/record.h"
#include <cstddef>
#include <cstring>

namespace tiny_lsm {

Record Record::createRecord(uint64_t tranc_id) {
  Record record;
  record.operation_type_ = OperationType::OP_CREATE;
  record.tranc_id_ = tranc_id;
  record.record_len_ = sizeof(uint16_t) + sizeof(uint64_t) + sizeof(uint8_t);
  return record;
}
Record Record::commitRecord(uint64_t tranc_id) {
  Record record;
  record.operation_type_ = OperationType::OP_COMMIT;
  record.tranc_id_ = tranc_id;
  record.record_len_ = sizeof(uint16_t) + sizeof(uint64_t) + sizeof(uint8_t);
  return record;
}
Record Record::rollbackRecord(uint64_t tranc_id) {
  Record record;
  record.operation_type_ = OperationType::OP_ROLLBACK;
  record.tranc_id_ = tranc_id;
  record.record_len_ = sizeof(uint16_t) + sizeof(uint64_t) + sizeof(uint8_t);
  return record;
}
Record Record::putRecord(uint64_t tranc_id, const std::string &key,
                         const std::string &value) {
  Record record;
  record.operation_type_ = OperationType::OP_PUT;
  record.tranc_id_ = tranc_id;
  record.key_ = key;
  record.value_ = value;
  record.record_len_ = sizeof(uint16_t) + sizeof(uint64_t) + sizeof(uint8_t) +
                       sizeof(uint16_t) + key.size() + sizeof(uint16_t) +
                       value.size();
  return record;
}
Record Record::deleteRecord(uint64_t tranc_id, const std::string &key) {
  Record record;
  record.operation_type_ = OperationType::OP_DELETE;
  record.tranc_id_ = tranc_id;
  record.key_ = key;
  record.record_len_ = sizeof(uint16_t) + sizeof(uint64_t) + sizeof(uint8_t) +
                       sizeof(uint16_t) + key.size();
  return record;
}

std::vector<uint8_t> Record::encode() const {
  std::vector<uint8_t> record;

  size_t key_offset = sizeof(uint16_t) + sizeof(uint64_t) + sizeof(uint8_t);
  // 记录长度本身(16) + 事务id(64) +
  // 操作类型(8), 固有的编码部分

  record.resize(record_len_, 0);

  // 编码 record_len
  std::memcpy(record.data(), &record_len_, sizeof(uint16_t));

  // 编码 tranc_id
  std::memcpy(record.data() + sizeof(uint16_t), &tranc_id_, sizeof(uint64_t));

  // 编码 operation_type
  auto type_byte = static_cast<uint8_t>(operation_type_);
  std::memcpy(record.data() + sizeof(uint16_t) + sizeof(uint64_t), &type_byte,
              sizeof(uint8_t));

  if (this->operation_type_ == OperationType::OP_PUT) {
    uint16_t key_len = key_.size();
    std::memcpy(record.data() + key_offset, &key_len, sizeof(uint16_t));
    std::memcpy(record.data() + key_offset + sizeof(uint16_t), key_.data(),
                key_.size());

    size_t value_offset = key_offset + sizeof(uint16_t) + key_.size();

    uint16_t value_len = value_.size();
    std::memcpy(record.data() + value_offset, &value_len, sizeof(uint16_t));
    std::memcpy(record.data() + value_offset + sizeof(uint16_t), value_.data(),
                value_.size());
  } else if (this->operation_type_ == OperationType::OP_DELETE) {
    uint16_t key_len = key_.size();
    std::memcpy(record.data() + key_offset, &key_len, sizeof(uint16_t));
    std::memcpy(record.data() + key_offset + sizeof(uint16_t), key_.data(),
                key_.size());
  }

  return record;
}

std::vector<Record> Record::decode(const std::vector<uint8_t> &data) {
  if (data.size() < sizeof(uint16_t) + sizeof(uint64_t) + sizeof(uint8_t)) {
    return {};
  }

  std::vector<Record> records;
  size_t pos = 0;

  while (pos < data.size()) {
    // 读取 record_len
    uint16_t record_len;
    std::memcpy(&record_len, data.data() + pos, sizeof(uint16_t));
    pos += sizeof(uint16_t);

    // 检查数据长度是否足够
    if (data.size() < record_len) {
      throw std::runtime_error("Data length does not match record length");
    }

    // 读取 tranc_id
    uint64_t tranc_id;
    std::memcpy(&tranc_id, data.data() + pos, sizeof(uint64_t));
    pos += sizeof(uint64_t);

    // 读取 operation_type
    uint8_t op_type = data[pos++];
    OperationType operation_type = static_cast<OperationType>(op_type);

    Record record;
    record.tranc_id_ = tranc_id;
    record.operation_type_ = operation_type;
    record.record_len_ = record_len;

    if (operation_type == OperationType::OP_PUT) {
      // 读取 key_len
      uint16_t key_len;
      std::memcpy(&key_len, data.data() + pos, sizeof(uint16_t));
      pos += sizeof(uint16_t);

      // 读取 key
      record.key_ = std::string(
          reinterpret_cast<const char *>(data.data() + pos), key_len);
      pos += key_len;

      // 读取 value_len
      uint16_t value_len;
      std::memcpy(&value_len, data.data() + pos, sizeof(uint16_t));
      pos += sizeof(uint16_t);

      // 读取 value
      record.value_ = std::string(
          reinterpret_cast<const char *>(data.data() + pos), value_len);
      pos += value_len;
    } else if (operation_type == OperationType::OP_DELETE) {
      // 读取 key_len
      uint16_t key_len;
      std::memcpy(&key_len, data.data() + pos, sizeof(uint16_t));
      pos += sizeof(uint16_t);

      // 读取 key
      record.key_ = std::string(
          reinterpret_cast<const char *>(data.data() + pos), key_len);
      pos += key_len;
    }

    records.push_back(record);
  }
  return records;
}
void Record::print() const {
  std::cout << "Record: tranc_id=" << tranc_id_
            << ", operation_type=" << static_cast<int>(operation_type_)
            << ", key=" << key_ << ", value=" << value_ << std::endl;
}

bool Record::operator==(const Record &other) const {
  if (tranc_id_ != other.tranc_id_ ||
      operation_type_ != other.operation_type_) {
    return false;
  }

  // 不需要 key 和 value 比较的情况
  if (operation_type_ == OperationType::OP_CREATE ||
      operation_type_ == OperationType::OP_COMMIT ||
      operation_type_ == OperationType::OP_ROLLBACK) {
    return true;
  }

  // 需要 key 比较的情况
  if (operation_type_ == OperationType::OP_DELETE) {
    return key_ == other.key_;
  }

  // 需要 key 和 value 比较的情况
  return key_ == other.key_ && value_ == other.value_;
}

bool Record::operator!=(const Record &other) const { return !(*this == other); }
} // namespace tiny_lsm