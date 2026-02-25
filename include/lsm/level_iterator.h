#pragma once
#include "iterator/iterator.h"
#include <memory>
#include <optional>
#include <shared_mutex>

namespace tiny_lsm {
class LSMEngine;

class Level_Iterator : public BaseIterator {
public:
  Level_Iterator() = default;
  Level_Iterator(std::shared_ptr<LSMEngine> engine_, uint64_t max_tranc_id);

  virtual BaseIterator &operator++() override;
  virtual bool operator==(const BaseIterator &other) const override;
  virtual bool operator!=(const BaseIterator &other) const override;
  virtual value_type operator*() const override;
  virtual IteratorType get_type() const override;
  virtual uint64_t get_tranc_id() const override;
  virtual bool is_end() const override;
  virtual bool is_valid() const override;

  BaseIterator::pointer operator->() const;

private:
  std::shared_ptr<LSMEngine> engine_;
  std::vector<std::shared_ptr<BaseIterator>> iter_vec;
  size_t cur_idx_;
  uint64_t max_tranc_id_;
  mutable std::optional<value_type> cached_value; // 缓存当前值
  std::shared_lock<std::shared_mutex> rlock_;

private:
  void update_current() const;
  std::pair<size_t, std::string> get_min_key_idx() const;
  void skip_key(const std::string &key);
};
} // namespace tiny_lsm
