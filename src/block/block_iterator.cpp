#include "block/block_iterator.h"
#include "block/block.h"
#include <cstdint>
#include <memory>
#include <stdexcept>

class Block;

namespace tiny_lsm {
BlockIterator::BlockIterator(std::shared_ptr<Block> b, size_t index,
                             uint64_t tranc_id)
    : block(b), current_index(index), tranc_id_(tranc_id),
      cached_value(std::nullopt) {
  skip_by_tranc_id();
}

BlockIterator::BlockIterator(std::shared_ptr<Block> b, const std::string &key,
                             uint64_t tranc_id)
    : block(b), tranc_id_(tranc_id), cached_value(std::nullopt) {
  auto key_idx_ops = block->get_idx_binary(key, tranc_id);
  if (key_idx_ops.has_value()) {
    current_index = key_idx_ops.value();
  } else {
    current_index = block->offsets.size();
  }
}

// BlockIterator::BlockIterator(std::shared_ptr<Block> b, uint64_t tranc_id)
//     : block(b), current_index(0), tranc_id_(tranc_id),
//       cached_value(std::nullopt) {
//   skip_by_tranc_id();
// }

BlockIterator::pointer BlockIterator::operator->() const {
  update_current();
  return &(*cached_value);
}

BlockIterator &BlockIterator::operator++() {
  if (block && current_index < block->size()) {
    auto prev_idx = current_index;
    auto prev_offset = block->get_offset_at(prev_idx);
    auto prev_entry = block->get_entry_at(prev_offset);

    ++current_index;

    // 跳过相同的key
    while (block && current_index < block->size()) {
      auto cur_offset = block->get_offset_at(current_index);
      auto cur_entry = block->get_entry_at(cur_offset);
      if (cur_entry.key != prev_entry.key) {
        break;
      }
      // 可能会连续出现多个key, 但由不同事务创建, 同样的key直接跳过
      ++current_index;
    }

    // 出现不同的key时, 还需要跳过不可见事务的键值对
    skip_by_tranc_id();
  }
  return *this;
}

bool BlockIterator::operator==(const BlockIterator &other) const {
  if (block == nullptr && other.block == nullptr) {
    return true;
  }
  if (block == nullptr || other.block == nullptr) {
    return false;
  }
  auto cmp = block == other.block && current_index == other.current_index;
  return cmp;
}

bool BlockIterator::operator!=(const BlockIterator &other) const {
  return !(*this == other);
}

BlockIterator::value_type BlockIterator::operator*() const {
  if (!block || current_index >= block->size()) {
    throw std::out_of_range("Iterator out of range");
  }

  // 使用缓存避免重复解析
  if (!cached_value.has_value()) {
    size_t offset = block->get_offset_at(current_index);
    cached_value =
        std::make_pair(block->get_key_at(offset), block->get_value_at(offset));
  }
  return *cached_value;
}

bool BlockIterator::is_end() { return current_index == block->offsets.size(); }

void BlockIterator::update_current() const {
  if (!cached_value && current_index < block->offsets.size()) {
    size_t offset = block->get_offset_at(current_index);
    cached_value =
        std::make_pair(block->get_key_at(offset), block->get_value_at(offset));
  }
}

void BlockIterator::skip_by_tranc_id() {
  if (tranc_id_ == 0) {
    // 没有开启事务功能
    cached_value = std::nullopt;
    return;
  }

  while (current_index < block->offsets.size()) {
    size_t offset = block->get_offset_at(current_index);
    auto tranc_id = block->get_tranc_id_at(offset);
    if (tranc_id <= tranc_id_) {
      // 位置合法
      break;
    }
    // 否则跳过不可见事务的键值对
    ++current_index;
  }
  cached_value = std::nullopt;
}
} // namespace tiny_lsm