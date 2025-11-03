#include "../../include/sst/concact_iterator.h"

namespace tiny_lsm {

ConcactIterator::ConcactIterator(std::vector<std::shared_ptr<SST>> ssts,
                                 uint64_t tranc_id)
    : ssts(ssts), cur_iter(nullptr, tranc_id), cur_idx(0),
      max_tranc_id_(tranc_id) {
  if (!this->ssts.empty()) {
    cur_iter = ssts[0]->begin(max_tranc_id_);
  }
}

BaseIterator &ConcactIterator::operator++() {
  ++cur_iter;

  if (cur_iter.is_end() || !cur_iter.is_valid()) {
    cur_idx++;
    if (cur_idx < ssts.size()) {
      cur_iter = ssts[cur_idx]->begin(max_tranc_id_);
    } else {
      cur_iter = SstIterator(nullptr, max_tranc_id_);
    }
  }
  return *this;
}

bool ConcactIterator::operator==(const BaseIterator &other) const {
  if (other.get_type() != IteratorType::ConcactIterator) {
    return false;
  }
  auto other2 = dynamic_cast<const ConcactIterator &>(other);
  return other2.cur_iter == cur_iter;
}

bool ConcactIterator::operator!=(const BaseIterator &other) const {
  return !(*this == other);
}

ConcactIterator::value_type ConcactIterator::operator*() const {
  return *cur_iter;
}

IteratorType ConcactIterator::get_type() const {
  return IteratorType::ConcactIterator;
}

uint64_t ConcactIterator::get_tranc_id() const { return max_tranc_id_; }

bool ConcactIterator::is_end() const {
  return cur_iter.is_end() || !cur_iter.is_valid();
}

bool ConcactIterator::is_valid() const {
  return !cur_iter.is_end() && cur_iter.is_valid();
}

ConcactIterator::pointer ConcactIterator::operator->() const {
  return cur_iter.operator->();
}

std::string ConcactIterator::key() { return cur_iter.key(); }

std::string ConcactIterator::value() { return cur_iter.value(); }
} // namespace tiny_lsm
