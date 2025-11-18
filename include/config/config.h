#pragma once

#include <string>
#include <vector>

// Include the necessary toml11 header
// Assuming a common setup where you include the main header:

namespace tiny_lsm {

class TomlConfig {
private:
  std::string config_file_path_; // 记录配置文件路径

  // --- LSM Core ---
  long long lsm_tol_mem_size_limit_;
  long long lsm_per_mem_size_limit_;
  int lsm_block_size_;
  int lsm_sst_level_ratio_;

  // --- LSM Cache ---
  int lsm_block_cache_capacity_;
  int lsm_block_cache_k_;

  // --- Redis Headers/Separators ---
  std::string redis_expire_header_;
  std::string redis_hash_value_preffix_;
  std::string redis_field_prefix_;
  char redis_field_separator_;
  char redis_list_separator_;
  std::string redis_sorted_set_prefix_;
  int redis_sorted_set_score_len_;
  std::string redis_set_prefix_;

  // --- Bloom Filter ---
  int bloom_filter_expected_size_;
  double bloom_filter_expected_error_rate_;

  // Private method to set default values
  void setDefaultValues();

  // Constructor
  TomlConfig(const std::string &filePath);

  // Method to load configuration from a TOML file
  // Returns true on success, false on failure (e.g., file not found, parse
  // error)
  bool loadFromFile(const std::string &filePath);

  bool saveToFile(const std::string &filePath);

public:
  ~TomlConfig();

  // --- Getter Methods ---
  // Declare all your getter methods here
  long long getLsmTolMemSizeLimit() const;
  long long getLsmPerMemSizeLimit() const;
  int getLsmBlockSize() const;
  int getLsmSstLevelRatio() const;

  int getLsmBlockCacheCapacity() const;
  int getLsmBlockCacheK() const;

  const std::string &getRedisExpireHeader() const;
  const std::string &getRedisHashValuePreffix() const;
  const std::string &getRedisFieldPrefix() const;
  char getRedisFieldSeparator() const;
  char getRedisListSeparator() const;
  const std::string &getRedisSortedSetPrefix() const;
  int getRedisSortedSetScoreLen() const;
  const std::string &getRedisSetPrefix() const;

  int getBloomFilterExpectedSize() const;
  double getBloomFilterExpectedErrorRate() const;

  static const TomlConfig &
  getInstance(const std::string &config_path = "config.toml");
  // getInstance(
  //     const std::string &config_path =
  //     "/tmp/tinilsmpr/tiny-lsm/config.toml");

  // for debug
  void modify_lsm_tol_mem_size_limit(long long one);
  void modify_lsm_per_mem_size_limit(long long one);
  void modify_lsm_block_size(int one);
};
} // namespace tiny_lsm