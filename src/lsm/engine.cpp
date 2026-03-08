#include "lsm/engine.h"
#include "config/config.h"
#include "consts.h"
#include "logger/logger.h"
#include "lsm/level_iterator.h"
#include "spdlog/spdlog.h"
#include "sst/concact_iterator.h"
#include "sst/sst.h"
#include "sst/sst_iterator.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tiny_lsm {

// *********************** LSMEngine ***********************
LSMEngine::LSMEngine(std::string path) : data_dir(path) {
  // TODO: Lab 4.2 引擎初始化
  // ? 1. 初始化日志: init_spdlog_file()
  // ? 2. 初始化 block_cache (容量和 K 值从 TomlConfig 读取)
  // ? 3. 若目录不存在则创建
  // ? 4. 初始化 VLog: vlog_ = VLog::open(data_dir + "/vlog.data")
  // ? 5. 遍历目录加载所有已存在的 SST 文件:
  // ?    - 文件名格式: sst_{id}.{level}
  // ?    - 调用 SST::open 并记录到 ssts 和 level_sst_ids
  // ?    - 维护 next_sst_id 和 cur_max_level
  // ? 6. next_sst_id 自增
  // ? 7. 对各层 sst_id_list 排序; L0 层需要 reverse (越大的 id 越新, 优先查询)
  init_spdlog_file();
}

LSMEngine::~LSMEngine() = default;

std::optional<std::pair<std::string, uint64_t>>
LSMEngine::get(const std::string &key, uint64_t tranc_id) {
  // TODO: Lab 4.2 查询
  // ? 1. 先查 memtable.get(key, tranc_id), 命中则返回 (value 非空) 或 nullopt (value 为空=删除)
  // ? 2. 加 ssts_mtx 读锁, 遍历 L0 的 sst_ids (越大越新), 通过 sst->get() 查询
  // ? 3. 遍历 L1 及以上各层, 对每层做二分查找确定 key 所在的 SST 文件
  // ? 注意: value 为空字符串表示 key 已被删除, 此时返回 nullopt
  return std::nullopt;
}

std::vector<
    std::pair<std::string, std::optional<std::pair<std::string, uint64_t>>>>
LSMEngine::get_batch(const std::vector<std::string> &keys, uint64_t tranc_id) {
  // TODO: Lab 4.2 批量查询
  // ? 1. 先从 memtable 批量查询: memtable.get_batch(keys, tranc_id)
  // ? 2. 若有未命中项, 加读锁后依次查 L0 各 SST 文件
  // ? 3. 若仍有未命中, 对各高层 SST 做二分查找补全结果
  return {};
}

std::optional<std::pair<std::string, uint64_t>>
LSMEngine::sst_get_(const std::string &key, uint64_t tranc_id) {
  // TODO: Lab 4.2 sst 内部查询 (不查 memtable)
  // ? 逻辑与 get() 的 SST 部分相同, 先 L0 后 L1+
  return std::nullopt;
}

uint64_t LSMEngine::put(const std::string &key, const std::string &value,
                        uint64_t tranc_id) {
  // TODO: Lab 4.1 插入
  // ? 调用 memtable.put(key, value, tranc_id)
  // ? 若 memtable 总大小 >= LsmTolMemSizeLimit 则调用 flush() 并返回其结果
  // ? 否则返回 0
  return 0;
}

uint64_t LSMEngine::put_batch(
    const std::vector<std::pair<std::string, std::string>> &kvs,
    uint64_t tranc_id) {
  // TODO: Lab 4.1 批量插入
  // ? 调用 memtable.put_batch(kvs, tranc_id)
  // ? 若超限则 flush() 并返回其结果
  return 0;
}

uint64_t LSMEngine::remove(const std::string &key, uint64_t tranc_id) {
  // TODO: Lab 4.1 删除
  // ? 在 LSM 中，删除实际上是插入一个空值
  // ? 调用 memtable.remove(key, tranc_id)
  // ? 若超限则 flush() 并返回其结果
  return 0;
}

uint64_t LSMEngine::remove_batch(const std::vector<std::string> &keys,
                                 uint64_t tranc_id) {
  // TODO: Lab 4.1 批量删除
  // ? 调用 memtable.remove_batch(keys, tranc_id)
  // ? 若超限则 flush() 并返回其结果
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

  // Re-create the vlog so new writes go to a fresh file
  if (vlog_) {
    vlog_->del_vlog();
    vlog_ = VLog::open(data_dir + "/vlog.data");
  }
}

