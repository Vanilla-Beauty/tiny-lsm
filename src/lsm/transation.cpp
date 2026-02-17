#include "lsm/engine.h"
#include "lsm/transaction.h"
#include "utils/files.h"
#include "utils/set_operation.h"
#include "spdlog/spdlog.h"
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace tiny_lsm {

inline std::string isolation_level_to_string(const IsolationLevel &level) {
  switch (level) {
  case IsolationLevel::READ_UNOP_COMMITTED:
    return "READ_UNOP_COMMITTED";
  case IsolationLevel::READ_OP_COMMITTED:
    return "READ_OP_COMMITTED";
  case IsolationLevel::REPEATABLE_READ:
    return "REPEATABLE_READ";
  case IsolationLevel::SERIALIZABLE:
    return "SERIALIZABLE";
  default:
    return "UNKNOWN";
  }
}

// *********************** TranContext ***********************
TranContext::TranContext(uint64_t tranc_id, std::shared_ptr<LSMEngine> engine,
                         std::shared_ptr<TranManager> tranManager,
                         const enum IsolationLevel &isolation_level)
    : tranc_id_(tranc_id), engine_(std::move(engine)),
      tranManager_(tranManager), isolation_level_(isolation_level) {
  operations.emplace_back(Record::createRecord(tranc_id_));
}

void TranContext::put(const std::string &key, const std::string &value) {
  spdlog::trace("LSM--"
                "lsm_iters_monotony_predicate: Starting query for tranc_id={}",
                this->tranc_id_);

  auto isolation_level = get_isolation_level();

  // 所有隔离级别都需要先写入 operations 中
  operations.emplace_back(Record::putRecord(this->tranc_id_, key, value));

  if (isolation_level == IsolationLevel::READ_UNOP_COMMITTED) {
    // 1 如果隔离级别是 READ_UNOP_COMMITTED, 直接写入 memtable
    // 先查询以前的记录, 因为回滚时可能需要
    auto prev_record = engine_->get(key, 0);
    rollback_map_[key] = prev_record;
    engine_->put(key, value, tranc_id_);

    spdlog::trace(
        "TranContext--READ_UNOP_COMMITTED: put({}, {}) applied to memtable",
        key, value);

    return;
  }

  // 2 其他隔离级别需要 暂存到 temp_map_ 中, 统一提交后才在数据库中生效
  temp_map_[key] = value;

  spdlog::trace("TranContext--{}: put({}, {}) stored in temp map",
                isolation_level_to_string(isolation_level_), key, value);
}

void TranContext::remove(const std::string &key) {
  spdlog::trace("TranContext--remove({}) called, tranc_id={}", key, tranc_id_);

  auto isolation_level = get_isolation_level();

  // 所有隔离级别都需要先写入 operations 中
  operations.emplace_back(Record::deleteRecord(this->tranc_id_, key));

  if (isolation_level == IsolationLevel::READ_UNOP_COMMITTED) {
    // 1 如果隔离级别是 READ_UNOP_COMMITTED, 直接写入 memtable
    // 先查询以前的记录, 因为回滚时可能需要
    auto prev_record = engine_->get(key, 0);
    rollback_map_[key] = prev_record;
    engine_->remove(key, tranc_id_);

    spdlog::trace(
        "TranContext--READ_UNOP_COMMITTED: remove({}) applied to memtable",
        key);

    return;
  }

  // 2 其他隔离级别需要 暂存到 temp_map_ 中, 统一提交后才在数据库中生效
  temp_map_[key] = "";
  spdlog::trace("TranContext--{}: remove({}) stored in temp map",
                isolation_level_to_string(isolation_level_), key);
}

std::optional<std::string> TranContext::get(const std::string &key) {
  spdlog::trace("TranContext--get({}) called, tranc_id={}", key, tranc_id_);

  auto isolation_level = get_isolation_level();

  // 1 所有隔离级别先就近在当前操作的临时缓存中查找
  if (temp_map_.find(key) != temp_map_.end()) {
    // READ_UNOP_COMMITTED 随单次操作更新数据库, 不需要最后的统一更新
    // 这一步骤肯定会自然跳过的
    spdlog::trace("TranContext--{}: get({}) found in temp map",
                  isolation_level_to_string(isolation_level), key);

    return temp_map_[key];
  }

  // 2 否则使用 engine 查询
  std::optional<std::pair<std::string, uint64_t>> query;
  if (isolation_level == IsolationLevel::READ_UNOP_COMMITTED) {
    // 2.1 如果隔离级别是 READ_UNOP_COMMITTED, 使用 engine
    // 查询时不需要判断 tranc_id, 直接获取最新值
    query = engine_->get(key, 0);
  } else if (isolation_level == IsolationLevel::READ_OP_COMMITTED) {
    // 2.2 如果隔离级别是 READ_OP_COMMITTED, 使用 engine
    // 查询时判断 tranc_id
    query = engine_->get(key, this->tranc_id_);
  } else {
    // 2.2 如果隔离级别是 SERIALIZABLE 或 REPEATABLE_READ, 第一次使用 engine
    // 查询后还需要暂存
    if (read_map_.find(key) != read_map_.end()) {
      query = read_map_[key];
    } else {
      query = engine_->get(key, this->tranc_id_);
      read_map_[key] = query;
    }
  }
  if (query.has_value()) {
    spdlog::trace("TranContext--{}: get({}) returned value={}",
                  isolation_level_to_string(isolation_level), key,
                  query->first);
  } else {
    spdlog::trace("TranContext--{}: get({}) returned no value",
                  isolation_level_to_string(isolation_level), key);
  }

  return query.has_value() ? std::make_optional(query->first) : std::nullopt;
}

bool TranContext::commit(bool test_fail) {
  spdlog::info("TranContext--commit(): Starting commit for transaction ID={}",
               tranc_id_);

  auto isolation_level = get_isolation_level();

  if (isolation_level == IsolationLevel::READ_UNOP_COMMITTED) {
    // READ_UNOP_COMMITTED 随单次操作更新数据库, 不需要最后的统一更新
    // 因此也不需要使用到后面的锁保证正确性
    operations.emplace_back(Record::commitRecord(this->tranc_id_));

    // 先刷入wal
    auto tranManager = tranManager_.lock();
    auto wal_success = tranManager->write_to_wal(operations);

    if (!wal_success) {
      spdlog::error(
          "TranContext--commit(): Failed to write WAL for transaction ID={}",
          tranc_id_);

      throw std::runtime_error("write to wal failed");
    }
    engine_->memtable.put_("", "", tranc_id_);
    isCommited = true;
    tranManager->add_ready_to_flush_tranc_id(tranc_id_,
                                             TransactionState::OP_COMMITTED);

    spdlog::info(
        "TranContext--commit(): Transaction ID={} committed successfully",
        tranc_id_);

    return true;
  }

  // commit 需要检查所有的操作是否合法

  // 遍历所有的记录, 判断是否合法

  // TODO: 目前为检查冲突, 全局获取了读锁, 后续考虑性能优化方案

  MemTable &memtable = engine_->memtable;
  std::unique_lock<std::shared_mutex> wlock1(memtable.frozen_mtx);
  std::unique_lock<std::shared_mutex> wlock2(memtable.cur_mtx);

  auto tranManager = tranManager_.lock();

  if (isolation_level == IsolationLevel::REPEATABLE_READ ||
      isolation_level == IsolationLevel::SERIALIZABLE) {
    // REPEATABLE_READ 需要校验冲突
    // TODO: 目前 SERIALIZABLE 还没有实现, 逻辑和 REPEATABLE_READ 相同

    // 只要需要校验的 隔离级别 需要加sst的锁
    std::shared_lock<std::shared_mutex> rlock3(engine_->ssts_mtx);

    for (auto &[k, v] : temp_map_) {
      // 步骤1: 先在内存表中判断该 key 是否冲突

      // ! 注意第二个参数设置为0, 表示忽略事务可见性的查询
      auto res = memtable.get_(k, 0);
      if (res.is_valid() && res.get_tranc_id() > tranc_id_) {
        // 数据库中存在相同的 key , 且其 tranc_id 大于当前 tranc_id
        // 表示更晚创建的事务修改了相同的key, 并先提交, 发生了冲突
        // 需要终止事务
        isAborted = true;
        tranManager->add_ready_to_flush_tranc_id(tranc_id_,
                                                 TransactionState::ABORTED);

        spdlog::warn("TranContext--commit(): Conflict detected on key={}, "
                     "aborting transaction ID={}",
                     k, tranc_id_);

        return false;
      } else {
        // 步骤2: 判断sst中是否是否存在冲突
        if (tranManager->get_max_flushed_tranc_id() <= tranc_id_) {
          // sst 中最大的 tranc_id 小于当前 tranc_id, 没有冲突
          continue;
        }

        // 否则要查询具体的key是否冲突
        // ! 注意第二个参数设置为0, 表示忽略事务可见性的查询
        auto res = engine_->sst_get_(k, 0);
        if (res.has_value()) {
          auto [v, tranc_id] = res.value();
          if (tranc_id > tranc_id_) {
            // 数据库中存在相同的 key , 且其 tranc_id 大于当前 tranc_id
            // 表示更晚创建的事务修改了相同的key, 并先提交, 发生了冲突
            // 需要终止事务
            isAborted = true;
            tranManager->add_ready_to_flush_tranc_id(tranc_id_,
                                                     TransactionState::ABORTED);

            spdlog::warn("TranContext--commit(): SST conflict on key={}, "
                         "aborting transaction ID={}",
                         k, tranc_id_);

            return false;
          }
        }
      }
    }
  }

  // 其他隔离级别不检查, 直接运行到这里

  // 校验全部通过, 可以刷入
  operations.emplace_back(Record::commitRecord(this->tranc_id_));

  // 先刷入wal
  auto wal_success = tranManager->write_to_wal(operations);

  if (!wal_success) {
    spdlog::error(
        "TranContext--commit(): Failed to write WAL for transaction ID={}",
        tranc_id_);

    throw std::runtime_error("write to wal failed");
  }

  // 将暂存数据应用到数据库
  if (!test_fail) {
    // 这里是手动调用 memtable 的无锁版本的 put_, 因为之前手动加了写锁
    for (auto &[k, v] : temp_map_) {
      memtable.put_(k, v, tranc_id_);
    }
    memtable.put_("", "", tranc_id_);
  }

  isCommited = true;
  tranManager->add_ready_to_flush_tranc_id(tranc_id_,
                                           TransactionState::OP_COMMITTED);

  spdlog::info(
      "TranContext--commit(): Transaction ID={} committed successfully",
      tranc_id_);

  return true;
}

bool TranContext::abort() {
  auto isolation_level = get_isolation_level();
  spdlog::info("TranContext--abort(): Aborting transaction ID={}", tranc_id_);

  auto tranManager = tranManager_.lock();

  if (isolation_level == IsolationLevel::READ_UNOP_COMMITTED) {
    // 需要手动恢复之前的更改
    // TODO: 需要使用批量化操作优化性能
    for (auto &[k, res] : rollback_map_) {
      if (res.has_value()) {
        engine_->put(k, res.value().first, res.value().second);
      } else {
        // 之前本就不存在, 需要移除当前事务的新增操作
        engine_->remove(k, tranc_id_);
      }
    }
    isAborted = true;
    tranManager->add_ready_to_flush_tranc_id(tranc_id_,
                                             TransactionState::ABORTED);

    spdlog::info("TranContext--abort(): Transaction ID={} aborted", tranc_id_);

    return true;
  }
  // ! 由于目前 records 是在 commit 时统一刷入 wal 的, 因此这个情况下, abort
  // ! 简单丢弃 operations 即可 ! 后续会将 records 分批次写入 wal
  // ! 这时就需要加上 ! rollback 标记了, 执行下面注释的逻辑

  // operations.emplace_back(Record::rollbackRecord(this->tranc_id_));
  // // 先刷入wal
  // auto wal_success = tranManager->write_to_wal(operations);

  // if (!wal_success) {
  //   throw std::runtime_error("write to wal failed");
  // }

  isAborted = true;
  tranManager->add_ready_to_flush_tranc_id(tranc_id_,
                                           TransactionState::ABORTED);
  return true;
}

enum IsolationLevel TranContext::get_isolation_level() {
  return isolation_level_;
}

// *********************** TranManager ***********************
TranManager::TranManager(std::string data_dir) : data_dir_(data_dir) {
  auto file_path = get_tranc_id_file_path();

  // 判断文件是否存在

  if (!std::filesystem::exists(file_path)) {
    tranc_id_file_ = FileObj::open(file_path, true);
    flushedTrancIds_.insert(0);
  } else {
    tranc_id_file_ = FileObj::open(file_path, false);
    read_tranc_id_file();
  }
}

void TranManager::init_new_wal() {
  spdlog::info("TranManager--init_new_wal(): Cleaning up old WAL files");

  // TODO: 1 和 4096 应该统一用宏定义
  // 先清理掉所有 wal. 开头的文件, 因为其已经被重放过了
  for (const auto &entry : std::filesystem::directory_iterator(data_dir_)) {
    if (entry.path().filename().string().find("wal.") == 0) {
      std::filesystem::remove(entry.path());
    }
  }
  wal = std::make_shared<WAL>(data_dir_, 128, get_max_flushed_tranc_id(), 1,
                              4096);
  flushedTrancIds_.clear();
  flushedTrancIds_.insert(nextTransactionId_.load() - 1);
  spdlog::info("TranManager--init_new_wal(): New WAL initialized");
}

void TranManager::set_engine(std::shared_ptr<LSMEngine> engine) {
  engine_ = std::move(engine);
}

TranManager::~TranManager() { write_tranc_id_file(); }

void TranManager::write_tranc_id_file() {
  // 共4个8字节的整型id记录
  // std::atomic<uint64_t> nextTransactionId_;
  // std::atomic<uint64_t> max_flushed_tranc_id_;
  // std::atomic<uint64_t> checkpoint_tranc_id_;

  int buffer_size = sizeof(uint64_t) * (flushedTrancIds_.size() + 2);
  std::vector<uint8_t> buf(buffer_size, 0);
  uint64_t nextTransactionId = nextTransactionId_.load();
  uint64_t tranc_size = flushedTrancIds_.size();
  memcpy(buf.data(), &nextTransactionId, sizeof(uint64_t));
  memcpy(buf.data() + sizeof(uint64_t), &tranc_size, sizeof(uint64_t));
  int offset = sizeof(uint64_t) * 2;
  for (auto &tranc_id : flushedTrancIds_) {
    memcpy(buf.data() + offset, &tranc_id, sizeof(uint64_t));
    offset += sizeof(uint64_t);
  }

  tranc_id_file_.write(0, buf);
  tranc_id_file_.sync();
}

void TranManager::read_tranc_id_file() {
  nextTransactionId_ = tranc_id_file_.read_uint64(0);
  uint64_t size = tranc_id_file_.read_uint64(sizeof(uint64_t));
  int offset = sizeof(uint64_t) * 2;
  for (int i = 0; i < size; i++) {
    uint64_t flushed_id = tranc_id_file_.read_uint64(offset);
    flushedTrancIds_.insert(flushed_id);
    offset += sizeof(uint64_t);
  }
}

void TranManager::add_ready_to_flush_tranc_id(uint64_t tranc_id,
                                              TransactionState state) {
  std::unique_lock lock(mutex_);
  readyToFlushTrancIds_[tranc_id] = state;
}

void TranManager::add_flushed_tranc_id(uint64_t tranc_id) {
  std::unique_lock lock(mutex_);
  std::vector<uint64_t> needRemove;
  for (auto &[readyId, state] : readyToFlushTrancIds_) {
    if (readyId < tranc_id && state == TransactionState::ABORTED) {
      flushedTrancIds_.insert(readyId);
      needRemove.push_back(readyId);
    } else if (readyId == tranc_id) {
      flushedTrancIds_.insert(readyId);
      needRemove.push_back(readyId);
      break;
    }
  }
  for (auto id : needRemove) {
    readyToFlushTrancIds_.erase(id);
  }

  flushedTrancIds_ = compressSet<uint64_t>(flushedTrancIds_);
}

uint64_t TranManager::getNextTransactionId() {
  return nextTransactionId_.fetch_add(1);
}

std::set<uint64_t> &TranManager::get_flushed_tranc_ids() {
  return flushedTrancIds_;
}

uint64_t TranManager::get_max_flushed_tranc_id() {
  // 需保证 size 至少为1
  return *flushedTrancIds_.rbegin();
}

uint64_t TranManager::get_checkpoint_tranc_id() {
  // 需保证 size 至少为1
  return *flushedTrancIds_.begin();
}

std::shared_ptr<TranContext>
TranManager::new_tranc(const IsolationLevel &isolation_level) {
  spdlog::debug("TranManager--new_tranc(): Creating new transaction with "
                "isolation level={}",
                static_cast<int>(isolation_level));

  // 获取锁
  std::unique_lock<std::mutex> lock(mutex_);

  auto tranc_id = getNextTransactionId();
  activeTrans_[tranc_id] = std::make_shared<TranContext>(
      tranc_id, engine_, shared_from_this(), isolation_level);

  spdlog::debug("TranManager--new_tranc(): Created transaction ID={} with "
                "isolation level={}",
                tranc_id, static_cast<int>(isolation_level));

  return activeTrans_[tranc_id];
}
std::string TranManager::get_tranc_id_file_path() {
  if (data_dir_.empty()) {
    data_dir_ = "./";
  }
  return data_dir_ + "/tranc_id";
}

std::map<uint64_t, std::vector<Record>> TranManager::check_recover() {
  spdlog::info("TranManager--check_recover(): Starting recovery from WAL");

  std::map<uint64_t, std::vector<Record>> wal_records =
      WAL::recover(data_dir_, *flushedTrancIds_.begin());

  spdlog::info("TranManager--check_recover(): Recovered {} transactions",
               wal_records.size());

  return wal_records;
}

bool TranManager::write_to_wal(const std::vector<Record> &records) {
  spdlog::trace("TranManager--write_to_wal(): Writing {} records to WAL",
                records.size());

  try {
    wal->log(records, true);
  } catch (const std::exception &e) {
    spdlog::error("TranManager--write_to_wal(): Exception occurred: {}",
                  e.what());

    return false;
  }

  spdlog::trace(
      "TranManager--write_to_wal(): Successfully wrote {} records to WAL",
      records.size());

  return true;
}

// void TranManager::flusher() {
//   while (flush_thread_running_.load()) {
//     std::this_thread::sleep_for(std::chrono::seconds(1));
//     write_tranc_id_file();
//   }
// }
} // namespace tiny_lsm