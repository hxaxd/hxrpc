#include "message_pool.h"
#include <utility>

namespace hxrpc {

PooledMessage::PooledMessage(MessagePool *pool, std::string key,
                             std::unique_ptr<google::protobuf::Message> message)
    : pool_(pool), key_(std::move(key)), message_(std::move(message)) {}

PooledMessage::PooledMessage(PooledMessage &&other) noexcept
    : pool_(std::exchange(other.pool_, nullptr)),
      key_(std::move(other.key_)),
      message_(std::move(other.message_)) {}

PooledMessage &PooledMessage::operator=(PooledMessage &&other) noexcept {
  if (this == &other) {
    return *this;
  }
  Release();
  pool_ = std::exchange(other.pool_, nullptr);
  key_ = std::move(other.key_);
  message_ = std::move(other.message_);
  return *this;
}

PooledMessage::~PooledMessage() { Release(); }

void PooledMessage::Release() {
  if (pool_ == nullptr || message_ == nullptr) {
    return;
  }
  message_->Clear();
  pool_->Release(std::move(key_), std::move(message_));
  pool_ = nullptr;
}

MessagePool::MessagePool(std::size_t bucket_limit) : bucket_limit_(bucket_limit) {}

PooledMessage MessagePool::Acquire(const google::protobuf::Message &prototype) {
  const std::string key = prototype.GetDescriptor()->full_name();

  std::unique_ptr<google::protobuf::Message> message;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto &bucket = buckets_[key];
    if (!bucket.empty()) {
      message = std::move(bucket.back());
      bucket.pop_back();
    }
  }

  if (message == nullptr) {
    message.reset(prototype.New());
  } else {
    message->Clear();
  }

  return PooledMessage(this, key, std::move(message));
}

void MessagePool::Release(std::string key,
                          std::unique_ptr<google::protobuf::Message> message) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto &bucket = buckets_[key];
  if (bucket.size() >= bucket_limit_) {
    return;
  }
  bucket.push_back(std::move(message));
}

} // namespace hxrpc
