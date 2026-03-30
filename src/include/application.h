#ifndef HXRPC_APPLICATION_H
#define HXRPC_APPLICATION_H

// src/include/application.h
// 应用级启动入口封装
// 职责: 统一管理配置加载与全局单例生命周期, 为其他模块提供只读配置访问点

#include <mutex>

#include "config.h"

class hxrpcApplication {
 public:
  // 初始化应用
  // 参数: argc/argv - 程序启动参数, 要求通过 `-i <config-file>` 指定配置路径
  // 返回: void
  // 错误语义: 配置缺失或加载失败时, 函数会记录错误并直接终止进程
  static void Init(int argc, char** argv);

  // 获取应用单例
  // 返回: 全局唯一实例引用
  // 错误语义: 不抛异常内部通过互斥锁保证并发安全
  static hxrpcApplication& GetInstance();

  // 获取全局配置对象
  // 返回: 已加载配置的可写引用 (用于框架内部读取与派生配置)
  static hxrpcconfig& GetConfig();

 private:
  static void DeleteInstance();
  static hxrpcApplication* application_;
  static hxrpcconfig config_;
  static std::mutex mutex_;

  hxrpcApplication() = default;
  ~hxrpcApplication() = default;
  hxrpcApplication(const hxrpcApplication&) = delete;
  hxrpcApplication& operator=(const hxrpcApplication&) = delete;
};

#endif
