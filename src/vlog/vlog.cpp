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
    auto vlog = std::make_shared<VLog>();
    vlog->path_ = path;

    // If the file does not exist, create an empty one first so that
    // FileObj::open(path, false) succeeds.
    if (!std::filesystem::exists(path)) {
        std::ofstream f(path, std::ios::binary);
    }
    // Open WITHOUT truncation to preserve existing records on reopen
    vlog->file_ = FileObj::open(path, false);
    spdlog::info("VLog::open: opened vlog at {}, size={}", path,
                 vlog->file_.size());
    return vlog;
}

uint64_t VLog::append(const std::string &key, const std::string &value) {
    std::lock_guard<std::mutex> lock(append_mtx_);

    uint64_t offset = file_.size();

    // Build the record buffer
    // [key_len:2][key:key_len][val_len:4][value:val_len][crc32:4]
    uint16_t key_len = static_cast<uint16_t>(key.size());
    uint32_t val_len = static_cast<uint32_t>(value.size());
    size_t record_size = sizeof(uint16_t) + key_len + sizeof(uint32_t) +
                         val_len + sizeof(uint32_t);

    std::vector<uint8_t> buf(record_size);
    size_t pos = 0;

    memcpy(buf.data() + pos, &key_len, sizeof(uint16_t));
    pos += sizeof(uint16_t);

    memcpy(buf.data() + pos, key.data(), key_len);
    pos += key_len;

    memcpy(buf.data() + pos, &val_len, sizeof(uint32_t));
    pos += sizeof(uint32_t);

    memcpy(buf.data() + pos, value.data(), val_len);
    pos += val_len;

    // CRC covers everything before the checksum field
    uint32_t crc = crc32_compute(buf.data(), pos);
    memcpy(buf.data() + pos, &crc, sizeof(uint32_t));

    if (!file_.append(buf)) {
        throw std::runtime_error("VLog::append: failed to write record");
    }

    spdlog::trace("VLog::append: wrote record at offset={}, key_len={}, "
                  "val_len={}",
                  offset, key_len, val_len);

    return offset;
}

std::string VLog::read_value(uint64_t offset, uint32_t value_size) {
    // Record layout: [key_len:2][key:key_len][val_len:4][value:val_len][crc:4]
    // First read key_len to skip past the key
    auto key_len_bytes = file_.read_to_slice(offset, sizeof(uint16_t));
    uint16_t key_len = 0;
    memcpy(&key_len, key_len_bytes.data(), sizeof(uint16_t));

    // value starts at: offset + 2 + key_len + 4
    uint64_t val_offset =
        offset + sizeof(uint16_t) + key_len + sizeof(uint32_t);

    auto val_bytes = file_.read_to_slice(val_offset, value_size);
    return std::string(val_bytes.begin(), val_bytes.end());
}

uint64_t VLog::tail_offset() const {
    return static_cast<uint64_t>(file_.size());
}

void VLog::sync() { file_.sync(); }

void VLog::del_vlog() { file_.del_file(); }

} // namespace tiny_lsm
