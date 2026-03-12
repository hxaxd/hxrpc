#include "../user.pb.h"
#include "Logger.h"
#include "application.h"
#include "config.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <cstdlib>

// 全局配置参数（仅保留线程模式相关）
std::string g_service_ip;
uint16_t g_service_port;
int g_concurrency = 3000;
int g_requests_per_conn = 25;

// 预先序列化好的完整请求包（包含长度头）
std::string g_prebuilt_packet;

// 帮助说明函数（仅保留线程模式相关选项）
void PrintUsage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n"
              << "Options:\n"
              << "  -i <config_file>   Specify the framework config file (e.g. -i test.conf)\n"
              << "  -c <num>           Concurrency (number of threads). Default: 3000\n"
              << "  -r <num>           Requests per connection. Default: 25\n"
              << "  --ip <ip>          Override server IP\n"
              << "  --port <port>      Override server Port\n"
              << "  -h, --help         Print this help message\n\n"
              << "Example:\n"
              << "  " << prog_name << " -i client.conf -c 5000 -r 100\n";
}

std::string encodeVarint(uint32_t value) {
    std::string result;
    while (value > 0x7F) {
        result.push_back((value & 0x7F) | 0x80);
        value >>= 7;
    }
    result.push_back(value & 0x7F);
    return result;
}

// 初始化时构建物理包，消除压测过程中的序列化开销
void PrebuildPacket(const std::string& service_name, const std::string& method_name, const std::string& args_str) {
    std::string rpc_header_str;
    rpc_header_str.push_back(0x0A);
    rpc_header_str += encodeVarint(service_name.size());
    rpc_header_str += service_name;
    rpc_header_str.push_back(0x12);
    rpc_header_str += encodeVarint(method_name.size());
    rpc_header_str += method_name;
    rpc_header_str.push_back(0x18);
    rpc_header_str += encodeVarint(args_str.size());
    
    uint32_t header_size = rpc_header_str.size();
    uint32_t total_len = 4 + header_size + args_str.size();
    uint32_t net_total_len = htonl(total_len);
    uint32_t net_header_len = htonl(header_size);
    
    g_prebuilt_packet.reserve(4 + 4 + header_size + args_str.size());
    g_prebuilt_packet.append((char*)&net_total_len, 4);
    g_prebuilt_packet.append((char*)&net_header_len, 4);
    g_prebuilt_packet.append(rpc_header_str);
    g_prebuilt_packet.append(args_str);
}

// 创建TCP连接（线程模式用阻塞连接）
int createConnection(const std::string& ip, uint16_t port, bool non_block = false) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;
    
    // 禁用 Nagle 算法，消除延迟，专为 RPC 优化
    int flag = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));

    if (non_block) {
        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    }
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);
    
    connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    return sockfd;
}

// 线程私有数据，纯无锁设计
struct ThreadData {
    int fd;
    int requests;
    int64_t success;
    std::vector<double> latencies;
};