uint64_t LSMEngine::flush() {
  // TODO: Lab 4.1 刷盘形成sst文件
  // ? 0. 若 memtable 为空直接返回 0
  // ? 1. 加 ssts_mtx 写锁
  // ? 2. 若 L0 层 SST 数量 >= LsmSstLevelRatio, 先触发 full_compact(0)
  // ? 3. 分配新的 sst_id: next_sst_id++
  // ? 4. 构造 SSTBuilder:
  // ?    - 若 WiscKey 阈值 > 0 且 vlog_ 存在, 使用 WiscKey 模式的构造函数
  // ?    - 否则使用普通模式
  // ? 5. 调用 memtable.flush_last() 生成 SST 文件
  // ? 6. 更新 ssts 和 level_sst_ids[0] (push_front 保证新的在前)
  // ? 7. 将 flushed_tranc_ids 通知给 tran_manager
  // ? 8. 返回新 SST 的 max_tranc_id
  return 0;
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
  // TODO: Lab 4.7 谓词查询
  // ? 1. 从 memtable 查询: memtable.iters_monotony_predicate(tranc_id, predicate)
  // ? 2. 遍历所有 SST, 对每个 SST 调用 sst_iters_monotony_predicate
  // ?    将所有结果合并到 item_vec (注意过滤事务可见性和相同 key 只保留最新版本)
  // ? 3. 构造 TwoMergeIterator 合并 memtable 结果和 sst 结果
  // ? 4. 若均为空返回 nullopt
  return std::nullopt;
}

Level_Iterator LSMEngine::begin(uint64_t tranc_id) {
  // TODO: Lab 4.7
  // ? 返回 Level_Iterator(shared_from_this(), tranc_id)
  throw std::runtime_error("Not implemented");
}

Level_Iterator LSMEngine::end() {
  // TODO: Lab 4.7
  // ? 返回空的 Level_Iterator{}
  throw std::runtime_error("Not implemented");
}

void LSMEngine::full_compact(size_t src_level) {
  // TODO: Lab 4.5 负责完成整个 full compact
  // ? 1. 递归判断下一级 level 是否需要 compact (level_sst_ids[src_level+1].size() >= ratio)
  // ? 2. 根据 src_level 是否为 0 分别调用 full_l0_l1_compact 或 full_common_compact
  // ? 3. 删除旧 SST 文件并从 ssts/level_sst_ids 中移除记录
  // ? 4. 将新的 SST 加入 level_sst_ids[src_level+1] 并排序
  // ? 5. 更新 cur_max_level
}

std::vector<std::shared_ptr<SST>>
LSMEngine::full_l0_l1_compact(std::vector<size_t> &l0_ids,
                              std::vector<size_t> &l1_ids) {
  // TODO: Lab 4.5 负责完成 l0 和 l1 的 full compact
  // ? L0 各 SST 的 key 有重叠, 需要先通过 SstIterator::merge_sst_iterator 合并
  // ? 再用 TwoMergeIterator 与 L1 的 ConcactIterator 合并
  // ? 最后调用 gen_sst_from_iter 生成新的 SST 文件 (目标大小 = PerMemSizeLimit * SstLevelRatio)
  return {};
}

std::vector<std::shared_ptr<SST>>
LSMEngine::full_common_compact(std::vector<size_t> &lx_ids,
                               std::vector<size_t> &ly_ids, size_t level_y) {
  // TODO: Lab 4.5 负责完成其他相邻 level 的 full compact
  // ? Lx 和 Ly 都是有序不重叠的 SST, 直接用 ConcactIterator 遍历
  // ? 通过 TwoMergeIterator 合并后调用 gen_sst_from_iter
  return {};
}

std::vector<std::shared_ptr<SST>>
LSMEngine::gen_sst_from_iter(BaseIterator &iter, size_t target_sst_size,
                             size_t target_level) {
  // TODO: Lab 4.5 实现从迭代器构造新的 SST
  // ? 循环从迭代器取 key-value 写入 SSTBuilder
  // ? 当 estimated_size >= target_sst_size 时 (注意不能在相同 key 的不同版本之间切分)
  // ?   调用 builder.build() 生成 SST 并重置 builder
  // ? 迭代结束后若 builder 非空则再次 build
  // ? 注意: WiscKey 模式下需使用带 vlog 参数的 SSTBuilder 构造函数
  return {};
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
  // TODO: Lab 5.5 控制WAL重放与组件的初始化
  // ? 1. 绑定 tran_manager 与 engine: 互相 set
  // ? 2. 调用 tran_manager_->check_recover() 获取需要重放的事务记录
  // ? 3. 遍历返回的 map<tranc_id, records>:
  // ?    - 若该 tranc_id 已在 flushed_tranc_ids 中则跳过 (已刷盘无需重放)
  // ?    - 否则根据 record.getOperationType() 调用 engine->put() 或 engine->remove()
  // ? 4. 调用 tran_manager_->init_new_wal() 开启新的 WAL 文件准备接收新写入
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
