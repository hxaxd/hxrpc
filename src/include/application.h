#ifndef HXRPC_APPLICATION_H
#define HXRPC_APPLICATION_H

#include "config.h"
#include <mutex>

class hxrpcApplication {
public:
  static void Init(int argc, char **argv);
  static hxrpcApplication &GetInstance();
  static hxrpcconfig &GetConfig();

private:
  static void DeleteInstance();
  static hxrpcApplication *application_;
  static hxrpcconfig config_;
  static std::mutex mutex_;

  hxrpcApplication() = default;
  ~hxrpcApplication() = default;
  hxrpcApplication(const hxrpcApplication &) = delete;
  hxrpcApplication &operator=(const hxrpcApplication &) = delete;
};

#endif
