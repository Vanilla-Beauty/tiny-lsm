#include "../../include/lsm/engine.h"
#include "../../include/config/config.h"
#include "../../include/consts.h"
#include "../../include/logger/logger.h"
#include "../../include/lsm/level_iterator.h"
#include "../../include/sst/concact_iterator.h"
#include "../../include/sst/sst.h"
#include "../../include/sst/sst_iterator.h"
#include "spdlog/spdlog.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tiny_lsm {

// *********************** LSMEngine ***********************
LSMEngine::LSMEngine(std::string path) : data_dir(path) {
  // 初始化日志
  init_spdlog_file();

  // 初始化 block_cahce
  block_cache = std::make_shared<BlockCache>(
      TomlConfig::getInstance().getLsmBlockCacheCapacity(),
      TomlConfig::getInstance().getLsmBlockCacheK());

  // 创建数据目录
  if (!std::filesystem::exists(path)) {
    spdlog::info("LSMEngine--"
                 "DB path ndo not exist. Creating data directory: {}",
                 path);
    std::filesystem::create_directory(path);
  } else {
    // 如果目录存在，则检查是否有 sst 文件并加载
    spdlog::info("LSMEngine--"
                 "DB path exist. Loading data directory: {} ...",
                 path);
    for (const auto &entry : std::filesystem::directory_iterator(path)) {
      if (!entry.is_regular_file()) {
        continue;
      }

      std::string filename = entry.path().filename().string();
      // SST文件名格式为: sst_{id}.level
      if (!filename.starts_with("sst_")) {
        continue;
      }

      // 找到 . 的位置
      size_t dot_pos = filename.find('.');
      if (dot_pos == std::string::npos || dot_pos == filename.length() - 1) {
        continue;
      }

      // 提取 level
      std::string level_str =
          filename.substr(dot_pos + 1, filename.length() - 1 - dot_pos);
      if (level_str.empty()) {
        continue;
      }
      size_t level = std::stoull(level_str);

      // 提取SST ID
      std::string id_str = filename.substr(4, dot_pos - 4); // 4 for "sst_"
      if (id_str.empty()) {
        continue;
      }
      size_t sst_id = std::stoull(id_str);

      // 加载SST文件, 初始化时需要加写锁
      std::unique_lock<std::shared_mutex> lock(ssts_mtx); // 写锁

      next_sst_id = std::max(sst_id, next_sst_id); // 记录目前最大的 sst_id
      cur_max_level = std::max(level, cur_max_level); // 记录目前最大的 level
      std::string sst_path = get_sst_path(sst_id, level);
      auto sst = SST::open(sst_id, FileObj::open(sst_path, false), block_cache);
      spdlog::info("LSMEngine--"
                   "Loaded SST: {} successfully!",
                   sst_path);
      ssts[sst_id] = sst;

      level_sst_ids[level].push_back(sst_id);
    }

    next_sst_id++; // 现有的最大 sst_id 自增后才是下一个分配的 sst_id

    for (auto &[level, sst_id_list] : level_sst_ids) {
      std::sort(sst_id_list.begin(), sst_id_list.end());
      if (level == 0) {
        // 其他 level 的 sst 都是没有重叠的, 且 id 小的表示 key
        // 排序在前面的部分, 不需要 reverse
        std::reverse(sst_id_list.begin(), sst_id_list.end());
      }
    }
  }
}

LSMEngine::~LSMEngine() = default;

