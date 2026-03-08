// src/wal/wal.cpp

#include "wal/wal.h"
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
  // TODO: Lab 5.4 实现WAL的初始化流程
  // ? 1. 设置 active_log_path_ = log_dir + "/wal.0"
  // ? 2. 用 FileObj::open(active_log_path_, true) 打开或创建 WAL 文件
  // ? 3. 启动清理线程: cleaner_thread_ = std::thread(&WAL::cleaner, this)
}

WAL::~WAL() {
  // TODO: Lab 5.4 实现WAL的清理流程
  // ? 1. 强制将缓冲区所有内容刷盘: log({}, true)
  // ? 2. 加锁设置 stop_cleaner_ = true
  // ? 3. 等待清理线程结束: cleaner_thread_.join()
  // ? 4. 显式关闭文件: log_file_.close()
}

std::map<uint64_t, std::vector<Record>>
WAL::recover(const std::string &log_dir, uint64_t checkpoint_tranc_id) {
  // TODO: Lab 5.5 检查需要重放的WAL日志
  // ? 1. 若 log_dir 不存在则直接返回空 map
  // ? 2. 遍历目录找到所有 "wal." 前缀的文件
  // ? 3. 按 seq 升序排序
  // ? 4. 逐文件读取所有 Record (Record::decode)
  // ?    仅保留 tranc_id > checkpoint_tranc_id 的记录
  // ? 5. 返回 map<tranc_id, records>
  return {};
}

// commit 时强制写入
void WAL::flush() {
  // TODO: Lab 5.4 强制刷盘
  // ? 当前实现仅需加锁保证当前写入完成即可
  // ? 若 log() 中使用了缓冲区, 这里需要确保缓冲区内容全部落盘
  std::lock_guard<std::mutex> lock(mutex_);
}

void WAL::set_checkpoint_tranc_id(uint64_t checkpoint_tranc_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  checkpoint_tranc_id_ = checkpoint_tranc_id;
}

void WAL::log(const std::vector<Record> &records, bool force_flush) {
  // TODO: Lab 5.4 实现WAL的写入流程
  // ? 1. 加锁
  // ? 2. 将 records 追加到 log_buffer_
  // ? 3. 若 log_buffer_.size() < buffer_size_ 且 !force_flush 则直接返回
  // ? 4. 否则将 log_buffer_ 中所有记录编码并写入 log_file_ (record.encode())
  // ? 5. 调用 log_file_.sync() 确保落盘
  // ? 6. 若文件大小超过 file_size_limit_ 则调用 reset_file() 滚动日志文件
}

void WAL::cleaner() {
  // TODO: Lab 5.4 实现WAL的清理线程
  // ? 循环:
  // ?   1. sleep clean_interval_ 秒
  // ?   2. 若 stop_cleaner_ 为 true 则退出
  // ?   3. 调用 cleanWALFile() 清理已可以删除的旧 WAL 文件
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
  for (int idx = 0; idx < (int)wal_paths.size() - 1; idx++) {
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
  log_file_ = FileObj::create_and_write(active_log_path_, {});
}
} // namespace tiny_lsm
