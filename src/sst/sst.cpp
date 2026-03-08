#include "sst/sst.h"
#include "config/config.h"
#include "consts.h"
#include "sst/sst_iterator.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>

namespace tiny_lsm {

// Magic byte identifying a WiscKey SST footer
static constexpr uint8_t WISCKEY_MAGIC = 0x4B;
// Old footer size (24 bytes)
static constexpr size_t OLD_FOOTER_SIZE =
    sizeof(uint32_t) * 2 + sizeof(uint64_t) * 2;
// New WiscKey footer size (26 bytes)
static constexpr size_t WISCKEY_FOOTER_SIZE = OLD_FOOTER_SIZE + 2;

// **************************************************
// SST
// **************************************************

std::shared_ptr<SST> SST::open(size_t sst_id, FileObj file,
                               std::shared_ptr<BlockCache> block_cache,
                               std::shared_ptr<VLog> vlog) {
  // TODO: Lab 3.6 打开一个SST文件, 返回一个描述类
  // ? 步骤:
  // ?   0. 检测文件末尾 magic byte 判断是否为 WiscKey 格式 (WISCKEY_MAGIC = 0x4B)
  // ?      footer 共 24 字节 (老格式) 或 26 字节 (WiscKey, 末尾多 storage_mode + magic)
  // ?   1. 从文件末尾读取 footer: meta_block_offset, bloom_offset, min_tranc_id, max_tranc_id
  // ?      如为 WiscKey 格式, 还需读取 storage_mode_
  // ?   2. 读取并解码 Bloom Filter (bloom_offset ~ meta_block_offset 之间)
  // ?   3. 读取并解码元数据块 (meta_block_offset ~ bloom_offset 之间)
  // ?      调用 BlockMeta::decode_meta_from_slice
  // ?   4. 设置 first_key 和 last_key
  // ?   注: vlog 用于 WiscKey 模式下的 value 读取, 直接赋值给 sst->vlog_
  return nullptr;
}

void SST::del_sst() { file.del_file(); }

std::shared_ptr<Block> SST::read_block(int64_t block_idx) {
  // TODO: Lab 3.6 根据 block 的 id 读取一个 Block
  // ? 先从 block_cache 查找; 未命中则计算该 block 的偏移和大小
  // ? 读取数据后调用 Block::decode(data, true) 解码
  // ? 解码后存入 block_cache 并返回
  // ? block 大小: 相邻 meta_entries 的 offset 差值; 最后一个 block 到 meta_block_offset
  return nullptr;
}

int64_t SST::find_block_idx(const std::string &key) {
  // TODO: Lab 3.6 二分查找
  // ? 先用布隆过滤器快速排除 (bloom_filter->possibly_contains(key))
  // ? 再在 meta_entries 上二分查找: first_key <= key <= last_key
  // ? 若未找到合适 block 返回 -1
  return 0;
}

SstIterator SST::get(const std::string &key, uint64_t tranc_id) {
  // TODO: Lab 3.6 根据查询 key 返回一个迭代器
  // ? 先检查 key 是否在 [first_key, last_key] 范围内, 否则返回 end()
  // ? 再用 bloom_filter 快速排除
  // ? 返回 SstIterator(shared_from_this(), key, tranc_id)
  throw std::runtime_error("Not implemented");
}

size_t SST::num_blocks() const { return meta_entries.size(); }

std::string SST::get_first_key() const { return first_key; }

std::string SST::get_last_key() const { return last_key; }

size_t SST::sst_size() const { return file.size(); }

size_t SST::get_sst_id() const { return sst_id; }

std::string SST::resolve_value(const std::string &raw_value) const {
  // WiscKey 模式下: raw_value 是 12 字节的 vlog 引用 [offset:8][size:4]
  // 普通模式下直接返回 raw_value
  if (storage_mode_ == 0 || raw_value.empty()) {
    return raw_value;
  }
  if (raw_value.size() < 12) {
    return raw_value;
  }
  uint64_t off = 0;
  uint32_t sz = 0;
  memcpy(&off, raw_value.data(), sizeof(uint64_t));
  memcpy(&sz, raw_value.data() + sizeof(uint64_t), sizeof(uint32_t));
  if (!vlog_) {
    throw std::runtime_error("SST::resolve_value: vlog is null for WiscKey SST");
  }
  return vlog_->read_value(off, sz);
}

bool SST::is_wisckey() const { return storage_mode_ == 1; }

SstIterator SST::begin(uint64_t tranc_id, bool keep_all_versions) {
  // TODO: Lab 3.6 返回起始位置迭代器
  // ? 返回 SstIterator(shared_from_this(), tranc_id, keep_all_versions)
  throw std::runtime_error("Not implemented");
}

SstIterator SST::end() {
  // TODO: Lab 3.6 返回终止位置迭代器
  // ? 构造一个 SstIterator 并将 m_block_idx 设为 meta_entries.size(), m_block_it 设为 nullptr
  throw std::runtime_error("Not implemented");
}

std::pair<uint64_t, uint64_t> SST::get_tranc_id_range() const {
  return std::make_pair(min_tranc_id_, max_tranc_id_);
}

// **************************************************
// SSTBuilder
// **************************************************

SSTBuilder::SSTBuilder(size_t block_size, bool has_bloom) : block(block_size) {
  // 初始化第一个block
  if (has_bloom) {
    bloom_filter = std::make_shared<BloomFilter>(
        TomlConfig::getInstance().getBloomFilterExpectedSize(),
        TomlConfig::getInstance().getBloomFilterExpectedErrorRate());
  }
  meta_entries.clear();
  data.clear();
  first_key.clear();
  last_key.clear();
}

SSTBuilder::SSTBuilder(size_t block_size, bool has_bloom,
                       std::shared_ptr<VLog> vlog,
                       size_t wisckey_threshold)
    : block(block_size), vlog_(std::move(vlog)),
      wisckey_threshold_(wisckey_threshold), storage_mode_(1) {
  // WiscKey 模式构造函数: vlog 用于大 value 分离存储
  if (has_bloom) {
    bloom_filter = std::make_shared<BloomFilter>(
        TomlConfig::getInstance().getBloomFilterExpectedSize(),
        TomlConfig::getInstance().getBloomFilterExpectedErrorRate());
  }
  meta_entries.clear();
  data.clear();
  first_key.clear();
  last_key.clear();
}

void SSTBuilder::add(const std::string &key, const std::string &value,
                     uint64_t tranc_id) {
  // TODO: Lab 3.5 添加键值对
  // ? 记录 first_key (第一次调用时)
  // ? 向 bloom_filter 中 add key
  // ? 更新 max_tranc_id_ / min_tranc_id_
  // ? WiscKey 模式下: 若 value 非空且超过 wisckey_threshold_, 将 value 写入 vlog
  // ?   并将 vlog 引用 [offset:8][size:4] 作为 actual_value
  // ? 尝试向 block 添加 entry; 若返回 false (block满) 先调用 finish_block() 再添加
  // ? 注意: 相同 key 必须在同一个 block 中 (force_write = key == last_key)
  // ? 更新 last_key
}

size_t SSTBuilder::real_size() const { return data.size() + block.cur_size(); }

size_t SSTBuilder::estimated_size() const { return data.size(); }

void SSTBuilder::finish_block() {
  // TODO: Lab 3.5 构建块
  // ? 将当前 block 编码并追加到 data, 同时向 meta_entries 添加元数据
  // ? 然后重置 block 为新的空 Block
  // ? meta_entries 记录: (当前data起始偏移, first_key, last_key)
}

std::shared_ptr<SST>
SSTBuilder::build(size_t sst_id, const std::string &path,
                  std::shared_ptr<BlockCache> block_cache) {
  // TODO: Lab 3.5 构建一个SST
  // ? 1. 若 block 非空则调用 finish_block()
  // ? 2. 若 meta_entries 为空则抛出异常
  // ? 3. 编码元数据块并追加到 data (BlockMeta::encode_meta_to_slice)
  // ? 4. 追加 Bloom Filter 编码
  // ? 5. 写入 footer (老格式 24B 或 WiscKey 26B):
  // ?    [meta_offset:uint32][bloom_offset:uint32][min_tranc_id:uint64][max_tranc_id:uint64]
  // ?    WiscKey 额外: [storage_mode_:uint8][WISCKEY_MAGIC:uint8]
  // ? 6. 调用 FileObj::create_and_write 写文件
  // ? 7. 构造并返回 SST 对象
  return nullptr;
}
} // namespace tiny_lsm
