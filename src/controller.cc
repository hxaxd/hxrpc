#include "controller.h"

hxrpccontroller::hxrpccontroller() = default;

void hxrpccontroller::Reset() {
  // 清理上一轮调用状态, 便于控制器对象复用
  failed_ = false;
  error_code_ = hxrpc::RpcStatusCode::kOk;
  error_text_.clear();
  request_metadata_.clear();
}

bool hxrpccontroller::Failed() const { return failed_; }

std::string hxrpccontroller::ErrorText() const { return error_text_; }

void hxrpccontroller::SetFailed(const std::string& reason) {
  // protobuf 接口未携带业务错误码, 这里统一映射到内部错误
  SetError(hxrpc::RpcStatusCode::kInternalError, reason);
}

void hxrpccontroller::SetError(hxrpc::RpcStatusCode code,
                               const std::string& reason) {
  failed_ = true;
  error_code_ = code;
  error_text_ = reason;
}

hxrpc::RpcStatusCode hxrpccontroller::ErrorCode() const { return error_code_; }

void hxrpccontroller::SetRequestMetadata(std::string metadata) {
  // 元数据按值接收并移动, 避免额外拷贝
  request_metadata_ = std::move(metadata);
}

const std::string& hxrpccontroller::RequestMetadata() const {
  return request_metadata_;
}

void hxrpccontroller::StartCancel() {
  // 当前版本不支持跨层取消传播, 此处保留空实现以满足接口契约
}

bool hxrpccontroller::IsCanceled() const { return false; }

void hxrpccontroller::NotifyOnCancel(google::protobuf::Closure* callback) {
  // 同上: 暂未实现取消回调链路
  (void)callback;
}
