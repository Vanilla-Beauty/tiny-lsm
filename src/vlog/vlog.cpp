#include "vlog/vlog.h"
#include "spdlog/spdlog.h"
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace tiny_lsm {

// Simple CRC32 implementation (polynomial 0xEDB88320)
static uint32_t crc32_compute(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFF;
}

std::shared_ptr<VLog> VLog::open(const std::string &path) {
    // TODO: Lab 6.1 打开或创建 VLog 文件
    // ? 1. 若文件不存在则创建空文件 (std::ofstream)
    // ? 2. 用 FileObj::open(path, false) 打开 (不截断, 保留已有记录)
    // ? 3. 记录 path_ 和 file_
    auto vlog = std::make_shared<VLog>();
    vlog->path_ = path;
    if (!std::filesystem::exists(path)) {
        std::ofstream f(path, std::ios::binary);
    }
    vlog->file_ = FileObj::open(path, false);
    spdlog::info("VLog::open: opened vlog at {}, size={}", path,
                 vlog->file_.size());
    return vlog;
}

uint64_t VLog::append(const std::string &key, const std::string &value) {
    // TODO: Lab 6.1 追加一条 KV 记录到 VLog, 返回记录起始偏移量
    // ? 记录格式: [key_len:uint16][key][val_len:uint32][value][crc32:uint32]
    // ? CRC32 覆盖除自身之外的所有字段
    // ? 注意: 需要加 append_mtx_ 互斥锁
    // ? offset = file_.size() (追加前的文件大小即为本次记录的起始位置)
    std::lock_guard<std::mutex> lock(append_mtx_);
    return 0;
}

std::string VLog::read_value(uint64_t offset, uint32_t value_size) {
    // TODO: Lab 6.1 从指定 offset 读取 value
    // ? 记录布局: [key_len:2][key:key_len][val_len:4][value:val_len][crc:4]
    // ? 先读 key_len 以跳过 key, 再定位 value 起始位置
    // ? value 起始 = offset + 2 + key_len + 4
    return "";
}

uint64_t VLog::tail_offset() const {
    return static_cast<uint64_t>(file_.size());
}

void VLog::sync() { file_.sync(); }

void VLog::del_vlog() { file_.del_file(); }

} // namespace tiny_lsm
