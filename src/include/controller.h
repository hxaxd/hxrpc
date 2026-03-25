#ifndef HXRPC_CONTROLLER_H
#define HXRPC_CONTROLLER_H

#include <google/protobuf/service.h>
#include <string>
#include "types.h"

class hxrpccontroller : public google::protobuf::RpcController {
public:
  hxrpccontroller();

  void Reset() override;
  [[nodiscard]] bool Failed() const override;
  [[nodiscard]] std::string ErrorText() const override;
  void SetFailed(const std::string &reason) override;
  void SetError(hxrpc::RpcStatusCode code, const std::string &reason);
  [[nodiscard]] hxrpc::RpcStatusCode ErrorCode() const;
  void SetRequestMetadata(std::string metadata);
  [[nodiscard]] const std::string &RequestMetadata() const;

  void StartCancel() override;
  [[nodiscard]] bool IsCanceled() const override;
  void NotifyOnCancel(google::protobuf::Closure *callback) override;

private:
  bool failed_{false};
  hxrpc::RpcStatusCode error_code_{hxrpc::RpcStatusCode::kOk};
  std::string error_text_;
  std::string request_metadata_;
};

#endif
