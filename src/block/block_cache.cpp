#include "block/block_cache.h"
#include "block/block.h"
#include <chrono>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace tiny_lsm {
BlockCache::BlockCache(size_t capacity, size_t k)
    : capacity_(capacity), k_(k) {}

BlockCache::~BlockCache() = default;

std::shared_ptr<Block> BlockCache::get(int sst_id, int block_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  ++total_requests_; // 增加总请求数
  auto key = std::make_pair(sst_id, block_id);
  auto it = cache_map_.find(key);
  if (it == cache_map_.end()) {
    return nullptr; // 缓存未命中
  }

  ++hit_requests_; // 增加命中请求数
  // 更新访问次数
  update_access_count(it->second);

  return it->second->cache_block;
}

void BlockCache::put(int sst_id, int block_id, std::shared_ptr<Block> block) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto key = std::make_pair(sst_id, block_id);
  auto it = cache_map_.find(key);

  if (it != cache_map_.end()) {
    // 更新已有缓存项
    // ! 照理说 Block 类的数据是不可变的，这里的更新分支应该不会存在,
    // 只是debug用
    it->second->cache_block = block;
    update_access_count(it->second);
  } else {
    // 插入新缓存项
    if (cache_map_.size() >= capacity_) {
      // 移除最久未使用的缓存项
      if (!cache_list_less_k.empty()) {
        // 优先从 cache_list_less_k 中移除
        cache_map_.erase(std::make_pair(cache_list_less_k.back().sst_id,
                                        cache_list_less_k.back().block_id));
        cache_list_less_k.pop_back();
      } else {
        cache_map_.erase(std::make_pair(cache_list_greater_k.back().sst_id,
                                        cache_list_greater_k.back().block_id));
        cache_list_greater_k.pop_back();
      }
    }

    CacheItem item = {sst_id, block_id, block, 1};
    cache_list_less_k.push_front(item);
    cache_map_[key] = cache_list_less_k.begin();
  }
}

double BlockCache::hit_rate() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return total_requests_ == 0
             ? 0.0
             : static_cast<double>(hit_requests_) / total_requests_;
}

void BlockCache::update_access_count(std::list<CacheItem>::iterator it) {
  ++it->access_count;
  if (it->access_count < k_) {
    // 更新后仍然位于cache_list_less_k
    // 重新置于cache_list_less_k头部
    cache_list_less_k.splice(cache_list_less_k.begin(), cache_list_less_k, it);
  } else if (it->access_count == k_) {
    // 更新后满足k次访问, 升级链表
    // 从 cache_list_less_k 移动到 cache_list_greater_k 头部
    auto item = *it;
    cache_list_less_k.erase(it);
    cache_list_greater_k.push_front(item);
    cache_map_[std::make_pair(item.sst_id, item.block_id)] =
        cache_list_greater_k.begin();
  } else if (it->access_count > k_) {
    // 本来就位于 cache_list_greater_k
    // 移动到 cache_list_greater_k 头部
    cache_list_greater_k.splice(cache_list_greater_k.begin(),
                                cache_list_greater_k, it);
  }
}
} // namespace tiny_lsm