void* threadWorker(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    data->success = 0;
    data->latencies.reserve(data->requests);
    
    char len_buf[4];
    char body_buf[8192];

    for (int i = 0; i < data->requests; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        
        send(data->fd, g_prebuilt_packet.c_str(), g_prebuilt_packet.size(), 0);
        
        // 阻塞接收，严格按照协议解析
        int n1 = recv(data->fd, len_buf, 4, MSG_WAITALL);
        if (n1 == 4) {
            uint32_t len = ntohl(*(uint32_t*)len_buf);
            if (len > 0 && len < sizeof(body_buf)) {
                int n2 = recv(data->fd, body_buf, len, MSG_WAITALL);
                if (n2 == (int)len) {
                    data->success++;
                }
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        double latency_us = std::chrono::duration<double, std::micro>(end - start).count();
        data->latencies.push_back(latency_us);
    }
    return nullptr;
}

void runThreadMode() {
    int total_requests = g_concurrency * g_requests_per_conn;
    std::cout << "Starting THREAD mode (" << g_concurrency << " threads)..." << std::endl;
    
    std::vector<int> fds;
    for (int i = 0; i < g_concurrency; ++i) {
        int fd = createConnection(g_service_ip, g_service_port, false);
        if (fd >= 0) fds.push_back(fd);
    }
    
    std::vector<pthread_t> threads;
    std::vector<ThreadData*> thread_datas;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (int fd : fds) {
        ThreadData* data = new ThreadData();
        data->fd = fd;
        data->requests = g_requests_per_conn;
        thread_datas.push_back(data);
        
        pthread_t tid;
        pthread_create(&tid, nullptr, threadWorker, data);
        threads.push_back(tid);
    }
    
    int64_t total_success = 0;
    std::vector<double> all_latencies;
    all_latencies.reserve(total_requests);

    // Map-Reduce: 在主线程统一聚合，做到真正的 0 锁开销
    for (size_t i = 0; i < threads.size(); ++i) {
        pthread_join(threads[i], nullptr);
        total_success += thread_datas[i]->success;
        all_latencies.insert(all_latencies.end(), 
                             thread_datas[i]->latencies.begin(), 
                             thread_datas[i]->latencies.end());
        close(thread_datas[i]->fd);
        delete thread_datas[i];
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;
    
    std::sort(all_latencies.begin(), all_latencies.end());
    double stats_min = 0, stats_max = 0, stats_avg = 0;
    double stats_p50 = 0, stats_p90 = 0, stats_p99 = 0;
    
    if (!all_latencies.empty()) {
        size_t n = all_latencies.size();
        stats_min = all_latencies[0];
        stats_max = all_latencies[n - 1];
        double sum = 0;
        for (double l : all_latencies) sum += l;
        stats_avg = sum / n;
        stats_p50 = all_latencies[n * 0.50];
        stats_p90 = all_latencies[n * 0.90];
        stats_p99 = all_latencies[n * 0.99];
    }
    
    double qps = total_requests / elapsed.count();
    
    std::cout << "\n========== BENCHMARK RESULTS ==========" << std::endl;
    std::cout << "Total requests:    " << total_requests << std::endl;
    std::cout << "Success parsed:    " << total_success << std::endl;
    std::cout << "Elapsed time:      " << elapsed.count() << " seconds" << std::endl;
    std::cout << "QPS:               " << (int)qps << " req/s" << std::endl;
    std::cout << "=======================================" << std::endl;
    std::cout << "Latency (us):" << std::endl;
    std::cout << "  Min:     " << stats_min << std::endl;
    std::cout << "  Avg:     " << stats_avg << std::endl;
    std::cout << "  P50:     " << stats_p50 << std::endl;
    std::cout << "  P90:     " << stats_p90 << std::endl;
    std::cout << "  P99:     " << stats_p99 << std::endl;
    std::cout << "  Max:     " << stats_max << std::endl;
    std::cout << "=======================================" << std::endl;
}

// 修正main函数签名，保留命令行参数解析（-c/-r/--ip/--port等）
int main(int argc, char** argv) {
    // 1. 拦截 -h / --help 提前打印帮助信息
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            PrintUsage(argv[0]);
            return 0;
        }
    }

    // 2. 框架初始化（读取配置文件）
    hxrpcApplication::Init(argc, argv);
    hxrpcconfig& config = hxrpcApplication::GetConfig();
    
    // 3. 读取配置文件作为默认值
    std::string conf_ip = config.Load("rpcserverip");
    std::string conf_port = config.Load("rpcserverport");
    g_service_ip = conf_ip.empty() ? "127.0.0.1" : conf_ip;
    g_service_port = conf_port.empty() ? 8000 : (uint16_t)std::stoi(conf_port);
    
    if (config.Load("concurrency") != "") g_concurrency = std::stoi(config.Load("concurrency"));
    if (config.Load("requests_per_conn") != "") g_requests_per_conn = std::stoi(config.Load("requests_per_conn"));

    // 4. 手动解析命令行参数，优先级最高，覆盖配置文件
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-c" && i + 1 < argc) {
            g_concurrency = std::stoi(argv[++i]);
        } else if (arg == "-r" && i + 1 < argc) {
            g_requests_per_conn = std::stoi(argv[++i]);
        } else if (arg == "--ip" && i + 1 < argc) {
            g_service_ip = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            g_service_port = (uint16_t)std::stoi(argv[++i]);
        }
    }

    // 打印当前的压测配置
    std::cout << "=== High Performance RPC Benchmark ===" << std::endl;
    std::cout << "Target Server: " << g_service_ip << ":" << g_service_port << std::endl;
    std::cout << "Concurrency:   " << g_concurrency << " threads/connections" << std::endl;
    std::cout << "Reqs/Conn:     " << g_requests_per_conn << std::endl;
    std::cout << "Total Reqs:    " << (g_concurrency * g_requests_per_conn) << std::endl;
    std::cout << "======================================" << std::endl;
    
    // 初始化构建测试数据包
    Kuser::LoginRequest request;
    request.set_name("zhangsan_the_speed_demon");
    request.set_pwd("123456");
    std::string args_str;
    request.SerializeToString(&args_str);
    PrebuildPacket("UserServiceRpc", "Login", args_str);
    
    // 启动线程模式压测
    runThreadMode();
    
    return 0;
}