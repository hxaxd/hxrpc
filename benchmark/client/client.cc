#include "../user.pb.h"
#include "application.h"
#include "logger.h"
#include "rpc_client.h"
#include "settings.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <sys/resource.h>
#include <thread>
#include <vector>

namespace {

constexpr int kDefaultConcurrency = 32;
constexpr int kDefaultRequestsPerWorker = 4;
constexpr int kDefaultTimeoutMs = 1500;

struct BenchmarkOptions {
  int concurrency{kDefaultConcurrency};
  int requests_per_worker{kDefaultRequestsPerWorker};
  int timeout_ms{kDefaultTimeoutMs};
};

std::optional<int> ParsePositiveInt(const std::string &raw) {
  try {
    const int value = std::stoi(raw);
    if (value > 0) {
      return value;
    }
  } catch (...) {
  }
  return std::nullopt;
}

BenchmarkOptions ParseOptions(int argc, char **argv) {
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

std::string MakeTimestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t current_time = std::chrono::system_clock::to_time_t(now);
  std::tm time_info{};
#if defined(_WIN32)
  localtime_s(&time_info, &current_time);
#else
  localtime_r(&current_time, &time_info);
#endif

  std::ostringstream stream;
  stream << std::put_time(&time_info, "%Y%m%d_%H%M%S");
  return stream.str();
}

std::string MakeReportPath(const std::string &timestamp) {
  return "logs/benchmark_report_" + timestamp + ".json";
}

std::string MakeClientLogPath(const std::string &timestamp) {
  return "logs/benchmark_client_" + timestamp + ".log";
}

struct Metrics {
  double success_rate{0.0};
  double avg_latency_ms{0.0};
  double p95_latency_ms{0.0};
  double p99_latency_ms{0.0};
};

double QuantileMs(const std::vector<double> &sorted_latencies, double quantile) {
  if (sorted_latencies.empty()) {
    return 0.0;
  }
  const std::size_t index = static_cast<std::size_t>(
      std::ceil(quantile * static_cast<double>(sorted_latencies.size())) - 1.0);
  return sorted_latencies[std::min(index, sorted_latencies.size() - 1)];
}

Metrics BuildMetrics(int total_requests, const std::vector<double> &latencies_ms,
                     int success_count) {
  Metrics metrics;
  if (total_requests > 0) {
    metrics.success_rate =
        100.0 * static_cast<double>(success_count) / static_cast<double>(total_requests);
  }
  if (latencies_ms.empty()) {
    return metrics;
  }

  double total_latency = 0.0;
  for (const double latency : latencies_ms) {
    total_latency += latency;
  }
  metrics.avg_latency_ms = total_latency / static_cast<double>(latencies_ms.size());

  std::vector<double> sorted = latencies_ms;
  std::sort(sorted.begin(), sorted.end());
  metrics.p95_latency_ms = QuantileMs(sorted, 0.95);
  metrics.p99_latency_ms = QuantileMs(sorted, 0.99);
  return metrics;
}

void WriteBenchmarkReport(const std::string &report_path,
                          const BenchmarkOptions &options, int total_requests,
                          int success_count, int framework_failures,
                          int business_failures, long long elapsed_ms, double qps,
                          const Metrics &metrics,
                          const std::optional<std::string> &launch_error) {
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
  output << "  \"requests_per_worker\": " << options.requests_per_worker << ",\n";
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

} // namespace

int main(int argc, char **argv) {
  rlimit core_limit{};
  core_limit.rlim_cur = 0;
  core_limit.rlim_max = 0;
  (void)::setrlimit(RLIMIT_CORE, &core_limit);

  hxrpcApplication::Init(argc, argv);
  const BenchmarkOptions benchmark_options = ParseOptions(argc, argv);
  const std::string timestamp = MakeTimestamp();
  const std::string report_path = MakeReportPath(timestamp);

  hxrpc::LoggerOptions client_logger_options;
  client_logger_options.async_enabled = true;
  client_logger_options.stderr_enabled = false;
  client_logger_options.file_path = MakeClientLogPath(timestamp);
  client_logger_options.min_level = hxrpc::LogLevel::kWarn;
  if (auto logger_result = hxrpc::Logger::Instance().Configure(client_logger_options);
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

  const auto *login_method =
      Kuser::UserServiceRpc::descriptor()->FindMethodByName("Login");
  if (login_method == nullptr) {
    LOG(Error) << "failed to find Login method descriptor";
    return EXIT_FAILURE;
  }

  hxrpc::ClientConfig client_config;
  client_config.discovery = server_config_result->discovery;
  client_config.discovery.static_services["UserServiceRpc.Login"] = {
      server_config_result->listen_endpoint,
  };
  client_config.discovery.static_services["UserServiceRpc.Register"] = {
      server_config_result->listen_endpoint,
  };
  client_config.serialization.backend = hxrpc::SerializationBackend::kProtobuf;
  client_config.call_options.timeout_ms = benchmark_options.timeout_ms;

  const int concurrency = benchmark_options.concurrency;
  const int requests_per_worker = benchmark_options.requests_per_worker;
  const int total_requests = concurrency * requests_per_worker;

  int success_count = 0;
  int framework_failures = 0;
  int business_failures = 0;
  std::mutex metrics_mutex;
  std::vector<double> successful_latencies_ms;
  successful_latencies_ms.reserve(static_cast<std::size_t>(total_requests));
  std::vector<std::thread> workers;
  workers.reserve(static_cast<std::size_t>(concurrency));
  std::optional<std::string> launch_error;

  const auto started_at = std::chrono::steady_clock::now();

  for (int worker_index = 0; worker_index < concurrency; ++worker_index) {
    try {
      workers.emplace_back([&, worker_index]() {
        hxrpc::RpcClient client(client_config);
        for (int request_index = 0; request_index < requests_per_worker; ++request_index) {
          Kuser::LoginRequest request;
          request.set_name("alice");
          request.set_pwd("123456");

          Kuser::LoginResponse response;
          hxrpc::CallOptions options = client_config.call_options;
          options.metadata = "benchmark_worker=" + std::to_string(worker_index);

          const auto request_started_at = std::chrono::steady_clock::now();
          const auto result = client.Invoke(login_method, request, response, options);
          const auto request_elapsed =
              std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
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
          ++success_count;
          successful_latencies_ms.push_back(request_elapsed);
        }
      });
    } catch (const std::system_error &error) {
      launch_error = error.what();
      LOG(Error) << "failed to launch benchmark worker " << worker_index << ": "
                 << error.what();
      break;
    }
  }

  for (auto &worker : workers) {
    worker.join();
  }

  const auto elapsed = std::chrono::steady_clock::now() - started_at;
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
  const double seconds =
      std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();
  const double qps = seconds > 0.0 ? static_cast<double>(total_requests) / seconds : 0.0;
  const Metrics metrics =
      BuildMetrics(total_requests, successful_latencies_ms, success_count);

  WriteBenchmarkReport(report_path, benchmark_options, total_requests, success_count,
                       framework_failures, business_failures, elapsed_ms.count(), qps,
                       metrics, launch_error);

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

  return framework_failures == 0 && !launch_error.has_value() ? EXIT_SUCCESS
                                                              : EXIT_FAILURE;
}
