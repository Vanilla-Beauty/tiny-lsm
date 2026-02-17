#include "iterator/iterator.h"
#include <tuple>
#include <vector>

namespace tiny_lsm {

// *************************** SearchItem ***************************
bool operator<(const SearchItem &a, const SearchItem &b) {
  if (a.key_ != b.key_) {
    return a.key_ < b.key_;
  }
  if (a.tranc_id_ > b.tranc_id_) {
    return true;
  }
  if (a.level_ < b.level_) {
    return true;
  }
  return a.idx_ < b.idx_;
}

bool operator>(const SearchItem &a, const SearchItem &b) {
  if (a.key_ != b.key_) {
    return a.key_ > b.key_;
  }
  if (a.tranc_id_ < b.tranc_id_) {
    return true;
  }
  if (a.level_ < b.level_) {
    return true;
  }
  return a.idx_ > b.idx_;
}

bool operator==(const SearchItem &a, const SearchItem &b) {
  return a.idx_ == b.idx_ && a.key_ == b.key_;
}

// *************************** HeapIterator ***************************
HeapIterator::HeapIterator(bool skip_delete) : skip_delete_(skip_delete) {
  // 默认构造函数
}
HeapIterator::HeapIterator(std::vector<SearchItem> item_vec,
                           uint64_t max_tranc_id, bool skip_delete)
    : max_tranc_id_(max_tranc_id), skip_delete_(skip_delete) {
  for (auto &item : item_vec) {

    items.push(item);
  }

  while (!top_value_legal()) {
    // 1. 先跳过事务 id 不可见的部分
    skip_by_tranc_id();

    if (!skip_delete_) {
      continue;
    }
    // 2. 跳过标记为删除的元素
    while (!items.empty() && items.top().value_.empty()) {
      // 如果当前元素的value为空，则说明该元素已经被删除，需要从优先队列中删除
      auto del_key = items.top().key_;
      while (!items.empty() && items.top().key_ == del_key) {
        items.pop();
      }
    }
  }
}

HeapIterator::pointer HeapIterator::operator->() const {
  update_current();
  return current.get();
}

HeapIterator::value_type HeapIterator::operator*() const {
  return std::make_pair(items.top().key_, items.top().value_);
}

BaseIterator &HeapIterator::operator++() {
  if (items.empty()) {
    return *this; // 处理空队列情况
  }

  auto old_item = items.top();
  items.pop();

  // 删除与旧元素key相同的元素
  while (!items.empty() && items.top().key_ == old_item.key_) {
    items.pop();
  }

  // 与构造函数相同, 下一个key中事务不可见部分和删除的元素需要跳过
  while (!top_value_legal()) {
    // 1. 先跳过事务 id 不可见的部分
    skip_by_tranc_id();

    if (!skip_delete_) {
      continue;
    }
    // 2. 跳过标记为删除的元素
    while (!items.empty() && items.top().value_.empty()) {
      // 如果当前元素的value为空，则说明该元素已经被删除，需要从优先队列中删除
      auto del_key = items.top().key_;
      while (!items.empty() && items.top().key_ == del_key) {
        items.pop();
      }
    }
  }

  return *this;
}

bool HeapIterator::operator==(const BaseIterator &other) const {
  if (other.get_type() != IteratorType::HeapIterator) {
    return false;
  }
  auto other2 = dynamic_cast<const HeapIterator &>(other);
  if (items.empty() && other2.items.empty()) {
    return true;
  }
  if (items.empty() || other2.items.empty()) {
    return false;
  }
  return items.top().key_ == other2.items.top().key_ &&
         items.top().value_ == other2.items.top().value_;
}

bool HeapIterator::operator!=(const BaseIterator &other) const {
  return !(*this == other);
}

bool HeapIterator::top_value_legal() const {
  if (items.empty()) {
    return true;
  }

  if (max_tranc_id_ == 0) {
    // 没有开启事务
    // 不为空的 value 才合法

    if (skip_delete_) {
      //如果这个迭代器需要跳过删除记录
      return items.top().value_.size() > 0;
    }
    //否则，就关于top_value是否合法，只需要在意事务id就好了，那么就会是true
    return true;
  }

  if (items.top().tranc_id_ <= max_tranc_id_) {
    // 事务id可见, 则判断其value是否为空

    if (skip_delete_) {
      //如果这个迭代器需要跳过删除记录
      return items.top().value_.size() > 0;
    }

    //否则，就关于top_value是否合法，只需要在意事务id就好了，那么就会是true
    return true;
  } else {
    // 事务id不可见, 即不合法
    return false;
  }
}

void HeapIterator::skip_by_tranc_id() {
  if (max_tranc_id_ == 0) {
    // 没有开启事务
    return;
  }
  while (!items.empty() && items.top().tranc_id_ > max_tranc_id_) {
    items.pop();
  }
}

bool HeapIterator::is_end() const { return items.empty(); }
bool HeapIterator::is_valid() const { return !items.empty(); }

void HeapIterator::update_current() const {
  if (!items.empty()) {
    current =
        std::make_shared<value_type>(items.top().key_, items.top().value_);
  } else {
    current.reset();
  }
}

IteratorType HeapIterator::get_type() const {
  return IteratorType::HeapIterator;
}

uint64_t HeapIterator::get_tranc_id() const { return max_tranc_id_; }
} // namespace tiny_lsm