std::optional<std::pair<std::string, uint64_t>>
LSMEngine::get(const std::string &key, uint64_t tranc_id) {
  // 1. 先查找 memtable
  auto mem_res = memtable.get(key, tranc_id);
  if (mem_res.is_valid()) {
    if (mem_res.get_value().size() > 0) {
      // 值存在且不为空（没有被删除）
      spdlog::trace("LSMEngine--"
                    "get({},{}): value = {}, tranc_id = {} "
                    "returning from memtable",
                    key, tranc_id, mem_res.get_value(), mem_res.get_tranc_id());
      return std::pair<std::string, uint64_t>{mem_res.get_value(),
                                              mem_res.get_tranc_id()};
    } else {
      // memtable返回的kv的value为空值表示被删除了
      spdlog::trace("LSMEngine--"
                    "get({},{}): key is deleted, returning "
                    "from memtable",
                    key, tranc_id);
      return std::nullopt;
    }
  }

  // 2. l0 sst中查询
  std::shared_lock<std::shared_mutex> rlock(ssts_mtx); // 读锁

  for (auto &sst_id : level_sst_ids[0]) {
    //  中的 sst_id 是按从大到小的顺序排列,
    // sst_id 越大, 表示是越晚刷入的, 优先查询
    auto &sst = ssts[sst_id];
    auto sst_iterator = sst->get(key, tranc_id);
    if (sst_iterator != sst->end()) {
      if ((sst_iterator)->second.size() > 0) {
        // 值存在且不为空（没有被删除）
        spdlog::trace("LSMEngine--"
                      "get({},{}): value = {}, tranc_id = {} "
                      "returning from l0 sst{}",
                      key, tranc_id, sst_iterator->second,
                      sst_iterator.get_tranc_id(), sst_id);
        return std::pair<std::string, uint64_t>{sst_iterator->second,
                                                sst_iterator.get_tranc_id()};
      } else {
        // 空值表示被删除了
        spdlog::trace("LSMEngine--"
                      "get({},{}): key is deleted or do not "
                      "exist , returning "
                      "from l0 sst{}",
                      key, tranc_id, sst_id);
        return std::nullopt;
      }
    }
  }

  // 3. 其他level的sst中查询
  for (size_t level = 1; level <= cur_max_level; level++) {
    std::deque<size_t> l_sst_ids = level_sst_ids[level];
    // 二分查询
    size_t left = 0;
    size_t right = l_sst_ids.size();
    while (left < right) {
      size_t mid = left + (right - left) / 2;
      auto &sst = ssts[l_sst_ids[mid]];
      if (sst->get_first_key() <= key && key <= sst->get_last_key()) {
        // 如果sst_id在中, 则在sst中查询
        auto sst_iterator = sst->get(key, tranc_id);
        if (sst_iterator.is_valid()) {
          if ((sst_iterator)->second.size() > 0) {
            // 值存在且不为空（没有被删除）
            spdlog::trace("LSMEngine--"
                          "get({},{}): value = {}, tranc_id = {} "
                          "returning from l{} sst{}",
                          key, tranc_id, sst_iterator->second,
                          sst_iterator.get_tranc_id(), level, l_sst_ids[mid]);

            return std::pair<std::string, uint64_t>{
                sst_iterator->second, sst_iterator.get_tranc_id()};
          } else {
            // 空值表示被删除了
            spdlog::trace("LSMEngine--"
                          "get({},{}): key is deleted or do not exist "
                          "returning from l{} sst{}",
                          key, tranc_id, level, l_sst_ids[mid]);

            return std::nullopt;
          }
        } else {
          break;
        }
      } else if (sst->get_last_key() < key) {
        left = mid + 1;
      } else {
        right = mid;
      }
    }
  }

  spdlog::trace("LSMEngine--"
                "get({},{}): key is not exist, returning "
                "after checking all ssts",
                key, tranc_id);

  return std::nullopt;
}

std::vector<
    std::pair<std::string, std::optional<std::pair<std::string, uint64_t>>>>
