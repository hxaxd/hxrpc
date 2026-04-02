# hxrpc

一个面向教学与工程演进的, 基于 `epoll + C++20 coroutine + protobuf` 的轻量级 C++ RPC 框架

当前版本已经去掉旧的 bridge facade, 对外只保留两类核心入口:

- `RpcServer`
- `RpcClient`

框架目标是把网络, 传输, 发现, 分发, 序列化这些能力分层拆清楚, 再在这套骨架上逐步演进协程, 对象池, 动态发现和服务治理

## 核心结构

### 服务端

- `Reactor`
    - 负责 `epoll` 生命周期和 fd 事件分发
- `ConnectionManager`
    - 负责监听, accept, 连接缓冲, 半包/粘包处理, 发送与关闭
- `RpcDispatcher`
    - 负责解码请求, 定位 service/method, 反序列化, 调用 protobuf service, 编码响应
- `RpcServer`
    - 负责装配上述组件并对外暴露服务注册与运行入口

### 客户端

- `ServiceResolver`
    - 负责解析服务实例
- `ClientTransport`
    - 负责请求-响应传输
- `Serializer`
    - 负责业务对象与 payload 之间的转换
- `RpcClient`
    - 负责把发现, 序列化, 协议编解码, 传输组合成一次完整 RPC 调用

## 当前能力

- 基于 `epoll` 的服务端 `Reactor`
- 非阻塞 socket 与长度帧协议
- 半包 / 粘包处理
- `protobuf` 业务对象序列化
- 同步 RPC 调用
- 基于 `C++20 coroutine` 的客户端异步调用接口
- `Zookeeper` 注册与发现
- 基于 `watcher + 本地缓存` 的动态服务实例刷新
- `Protobuf Message` 对象池复用

## 协议格式

请求与响应统一采用如下格式:

```text
+----------------------+----------------------+----------------------+------------------+
| 4B total_length      | 4B header_length     | protobuf header      | payload          |
+----------------------+----------------------+----------------------+------------------+
```

请求头包含:

- `request_id`
- `service_name`
- `method_name`
- `args_size`
- `meta_size`

响应头包含:

- `request_id`
- `status_code`
- `error_text`
- `payload_size`

其中请求 `metadata` 已经进入调用语义, 会跟随一次调用一起编码, 传输并在服务端通过 `hxrpccontroller` 暴露给业务实现

## 配置模型

启动时配置文件会被加载成强类型对象:

- `ServerSettings`
- `ClientSettings`
- `LoggerSettings`
- `ServerConfig`
- `ClientConfig`
- `DiscoveryConfig`
- `ReactorConfig`
- `SerializationConfig`
- `LoggerConfig`
- `CallOptions`

静态发现示例:

```yaml
client:
  rpc_timeout_ms: 1500

discovery:
  backend: static
  services:
    UserServiceRpc.Login: 127.0.0.1:8000
    UserServiceRpc.Register: 127.0.0.1:8000
```

Zookeeper 发现示例:

```yaml
client:
  rpc_timeout_ms: 1500

discovery:
  backend: zookeeper
  zookeeper:
    host: 127.0.0.1
    port: 2181
```

## 使用方式

### 服务端注册

```cpp
auto logger = hxrpc::LoggerSettings::Load("server.yaml").value();
hxrpc::Logger::Instance().Configure(logger.config.ToOptions());

auto server_settings = hxrpc::ServerSettings::Load("server.yaml").value();
hxrpc::RpcServer server(server_settings.config);
server.RegisterService(new UserService());
server.Run();
```

### 客户端同步调用

```cpp
auto client_settings = hxrpc::ClientSettings::Load("server.yaml").value();
hxrpc::RpcClient client(client_settings.config);

Kuser::LoginRequest request;
Kuser::LoginResponse response;
hxrpc::CallOptions options = client_settings.config.call_options;
options.metadata = "trace_id=sync-demo";

const auto* method = Kuser::UserServiceRpc::descriptor()->FindMethodByName("Login");
auto result = client.Invoke(method, request, response, options);
```

### 客户端协程调用

```cpp
hxrpc::Task<std::expected<void, hxrpc::RpcError>> Call(
    hxrpc::RpcClient& client,
    const google::protobuf::MethodDescriptor* method,
    const Kuser::LoginRequest& request,
    Kuser::LoginResponse& response) {
  co_return co_await client.InvokeAsync(method, request, response);
}
```

## 项目结构

```text
src/
  include/
    async_runtime.h
    client_transport.h
    codec.h
    connection_manager.h
    controller.h
    message_pool.h
    reactor.h
    rpc_client.h
    rpc_dispatcher.h
    rpc_server.h
    serializer.h
    service_discovery.h
    service_registry.h
    settings.h
    task.h
    types.h
    zookeeperutil.h
```

## 编译与运行

### 命令速查

```bash
# 配置
cmake -S . -B build

# 编译
cmake --build build -j$(nproc)

# 跑全部测试
cd build && ctest --output-on-failure

# 启动 benchmark server
./bin/benchmark_server

# 跑默认 benchmark
./bin/benchmark_client

# 跑自定义 benchmark
./bin/benchmark_client --concurrency 1024 --requests 2 --timeout-ms 3000
```

### 环境

- Linux
- GCC 13+ / Clang 16+
- CMake 3.20+
- Protobuf
- Zookeeper C Client

### 编译

```bash
git clone <your-repo>
cd hxrpc
cmake -S . -B build
cmake --build build -j$(nproc)
```

### 运行服务端与 benchmark 客户端

```bash
./bin/benchmark_server
./bin/benchmark_client
```

也可以覆盖 benchmark 参数:

```bash
./bin/benchmark_client --concurrency 256 --requests 8 --timeout-ms 1500
```

`benchmark_client` 会从默认的 `server.yaml` 读取服务端地址与发现方式,
并继续通过命令行参数覆盖 benchmark 的并发/请求数/超时

benchmark 结果会额外写入:

- `logs/benchmark_report_<timestamp>.json`

报告包含:

- `success_rate`
- `avg_latency_ms`
- `p95_latency_ms`
- `p99_latency_ms`

## 日志配置

当前日志支持:

- 异步写入
- 同时输出到 `stderr`
- 同时输出到配置文件路径

示例:

```yaml
client:
  rpc_timeout_ms: 1500

server:
  host: 127.0.0.1
  port: 8000

discovery:
  backend: static
  zookeeper:
    host: 127.0.0.1
    port: 2181
  services:
    UserServiceRpc.Login: 127.0.0.1:8000
    UserServiceRpc.Register: 127.0.0.1:8000

logging:
  mode: async
  sink: stderr_and_file
  file_path: logs/benchmark_server.log
  min_level: info
```

## 测试

当前测试覆盖:

- 配置解析
- 协议编解码
- 静态发现与 ZK 路径约定
- dispatcher 业务分发
- 真实 client/server 端到端调用
- metadata 透传
- MessagePool 复用
- 协程客户端调用
- 失败路径
- 半包 / 粘包边界行为

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## 当前限制

当前版本仍然刻意保持简单:

- 服务端业务处理仍基于 protobuf 同步回调, 不是 coroutine-native handler
- 暂未实现 retry, 熔断, 限流, metrics, trace
- 对象池目前只覆盖 `Protobuf Message`

但这几类能力都已经有明确的插入位置, 不需要推翻核心结构
