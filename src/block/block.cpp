#include "../../include/block/block.h"
#include "../../include/block/block_iterator.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace tiny_lsm {
Block::Block(size_t capacity) : capacity(capacity) {}

std::vector<uint8_t> Block::encode() {
  // TODO Lab 3.1 编码单个类实例形成一段字节数组
  return {};
}

std::shared_ptr<Block> Block::decode(const std::vector<uint8_t> &encoded,
                                     bool with_hash) {
  // TODO Lab 3.1 解码字节数组形成类实例
  return nullptr;
}

std::string Block::get_first_key() {
  if (data.empty() || offsets.empty()) {
    return "";
  }

  // 读取第一个key的长度（前2字节）
  uint16_t key_len;
  memcpy(&key_len, data.data(), sizeof(uint16_t));

  // 读取key
  std::string key(reinterpret_cast<char *>(data.data() + sizeof(uint16_t)),
                  key_len);
  return key;
}

size_t Block::get_offset_at(size_t idx) const {
  if (idx > offsets.size()) {
    throw std::runtime_error("idx out of offsets range");
  }
  return offsets[idx];
}

bool Block::add_entry(const std::string &key, const std::string &value,
                      uint64_t tranc_id, bool force_write) {
  // TODO Lab 3.1 添加一个键值对到block中
  // ? 返回值说明：
  // ? true: 成功添加
  // ? false: block已满, 拒绝此次添加
  return false;
}

// 从指定偏移量获取entry的key
std::string Block::get_key_at(size_t offset) const {
  // TODO Lab 3.1 从指定偏移量获取entry的key
  return "";
}

// 从指定偏移量获取entry的value
std::string Block::get_value_at(size_t offset) const {
  // TODO Lab 3.1 从指定偏移量获取entry的value
  return "";
}

uint64_t Block::get_tranc_id_at(size_t offset) const {
  // TODO Lab 3.1 从指定偏移量获取entry的tranc_id
  // ? 你不需要理解tranc_id的具体含义, 直接返回即可
  return 0;
}

// 比较指定偏移量处的key与目标key
int Block::compare_key_at(size_t offset, const std::string &target) const {
  std::string key = get_key_at(offset);
  return key.compare(target);
}

// 相同的key连续分布, 且相同的key的事务id从大到小排布
// 这里的逻辑是找到最接近 tranc_id 的键值对的索引位置
int Block::adjust_idx_by_tranc_id(size_t idx, uint64_t tranc_id) {
  // TODO Lab3.1 不需要在Lab3.1中实现, 只是进行标记,
  // ? 后续实现事务后需要更新这里的实现
  return -1;
}

bool Block::is_same_key(size_t idx, const std::string &target_key) const {
  if (idx >= offsets.size()) {
    return false; // 索引超出范围
  }
  return get_key_at(offsets[idx]) == target_key;
}

// 使用二分查找获取value
// 要求在插入数据时有序插入
std::optional<std::string> Block::get_value_binary(const std::string &key,
                                                   uint64_t tranc_id) {
  auto idx = get_idx_binary(key, tranc_id);
  if (!idx.has_value()) {
    return std::nullopt;
  }

  return get_value_at(offsets[*idx]);
}

std::optional<size_t> Block::get_idx_binary(const std::string &key,
                                            uint64_t tranc_id) {
  // TODO Lab 3.1 使用二分查找获取key对应的索引
  return std::nullopt;
}

std::optional<
    std::pair<std::shared_ptr<BlockIterator>, std::shared_ptr<BlockIterator>>>
Block::iters_preffix(uint64_t tranc_id, const std::string &preffix) {
  // TODO Lab 3.3 获取前缀匹配的区间迭代器
  return std::nullopt;
}

// 返回第一个满足谓词的位置和最后一个满足谓词的位置
// 如果不存在, 范围nullptr
// 谓词作用于key, 且保证满足谓词的结果只在一段连续的区间内, 例如前缀匹配的谓词
// 返回的区间是闭区间, 开区间需要手动对返回值自增
// predicate返回值:
//   0: 满足谓词
//   >0: 不满足谓词, 需要向右移动
//   <0: 不满足谓词, 需要向左移动
std::optional<
    std::pair<std::shared_ptr<BlockIterator>, std::shared_ptr<BlockIterator>>>
Block::get_monotony_predicate_iters(
    uint64_t tranc_id, std::function<int(const std::string &)> predicate) {
  // TODO: Lab 3.3 使用二分查找获取满足谓词的区间迭代器
  return std::nullopt;
}

Block::Entry Block::get_entry_at(size_t offset) const {
  Entry entry;
  entry.key = get_key_at(offset);
  entry.value = get_value_at(offset);
  entry.tranc_id = get_tranc_id_at(offset);
  return entry;
}

size_t Block::size() const { return offsets.size(); }

size_t Block::cur_size() const {
  return data.size() + offsets.size() * sizeof(uint16_t) + sizeof(uint16_t);
}

bool Block::is_empty() const { return offsets.empty(); }

BlockIterator Block::begin(uint64_t tranc_id) {
  // TODO Lab 3.2 获取begin迭代器
  return BlockIterator(nullptr, 0, 0);
}

BlockIterator Block::end() {
  // TODO Lab 3.2 获取end迭代器
  return BlockIterator(nullptr, 0, 0);
}
} // namespace tiny_lsm