LSMEngine::get_batch(const std::vector<std::string> &keys, uint64_t tranc_id) {
  // 1. 先从 memtable 中批量查找
  auto results = memtable.get_batch(keys, tranc_id);

  // 2. 如果所有键都在memtable 中找到，直接返回
  bool need_search_sst = false;
  for (const auto &[key, value] : results) {
    if (!value.has_value()) {
      // 需要查找
      need_search_sst = true;
      break;
    }
  }

  if (!need_search_sst) {
    return results; // 不需要查sst
  }

  // 2. 从 L0 层 SST 文件中批量查找未命中的键
  std::shared_lock<std::shared_mutex> rlock(ssts_mtx); // 加读锁
  for (auto &[key, value] : results) {
    for (auto &sst_id : level_sst_ids[0]) {
      auto &sst = ssts[sst_id];
      auto sst_iterator = sst->get(key, tranc_id);
      if (sst_iterator != sst->end()) {
        if (sst_iterator->second.size() > 0) {
          // 值存在且不为空
          value =
              std::make_pair(sst_iterator->second, sst_iterator.get_tranc_id());
        } else {
          // 空值表示被删除
          value = std::nullopt;
        }
        break; // 停止继续查找
      }
    }
  }

  // 3. 从其他层级 SST 文件中批量查找未命中的键
  for (size_t level = 1; level <= cur_max_level; level++) {
    std::deque<size_t> l_sst_ids = level_sst_ids[level];

    for (auto &[key, value] : results) {
      if (value.has_value()) // 已找到，跳过
      {
        continue;
      }

      // 二分查找确定键可能所在的 SST 文件
      size_t left = 0;
      size_t right = l_sst_ids.size();
      while (left < right) {
        size_t mid = left + (right - left) / 2;
        auto &sst = ssts[l_sst_ids[mid]];

        if (sst->get_first_key() <= key && key <= sst->get_last_key()) {
          // 如果键在当前 SST 文件范围内，则在 SST 中查找
          auto sst_iterator = sst->get(key, tranc_id);
          if (sst_iterator.is_valid()) {
            if (sst_iterator->second.size() > 0) {
              // 值存在且不为空
              value = std::make_pair(sst_iterator->second,
                                     sst_iterator.get_tranc_id());
            } else {
              // 空值表示被删除
              value = std::nullopt;
            }
          }
          break; // 停止继续查找
        } else if (sst->get_last_key() < key) {
          left = mid + 1;
        } else {
          right = mid;
        }
      }
    }
  }

  return results;
}

std::optional<std::pair<std::string, uint64_t>>
LSMEngine::sst_get_(const std::string &key, uint64_t tranc_id) {

  // 1. l0 sst中查询
  for (auto &sst_id : level_sst_ids[0]) {
    //  中的 sst_id 是按从大到小的顺序排列,
    // sst_id 越大, 表示是越晚刷入的, 优先查询
    auto sst = ssts[sst_id];
    auto sst_iterator = sst->get(key, tranc_id);
    if (sst_iterator != sst->end()) {
      if ((sst_iterator)->second.size() > 0) {
        // 值存在且不为空（没有被删除）
        // L0 SST 查询命中
        spdlog::trace("LSMEngine--"
                      "sst_get({}{}): found in l0 sst{}",
                      key, tranc_id, sst_id);

        return std::pair<std::string, uint64_t>{sst_iterator->second,
                                                sst_iterator.get_tranc_id()};
      } else {
        // 空值表示被删除了
        return std::nullopt;
      }
    }
  }

  // 2. 其他level的sst中查询
  for (size_t level = 1; level <= cur_max_level; level++) {
    std::deque<size_t> l_sst_ids = level_sst_ids[level];
    // 二分查询
    size_t left = 0;
    size_t right = l_sst_ids.size();
    while (left < right) {
      size_t mid = left + (right - left) / 2;
      auto sst = ssts[l_sst_ids[mid]];
      if (sst->get_first_key() <= key && key <= sst->get_last_key()) {
        // 如果sst_id在中, 则在sst中查询
        auto sst_iterator = sst->get(key, tranc_id);
        if (sst_iterator.is_valid()) {
          if ((sst_iterator)->second.size() > 0) {
            // 值存在且不为空（没有被删除）
            // 其他 Level SST 查询命中
            spdlog::trace("LSMEngine--"
                          "sst_get({}{}): found in l{} sst{}",
                          key, tranc_id, level, sst_iterator.get_tranc_id());

            return std::pair<std::string, uint64_t>{
                sst_iterator->second, sst_iterator.get_tranc_id()};
          } else {
            // 空值表示被删除了
            return std::nullopt;
          }
        } else {
          break;
        }
      } else if (sst->get_last_key() < key) {
        left = mid + 1;
      } else {
        right = mid;
      }
    }
  }

  spdlog::trace("LSMEngine--"
                "sst_get({}{}): key is not exist, returning "
                "after checking all ssts",
                key, tranc_id);

  return std::nullopt;
}

