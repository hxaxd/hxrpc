#include "controller.h"

hxrpccontroller::hxrpccontroller() = default;

void hxrpccontroller::Reset() {
  failed_ = false;
  error_code_ = hxrpc::RpcStatusCode::kOk;
  error_text_.clear();
  request_metadata_.clear();
}

bool hxrpccontroller::Failed() const { return failed_; }

std::string hxrpccontroller::ErrorText() const { return error_text_; }

void hxrpccontroller::SetFailed(const std::string &reason) {
  SetError(hxrpc::RpcStatusCode::kInternalError, reason);
}

void hxrpccontroller::SetError(hxrpc::RpcStatusCode code,
                               const std::string &reason) {
  failed_ = true;
  error_code_ = code;
  error_text_ = reason;
}

hxrpc::RpcStatusCode hxrpccontroller::ErrorCode() const { return error_code_; }

void hxrpccontroller::SetRequestMetadata(std::string metadata) {
  request_metadata_ = std::move(metadata);
}

const std::string &hxrpccontroller::RequestMetadata() const {
  return request_metadata_;
}

void hxrpccontroller::StartCancel() {}

bool hxrpccontroller::IsCanceled() const { return false; }

void hxrpccontroller::NotifyOnCancel(google::protobuf::Closure *callback) {
  (void)callback;
}
