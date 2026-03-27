#include "message_pool.h"

#include <utility>

namespace hxrpc {

PooledMessage::PooledMessage(MessagePool* pool, std::string key,
                             std::unique_ptr<google::protobuf::Message> message)
    : pool_(pool), key_(std::move(key)), message_(std::move(message)) {}

// 移动构造后源对象失效, 确保同一消息只会被归还一次
PooledMessage::PooledMessage(PooledMessage&& other) noexcept
    : pool_(std::exchange(other.pool_, nullptr)),
      key_(std::move(other.key_)),
      message_(std::move(other.message_)) {}

PooledMessage& PooledMessage::operator=(PooledMessage&& other) noexcept {
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
  // 空句柄或已释放句柄直接返回, 避免重复回收
  if (pool_ == nullptr || message_ == nullptr) {
    return;
  }
  // 回池前清空消息内容, 防止旧请求数据泄漏到下一次复用
  message_->Clear();
  pool_->Release(std::move(key_), std::move(message_));
  pool_ = nullptr;
}

MessagePool::MessagePool(std::size_t bucket_limit)
    : bucket_limit_(bucket_limit) {}

PooledMessage MessagePool::Acquire(const google::protobuf::Message& prototype) {
  // 用 descriptor 全名作为桶键, 确保同类型消息在同一复用池内
  const std::string key = prototype.GetDescriptor()->full_name();

  std::unique_ptr<google::protobuf::Message> message;
  {
    // 锁仅覆盖桶访问, 缩短临界区以降低并发争用
    std::lock_guard<std::mutex> lock(mutex_);
    auto& bucket = buckets_[key];
    if (!bucket.empty()) {
      message = std::move(bucket.back());
      bucket.pop_back();
    }
  }

  if (message == nullptr) {
    // 桶为空时按 prototype 动态创建新实例
    message.reset(prototype.New());
  } else {
    // 复用路径也做一次 Clear, 确保返回给调用方的是干净对象
    message->Clear();
  }

  return PooledMessage(this, key, std::move(message));
}

void MessagePool::Release(std::string key,
                          std::unique_ptr<google::protobuf::Message> message) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& bucket = buckets_[key];
  // 超过桶上限时直接丢弃, 让缓存规模保持可控
  if (bucket.size() >= bucket_limit_) {
    return;
  }
  bucket.push_back(std::move(message));
}

}  // namespace hxrpc
