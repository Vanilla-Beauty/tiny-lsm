#pragma once
#include "../iterator/iterator.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <shared_mutex>
#include <string>
#include <sys/types.h>
#include <tuple>
#include <utility>
#include <vector>

namespace tiny_lsm {

// ************************ SkipListNode ************************
struct SkipListNode {
  std::string key_;   // 节点存储的键
  std::string value_; // 节点存储的值
  uint64_t tranc_id_; // 事务 id
  std::vector<std::shared_ptr<SkipListNode>>
      forward_; // 指向不同层级的下一个节点的指针数组
  std::vector<std::weak_ptr<SkipListNode>>
      backward_; // 指向不同层级的下一个节点的指针数组
  SkipListNode(const std::string &k, const std::string &v, int level,
               uint64_t tranc_id)
      : key_(k), value_(v), forward_(level, nullptr),
        backward_(level, std::weak_ptr<SkipListNode>()), tranc_id_(tranc_id) {}

  void set_backward(int level, std::shared_ptr<SkipListNode> node) {
    backward_[level] = std::weak_ptr<SkipListNode>(node);
  }

  bool operator==(const SkipListNode &other) const {
    return key_ == other.key_ && value_ == other.value_ &&
           tranc_id_ == other.tranc_id_;
  }

  bool operator!=(const SkipListNode &other) const { return !(*this == other); }

  bool operator<(const SkipListNode &other) const {
    if (key_ == other.key_) {
      // key 相等时，trans_id 更大的优先级更高
      return tranc_id_ > other.tranc_id_;
    }
    return key_ < other.key_;
  }
  bool operator>(const SkipListNode &other) const {
    if (key_ == other.key_) {
      // key 相等时，trans_id 更大的优先级更高
      return tranc_id_ < other.tranc_id_;
    }
    return key_ > other.key_;
  }
};

// ************************ SkipListIterator ************************

class SkipListIterator : public BaseIterator {
public:
  // ! deprecated: 构造函数，接收锁
  // SkipListIterator(std::shared_ptr<SkipListNode> node, std::shared_mutex
  // &mutex)
  //     : current(node),
  //       lock(std::make_shared<std::shared_lock<std::shared_mutex>>(mutex)) {}

  // 构造函数
  SkipListIterator(std::shared_ptr<SkipListNode> node) : current(node) {}

  // 空迭代器构造函数
  SkipListIterator() : current(nullptr), lock(nullptr) {}

  virtual BaseIterator &operator++() override;
  virtual bool operator==(const BaseIterator &other) const override;
  virtual bool operator!=(const BaseIterator &other) const override;
  virtual value_type operator*() const override;
  virtual IteratorType get_type() const override;
  virtual bool is_end() const override;
  virtual bool is_valid() const override;
  std::string get_key() const;
  std::string get_value() const;
  uint64_t get_tranc_id() const override;

private:
  std::shared_ptr<SkipListNode> current;
  std::shared_ptr<std::shared_lock<std::shared_mutex>>
      lock; // 持有读锁, 整个迭代器有效期间都持有读锁
};

// ************************ SkipList ************************

class SkipList {
private:
  std::shared_ptr<SkipListNode>
      head;              // 跳表的头节点，不存储实际数据，用于遍历跳表
  int max_level;         // 跳表的最大层级数，限制跳表的高度
  int current_level;     // 跳表当前的实际层级数，动态变化
  size_t size_bytes = 0; // 跳表当前占用的内存大小（字节数），用于跟踪内存使用
  // std::shared_mutex rw_mutex; // ! 目前看起来这个锁是冗余的, 在上层控制即可,
  // 后续考虑是否需要细粒度的锁

  std::uniform_int_distribution<> dis_01;
  std::uniform_int_distribution<> dis_level;
  std::mt19937 gen;

private:
  int random_level(); // 生成新节点的随机层级数

public:
  SkipList(int max_lvl = 16); // 构造函数，初始化跳表

  ~SkipList() {
    // std::unique_lock<std::shared_mutex> lock(rw_mutex);
    // Disconnect all nodes before cleanup to avoid stack overflow in case of
    // deep recursion during shared_ptr destruction
    auto current = head;
    while (current && current->forward_[0]) {
      auto next = current->forward_[0];
      // Clear all forward_ pointers of the current node
      for (size_t i = 0; i < current->forward_.size(); ++i) {
        current->forward_[i].reset();
      }
      current = next;
    }

    // Finally clear the head
    head.reset();
  }

  // 插入或更新键值对
  // 这里不对 tranc_id 进行检查，由上层保证 tranc_id 的合法性
  void put(const std::string &key, const std::string &value, uint64_t tranc_id);

  // 查找键对应的值
  // 事务 id 为0 表示没有开启事务
  // 否则只能查找事务 id 小于等于 tranc_id 的值
  // 返回值: 如果找到，返回 value 和 tranc_id，否则返回空
  SkipListIterator get(const std::string &key, uint64_t tranc_id);

  // !!! 这里的 remove 是跳表本身真实的 remove,  lsm 应该使用 put 空值表示删除
  void remove(const std::string &key); // 删除键值对

  // 将跳表数据刷出，返回有序键值对列表
  // value 为 真实 value 和 tranc_id 的二元组
  std::vector<std::tuple<std::string, std::string, uint64_t>> flush();

  size_t get_size();

  void clear(); // 清空跳表，释放内存

  SkipListIterator begin();
  SkipListIterator begin_preffix(const std::string &preffix);

  SkipListIterator end();
  SkipListIterator end_preffix(const std::string &preffix);

  std::optional<std::pair<SkipListIterator, SkipListIterator>>
  iters_monotony_predicate(std::function<int(const std::string &)> predicate);

  void print_skiplist();
};
} // namespace tiny_lsm