uint64_t LSMEngine::put(const std::string &key, const std::string &value,
                        uint64_t tranc_id) {
  memtable.put(key, value, tranc_id);
  spdlog::trace("LSMEngine--"
                "put({}, {}, {})"
                "inserted into memtable",
                key, value, tranc_id);

  // 如果 memtable 太大，需要刷新到磁盘
  if (memtable.get_total_size() >=
      TomlConfig::getInstance().getLsmTolMemSizeLimit()) {
    return flush();
  }

  return 0;
}

uint64_t LSMEngine::put_batch(
    const std::vector<std::pair<std::string, std::string>> &kvs,
    uint64_t tranc_id) {
  memtable.put_batch(kvs, tranc_id);

  spdlog::trace("LSMEngine--"
                "put_batch with {} keys inserted into memtable",
                kvs.size());

  // 如果 memtable 太大，需要刷新到磁盘
  if (memtable.get_total_size() >=
      TomlConfig::getInstance().getLsmTolMemSizeLimit()) {
    return flush();
  }
  return 0;
}
uint64_t LSMEngine::remove(const std::string &key, uint64_t tranc_id) {
  // 在 LSM 中，删除实际上是插入一个空值
  memtable.remove(key, tranc_id);

  spdlog::trace("LSMEngine--"
                "remove({}, {}) marked as "
                "deleted in memtable",
                key, tranc_id);

  // 如果 memtable 太大，需要刷新到磁盘
  if (memtable.get_total_size() >=
      TomlConfig::getInstance().getLsmTolMemSizeLimit()) {
    return flush();
  }
  return 0;
}

uint64_t LSMEngine::remove_batch(const std::vector<std::string> &keys,
                                 uint64_t tranc_id) {
  memtable.remove_batch(keys, tranc_id);

  spdlog::trace("LSMEngine--"
                "remove_batch with {} keys tagged into memtable",
                keys.size());

  // 如果 memtable 太大，需要刷新到磁盘
  if (memtable.get_total_size() >=
      TomlConfig::getInstance().getLsmTolMemSizeLimit()) {
    return flush();
  }
  return 0;
}

void LSMEngine::clear() {
  memtable.clear();
  level_sst_ids.clear();
  ssts.clear();
  // 清空当前文件夹的所有内容
  try {
    for (const auto &entry : std::filesystem::directory_iterator(data_dir)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      std::filesystem::remove(entry.path());

      spdlog::info("LSMEngine--"
                   "clear file {} successfully.",
                   entry.path().string());
    }
  } catch (const std::filesystem::filesystem_error &e) {
    // 处理文件系统错误
    spdlog::error("Error clearing directory: {}", e.what());
  }
}

