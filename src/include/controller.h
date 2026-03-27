#ifndef HXRPC_CONTROLLER_H
#define HXRPC_CONTROLLER_H

#include <google/protobuf/service.h>

#include <string>

#include "types.h"

// hxrpccontroller 适配 protobuf 的 RpcController 接口,
// 负责在一次 RPC 调用内携带错误状态和请求元数据
class hxrpccontroller : public google::protobuf::RpcController {
 public:
  hxrpccontroller();

  // 重置到无错误的初始状态, 供对象复用
  void Reset() override;
  // 是否已记录失败状态
  [[nodiscard]] bool Failed() const override;
  // 返回失败文本 (成功时通常为空)
  [[nodiscard]] std::string ErrorText() const override;
  // 兼容 protobuf 约定: 设置失败并标记为内部错误码
  void SetFailed(const std::string& reason) override;
  // 显式设置框架错误码与错误文本
  void SetError(hxrpc::RpcStatusCode code, const std::string& reason);
  // 获取当前框架错误码
  [[nodiscard]] hxrpc::RpcStatusCode ErrorCode() const;
  // 写入请求附带的元数据 (如 tracing/auth 信息)
  void SetRequestMetadata(std::string metadata);
  // 读取请求元数据
  [[nodiscard]] const std::string& RequestMetadata() const;

  // 取消相关接口为占位实现, 当前传输层未实现主动取消链路
  void StartCancel() override;
  [[nodiscard]] bool IsCanceled() const override;
  void NotifyOnCancel(google::protobuf::Closure* callback) override;

 private:
  bool failed_{false};
  hxrpc::RpcStatusCode error_code_{hxrpc::RpcStatusCode::kOk};
  std::string error_text_;
  std::string request_metadata_;
};

#endif
