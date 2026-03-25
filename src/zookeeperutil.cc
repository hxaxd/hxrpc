#define THREADED
#include "zookeeperutil.h"
#include "application.h"
#include "types.h"
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <utility>

namespace {

std::mutex g_connection_mutex;
std::condition_variable g_connection_cv;
bool g_connected = false;

hxrpc::RpcError MakeZkError(hxrpc::RpcStatusCode code, std::string message) {
  return hxrpc::RpcError{code, std::move(message)};
}

void GlobalWatcher(zhandle_t *handle, int type, int state, const char *path,
                   void *watcher_ctx) {
  (void)handle;
  (void)path;
  (void)watcher_ctx;

  if (type == ZOO_SESSION_EVENT && state == ZOO_CONNECTED_STATE) {
    std::lock_guard<std::mutex> lock(g_connection_mutex);
    g_connected = true;
    g_connection_cv.notify_all();
  }
}

} // namespace

struct ZkClient::ChildWatch {
  std::string path;
  std::function<void(const std::string &)> callback;
};

ZkClient::ZkClient() = default;

ZkClient::~ZkClient() {
  if (handle_ != nullptr) {
    zookeeper_close(handle_);
  }
}

std::expected<void, hxrpc::RpcError> ZkClient::Start() {
  const auto host = hxrpcApplication::GetConfig().Load("discovery.zookeeper.host");
  const auto port = hxrpcApplication::GetConfig().Load("discovery.zookeeper.port");
  return Start(hxrpc::Endpoint{host, static_cast<std::uint16_t>(std::stoi(port))});
}

std::expected<void, hxrpc::RpcError>
ZkClient::Start(const hxrpc::Endpoint &endpoint) {
  const std::string address = endpoint.ToString();

  {
    std::lock_guard<std::mutex> lock(g_connection_mutex);
    g_connected = false;
  }

  handle_ = zookeeper_init(address.c_str(), GlobalWatcher, 6000, nullptr, nullptr, 0);
  if (handle_ == nullptr) {
    return std::unexpected(hxrpc::RpcError{
        hxrpc::RpcStatusCode::kDiscoveryError, "zookeeper_init failed"});
  }

  std::unique_lock<std::mutex> lock(g_connection_mutex);
  g_connection_cv.wait(lock, [] { return g_connected; });
  return {};
}

std::expected<void, hxrpc::RpcError>
ZkClient::CreateNode(const char *path, const char *data, int datalen, int flags) {
  char path_buffer[512] = {};
  constexpr int buffer_length = sizeof(path_buffer);

  const int exists_result = zoo_exists(handle_, path, 0, nullptr);
  if (exists_result == ZOK) {
    return {};
  }
  if (exists_result != ZNONODE) {
    return std::unexpected(MakeZkError(hxrpc::RpcStatusCode::kDiscoveryError,
                                       "zoo_exists failed"));
  }

  const int create_result =
      zoo_create(handle_, path, data, datalen, &ZOO_OPEN_ACL_UNSAFE, flags,
                 path_buffer, buffer_length);
  if (create_result != ZOK) {
    return std::unexpected(MakeZkError(hxrpc::RpcStatusCode::kDiscoveryError,
                                       "zoo_create failed"));
  }
  return {};
}

std::expected<void, hxrpc::RpcError> ZkClient::EnsurePath(const char *path) {
  if (std::strcmp(path, "/") == 0) {
    return {};
  }

  std::string current;
  const std::string full_path(path);
  std::size_t cursor = 0;
  while (cursor < full_path.size()) {
    const auto next_separator = full_path.find('/', cursor + 1);
    const auto end = next_separator == std::string::npos ? full_path.size() : next_separator;
    current = full_path.substr(0, end);

    if (!current.empty()) {
      if (auto result = CreateNode(current.c_str(), nullptr, 0, 0); !result) {
        return std::unexpected(result.error());
      }
    }

    if (next_separator == std::string::npos) {
      break;
    }
    cursor = next_separator;
  }

  return {};
}

std::expected<std::string, hxrpc::RpcError>
ZkClient::GetData(const char *path) const {
  char buffer[512] = {};
  int length = sizeof(buffer);
  const int result = zoo_get(handle_, path, 0, buffer, &length, nullptr);
  if (result != ZOK) {
    return std::unexpected(MakeZkError(hxrpc::RpcStatusCode::kDiscoveryError,
                                       "zoo_get failed"));
  }
  return std::string(buffer, length);
}

std::expected<std::vector<std::string>, hxrpc::RpcError>
ZkClient::GetChildren(const char *path) const {
  String_vector children;
  const int result = zoo_get_children(handle_, path, 0, &children);
  if (result != ZOK) {
    return std::unexpected(MakeZkError(hxrpc::RpcStatusCode::kDiscoveryError,
                                       "zoo_get_children failed"));
  }

  std::vector<std::string> values;
  values.reserve(children.count);
  for (int index = 0; index < children.count; ++index) {
    values.emplace_back(children.data[index]);
  }

  deallocate_String_vector(&children);
  return values;
}

namespace {

void ChildWatcher(zhandle_t *handle, int type, int state, const char *path,
                  void *watcher_ctx) {
  (void)handle;
  (void)state;

  if (watcher_ctx == nullptr) {
    return;
  }

  auto *watch = static_cast<ZkClient::ChildWatch *>(watcher_ctx);
  if (type == ZOO_CHILD_EVENT || type == ZOO_CREATED_EVENT ||
      type == ZOO_DELETED_EVENT) {
    watch->callback(path != nullptr ? std::string(path) : watch->path);
  }
}

} // namespace

std::expected<std::vector<std::string>, hxrpc::RpcError>
ZkClient::GetChildrenWatched(const std::string &path,
                             std::function<void(const std::string &)> on_change) {
  auto watch = std::make_shared<ChildWatch>();
  watch->path = path;
  watch->callback = std::move(on_change);

  String_vector children;
  const int result =
      zoo_wget_children(handle_, path.c_str(), ChildWatcher, watch.get(), &children);
  if (result != ZOK) {
    return std::unexpected(MakeZkError(hxrpc::RpcStatusCode::kDiscoveryError,
                                       "zoo_wget_children failed"));
  }

  child_watches_.push_back(watch);

  std::vector<std::string> values;
  values.reserve(children.count);
  for (int index = 0; index < children.count; ++index) {
    values.emplace_back(children.data[index]);
  }

  deallocate_String_vector(&children);
  return values;
}
