#pragma once

#include "sst.h"
#include "sst_iterator.h"
#include <memory>
#include <vector>

namespace tiny_lsm {
class ConcactIterator : public BaseIterator {
private:
  SstIterator cur_iter;
  size_t cur_idx; // 不是真实的sst_id, 而是在需要连接的sst数组中的索引
  std::vector<std::shared_ptr<SST>> ssts;
  uint64_t max_tranc_id_;
  bool keep_all_versions_ = false;

public:
  ConcactIterator(std::vector<std::shared_ptr<SST>> ssts, uint64_t tranc_id,
                  bool keep_all_versions = false);

  std::string key();
  std::string value();

  virtual BaseIterator &operator++() override;
  virtual bool operator==(const BaseIterator &other) const override;
  virtual bool operator!=(const BaseIterator &other) const override;
  virtual value_type operator*() const override;
  virtual IteratorType get_type() const override;
  virtual uint64_t get_tranc_id() const override;
  virtual bool is_end() const override;
  virtual bool is_valid() const override;

  pointer operator->() const;
};
} // namespace tiny_lsm
