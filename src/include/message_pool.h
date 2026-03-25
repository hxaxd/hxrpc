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

class PooledMessage {
public:
  PooledMessage() = default;
  PooledMessage(MessagePool *pool, std::string key,
                std::unique_ptr<google::protobuf::Message> message);
  PooledMessage(PooledMessage &&other) noexcept;
  PooledMessage &operator=(PooledMessage &&other) noexcept;
  PooledMessage(const PooledMessage &) = delete;
  PooledMessage &operator=(const PooledMessage &) = delete;
  ~PooledMessage();

  [[nodiscard]] google::protobuf::Message *get() const { return message_.get(); }
  [[nodiscard]] google::protobuf::Message &operator*() const { return *message_; }
  [[nodiscard]] google::protobuf::Message *operator->() const { return message_.get(); }
  [[nodiscard]] explicit operator bool() const { return message_ != nullptr; }

private:
  void Release();

  MessagePool *pool_{nullptr};
  std::string key_;
  std::unique_ptr<google::protobuf::Message> message_;
};

class MessagePool {
public:
  explicit MessagePool(std::size_t bucket_limit = 32);

  [[nodiscard]] PooledMessage Acquire(const google::protobuf::Message &prototype);
  void Release(std::string key, std::unique_ptr<google::protobuf::Message> message);

private:
  std::size_t bucket_limit_{32};
  std::mutex mutex_;
  std::unordered_map<std::string,
                     std::vector<std::unique_ptr<google::protobuf::Message>>>
      buckets_;
};

} // namespace hxrpc

#endif
