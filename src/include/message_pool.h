#ifndef HXRPC_MESSAGE_POOL_H
#define HXRPC_MESSAGE_POOL_H

#include <google/protobuf/message.h>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace hxrpc {

class MessagePool;

// PooledMessage 是一个轻量 RAII 句柄:
// - 析构时自动把 message 归还到 MessagePool；
// - 通过移动语义转移所有权, 禁止拷贝避免重复归还
class PooledMessage {
 public:
  PooledMessage() = default;
  PooledMessage(MessagePool* pool, std::string key,
                std::unique_ptr<google::protobuf::Message> message);
  PooledMessage(PooledMessage&& other) noexcept;
  PooledMessage& operator=(PooledMessage&& other) noexcept;
  PooledMessage(const PooledMessage&) = delete;
  PooledMessage& operator=(const PooledMessage&) = delete;
  ~PooledMessage();

  // 获取底层 protobuf 消息裸指针 (不转移所有权)
  [[nodiscard]] google::protobuf::Message* get() const {
    return message_.get();
  }
  // 语法糖: 像智能指针一样解引用访问消息对象
  [[nodiscard]] google::protobuf::Message& operator*() const {
    return *message_;
  }
  [[nodiscard]] google::protobuf::Message* operator->() const {
    return message_.get();
  }
  // 判断句柄当前是否持有可用消息实例
  [[nodiscard]] explicit operator bool() const { return message_ != nullptr; }

 private:
  // 将对象清空后回收到池；该函数由析构和移动赋值路径复用
  void Release();

  MessagePool* pool_{nullptr};
  std::string key_;
  std::unique_ptr<google::protobuf::Message> message_;
};

// MessagePool 按 protobuf 全限定类型名分桶缓存消息对象,
// 以减少高频请求下的 New/Delete 开销
class MessagePool {
 public:
  // bucket_limit: 每个消息类型桶允许缓存的最大对象数
  explicit MessagePool(std::size_t bucket_limit = 32);

  // 按 prototype 类型获取一个可复用消息实例；
  // 返回值为 RAII 句柄, 生命周期结束后会自动归还
  [[nodiscard]] PooledMessage Acquire(
      const google::protobuf::Message& prototype);
  // 归还消息到对应类型桶；通常由 PooledMessage 自动调用
  void Release(std::string key,
               std::unique_ptr<google::protobuf::Message> message);

 private:
  std::size_t bucket_limit_{32};
  std::mutex mutex_;
  std::unordered_map<std::string,
                     std::vector<std::unique_ptr<google::protobuf::Message>>>
      buckets_;
};

}  // namespace hxrpc

#endif
