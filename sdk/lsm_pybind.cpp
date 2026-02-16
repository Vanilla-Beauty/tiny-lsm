#include "../include/lsm/engine.h"
#include "../include/lsm/level_iterator.h"
#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

// 绑定 TwoMergeIterator 迭代器
void bind_TwoMergeIterator(py::module &m) {
  py::class_<tiny_lsm::TwoMergeIterator>(m, "TwoMergeIterator")
      .def("__iter__", [](tiny_lsm::TwoMergeIterator &it) { return &it; })
      .def("__next__", [](tiny_lsm::TwoMergeIterator &it) {
        if (!it.is_valid())
          throw py::stop_iteration();
        auto kv = *it;
        ++it;
        return py::make_tuple(kv.first, kv.second);
      });
}

void bind_Level_Iterator(py::module &m) {
  py::class_<tiny_lsm::Level_Iterator>(m, "Level_Iterator")
      .def("__iter__", [](tiny_lsm::Level_Iterator &it) { return &it; })
      .def("__next__", [](tiny_lsm::Level_Iterator &it) {
        if (!it.is_valid())
          throw py::stop_iteration();
        auto kv = *it;
        ++it;
        return py::make_tuple(kv.first, kv.second);
      });
}

// 绑定 TranContext 事务上下文

// 提前声明 TranContext（如果头文件未包含）
using TranContext = tiny_lsm::TranContext;

// 绑定 TranContext
void bind_TranContext(py::module &m) {
  py::class_<TranContext, std::shared_ptr<TranContext>>(m, "TranContext")
      .def("commit", &tiny_lsm::TranContext::commit,
           py::arg("test_fail") = false) // 处理默认参数
      .def("abort", &tiny_lsm::TranContext::abort)
      .def("get", &tiny_lsm::TranContext::get)
      .def("remove", &tiny_lsm::TranContext::remove)
      .def("put", &tiny_lsm::TranContext::put);
}

void bind_IsolationLevel(py::module &m) {
  py::enum_<tiny_lsm::IsolationLevel>(m, "IsolationLevel")
      .value("READ_UNOP_COMMITTED", tiny_lsm::IsolationLevel::READ_UNOP_COMMITTED)
      .value("READ_OP_COMMITTED", tiny_lsm::IsolationLevel::READ_OP_COMMITTED)
      .value("REPEATABLE_READ", tiny_lsm::IsolationLevel::REPEATABLE_READ)
      .value("SERIALIZABLE", tiny_lsm::IsolationLevel::SERIALIZABLE)
      .export_values();
}

PYBIND11_MODULE(lsm_pybind, m) {
  // 绑定辅助类
  bind_TwoMergeIterator(m);
  bind_Level_Iterator(m);
  bind_TranContext(m);
  bind_IsolationLevel(m);

  // 主类 LSM
  py::class_<tiny_lsm::LSM>(m, "LSM")
      .def(py::init<const std::string &>())
      // 基础操作
      .def("put", &tiny_lsm::LSM::put, py::arg("key"), py::arg("value"),
           "Insert a key-value pair (bytes type)")
      .def("get", &tiny_lsm::LSM::get, py::arg("key"),
           "Get value by key, returns None if not found")
      .def("remove", &tiny_lsm::LSM::remove, py::arg("key"), "Delete a key")
      // 批量操作
      .def("put_batch", &tiny_lsm::LSM::put_batch, py::arg("kvs"),
           "Batch insert key-value pairs")
      .def("remove_batch", &tiny_lsm::LSM::remove_batch, py::arg("keys"),
           "Batch delete keys")
      // 迭代器
      .def("begin", &tiny_lsm::LSM::begin, py::arg("tranc_id"),
           "Start an iterator with transaction ID")
      .def("end", &tiny_lsm::LSM::end, "Get end iterator")
      // 事务
      .def("begin_tran", &tiny_lsm::LSM::begin_tran, py::arg("isolation_level"),
           "Start a transaction")
      // 其他方法
      .def("clear", &tiny_lsm::LSM::clear, "Clear all data") // ! Fix bugs
      .def("flush", &tiny_lsm::LSM::flush, "Flush memory table to disk")
      .def("flush_all", &tiny_lsm::LSM::flush_all, "Flush all pending data");
}
