// benchmark/client/client.cc
// 基准压测客户端示例。
// 设计目标：以最小业务逻辑展示 hxrpc
// 客户端并发调用链路，并输出可回归比较的结构化指标报告。
// 本文件仅演示调用方式与统计口径，不负责业务协议演进。

#include <sys/resource.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../user.pb.h"
#include "application.h"
#include "logger.h"
#include "rpc_client.h"
#include "settings.h"

namespace {

constexpr int kDefaultConcurrency{32};
constexpr int kDefaultRequestsPerWorker{4};
constexpr int kDefaultTimeoutMs{1500};

struct BenchmarkOptions {
  // 并发 worker 线程数
  int concurrency{kDefaultConcurrency};
  // 每个 worker 发起的请求数
  int requests_per_worker{kDefaultRequestsPerWorker};
  // 单次 RPC 调用超时时间 (毫秒)
  int timeout_ms{kDefaultTimeoutMs};
};

// 把字符串解析成正整数。
// 参数：raw - 待解析字符串。
// 返回：
//   - 解析成功且值 > 0：返回对应整数；
//   - 解析失败、存在非数字字符或值 <= 0：返回 std::nullopt。
// 错误语义：本函数不抛异常，调用方通过 optional 判定并回退到默认值。
std::optional<int> ParsePositiveInt(const std::string& raw) {
  int value{0};

  // 把字符串转成 int, 返回错误码 + 结束位置
  auto [ptr, ec] = std::from_chars(raw.begin(), raw.end(), value);

  if (ec != std::errc{} || ptr == raw.begin() || ptr != raw.end())
    return std::nullopt;

  return value;
}

// 解析命令行参数并构建压测配置。
// 参数：argc/argv - 程序启动参数。
// 返回：BenchmarkOptions。
// 设计原因：命令行参数仅覆盖关键压测维度，其余保持默认值，减少压测脚本维护成本。
// 错误语义：非法参数值会被忽略并使用默认值，不会中断进程启动。
BenchmarkOptions ParseOptions(int argc, char** argv) {
  BenchmarkOptions options;
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--concurrency" && index + 1 < argc) {
      if (auto value = ParsePositiveInt(argv[++index]); value) {
        options.concurrency = *value;
      }
    } else if ((arg == "--requests" || arg == "--requests-per-worker") &&
               index + 1 < argc) {
      if (auto value = ParsePositiveInt(argv[++index]); value) {
        options.requests_per_worker = *value;
      }
    } else if (arg == "--timeout-ms" && index + 1 < argc) {
      if (auto value = ParsePositiveInt(argv[++index]); value) {
        options.timeout_ms = *value;
      }
    }
  }
  return options;
}

// 生成形如 20260327_153000 的时间戳字符串, 用于报告文件命名和日志文件命名
std::string MakeTimestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t current_time = std::chrono::system_clock::to_time_t(now);
  std::tm time_info{};

  // 可重入版本的 localtime (不可重入版本的 localtime 不是线程安全的,
  // 用了静态变量来做缓冲)
  localtime_r(&current_time, &time_info);  // 时间戳 -> 时间结构体

  return std::format("{:%Y%m%d_%H%M%S}", time_info);
}

std::string MakeReportPath(const std::string& timestamp) {
  return "logs/benchmark_report_" + timestamp + ".json";
}

std::string MakeClientLogPath(const std::string& timestamp) {
  return "logs/benchmark_client_" + timestamp + ".log";
}

struct Metrics {
  // 成功率 (成功请求数 / 总请求数)
  double success_rate{0.0};
  // 平均延迟 (毫秒)
  double avg_latency_ms{0.0};
  // P95/P99 延迟 (毫秒)
  double p95_latency_ms{0.0};
  double p99_latency_ms{0.0};
};

