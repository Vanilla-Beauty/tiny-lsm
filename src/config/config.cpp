#include "config/config.h"
#include "spdlog/spdlog.h"
#include <iostream>
#include <toml.hpp>

namespace tiny_lsm {

// Private helper to set all default values
void TomlConfig::setDefaultValues() {
  // --- LSM Core ---
  lsm_tol_mem_size_limit_ = 67108864; // Default: 64 * 1024 * 1024
  lsm_per_mem_size_limit_ = 4194304;  // Default: 4 * 1024 * 1024
  lsm_block_size_ = 32768;            // Default: 32 * 1024
  lsm_sst_level_ratio_ = 4;           // Default: 4

  // --- LSM Cache ---
  lsm_block_cache_capacity_ = 1024; // Default: 1024
  lsm_block_cache_k_ = 8;           // Default: 8

  // --- Redis Headers/Separators ---
  redis_expire_header_ = "REDIS_EXPIRE_";
  redis_hash_value_preffix_ = "REDIS_HASH_VALUE_";
  redis_field_prefix_ = "REDIS_FIELD_";
  redis_field_separator_ = '$';
  redis_list_separator_ = '#';
  redis_sorted_set_prefix_ = "REDIS_SORTED_SET_";
  redis_sorted_set_score_len_ = 32;
  redis_set_prefix_ = "REDIS_SET_";

  // --- Bloom Filter ---
  bloom_filter_expected_size_ = 65536;
  bloom_filter_expected_error_rate_ = 0.1;
}

//////////////////////////////////////////////////////////////////
// for debug/test
void TomlConfig::modify_lsm_tol_mem_size_limit(long long one) {
  lsm_tol_mem_size_limit_ = one;
}

void TomlConfig::modify_lsm_block_size(int one) { lsm_block_size_ = one; }

void TomlConfig::modify_lsm_per_mem_size_limit(long long one) {
  lsm_per_mem_size_limit_ = one;
}
//////////////////////////////////////////////////////////////////

// Constructor implementation
TomlConfig::TomlConfig(const std::string &filePath)
    : config_file_path_(filePath) {
  // Initialize member variables with default values upon creation
  if (filePath.empty()) {
    setDefaultValues();
  } else {
    // Load configuration from the specified file
    try {
      loadFromFile(filePath);
    } catch (const std::exception &err) {
      std::cerr << "Error loading configuration: " << err.what() << std::endl;
      setDefaultValues();
    }
  }
}

// Method to load configuration from a TOML file implementation
bool TomlConfig::loadFromFile(const std::string &filePath) {
  // Reset to defaults before loading to ensure a clean state
  setDefaultValues();

  try {
    // Use toml::parse_file to load and parse the TOML file
    auto config = toml::parse(filePath);

    // --- Load LSM Core ---
    // Use value_or to get the value or the current default if the key or table
    // is missing
    auto core_config = config["lsm"]["core"];

    lsm_tol_mem_size_limit_ =
        core_config.at("LSM_TOL_MEM_SIZE_LIMIT").as_integer();
    lsm_per_mem_size_limit_ =
        core_config.at("LSM_PER_MEM_SIZE_LIMIT").as_integer();
    lsm_block_size_ = core_config.at("LSM_BLOCK_SIZE").as_integer();
    lsm_sst_level_ratio_ = core_config.at("LSM_SST_LEVEL_RATIO").as_integer();

    // --- Load LSM Cache ---
    auto cache_config = config["lsm"]["cache"];

    lsm_block_cache_capacity_ =
        cache_config.at("LSM_BLOCK_CACHE_CAPACITY").as_integer();
    lsm_block_cache_k_ = cache_config.at("LSM_BLOCK_CACHE_K").as_integer();

    // --- Load Redis Headers/Separators ---
    auto redis_config = config["redis"];

    redis_expire_header_ = redis_config.at("REDIS_EXPIRE_HEADER").as_string();
    redis_hash_value_preffix_ =
        redis_config.at("REDIS_HASH_VALUE_PREFFIX").as_string();
    redis_field_prefix_ = redis_config.at("REDIS_FIELD_PREFIX").as_string();

    redis_field_separator_ =
        redis_config.at("REDIS_FIELD_SEPARATOR").as_string()[0];

    redis_list_separator_ =
        redis_config.at("REDIS_LIST_SEPARATOR").as_string()[0];

    redis_sorted_set_prefix_ =
        redis_config.at("REDIS_SORTED_SET_PREFIX").as_string();

    redis_sorted_set_score_len_ =
        redis_config.at("REDIS_SORTED_SET_SCORE_LEN").as_integer();

    redis_set_prefix_ = redis_config.at("REDIS_SET_PREFIX").as_string();

    // --- Load Bloom Filter ---
    auto bloom_config = config["bloom_filter"];

    bloom_filter_expected_size_ =
        bloom_config.at("BLOOM_FILTER_EXPECTED_SIZE").as_integer();

    bloom_filter_expected_error_rate_ =
        bloom_config.at("BLOOM_FILTER_EXPECTED_ERROR_RATE").as_floating();

    spdlog::info("Configuration loaded successfully from {}", filePath);
    return true;

  } catch (const std::exception &err) {
    // Handle other potential exceptions
    std::cerr
        << "An unexpected error occurred while loading configuration from "
        << filePath << ": " << err.what() << std::endl;
    // Configuration remains at default values
    return false; // Loading failed
  }
}

// --- Getter Methods Implementations ---

long long TomlConfig::getLsmTolMemSizeLimit() const {
  return lsm_tol_mem_size_limit_;
}
long long TomlConfig::getLsmPerMemSizeLimit() const {
  return lsm_per_mem_size_limit_;
}
int TomlConfig::getLsmBlockSize() const { return lsm_block_size_; }
int TomlConfig::getLsmSstLevelRatio() const { return lsm_sst_level_ratio_; }

int TomlConfig::getLsmBlockCacheCapacity() const {
  return lsm_block_cache_capacity_;
}
int TomlConfig::getLsmBlockCacheK() const { return lsm_block_cache_k_; }

const std::string &TomlConfig::getRedisExpireHeader() const {
  return redis_expire_header_;
}
const std::string &TomlConfig::getRedisHashValuePreffix() const {
  return redis_hash_value_preffix_;
}
const std::string &TomlConfig::getRedisFieldPrefix() const {
  return redis_field_prefix_;
}
char TomlConfig::getRedisFieldSeparator() const {
  return redis_field_separator_;
}
char TomlConfig::getRedisListSeparator() const { return redis_list_separator_; }
const std::string &TomlConfig::getRedisSortedSetPrefix() const {
  return redis_sorted_set_prefix_;
}
int TomlConfig::getRedisSortedSetScoreLen() const {
  return redis_sorted_set_score_len_;
}
const std::string &TomlConfig::getRedisSetPrefix() const {
  return redis_set_prefix_;
}

int TomlConfig::getBloomFilterExpectedSize() const {
  return bloom_filter_expected_size_;
}
double TomlConfig::getBloomFilterExpectedErrorRate() const {
  return bloom_filter_expected_error_rate_;
}

const TomlConfig &TomlConfig::getInstance(const std::string &config_path) {
  // 静态实例确保只创建一次
  static const TomlConfig instance([&]() -> std::string {
    // 检查文件是否存在且可读
    std::ifstream file(config_path);
    if (file.good()) {
      return config_path;
    } else {
      std::cerr << "Config file not found or unreadable: " << config_path
                << ", using default configuration" << std::endl;
      return "config.toml"; // 使用空路径初始化默认配置
    }
  }());
  return instance;
}

TomlConfig::~TomlConfig() {
  // 如果配置文件不存在，则持久化当前配置
  if (!config_file_path_.empty()) {
    std::ifstream file(config_file_path_);
    if (!file.good()) {
      saveToFile(config_file_path_);
    }
  }
}

// 在config.cpp中添加saveToFile方法实现
bool TomlConfig::saveToFile(const std::string &filePath) {
  try {
    // 创建TOML表格结构
    toml::table config;

    // --- LSM Core ---
    config["lsm"]["core"]["LSM_TOL_MEM_SIZE_LIMIT"] = lsm_tol_mem_size_limit_;
    config["lsm"]["core"]["LSM_PER_MEM_SIZE_LIMIT"] = lsm_per_mem_size_limit_;
    config["lsm"]["core"]["LSM_BLOCK_SIZE"] = lsm_block_size_;
    config["lsm"]["core"]["LSM_SST_LEVEL_RATIO"] = lsm_sst_level_ratio_;

    // --- LSM Cache ---
    config["lsm"]["cache"]["LSM_BLOCK_CACHE_CAPACITY"] =
        lsm_block_cache_capacity_;
    config["lsm"]["cache"]["LSM_BLOCK_CACHE_K"] = lsm_block_cache_k_;

    // --- Redis Headers/Separators ---
    config["redis"]["REDIS_EXPIRE_HEADER"] = redis_expire_header_;
    config["redis"]["REDIS_HASH_VALUE_PREFFIX"] = redis_hash_value_preffix_;
    config["redis"]["REDIS_FIELD_PREFIX"] = redis_field_prefix_;
    config["redis"]["REDIS_FIELD_SEPARATOR"] =
        std::string(1, redis_field_separator_);
    config["redis"]["REDIS_LIST_SEPARATOR"] =
        std::string(1, redis_list_separator_);
    config["redis"]["REDIS_SORTED_SET_PREFIX"] = redis_sorted_set_prefix_;
    config["redis"]["REDIS_SORTED_SET_SCORE_LEN"] = redis_sorted_set_score_len_;
    config["redis"]["REDIS_SET_PREFIX"] = redis_set_prefix_;

    // --- Bloom Filter ---
    config["bloom_filter"]["BLOOM_FILTER_EXPECTED_SIZE"] =
        bloom_filter_expected_size_;
    config["bloom_filter"]["BLOOM_FILTER_EXPECTED_ERROR_RATE"] =
        bloom_filter_expected_error_rate_;

    // 写入到文件
    std::ofstream outFile(filePath);
    if (outFile.is_open()) {
      const toml::value config_value(config);
      // 使用 toml::format 将 table 转换为字符串形式
      std::string config_str = toml::format(config_value);
      outFile << config_str;
      outFile.close();
      spdlog::info("Configuration saved successfully to {}", filePath);
      return true;
    } else {
      spdlog::error("Failed to open file for writing: {}", filePath);

      return false;
    }
  } catch (const std::exception &err) {
    spdlog::error("An error occurred while saving configuration to {}: {}",
                  filePath, err.what());
    return false;
  }
}
} // namespace tiny_lsm