uint64_t LSMEngine::flush() {
  if (memtable.get_total_size() == 0) {
    return 0;
  }

  std::unique_lock<std::shared_mutex> lock(ssts_mtx); // 写锁

  // 1. 先判断 l0 sst 是否数量超限需要concat到 l1
  if (level_sst_ids.find(0) != level_sst_ids.end() &&
      level_sst_ids[0].size() >=
          TomlConfig::getInstance().getLsmSstLevelRatio()) {
    full_compact(0);
  }

  // 2. 创建新的 SST ID
  size_t new_sst_id = next_sst_id++;

  // 3. 准备 SSTBuilder
  SSTBuilder builder(TomlConfig::getInstance().getLsmBlockSize(),
                     true); // 4KB block size

  // 4. 将 memtable 中最旧的表写入 SST
  std::vector<uint64_t> flushed_tranc_ids;
  auto sst_path = get_sst_path(new_sst_id, 0);
  auto new_sst = memtable.flush_last(builder, sst_path, new_sst_id,
                                     flushed_tranc_ids, block_cache);

  // 5. 更新内存索引
  ssts[new_sst_id] = new_sst;

  // 6. 更新 sst_ids
  level_sst_ids[0].push_front(new_sst_id);

  // 7. 添加到 flushed 集合
  for (auto &id : flushed_tranc_ids) {
    tran_manager.lock()->add_flushed_tranc_id(id);
  }

  // 返回新刷入的 sst 的最大的 tranc_id
  spdlog::info("LSMEngine--"
               "Flush: Memtable flushed to SST with new sst_id={}, level=0",
               new_sst_id);

  return new_sst->get_tranc_id_range().second;
}

std::string LSMEngine::get_sst_path(size_t sst_id, size_t target_level) {
  // sst的文件路径格式为: data_dir/sst_<sst_id>，sst_id格式化为32位数字
  std::stringstream ss;
  ss << data_dir << "/sst_" << std::setfill('0') << std::setw(32) << sst_id
     << '.' << target_level;
  return ss.str();
}

std::optional<std::pair<TwoMergeIterator, TwoMergeIterator>>
LSMEngine::lsm_iters_monotony_predicate(
    uint64_t tranc_id, std::function<int(const std::string &)> predicate) {

  //  先从 memtable 中查询
  auto mem_result = memtable.iters_monotony_predicate(tranc_id, predicate);

  // 再从 sst 中查询
  std::vector<SearchItem> item_vec;
  for (auto &[sst_level, sst_ids] : level_sst_ids) {
    for (auto &sst_id : sst_ids) {
      auto sst = ssts[sst_id];
      auto result = sst_iters_monotony_predicate(sst, tranc_id, predicate);
      if (!result.has_value()) {
        continue;
      }

      spdlog::trace("LSMEngine--"
                    "lsm_iters_monotony_predicate(tranc_id={}): find a range "
                    "from l{} sst{}",
                    tranc_id, sst_level, sst_id);

      auto [it_begin, it_end] = result.value();
      for (; it_begin != it_end && it_begin.is_valid(); ++it_begin) {
        // l0中, 这里越古老的sst的idx越小, 我们需要让新的sst优先在堆顶
        // 让新的sst(拥有更大的idx)排序在前面, 反转符号就行了
        if (tranc_id != 0 && it_begin.get_tranc_id() > tranc_id) {
          // 如果开启了事务, 比当前事务 id 更大的记录是不可见的
          continue;
        }
        if (!item_vec.empty() && item_vec.back().key_ == it_begin.key()) {
          // 如果key相同，则只保留最新的事务修改的记录即可
          // 且这个记录既然已经存在于item_vec中，则其肯定满足了事务的可见性判断
          continue;
        }
        item_vec.emplace_back(it_begin.key(), it_begin.value(), -sst_id,
                              sst_level, it_begin.get_tranc_id());
      }
    }
  }

  std::shared_ptr<HeapIterator> l0_iter_ptr =
      std::make_shared<HeapIterator>(item_vec, tranc_id);

  if (!mem_result.has_value() && item_vec.empty()) {
    return std::nullopt;
  }
  if (mem_result.has_value()) {
    auto [mem_start, mem_end] = mem_result.value();
    std::shared_ptr<HeapIterator> mem_start_ptr =
        std::make_shared<HeapIterator>();
    *mem_start_ptr = mem_start;
    auto start = TwoMergeIterator(mem_start_ptr, l0_iter_ptr, tranc_id);
    auto end = TwoMergeIterator{};
    return std::make_optional<std::pair<TwoMergeIterator, TwoMergeIterator>>(
        start, end);
  } else {
    auto start = TwoMergeIterator(std::make_shared<HeapIterator>(), l0_iter_ptr,
                                  tranc_id);
    auto end = TwoMergeIterator{};
    return std::make_optional<std::pair<TwoMergeIterator, TwoMergeIterator>>(
        start, end);
  }
}

