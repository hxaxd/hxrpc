#ifndef HXRPC_ZOOKEEPER_UTIL_H
#define HXRPC_ZOOKEEPER_UTIL_H

#define THREADED

#include "types.h"
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <zookeeper/zookeeper.h>

class ZkClient {
public:
  struct ChildWatch;

  ZkClient();
  ~ZkClient();

  ZkClient(const ZkClient &) = delete;
  ZkClient &operator=(const ZkClient &) = delete;

  [[nodiscard]] std::expected<void, hxrpc::RpcError> Start();
  [[nodiscard]] std::expected<void, hxrpc::RpcError>
  Start(const hxrpc::Endpoint &endpoint);
  [[nodiscard]] std::expected<void, hxrpc::RpcError>
  CreateNode(const char *path, const char *data, int datalen, int flags = 0);
  [[nodiscard]] std::expected<void, hxrpc::RpcError>
  EnsurePath(const char *path);
  [[nodiscard]] std::expected<std::string, hxrpc::RpcError>
  GetData(const char *path) const;
  [[nodiscard]] std::expected<std::vector<std::string>, hxrpc::RpcError>
  GetChildren(const char *path) const;
  [[nodiscard]] std::expected<std::vector<std::string>, hxrpc::RpcError>
  GetChildrenWatched(const std::string &path,
                     std::function<void(const std::string &)> on_change);

private:
  zhandle_t *handle_{nullptr};
  std::vector<std::shared_ptr<ChildWatch>> child_watches_;
};

#endif
