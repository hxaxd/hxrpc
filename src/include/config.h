#ifndef HXRPC_CONFIG_H
#define HXRPC_CONFIG_H

// src/include/config.h
// 轻量配置加载器接口
// 设计定位: 解析项目约定的 YAML 子集, 并以扁平
// key-value (a.b.c) 形式提供读取能力

#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>

class hxrpcconfig {
 public:
  // 加载配置文件
  // 参数: config_file - 配置文件路径
  // 返回:
  //   - 成功: std::expected<void, std::string>{};
  //   - 失败: unexpected(error_message)
  // 错误语义: 仅返回错误文本, 不抛异常
  [[nodiscard]] std::expected<void, std::string> LoadConfigFile(
      const char* config_file);

  // 按 key 读取配置项
  // 参数: key - 扁平键名 (例如 logging.file_path)
  // 返回: 命中的值若不存在返回空字符串
  [[nodiscard]] std::string Load(std::string_view key) const;

  // 返回全部配置条目 (只读视图)
  // 返回: 内部 map 的常量引用
  [[nodiscard]] const std::unordered_map<std::string, std::string>& Entries()
      const;

  // 原地去除字符串首尾空白字符
  // 参数: value - 待处理字符串
  static void Trim(std::string& value);

 private:
  std::unordered_map<std::string, std::string> config_map_;
};

#endif