// 计算分位延迟。
// 参数：
//   - sorted_latencies: 已按升序排序的延迟样本（毫秒）；
//   - quantile: 分位点（例如 0.95/0.99）。
// 返回：对应分位延迟（毫秒）；当样本为空时返回 0。
// 设计原因：采用“向上取整后减一”的离散索引规则，保证尾部延迟统计更保守。
double QuantileMs(const std::vector<double>& sorted_latencies,
                  double quantile) {
  if (sorted_latencies.empty()) {
    return 0.0;
  }
  const std::size_t index = static_cast<std::size_t>(
      std::ceil(quantile * static_cast<double>(sorted_latencies.size())) - 1.0);
  return sorted_latencies[std::min(index, sorted_latencies.size() - 1)];
}

// 由原始统计值构建核心指标。
// 参数：
//   - total_requests: 总请求数；
//   - latencies_ms: 成功请求的延迟样本（毫秒）；
//   - success_count: 业务成功请求数。
// 返回：Metrics，包含成功率、平均延迟与分位延迟。
// 错误语义：当 total_requests 或样本为空时，返回值中的对应指标保持 0。
Metrics BuildMetrics(int total_requests,
                     const std::vector<double>& latencies_ms,
                     int success_count) {
  Metrics metrics;
  if (total_requests > 0) {
    metrics.success_rate = 100.0 * static_cast<double>(success_count) /
                           static_cast<double>(total_requests);
  }
  if (latencies_ms.empty()) {
    return metrics;
  }

  double total_latency = 0.0;
  for (const double latency : latencies_ms) {
    total_latency += latency;
  }
  metrics.avg_latency_ms =
      total_latency / static_cast<double>(latencies_ms.size());

  std::vector<double> sorted = latencies_ms;
  std::sort(sorted.begin(), sorted.end());
  metrics.p95_latency_ms = QuantileMs(sorted, 0.95);
  metrics.p99_latency_ms = QuantileMs(sorted, 0.99);
  return metrics;
}

// 将压测结果写入 JSON 报告文件，便于后续回归对比。
// 参数：
//   - report_path: 输出文件路径；
//   - options: 压测配置；
//   - total_requests/success_count/framework_failures/business_failures:
//   计数指标；
//   - elapsed_ms/qps: 吞吐指标；
//   - metrics: 延迟与成功率指标；
//   - launch_error: 线程启动阶段错误信息（若存在）。
// 错误语义：目录创建或文件打开失败时仅记录日志并返回，不抛异常、不终止主流程。
void WriteBenchmarkReport(const std::string& report_path,
                          const BenchmarkOptions& options, int total_requests,
                          int success_count, int framework_failures,
                          int business_failures, long long elapsed_ms,
                          double qps, const Metrics& metrics,
                          const std::optional<std::string>& launch_error) {
  const std::filesystem::path output_path(report_path);
  if (!output_path.parent_path().empty()) {
    std::error_code error;
    std::filesystem::create_directories(output_path.parent_path(), error);
    if (error) {
      LOG(Warn) << "failed to create benchmark report directory: "
                << error.message();
      return;
    }
  }

  std::ofstream output(output_path, std::ios::out | std::ios::trunc);
  if (!output.is_open()) {
    LOG(Warn) << "failed to open benchmark report file: " << report_path;
    return;
  }

  output << std::fixed << std::setprecision(2);
  output << "{\n";
  output << "  \"concurrency\": " << options.concurrency << ",\n";
  output << "  \"requests_per_worker\": " << options.requests_per_worker
         << ",\n";
  output << "  \"timeout_ms\": " << options.timeout_ms << ",\n";
  output << "  \"total_requests\": " << total_requests << ",\n";
  output << "  \"success_count\": " << success_count << ",\n";
  output << "  \"framework_failures\": " << framework_failures << ",\n";
  output << "  \"business_failures\": " << business_failures << ",\n";
  output << "  \"success_rate\": " << metrics.success_rate << ",\n";
  output << "  \"avg_latency_ms\": " << metrics.avg_latency_ms << ",\n";
  output << "  \"p95_latency_ms\": " << metrics.p95_latency_ms << ",\n";
  output << "  \"p99_latency_ms\": " << metrics.p99_latency_ms << ",\n";
  output << "  \"elapsed_ms\": " << elapsed_ms << ",\n";
  output << "  \"qps\": " << qps;
  if (launch_error.has_value()) {
    output << ",\n";
    output << "  \"launch_error\": \"" << *launch_error << "\"\n";
  } else {
    output << "\n";
  }
  output << "}\n";
}

}  // namespace

