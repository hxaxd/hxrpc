#include "../user.pb.h"
#include "Logger.h"
#include "application.h"
#include "controller.h"
#include "channel.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>
#include <string.h>

std::string g_service_ip;
uint16_t g_service_port;

int g_concurrency = 3000;
int g_requests_per_conn = 25;
bool g_use_epoll = true;
int g_preconnect = 1000;
bool g_verbose = false;

struct Connection {
    int fd;
    int pending_requests;
    bool connected;
    int sent_count;
};

std::string encodeVarint(uint32_t value) {
    std::string result;
    while (value > 0x7F) {
        result.push_back((value & 0x7F) | 0x80);
        value >>= 7;
    }
    result.push_back(value & 0x7F);
    return result;
}

bool sendRpcRequest(int sockfd, const std::string& service_name, const std::string& method_name, 
                    const std::string& args_str) {
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
    
    std::string send_rpc_str;
    send_rpc_str.reserve(4 + 4 + header_size + args_str.size());
    send_rpc_str.append((char*)&net_total_len, 4);
    send_rpc_str.append((char*)&net_header_len, 4);
    send_rpc_str.append(rpc_header_str);
    send_rpc_str.append(args_str);
    
    ssize_t sent = send(sockfd, send_rpc_str.c_str(), send_rpc_str.size(), 0);
    return sent == (ssize_t)send_rpc_str.size();
}

int createConnection(const std::string& ip, uint16_t port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;
    
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);
    
    connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    return sockfd;
}

void runEpollMode(const std::string& args_str) {
    int total_requests = g_concurrency * g_requests_per_conn;
    
    std::cout << "Running in EPOLL mode (single thread)" << std::endl;
    
    std::vector<Connection> conns;
    conns.reserve(g_concurrency);
    
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        std::cerr << "epoll_create1 failed" << std::endl;
        return;
    }
    
    struct epoll_event ev;
    ev.events = EPOLLOUT | EPOLLIN;
    
    for (int i = 0; i < g_concurrency; ++i) {
        int fd = createConnection(g_service_ip, g_service_port);
        if (fd < 0) {
            continue;
        }
        
        Connection conn;
        conn.fd = fd;
        conn.pending_requests = g_requests_per_conn;
        conn.connected = false;
        conn.sent_count = 0;
        conns.push_back(conn);
        
        ev.data.u32 = i;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
    }
    
    if (g_verbose) {
        std::cout << "Established " << conns.size() << " connections" << std::endl;
    }
    
    std::atomic<int64_t> success_count(0);
    std::vector<double> latencies;
    latencies.reserve(total_requests);
    std::mutex latencies_mutex;
    
    std::vector<std::chrono::high_resolution_clock::time_point> start_times;
    start_times.resize(total_requests);
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    const int max_events = 8192;
    struct epoll_event events[max_events];
    
    int completed = 0;
    int sent_total = 0;
    
    while (completed < total_requests) {
        int n = epoll_wait(epoll_fd, events, max_events, -1);
        
        for (int i = 0; i < n; ++i) {
            int idx = events[i].data.u32;
            Connection& conn = conns[idx];
            
            if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                continue;
            }
            
            if (events[i].events & EPOLLOUT) {
                if (!conn.connected) {
                    conn.connected = true;
                }
                
                while (conn.pending_requests > 0) {
                    if (sendRpcRequest(conn.fd, "UserServiceRpc", "Login", args_str)) {
                        start_times[sent_total] = std::chrono::high_resolution_clock::now();
                        sent_total++;
                        conn.pending_requests--;
                        conn.sent_count++;
                    } else {
                        break;
                    }
                }
            }
            
            if (events[i].events & EPOLLIN) {
                char len_buf[4];
                ssize_t nread = recv(conn.fd, len_buf, 4, MSG_DONTWAIT);
                if (nread == 4) {
                    uint32_t response_len = ntohl(*(uint32_t*)len_buf);
                    if (response_len > 0 && response_len < 65536) {
                        char buf[65536];
                        recv(conn.fd, buf, response_len, MSG_DONTWAIT);
                    }
                    
                    if (completed < total_requests) {
                        auto end = std::chrono::high_resolution_clock::now();
                        double latency_us = std::chrono::duration<double, std::micro>(end - start_times[completed]).count();
                        
                        std::lock_guard<std::mutex> lock(latencies_mutex);
                        latencies.push_back(latency_us);
                        success_count++;
                        completed++;
                    }
                }
            }
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;
    
    std::sort(latencies.begin(), latencies.end());
    
    double stats_min = 0, stats_max = 0, stats_avg = 0;
    double stats_p50 = 0, stats_p90 = 0, stats_p95 = 0, stats_p99 = 0, stats_p999 = 0;
    
    if (!latencies.empty()) {
        size_t n = latencies.size();
        stats_min = latencies[0];
        stats_max = latencies[n - 1];
        double sum = 0;
        for (double l : latencies) sum += l;
        stats_avg = sum / n;
        stats_p50 = latencies[n * 0.50];
        stats_p90 = latencies[n * 0.90];
        stats_p95 = latencies[n * 0.95];
        stats_p99 = latencies[n * 0.99];
        stats_p999 = latencies[n * 0.999];
    }
    
    int64_t total_success = success_count.load();
    double qps = total_requests / elapsed.count();
    
    std::cout << "\n========== RESULTS ==========" << std::endl;
    std::cout << "Total requests:    " << total_requests << std::endl;
    std::cout << "Success:           " << total_success << std::endl;
    std::cout << "Elapsed time:      " << elapsed.count() << " seconds" << std::endl;
    std::cout << "QPS:               " << (int)qps << std::endl;
    std::cout << "=======================" << std::endl;
    std::cout << "Latency (us):" << std::endl;
    std::cout << "  Min:     " << stats_min << std::endl;
    std::cout << "  Avg:     " << stats_avg << std::endl;
    std::cout << "  P50:     " << stats_p50 << std::endl;
    std::cout << "  P90:     " << stats_p90 << std::endl;
    std::cout << "  P95:     " << stats_p95 << std::endl;
    std::cout << "  P99:     " << stats_p99 << std::endl;
    std::cout << "  P99.9:   " << stats_p999 << std::endl;
    std::cout << "  Max:     " << stats_max << std::endl;
    std::cout << "=======================" << std::endl;
    
    for (auto& conn : conns) {
        close(conn.fd);
    }
    close(epoll_fd);
}

