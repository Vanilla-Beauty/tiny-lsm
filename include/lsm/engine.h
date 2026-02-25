#pragma once

#include "memtable/memtable.h"
#include "sst/sst.h"
#include "compact.h"
#include "transaction.h"
#include "two_merge_iterator.h"
#include <cstddef>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace tiny_lsm {

class Level_Iterator;

class LSMEngine : public std::enable_shared_from_this<LSMEngine> {
public:
  std::string data_dir;
  MemTable memtable;
  std::map<size_t, std::deque<size_t>> level_sst_ids;
  std::unordered_map<size_t, std::shared_ptr<SST>> ssts;
  std::shared_mutex ssts_mtx;
  std::shared_ptr<BlockCache> block_cache;
  std::weak_ptr<TranManager> tran_manager;
  size_t next_sst_id = 0;
  size_t cur_max_level = 0;

public:
  LSMEngine(std::string path);
  ~LSMEngine();

  std::optional<std::pair<std::string, uint64_t>> get(const std::string &key,
                                                      uint64_t tranc_id);
  std::vector<
      std::pair<std::string, std::optional<std::pair<std::string, uint64_t>>>>
  get_batch(const std::vector<std::string> &keys, uint64_t tranc_id);

  std::optional<std::pair<std::string, uint64_t>>
  sst_get_(const std::string &key, uint64_t tranc_id);

  // 如果触发了刷盘, 返回当前刷入sst的最大事务id
  uint64_t put(const std::string &key, const std::string &value,
               uint64_t tranc_id);

  uint64_t
  put_batch(const std::vector<std::pair<std::string, std::string>> &kvs,
            uint64_t tranc_id);

  uint64_t remove(const std::string &key, uint64_t tranc_id);
  uint64_t remove_batch(const std::vector<std::string> &keys,
                        uint64_t tranc_id);
  void clear();
  uint64_t flush();

  std::string get_sst_path(size_t sst_id, size_t target_level);

  std::optional<std::pair<TwoMergeIterator, TwoMergeIterator>>
  lsm_iters_monotony_predicate(
      uint64_t tranc_id, std::function<int(const std::string &)> predicate);

  Level_Iterator begin(uint64_t tranc_id);
  Level_Iterator end();

  static size_t get_sst_size(size_t level);

  void set_tran_manager(std::shared_ptr<TranManager> tran_manager);

private:
  void full_compact(size_t src_level);
  std::vector<std::shared_ptr<SST>>
  full_l0_l1_compact(std::vector<size_t> &l0_ids, std::vector<size_t> &l1_ids);

  std::vector<std::shared_ptr<SST>>
  full_common_compact(std::vector<size_t> &lx_ids, std::vector<size_t> &ly_ids,
                      size_t level_y);

  std::vector<std::shared_ptr<SST>> gen_sst_from_iter(BaseIterator &iter,
                                                      size_t target_sst_size,
                                                      size_t target_level);
};

class LSM {
private:
  std::shared_ptr<LSMEngine> engine;
  std::shared_ptr<TranManager> tran_manager_;

public:
  LSM(std::string path);
  ~LSM();

  std::optional<std::string> get(const std::string &key);
  std::vector<std::pair<std::string, std::optional<std::string>>>
  get_batch(const std::vector<std::string> &keys);

  void put(const std::string &key, const std::string &value);
  void put_batch(const std::vector<std::pair<std::string, std::string>> &kvs);

  void remove(const std::string &key);
  void remove_batch(const std::vector<std::string> &keys);

  using LSMIterator = Level_Iterator;
  LSMIterator begin(uint64_t tranc_id);
  LSMIterator end();
  std::optional<std::pair<TwoMergeIterator, TwoMergeIterator>>
  lsm_iters_monotony_predicate(
      uint64_t tranc_id, std::function<int(const std::string &)> predicate);
  void clear();
  void flush();
  void flush_all();

  // 开启一个事务
  std::shared_ptr<TranContext>
  begin_tran(const IsolationLevel &isolation_level);

  // 重设日志级别
  void set_log_level(const std::string &level);
};
} // namespace tiny_lsm