Level_Iterator LSMEngine::begin(uint64_t tranc_id) {
  return Level_Iterator(shared_from_this(), tranc_id);
}

Level_Iterator LSMEngine::end() { return Level_Iterator{}; }

void LSMEngine::full_compact(size_t src_level) {
  // 将 src_level 的 sst 全体压缩到 src_level + 1

  // 递归地判断下一级 level 是否需要 full compact
  if (level_sst_ids[src_level + 1].size() >=
      TomlConfig::getInstance().getLsmSstLevelRatio()) {
    full_compact(src_level + 1);
  }

  spdlog::debug("LSMEngine--"
                "Compaction: Starting full compaction from level{} to level{}",
                src_level, src_level + 1);

  // 获取源level和目标level的 sst_id
  auto old_level_id_x = level_sst_ids[src_level];
  auto old_level_id_y = level_sst_ids[src_level + 1];
  std::vector<std::shared_ptr<SST>> new_ssts;
  std::vector<size_t> lx_ids(old_level_id_x.begin(), old_level_id_x.end());
  std::vector<size_t> ly_ids(old_level_id_y.begin(), old_level_id_y.end());
  if (src_level == 0) {
    // l0这一层不同sst的key有重叠, 需要额外处理
    new_ssts = full_l0_l1_compact(lx_ids, ly_ids);
  } else {
    new_ssts = full_common_compact(lx_ids, ly_ids, src_level + 1);
  }
  // 完成 compact 后移除旧的sst记录
  for (auto &old_sst_id : old_level_id_x) {
    ssts[old_sst_id]->del_sst();
    ssts.erase(old_sst_id);
  }
  for (auto &old_sst_id : old_level_id_y) {
    ssts[old_sst_id]->del_sst();
    ssts.erase(old_sst_id);
  }
  level_sst_ids[src_level].clear();
  level_sst_ids[src_level + 1].clear();

  cur_max_level = std::max(cur_max_level, src_level + 1);

  // 添加新的sst
  for (auto &new_sst : new_ssts) {
    level_sst_ids[src_level + 1].push_back(new_sst->get_sst_id());
    ssts[new_sst->get_sst_id()] = new_sst;
  }
  // 此处没必要reverse了
  std::sort(level_sst_ids[src_level + 1].begin(),
            level_sst_ids[src_level + 1].end());

  spdlog::debug("LSMEngine--"
                "Compaction: Finished compaction. New SSTs added at level{}",
                src_level + 1);
}

std::vector<std::shared_ptr<SST>>
LSMEngine::full_l0_l1_compact(std::vector<size_t> &l0_ids,
                              std::vector<size_t> &l1_ids) {
  // TODO: 这里需要补全的是对已经完成事务的删除
  std::vector<SstIterator> l0_iters;
  std::vector<std::shared_ptr<SST>> l1_ssts;

  for (auto id : l0_ids) {
    auto sst_it = ssts[id]->begin(0);
    l0_iters.push_back(sst_it);
  }
  for (auto id : l1_ids) {
    l1_ssts.push_back(ssts[id]);
  }
  // l0 的sst之间的key有重叠, 需要合并
  auto [l0_begin, l0_end] = SstIterator::merge_sst_iterator(l0_iters, 0);

  std::shared_ptr<HeapIterator> l0_begin_ptr = std::make_shared<HeapIterator>();
  *l0_begin_ptr = l0_begin;

  std::shared_ptr<ConcactIterator> old_l1_begin_ptr =
      std::make_shared<ConcactIterator>(l1_ssts, 0);

  TwoMergeIterator l0_l1_begin(l0_begin_ptr, old_l1_begin_ptr, 0);

  return gen_sst_from_iter(l0_l1_begin,
                           TomlConfig::getInstance().getLsmPerMemSizeLimit() *
                               TomlConfig::getInstance().getLsmSstLevelRatio(),
                           1);
}

