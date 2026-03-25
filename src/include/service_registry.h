#ifndef HXRPC_SERVICE_REGISTRY_H
#define HXRPC_SERVICE_REGISTRY_H

#include "types.h"
#include <expected>
#include <google/protobuf/service.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hxrpc {

struct RegisteredMethod {
  std::string service_name;
  std::string method_name;
  google::protobuf::Service *service{nullptr};
  const google::protobuf::MethodDescriptor *method{nullptr};
};

// ServiceRegistry 保存“服务名 + 方法名 -> protobuf service/method”的映射。
// Dispatcher 只依赖这个注册表，不直接扫描业务类。
class ServiceRegistry {
public:
  void RegisterService(google::protobuf::Service *service);

  [[nodiscard]] std::expected<RegisteredMethod, RpcError>
  Find(std::string_view service_name, std::string_view method_name) const;

  [[nodiscard]] std::vector<RegisteredMethod> ListMethods() const;

private:
  std::unordered_map<std::string, RegisteredMethod> methods_;
};

} // namespace hxrpc

#endif
