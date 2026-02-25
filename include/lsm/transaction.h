#pragma once

#include "utils/files.h"
#include "wal/wal.h"
#include <atomic>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tiny_lsm {

enum class IsolationLevel {
  READ_UNOP_COMMITTED,
  READ_OP_COMMITTED,
  REPEATABLE_READ,
  SERIALIZABLE
};

enum class TransactionState {
  OP_COMMITTED,
  ABORTED
};

inline std::string isolation_level_to_string(const IsolationLevel &level);

class LSMEngine;
class TranManager;

class TranContext {
  friend class TranManager;

public:
  TranContext(uint64_t tranc_id, std::shared_ptr<LSMEngine> engine,
              std::shared_ptr<TranManager> tranManager,
              const enum IsolationLevel &isolation_level);
  void put(const std::string &key, const std::string &value);
  void remove(const std::string &key);
  std::optional<std::string> get(const std::string &key);

  // ! test_fail = true 是测试中手动触发的崩溃
  bool commit(bool test_fail = false);
  bool abort();
  enum IsolationLevel get_isolation_level();

public:
  std::shared_ptr<LSMEngine> engine_;
  std::weak_ptr<TranManager> tranManager_;
  uint64_t tranc_id_;
  std::vector<Record> operations;
  std::unordered_map<std::string, std::string> temp_map_;
  bool isCommited = false;
  bool isAborted = false;
  enum IsolationLevel isolation_level_;

private:
  std::unordered_map<std::string,
                     std::optional<std::pair<std::string, uint64_t>>>
      read_map_;
  std::unordered_map<std::string,
                     std::optional<std::pair<std::string, uint64_t>>>
      rollback_map_;
};

class TranManager : public std::enable_shared_from_this<TranManager> {
public:
  TranManager(std::string data_dir);
  ~TranManager();
  void init_new_wal();
  void set_engine(std::shared_ptr<LSMEngine> engine);
  std::shared_ptr<TranContext> new_tranc(const IsolationLevel &isolation_level);

  uint64_t getNextTransactionId();
  uint64_t get_max_flushed_tranc_id();
  uint64_t get_checkpoint_tranc_id();
  std::set<uint64_t>& get_flushed_tranc_ids();

  void add_ready_to_flush_tranc_id(uint64_t tranc_id, TransactionState state);
  void add_flushed_tranc_id(uint64_t tranc_id);
  // void remove_active_tranc_id(uint64_t tranc_id);

  bool write_to_wal(const std::vector<Record> &records);

  std::map<uint64_t, std::vector<Record>> check_recover();

  std::string get_tranc_id_file_path();
  void write_tranc_id_file();
  void read_tranc_id_file();
  // void flusher();

private:
  mutable std::mutex mutex_;
  std::shared_ptr<LSMEngine> engine_;
  std::shared_ptr<WAL> wal;
  std::string data_dir_;
  // std::atomic<bool> flush_thread_running_ = true;
  std::atomic<uint64_t> nextTransactionId_ = 1;
  std::map<uint64_t, std::shared_ptr<TranContext>> activeTrans_;
  std::map<uint64_t, TransactionState> readyToFlushTrancIds_;
  std::set<uint64_t> flushedTrancIds_;
  FileObj tranc_id_file_;
};

} // namespace tiny_lsm