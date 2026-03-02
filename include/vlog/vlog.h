#pragma once

#include "utils/files.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace tiny_lsm {

/**
 * Value Log (VLog) for WiscKey-style value separation.
 *
 * File format (per record, append-only):
 * [key_len : uint16]
 * [key     : key_len bytes]
 * [val_len : uint32]
 * [value   : val_len bytes]
 * [crc32   : uint32]   <- covers all above fields
 *
 * Reference stored in SST block value field (12 bytes):
 * [vlog_offset: uint64]  <- byte offset of record start in vlog.data
 * [value_size : uint32]  <- original value length
 */
class VLog {
public:
    static std::shared_ptr<VLog> open(const std::string &path);

    // Append a key-value record, returns the byte offset of the new record
    uint64_t append(const std::string &key, const std::string &value);

    // Read the value of a record at the given offset
    std::string read_value(uint64_t offset, uint32_t value_size);

    // Return the current end offset (== file size)
    uint64_t tail_offset() const;

    // Sync the file to disk
    void sync();

    // Delete the vlog file
    void del_vlog();

private:
    FileObj file_;
    std::string path_;
    mutable std::mutex append_mtx_;
};

} // namespace tiny_lsm
