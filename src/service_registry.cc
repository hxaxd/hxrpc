#include "service_registry.h"

#include <google/protobuf/descriptor.h>

#include <ranges>

namespace hxrpc {

// 把 service 的所有方法展开为独立键: "Service.Method"
// 这样 Find 可在 O(1) 平均复杂度下完成定位
void ServiceRegistry::RegisterService(google::protobuf::Service* service) {
  const auto* descriptor = service->GetDescriptor();
  for (int index = 0; index < descriptor->method_count(); ++index) {
    const auto* method = descriptor->method(index);
    const std::string key = descriptor->name() + "." + method->name();
    methods_[key] =
        RegisteredMethod{descriptor->name(), method->name(), service, method};
  }
}

// 先做精确键查找若失败再判定服务是否存在, 以便返回更明确的错误码
std::expected<RegisteredMethod, RpcError> ServiceRegistry::Find(
    std::string_view service_name, std::string_view method_name) const {
  const std::string key =
      std::string(service_name) + "." + std::string(method_name);
  const auto it = methods_.find(key);
  if (it == methods_.end()) {
    const bool service_exists =
        std::ranges::any_of(methods_, [&](const auto& entry) {
          return entry.second.service_name == service_name;
        });
    return std::unexpected(
        RpcError{service_exists ? RpcStatusCode::kUnknownMethod
                                : RpcStatusCode::kUnknownService,
                 service_exists ? "unknown method" : "unknown service"});
  }
  return it->second;
}

// 导出当前注册表快照, 供上层进行 endpoint 注册或可观测性输出
std::vector<RegisteredMethod> ServiceRegistry::ListMethods() const {
  std::vector<RegisteredMethod> methods;
  methods.reserve(methods_.size());
  for (const auto& [key, method] : methods_) {
    (void)key;
    methods.push_back(method);
  }
  return methods;
}

}  // namespace hxrpc