struct ThreadData {
    int fd;
    int requests;
    std::atomic<int64_t>* success_count;
    std::atomic<int64_t>* fail_count;
    std::vector<double>* latencies;
    std::mutex* latencies_mutex;
};

void* threadWorker(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    
    Kuser::LoginRequest request;
    request.set_name("zhangsan");
    request.set_pwd("123456");
    std::string args_str;
    request.SerializeToString(&args_str);
    
    for (int i = 0; i < data->requests; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        
        bool ok = sendRpcRequest(data->fd, "UserServiceRpc", "Login", args_str);
        
        auto end = std::chrono::high_resolution_clock::now();
        double latency_us = std::chrono::duration<double, std::micro>(end - start).count();
        
        if (ok) {
            data->success_count->fetch_add(1);
        } else {
            data->fail_count->fetch_add(1);
        }
        
        {
            std::lock_guard<std::mutex> lock(*data->latencies_mutex);
            data->latencies->push_back(latency_us);
        }
    }
    
    return nullptr;
}

void runThreadMode(const std::string& args_str) {
    int total_requests = g_concurrency * g_requests_per_conn;
    
    std::cout << "Running in THREAD mode (" << g_concurrency << " threads)" << std::endl;
    
    std::vector<int> fds;
    fds.reserve(g_concurrency);
    
    for (int i = 0; i < g_concurrency; ++i) {
        int fd = createConnection(g_service_ip, g_service_port);
        if (fd >= 0) {
            fds.push_back(fd);
        }
        usleep(1000);
    }
    
    if (g_verbose) {
        std::cout << "Established " << fds.size() << " connections" << std::endl;
    }
    
    std::atomic<int64_t> success_count(0);
    std::atomic<int64_t> fail_count(0);
    std::vector<double> latencies;
    latencies.reserve(total_requests);
    std::mutex latencies_mutex;
    
    std::vector<pthread_t> threads;
    threads.reserve(fds.size());
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (int fd : fds) {
        ThreadData* data = new ThreadData();
        data->fd = fd;
        data->requests = g_requests_per_conn;
        data->success_count = &success_count;
        data->fail_count = &fail_count;
        data->latencies = &latencies;
        data->latencies_mutex = &latencies_mutex;
        
        pthread_t tid;
        pthread_create(&tid, nullptr, threadWorker, data);
        threads.push_back(tid);
    }
    
    for (pthread_t tid : threads) {
        pthread_join(tid, nullptr);
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;
    
    std::sort(latencies.begin(), latencies.end());
    
    double stats_min = 0, stats_max = 0, stats_avg = 0;
    double stats_p50 = 0, stats_p90 = 0, stats_p95 = 0, stats_p99 = 0, stats_p999 = 0;
    
    if (!latencies.empty()) {
        size_t n = latencies.size();
        stats_min = latencies[0];
        stats_max = latencies[n - 1];
        double sum = 0;
        for (double l : latencies) sum += l;
        stats_avg = sum / n;
        stats_p50 = latencies[n * 0.50];
        stats_p90 = latencies[n * 0.90];
        stats_p95 = latencies[n * 0.95];
        stats_p99 = latencies[n * 0.99];
        stats_p999 = latencies[n * 0.999];
    }
    
    int64_t total_success = success_count.load();
    int64_t total_fail = fail_count.load();
    double qps = total_requests / elapsed.count();
    
    std::cout << "\n========== RESULTS ==========" << std::endl;
    std::cout << "Total requests:    " << total_requests << std::endl;
    std::cout << "Success:           " << total_success << std::endl;
    std::cout << "Failed:            " << total_fail << std::endl;
    std::cout << "Elapsed time:      " << elapsed.count() << " seconds" << std::endl;
    std::cout << "QPS:               " << (int)qps << std::endl;
    std::cout << "=======================" << std::endl;
    std::cout << "Latency (us):" << std::endl;
    std::cout << "  Min:     " << stats_min << std::endl;
    std::cout << "  Avg:     " << stats_avg << std::endl;
    std::cout << "  P50:     " << stats_p50 << std::endl;
    std::cout << "  P90:     " << stats_p90 << std::endl;
    std::cout << "  P95:     " << stats_p95 << std::endl;
    std::cout << "  P99:     " << stats_p99 << std::endl;
    std::cout << "  P99.9:   " << stats_p999 << std::endl;
    std::cout << "  Max:     " << stats_max << std::endl;
    std::cout << "=======================" << std::endl;
    
    for (int fd : fds) {
        close(fd);
    }
}

int main(int argc, char** argv) {
    hxrpcApplication::Init(argc, argv);
    hxrpcLogger logger("Benchmark");
    
    hxrpcconfig& config = hxrpcApplication::GetConfig();
    std::string rpc_server_ip = config.Load("rpcserverip");
    std::string rpc_server_port = config.Load("rpcserverport");
    
    g_service_ip = rpc_server_ip;
    g_service_port = (uint16_t)std::stoi(rpc_server_port);
    
    if (config.Load("concurrency") != "") {
        g_concurrency = std::stoi(config.Load("concurrency"));
    }
    if (config.Load("requests_per_conn") != "") {
        g_requests_per_conn = std::stoi(config.Load("requests_per_conn"));
    }
    if (config.Load("use_epoll") != "") {
        g_use_epoll = (config.Load("use_epoll") == "true");
    }
    if (config.Load("preconnect") != "") {
        g_preconnect = std::stoi(config.Load("preconnect"));
    }
    if (config.Load("verbose") != "") {
        g_verbose = (config.Load("verbose") == "true");
    }
    
    std::cout << "=== High Performance RPC Benchmark ===" << std::endl;
    std::cout << "Server:    " << g_service_ip << ":" << g_service_port << std::endl;
    std::cout << "Mode:      " << (g_use_epoll ? "EPOLL (single thread)" : "THREAD (multi thread)") << std::endl;
    std::cout << "Conns:     " << g_concurrency << std::endl;
    std::cout << "Req/Conn:  " << g_requests_per_conn << std::endl;
    std::cout << "Total:     " << g_concurrency * g_requests_per_conn << std::endl;
    std::cout << "========================================" << std::endl;
    
    Kuser::LoginRequest request;
    request.set_name("zhangsan");
    request.set_pwd("123456");
    std::string args_str;
    request.SerializeToString(&args_str);
    
    if (g_use_epoll) {
        runEpollMode(args_str);
    } else {
        runThreadMode(args_str);
    }
    
    return 0;
}
