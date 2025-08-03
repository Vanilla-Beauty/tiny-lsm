#include <asio.hpp>
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// 假设这些头文件和实现已经存在，并且与您的 Muduo 版本兼容
#include "../../include/redis_wrapper/redis_wrapper.h"
#include "../include/handler.h"

// 简单的日志宏，替代 Muduo 的 LOG_INFO
#define ASYNC_REDIS_SERVER_LOG_INFO(msg)                                       \
  std::cout << "[INFO] " << msg << std::endl;
#define ASYNC_REDIS_SERVER_LOG_DEBUG(                                          \
    msg) /* std::cout << "[DEBUG] " << msg << std::endl; */ // 在发布版本中可以注释掉

using asio::ip::tcp;

// 每个客户端连接的会话类
class RedisSession : public std::enable_shared_from_this<RedisSession> {
public:
  RedisSession(tcp::socket socket, RedisWrapper &redis_db)
      : socket_(std::move(socket)), redis_(redis_db) {}

  void start() {
    ASYNC_REDIS_SERVER_LOG_INFO(
        "Connection from " << socket_.remote_endpoint().address().to_string()
                           << ":" << socket_.remote_endpoint().port());
    do_read();
  }

private:
  void do_read() {
    auto self(shared_from_this());
    asio::async_read_until(
        socket_, buffer_, "\r\n",
        [this, self](const asio::error_code &ec,
                     std::size_t bytes_transferred) {
          if (!ec) {
            // 读取请求的完整内容，可能包含多行
            std::istream is(&buffer_);
            std::string request_line;
            std::getline(is, request_line); // 读取第一行，例如 "*3" 或 "PING"
            request_line += "\r\n"; // getline 会去除换行符，Redis协议需要

            // 对于PING，特殊处理，避免后续解析错误
            if (request_line == "PING\r\n") {
              std::string response = "+PONG\r\n";
              do_write(response);
              return; // PONG命令只有一行
            }

            // 尝试解析数组的元素数量
            if (request_line.length() < 2 || request_line[0] != '*') {
              do_write("-ERR Protocol error: expected '*'\r\n");
              return;
            }

            int numElements = 0;
            try {
              numElements = std::stoi(request_line.substr(1));
            } catch (const std::exception &e) {
              do_write("-ERR Protocol error: invalid number of elements\r\n");
              return;
            }

            // 继续读取剩余的批量字符串数据
            std::string full_request = request_line;
            for (int i = 0; i < numElements * 2;
                 ++i) {                  // 每个元素有两个部分：长度行和数据行
              if (buffer_.size() == 0) { // 需要更多数据
                // 确保缓冲区有足够的数据来读取完整的请求
                // 这是一种简化的处理方式，实际生产中需要更复杂的解析逻辑
                // 例如，可以分阶段读取，或者使用一个可增长的缓冲区
                do_write("-ERR Protocol error: partial request received\r\n");
                return;
              }
              std::string line;
              std::getline(is, line);
              full_request += line + "\r\n";
            }

            ASYNC_REDIS_SERVER_LOG_DEBUG("Received message:\n" << full_request);

            // 解析并处理请求
            std::string response = handleRequest(full_request);
            do_write(response);
          } else if (ec == asio::error::eof ||
                     ec == asio::error::connection_reset) {
            ASYNC_REDIS_SERVER_LOG_INFO(
                "Connection closed from "
                << socket_.remote_endpoint().address().to_string() << ":"
                << socket_.remote_endpoint().port());
          } else {
            ASYNC_REDIS_SERVER_LOG_INFO("Error on read: " << ec.message());
          }
        });
  }

  void do_write(const std::string &response) {
    auto self(shared_from_this());
    asio::async_write(
        socket_, asio::buffer(response),
        [this, self, response](const asio::error_code &ec,
                               std::size_t /*bytes_transferred*/) {
          if (!ec) {
            ASYNC_REDIS_SERVER_LOG_DEBUG("Sent response:\n" << response);
            do_read(); // 继续读取下一个请求
          } else if (ec == asio::error::eof ||
                     ec == asio::error::connection_reset) {
            ASYNC_REDIS_SERVER_LOG_INFO(
                "Connection closed from "
                << socket_.remote_endpoint().address().to_string() << ":"
                << socket_.remote_endpoint().port());
          } else {
            ASYNC_REDIS_SERVER_LOG_INFO("Error on write: " << ec.message());
          }
        });
  }

  // 与 Muduo 版本相同的 handleRequest 逻辑
  std::string handleRequest(const std::string &request) {
    // TODO: Lab 6.6 处理网络传输的RESP字节流
    // TODO: Lab 6.6 形成参数并调用 redis_wrapper 的api
    // TODO: Lab 6.6 返回结果
    return "";
  }

  tcp::socket socket_;
  asio::streambuf buffer_; // 使用 streambuf 处理读入的数据
  RedisWrapper &redis_;    // 数据库实例，通过引用传递
};

class RedisServer {
public:
  RedisServer(asio::io_context &io_context, unsigned short port)
      : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
        redis_("example_db") { // 初始化 RedisWrapper
    do_accept();
  }

private:
  void do_accept() {
    acceptor_.async_accept(
        [this](const asio::error_code &ec, tcp::socket socket) {
          if (!ec) {
            std::make_shared<RedisSession>(std::move(socket), redis_)->start();
          } else {
            ASYNC_REDIS_SERVER_LOG_INFO("Error on accept: " << ec.message());
          }
          do_accept(); // 继续接受新的连接
        });
  }

  tcp::acceptor acceptor_;
  RedisWrapper redis_; // 数据库实例
};

int main() {
  try {
    asio::io_context io_context;
    RedisServer server(io_context, 6379); // Redis 默认端口
    io_context.run();                     // 运行事件循环
  } catch (std::exception &e) {
    std::cerr << "Exception: " << e.what() << std::endl;
  }

  return 0;
}