std::vector<std::shared_ptr<SST>>
LSMEngine::full_common_compact(std::vector<size_t> &lx_ids,
                               std::vector<size_t> &ly_ids, size_t level_y) {
  // TODO 需要补全已完成事务的滤除
  std::vector<std::shared_ptr<SST>> lx_iters;
  std::vector<std::shared_ptr<SST>> ly_iters;

  for (auto id : lx_ids) {
    lx_iters.push_back(ssts[id]);
  }
  for (auto id : ly_ids) {
    ly_iters.push_back(ssts[id]);
  }

  std::shared_ptr<ConcactIterator> old_lx_begin_ptr =
      std::make_shared<ConcactIterator>(lx_iters, 0);

  std::shared_ptr<ConcactIterator> old_ly_begin_ptr =
      std::make_shared<ConcactIterator>(ly_iters, 0);

  TwoMergeIterator lx_ly_begin(old_lx_begin_ptr, old_ly_begin_ptr, 0);

  // TODO:如果目标 level 的下一级 level+1 不存在, 则为底层的level,
  // 可以清理掉删除标记

  return gen_sst_from_iter(lx_ly_begin, LSMEngine::get_sst_size(level_y),
                           level_y);
}

std::vector<std::shared_ptr<SST>>
LSMEngine::gen_sst_from_iter(BaseIterator &iter, size_t target_sst_size,
                             size_t target_level) {
  // TODO: 这里需要补全的是对已经完成事务的删除
  // std::cout << "process into gen\n";
  std::vector<std::shared_ptr<SST>> new_ssts;
  auto new_sst_builder =
      SSTBuilder(TomlConfig::getInstance().getLsmBlockSize(), true);
  while (iter.is_valid() && !iter.is_end()) {

    new_sst_builder.add((*iter).first, (*iter).second, 0);
    ++iter;

    if (new_sst_builder.estimated_size() >= target_sst_size) {
      size_t sst_id = next_sst_id++; // TODO: 后续优化并发性
      std::string sst_path = get_sst_path(sst_id, target_level);
      auto new_sst = new_sst_builder.build(sst_id, sst_path, this->block_cache);
      new_ssts.push_back(new_sst);

      spdlog::debug("LSMEngine--"
                    "Compaction: Generated new SST file with sst_id={}"
                    "at level{}",
                    sst_id, target_level);

      new_sst_builder = SSTBuilder(TomlConfig::getInstance().getLsmBlockSize(),
                                   true); // 重置builder
    }
  }

  //只要builder里面存在没有落盘的数据，就要把它放到sst里面去。
  if (new_sst_builder.real_size() > 0) {
    size_t sst_id = next_sst_id++; // TODO: 后续优化并发性
    std::string sst_path = get_sst_path(sst_id, target_level);
    auto new_sst = new_sst_builder.build(sst_id, sst_path, this->block_cache);
    new_ssts.push_back(new_sst);

    spdlog::debug("LSMEngine--"
                  "Compaction: Generated new SST file with sst_id={} "
                  "at level{}",
                  sst_id, target_level);
  }

  return new_ssts;
}

size_t LSMEngine::get_sst_size(size_t level) {
  if (level == 0) {
    return TomlConfig::getInstance().getLsmPerMemSizeLimit();
  } else {
    return TomlConfig::getInstance().getLsmPerMemSizeLimit() *
           static_cast<size_t>(std::pow(
               TomlConfig::getInstance().getLsmSstLevelRatio(), level));
  }
}

void LSMEngine::set_tran_manager(std::shared_ptr<TranManager> tran_manager) {
  this->tran_manager = tran_manager;
}

