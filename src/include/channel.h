#ifndef _hxrpcchannel_h_
#define _hxrpcchannel_h_

#include "zookeeperutil.h"
#include <google/protobuf/service.h>
#include <unistd.h>
class hxrpcChannel : public google::protobuf::RpcChannel {
public:
  hxrpcChannel(bool connectNow);
  virtual ~hxrpcChannel() {
    if (m_clientfd >= 0) {
      close(m_clientfd);
    }
  }
  void CallMethod(const ::google::protobuf::MethodDescriptor *method,
                  ::google::protobuf::RpcController *controller,
                  const ::google::protobuf::Message *request,
                  ::google::protobuf::Message *response,
                  ::google::protobuf::Closure *done)
      override; // override可以验证是否是虚函数
private:
  int m_clientfd; // 存放客户端套接字
  std::string service_name;
  std::string m_ip;
  uint16_t m_port;
  std::string method_name;
  int m_idx; // 用来划分服务器ip和port的下标
  bool newConnect(const char *ip, uint16_t port);
  std::string QueryServiceHost(ZkClient *zkclient, std::string service_name,
                               std::string method_name, int &idx);
  // 新增：确保读取指定长度的数据，解决TCP拆包
  ssize_t recv_exact(int fd, char *buf, size_t size);
};
#endif
