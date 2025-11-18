#pragma once

#include "../iterator/iterator.h"
#include "../skiplist/skiplist.h"
#include <cstddef>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace tiny_lsm {

class BlockCache;
class SST;
class SSTBuilder;
class TranContext;

class MemTable {
  friend class TranContext;
  friend class HeapIterator;

private:
  void put_(const std::string &key, const std::string &value,
            uint64_t tranc_id);

  SkipListIterator get_(const std::string &key, uint64_t tranc_id);

  SkipListIterator cur_get_(const std::string &key, uint64_t tranc_id);

  SkipListIterator frozen_get_(const std::string &key, uint64_t tranc_id);

  void remove_(const std::string &key, uint64_t tranc_id);
  void frozen_cur_table_(); // _ 表示不需要锁的版本

public:
  MemTable();
  ~MemTable();

  void put(const std::string &key, const std::string &value, uint64_t tranc_id);
  void put_batch(const std::vector<std::pair<std::string, std::string>> &kvs,
                 uint64_t tranc_id);

  SkipListIterator get(const std::string &key, uint64_t tranc_id);
  std::vector<
      std::pair<std::string, std::optional<std::pair<std::string, uint64_t>>>>
  get_batch(const std::vector<std::string> &keys, uint64_t tranc_id);
  void remove(const std::string &key, uint64_t tranc_id);
  void remove_batch(const std::vector<std::string> &keys, uint64_t tranc_id);

  void clear();
  std::shared_ptr<SST> flush_last(SSTBuilder &builder, std::string &sst_path,
                                  size_t sst_id,
                                  std::vector<uint64_t> &flushed_tranc_ids,
                                  std::shared_ptr<BlockCache> block_cache);
  void frozen_cur_table();
  size_t get_cur_size();
  size_t get_frozen_size();
  size_t get_total_size();
  HeapIterator begin(uint64_t tranc_id);
  HeapIterator iters_preffix(const std::string &preffix, uint64_t tranc_id);

  std::optional<std::pair<HeapIterator, HeapIterator>>
  iters_monotony_predicate(uint64_t tranc_id,
                           std::function<int(const std::string &)> predicate);

  HeapIterator end();

private:
  std::shared_ptr<SkipList> current_table;
  std::list<std::shared_ptr<SkipList>> frozen_tables;
  size_t frozen_bytes;
  std::shared_mutex frozen_mtx; // 冻结表的锁
  std::shared_mutex cur_mtx;    // 活跃表的锁
};
} // namespace tiny_lsm