// *********************** LSM ***********************
LSM::LSM(std::string path)
    : engine(std::make_shared<LSMEngine>(path)),
      tran_manager_(std::make_shared<TranManager>(path)) {
  tran_manager_->set_engine(engine);
  engine->set_tran_manager(tran_manager_);
  auto check_recover_res = tran_manager_->check_recover();
  for (auto &[tranc_id, records] : check_recover_res) {
    if (tran_manager_->get_flushed_tranc_ids().count(tranc_id)) {
      continue;
    }
    for (auto &record : records) {
      if (record.getOperationType() == OperationType::PUT) {
        engine->put(record.getKey(), record.getValue(), tranc_id);
      } else if (record.getOperationType() == OperationType::DELETE) {
        engine->remove(record.getKey(), tranc_id);
      }
    }
    spdlog::debug("LSMEngine--"
                  "Recover: Recovered transaction with tranc_id={}",
                  tranc_id);
  }
  tran_manager_->init_new_wal();
}

LSM::~LSM() {
  flush_all();
  tran_manager_->write_tranc_id_file();
}

std::optional<std::string> LSM::get(const std::string &key) {
  auto tranc_id = tran_manager_->getNextTransactionId();
  auto res = engine->get(key, tranc_id);

  if (res.has_value()) {
    return res.value().first;
  }
  return std::nullopt;
}

std::vector<std::pair<std::string, std::optional<std::string>>>
LSM::get_batch(const std::vector<std::string> &keys) {
  // 1. 获取事务ID
  auto tranc_id = tran_manager_->getNextTransactionId();

  // 2. 调用 engine 的批量查询接口
  auto batch_results = engine->get_batch(keys, tranc_id);

  // 3. 构造最终结果
  std::vector<std::pair<std::string, std::optional<std::string>>> results;
  for (const auto &[key, value] : batch_results) {
    if (value.has_value()) {
      results.emplace_back(key, value->first); // 提取值部分
    } else {
      results.emplace_back(key, std::nullopt); // 键不存在
    }
  }

  return results;
}

void LSM::put(const std::string &key, const std::string &value) {
  auto tranc_id = tran_manager_->getNextTransactionId();
  engine->put(key, value, tranc_id);
}

void LSM::put_batch(
    const std::vector<std::pair<std::string, std::string>> &kvs) {
  auto tranc_id = tran_manager_->getNextTransactionId();
  engine->put_batch(kvs, tranc_id);
}
void LSM::remove(const std::string &key) {
  auto tranc_id = tran_manager_->getNextTransactionId();
  engine->remove(key, tranc_id);
}

void LSM::remove_batch(const std::vector<std::string> &keys) {
  auto tranc_id = tran_manager_->getNextTransactionId();
  engine->remove_batch(keys, tranc_id);
}

void LSM::clear() { engine->clear(); }

void LSM::flush() { auto max_tranc_id = engine->flush(); }

void LSM::flush_all() {
  while (engine->memtable.get_total_size() > 0) {
    auto max_tranc_id = engine->flush();
    // tran_manager_->update_checkpoint_tranc_id(max_tranc_id);
  }
}

LSM::LSMIterator LSM::begin(uint64_t tranc_id) {
  return engine->begin(tranc_id);
}

LSM::LSMIterator LSM::end() { return engine->end(); }

std::optional<std::pair<TwoMergeIterator, TwoMergeIterator>>
LSM::lsm_iters_monotony_predicate(
    uint64_t tranc_id, std::function<int(const std::string &)> predicate) {
  return engine->lsm_iters_monotony_predicate(tranc_id, predicate);
}

// 开启一个事务
std::shared_ptr<TranContext>
LSM::begin_tran(const IsolationLevel &isolation_level) {
  auto tranc_context = tran_manager_->new_tranc(isolation_level);

  spdlog::info("LSM--"
               "lsm_iters_monotony_predicate: Starting query for tranc_id={}",
               tranc_context->tranc_id_);

  return tranc_context;
}

void LSM::set_log_level(const std::string &level) { reset_log_level(level); }
} // namespace tiny_lsm