int main(int argc, char** argv) {
  // 设计原因：benchmark 关注的是延迟与吞吐，可观测性由日志与报告承担，关闭 core
  // dump 可避免磁盘噪声。 压测程序通常不需要 core dump, 避免生成大体积转储文件
  rlimit core_limit{};
  core_limit.rlim_cur = 0;  // 软限制
  core_limit.rlim_max = 0;  // 硬限制
  [[maybe_unused]] ::setrlimit(RLIMIT_CORE,
                               &core_limit);  // 设置 core dump 大小限制为 0

  // 初始化应用 (读取 -i 指定配置、初始化全局组件等)
  hxrpcApplication::Init(argc, argv);

  // 解析命令行与报告路径
  const BenchmarkOptions benchmark_options = ParseOptions(argc, argv);
  const std::string timestamp = MakeTimestamp();
  const std::string report_path = MakeReportPath(timestamp);

  // 配置 benchmark 客户端日志：默认仅写文件, 降低终端噪声
  hxrpc::LoggerOptions client_logger_options;
  client_logger_options.async_enabled = true;
  client_logger_options.stderr_enabled = false;
  client_logger_options.file_path = MakeClientLogPath(timestamp);
  client_logger_options.min_level = hxrpc::LogLevel::kWarn;
  if (auto logger_result =
          hxrpc::Logger::Instance().Configure(client_logger_options);
      !logger_result) {
    LOG(Warn) << "failed to configure benchmark client logger: "
              << logger_result.error();
  }

  auto server_config_result =
      hxrpc::SettingsLoader::LoadServerConfig(hxrpcApplication::GetConfig());
  if (!server_config_result) {
    LOG(Error) << "failed to build server-derived config: "
               << server_config_result.error();
    return EXIT_FAILURE;
  }

  const auto* login_method =
      Kuser::UserServiceRpc::descriptor()->FindMethodByName("Login");
  if (login_method == nullptr) {
    LOG(Error) << "failed to find Login method descriptor";
    return EXIT_FAILURE;
  }

  hxrpc::ClientConfig client_config;
  // 这里复用配置中的发现信息, 并指定静态服务地址, 方便 benchmark 开箱即跑
  client_config.discovery = server_config_result->discovery;
  client_config.discovery.static_services["UserServiceRpc.Login"] = {
      server_config_result->listen_endpoint,
  };
  client_config.discovery.static_services["UserServiceRpc.Register"] = {
      server_config_result->listen_endpoint,
  };
  client_config.serialization.backend = hxrpc::SerializationBackend::kProtobuf;
  client_config.call_options.timeout_ms = benchmark_options.timeout_ms;

  // 全局请求规模 = 并发线程数 * 每线程请求数
  const int concurrency = benchmark_options.concurrency;
  const int requests_per_worker = benchmark_options.requests_per_worker;
  const int total_requests = concurrency * requests_per_worker;

  // 统计项：
  // - framework_failures: 框架层失败 (网络/超时/协议等)
  // - business_failures: 业务层失败 (RPC 成功返回但 success=false)
  int success_count = 0;
  int framework_failures = 0;
  int business_failures = 0;

  // 并发写统计数据需要互斥保护
  std::mutex metrics_mutex;
  std::vector<double> successful_latencies_ms;
  successful_latencies_ms.reserve(static_cast<std::size_t>(total_requests));
  std::vector<std::thread> workers;
  workers.reserve(static_cast<std::size_t>(concurrency));
  std::optional<std::string> launch_error;

  const auto started_at = std::chrono::steady_clock::now();

  // 启动并发 worker, 每个 worker 内部串行发 requests_per_worker 次请求
  for (int worker_index = 0; worker_index < concurrency; ++worker_index) {
    try {
      workers.emplace_back([&, worker_index]() {
        // 每个线程独立持有 RpcClient, 减少共享状态干扰
        hxrpc::RpcClient client(client_config);
        for (int request_index = 0; request_index < requests_per_worker;
             ++request_index) {
          // 构造固定请求体, 便于不同轮 benchmark 横向对比
          Kuser::LoginRequest request;
          request.set_name("alice");
          request.set_pwd("123456");

          Kuser::LoginResponse response;
          hxrpc::CallOptions options = client_config.call_options;
          // 通过 metadata 注入 worker 信息, 便于服务端排查
          options.metadata = "benchmark_worker=" + std::to_string(worker_index);

          // 统计单次请求耗时 (仅成功请求会进入延迟样本)
          const auto request_started_at = std::chrono::steady_clock::now();
          const auto result =
              client.Invoke(login_method, request, response, options);
          const auto request_elapsed =
              std::chrono::duration_cast<
                  std::chrono::duration<double, std::milli>>(
                  std::chrono::steady_clock::now() - request_started_at)
                  .count();

          std::lock_guard<std::mutex> lock(metrics_mutex);
          if (!result) {
            ++framework_failures;
            LOG(Warn) << "benchmark request failed worker=" << worker_index
                      << " request=" << request_index
                      << " message=" << result.error().message;
            continue;
          }

          if (!response.success()) {
            ++business_failures;
            continue;
          }

          // 仅业务成功请求计入 success_count 与延迟样本
          ++success_count;
          successful_latencies_ms.push_back(request_elapsed);
        }
      });
    } catch (const std::system_error& error) {
      // 线程创建失败通常意味着系统资源吃紧, 提前结束启动流程
      launch_error = error.what();
      LOG(Error) << "failed to launch benchmark worker " << worker_index << ": "
                 << error.what();
      break;
    }
  }

  for (auto& worker : workers) {
    // 等待所有 worker 完成, 保证统计口径完整
    worker.join();
  }

  // 计算总耗时/QPS/分位延迟等指标
  const auto elapsed = std::chrono::steady_clock::now() - started_at;
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
  const double seconds =
      std::chrono::duration_cast<std::chrono::duration<double>>(elapsed)
          .count();
  const double qps =
      seconds > 0.0 ? static_cast<double>(total_requests) / seconds : 0.0;
  const Metrics metrics =
      BuildMetrics(total_requests, successful_latencies_ms, success_count);

  WriteBenchmarkReport(report_path, benchmark_options, total_requests,
                       success_count, framework_failures, business_failures,
                       elapsed_ms.count(), qps, metrics, launch_error);

  // 控制台输出一份简洁摘要, 便于本地直接观察结果
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "Benchmark completed" << std::endl;
  std::cout << "report_path=" << report_path << std::endl;
  std::cout << "concurrency=" << concurrency << std::endl;
  std::cout << "requests_per_worker=" << requests_per_worker << std::endl;
  std::cout << "timeout_ms=" << benchmark_options.timeout_ms << std::endl;
  std::cout << "total_requests=" << total_requests << std::endl;
  std::cout << "success_count=" << success_count << std::endl;
  std::cout << "framework_failures=" << framework_failures << std::endl;
  std::cout << "business_failures=" << business_failures << std::endl;
  std::cout << "success_rate=" << metrics.success_rate << std::endl;
  std::cout << "avg_latency_ms=" << metrics.avg_latency_ms << std::endl;
  std::cout << "p95_latency_ms=" << metrics.p95_latency_ms << std::endl;
  std::cout << "p99_latency_ms=" << metrics.p99_latency_ms << std::endl;
  std::cout << "elapsed_ms=" << elapsed_ms.count() << std::endl;
  std::cout << "qps=" << qps << std::endl;
  if (launch_error.has_value()) {
    std::cout << "launch_error=" << *launch_error << std::endl;
  }

  // 仅当无框架失败且线程启动无异常时返回成功
  return framework_failures == 0 && !launch_error.has_value() ? EXIT_SUCCESS
                                                              : EXIT_FAILURE;
}
