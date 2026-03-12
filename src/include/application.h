#ifndef _hxrpcapplication_H
#define _hxrpcapplication_H
#include "channel.h"
#include "config.h"
#include "controller.h"
#include <mutex>
// hxrpc基础类，负责框架的一些初始化操作
class hxrpcApplication {
public:
  static void Init(int argc, char **argv);
  static hxrpcApplication &GetInstance();
  static void deleteInstance();
  static hxrpcconfig &GetConfig();

private:
  static hxrpcconfig m_config;
  static hxrpcApplication *m_application; // 全局唯一单例访问对象
  static std::mutex m_mutex;
  hxrpcApplication() {}
  ~hxrpcApplication() {}
  hxrpcApplication(const hxrpcApplication &) = delete;
  hxrpcApplication(hxrpcApplication &&) = delete;
};
#endif
