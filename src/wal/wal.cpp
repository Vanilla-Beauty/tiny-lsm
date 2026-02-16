// src/wal/wal.cpp

#include "../../include/wal/wal.h"
#include "spdlog/spdlog.h"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

namespace tiny_lsm {

// 从零开始的初始化流程
WAL::WAL(const std::string &log_dir, size_t buffer_size,
         uint64_t checkpoint_tranc_id, uint64_t clean_interval,
         uint64_t file_size_limit)
    : buffer_size_(buffer_size), checkpoint_tranc_id_(checkpoint_tranc_id),
      stop_cleaner_(false), clean_interval_(clean_interval),
      file_size_limit_(file_size_limit) {
  active_log_path_ = log_dir + "/wal.0";
  log_file_ = FileObj::open(active_log_path_, true);

  cleaner_thread_ = std::thread(&WAL::cleaner, this);
}

WAL::~WAL() {
  // 先将缓冲区所有内容强制刷入
  log({}, true);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_cleaner_ = true;
  }

  // 确保线程被正确唤醒并退出
  if (cleaner_thread_.joinable()) {
    cleaner_thread_.join();
  }

  // 显式关闭文件
  log_file_.close(); // 显式关闭文件
}

std::map<uint64_t, std::vector<Record>>
WAL::recover(const std::string &log_dir, uint64_t checkpoint_tranc_id) {
  std::map<uint64_t, std::vector<Record>> tranc_records{};

  // 引擎启动时判断
  if (!std::filesystem::exists(log_dir)) {
    return tranc_records;
  }

  // 遍历log_dir下的所有文件
  std::vector<std::string> wal_paths;
  for (const auto &entry : std::filesystem::directory_iterator(log_dir)) {
    if (entry.is_regular_file()) {
      // 获取/符号后的文件名
      std::string filename = entry.path().filename().string();
      if (filename.substr(0, 4) != "wal.") {
        continue;
      }

      wal_paths.push_back(entry.path().string());
    }
  }

  // 按照seq升序排序
  std::sort(wal_paths.begin(), wal_paths.end(),
            [](const std::string &a, const std::string &b) {
              auto a_seq_str = a.substr(a.find_last_of(".") + 1);
              auto b_seq_str = b.substr(b.find_last_of(".") + 1);
              return std::stoi(a_seq_str) < std::stoi(b_seq_str);
            });

  // 读取所有的记录
  for (const auto &wal_path : wal_paths) {
    auto wal_file = FileObj::open(wal_path, false);
    auto wal_records_slice = wal_file.read_to_slice(0, wal_file.size());
    auto records = Record::decode(wal_records_slice);
    for (const auto &record : records) {
      if (record.getTrancId() > checkpoint_tranc_id) {
        // 如果记录的 tranc_id 大于 checkpoint_tranc_id, 才需要尝试恢复
        tranc_records[record.getTrancId()].push_back(record);
      }
    }
  }

  return tranc_records;
}

// commit 时 强制写入
void WAL::flush() { std::lock_guard<std::mutex> lock(mutex_); }

void WAL::set_checkpoint_tranc_id(uint64_t checkpoint_tranc_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  checkpoint_tranc_id_ = checkpoint_tranc_id;
}

void WAL::log(const std::vector<Record> &records, bool force_flush) {
  std::unique_lock<std::mutex> lock(mutex_);

  // 将 records 的所有记录添加到 log_buffer_
  for (const auto &record : records) {
    log_buffer_.push_back(record);
  }

  if (log_buffer_.size() < buffer_size_ && !force_flush) {
    // 如果 log_buffer_ 的大小小于 buffer_size_ 且 force_flush 为 false,
    // 不进行写入
    return;
  }

  // 否则写入 wal 文件
  auto pre_buffer = std::move(log_buffer_);
  for (const auto &record : pre_buffer) {
    std::vector<uint8_t> encoded_record = record.encode();
    log_file_.append(encoded_record);
  }
  if (!log_file_.sync()) {
    // 确保日志立即写入磁盘
    throw std::runtime_error("Failed to sync WAL file");
  }

  auto cur_file_size = log_file_.size();
  if (cur_file_size > file_size_limit_) {
    reset_file();
  }
}

void WAL::cleaner() {
  while (true) {
    {
      // 睡眠 clean_interval_ s
      std::this_thread::sleep_for(std::chrono::seconds(clean_interval_));
      if (stop_cleaner_) {
        break;
      }
      cleanWALFile();
    }
  }
}

void WAL::cleanWALFile() {
  // 遍历log_file_所在的文件夹
  std::string dir_path;

  std::unique_lock<std::mutex> lock(mutex_); // 只在获取当前文件路径时获取锁
  if (active_log_path_.find("/") != std::string::npos) {
    dir_path =
        active_log_path_.substr(0, active_log_path_.find_last_of("/")) + "/";
  } else {
    dir_path = "./";
  }
  lock.unlock();

  // wal文件格式为:
  // wal.seq

  std::vector<std::pair<size_t, std::string>> wal_paths;

  for (const auto &entry : std::filesystem::directory_iterator(dir_path)) {
    if (entry.is_regular_file() &&
        entry.path().filename().string().substr(0, 4) == "wal.") {
      std::string filename = entry.path().filename().string();
      size_t dot_pos = filename.find_last_of(".");
      std::string seq_str = filename.substr(dot_pos + 1);
      uint64_t seq = std::stoull(seq_str);
      wal_paths.push_back({seq, entry.path().string()});
    }
  }

  // 按照seq升序排序
  std::sort(wal_paths.begin(), wal_paths.end(),
            [](const std::pair<size_t, std::string> &a,
               const std::pair<size_t, std::string> &b) {
              return a.first < b.first;
            });

  // 判断是否可以删除
  std::vector<FileObj> del_paths;
  for (int idx = 0; idx < wal_paths.size() - 1; idx++) {
    auto cur_path = wal_paths[idx].second;
    auto cur_file = FileObj::open(cur_path, false);
    // 遍历文件记录, 读取所有的tranc_id,
    // 判断是否都小于等于checkpoint_tranc_id_
    size_t offset = 0;
    bool has_unfinished = false;
    while (offset + sizeof(uint16_t) < cur_file.size()) {
      uint16_t record_size = cur_file.read_uint16(offset);
      uint64_t tranc_id = cur_file.read_uint64(offset + sizeof(uint16_t));
      if (tranc_id > checkpoint_tranc_id_) {
        has_unfinished = true;
        break;
      }
      offset += record_size;
    }
    if (!has_unfinished) {
      del_paths.push_back(std::move(cur_file));
    }
  }

  for (auto &del_file : del_paths) {
    del_file.del_file();
  }
}

void WAL::reset_file() {
  // wal文件格式为:
  // wal.seq
  // 当当前wal文件容量超出阈值后, 创建新的文件, 将seq自增

  auto old_path = active_log_path_;
  // 字符串处理获取seq
  auto seq = std::stoi(old_path.substr(old_path.find_last_of(".") + 1));
  seq++;

  active_log_path_ = old_path.substr(0, old_path.find_last_of(".")) + "." +
                     std::to_string(seq);

  // 创建新的文件
  // ? 如果不注释下面这行, debug 模式下 test_wal 能通过但 release 模式下报错
  // log_file_.~FileObj();
  log_file_ = FileObj::create_and_write(active_log_path_, {});
}
} // namespace tiny_lsm