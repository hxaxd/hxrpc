// src/config.cc
// 配置加载器实现
// 关键流程: 逐行读取 YAML 子集 -> 维护缩进 section 栈 -> 生成扁平路径键值
// 设计原因: 避免引入完整 YAML 依赖, 在教学与轻量部署场景下降低构建复杂度

#include "config.h"

#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "string_utils.h"

void ParseYamlLine(std::string line,
                   std::vector<std::pair<std::size_t, std::string>>& sections,
                   std::unordered_map<std::string, std::string>& config_map) {
  // 参数:
  //   - line: 当前文本行
  //   - sections: 层级栈 (缩进 -> section 名)
  //   - config_map: 输出扁平配置表
  // 错误语义: 格式不完整的行会被忽略, 不中断加载流程
  const auto comment = line.find('#');
  if (comment != std::string::npos) {
    line.erase(comment);
  }
  if (hxrpc::detail::TrimCopy(line).empty()) {
    return;
  }

  const std::size_t indent = hxrpc::detail::CountIndent(line);
  std::string trimmed = hxrpc::detail::TrimCopy(line);
  const auto delimiter = trimmed.find(':');
  if (delimiter == std::string::npos) {
    return;
  }

  std::string key = hxrpc::detail::StripQuotes(
      hxrpc::detail::TrimCopy(trimmed.substr(0, delimiter)));
  std::string value = hxrpc::detail::StripQuotes(
      hxrpc::detail::TrimCopy(trimmed.substr(delimiter + 1)));

  while (!sections.empty() && sections.back().first >= indent) {
    sections.pop_back();
  }

  if (value.empty()) {
    sections.emplace_back(indent, key);
    return;
  }

  std::string path;
  for (const auto& [section_indent, section] : sections) {
    (void)section_indent;
    if (!path.empty()) {
      path.push_back('.');
    }
    path += section;
  }
  if (!path.empty()) {
    path.push_back('.');
  }
  path += key;
  config_map[path] = value;
}

std::expected<void, std::string> hxrpcconfig::LoadConfigFile(
    const char* config_file) {
  // 错误语义: 文件无法打开时返回 unexpected, 不抛异常
  FILE* raw_file = std::fopen(config_file, "r");
  if (raw_file == nullptr) {
    return std::unexpected("failed to open config file");
  }

  std::unique_ptr<FILE, decltype(&std::fclose)> file(raw_file, &std::fclose);
  config_map_.clear();
  std::vector<std::pair<std::size_t, std::string>> yaml_sections;

  char buffer[1024];
  while (std::fgets(buffer, sizeof(buffer), file.get()) != nullptr) {
    std::string line(buffer);
    const std::string trimmed = hxrpc::detail::TrimCopy(line);
    if (trimmed.empty() || trimmed.front() == '#') {
      continue;
    }

    ParseYamlLine(line, yaml_sections, config_map_);
  }

  return {};
}

std::string hxrpcconfig::Load(std::string_view key) const {
  // 查询失败返回空字符串, 调用方可通过 empty 判定是否配置缺失
  const auto it = config_map_.find(std::string(key));
  if (it == config_map_.end()) {
    return {};
  }
  return it->second;
}

const std::unordered_map<std::string, std::string>& hxrpcconfig::Entries()
    const {
  return config_map_;
}

void hxrpcconfig::Trim(std::string& value) {
  // 原地裁剪首尾空白, 保留中间内容不变
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    value.clear();
    return;
  }

  const auto end = value.find_last_not_of(" \t\r\n");
  value = value.substr(begin, end - begin + 1);
}
