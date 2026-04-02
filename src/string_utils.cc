// src/string_utils.cc
// 内部字符串工具实现

#include "string_utils.h"

namespace hxrpc::detail {

std::string TrimCopy(std::string value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string StripQuotes(std::string value) {
  if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                            (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

std::size_t CountIndent(std::string_view line) {
  std::size_t indent = 0;
  while (indent < line.size() &&
         (line[indent] == ' ' || line[indent] == '\t')) {
    ++indent;
  }
  return indent;
}

std::vector<std::string> Split(std::string_view raw, char delimiter) {
  std::vector<std::string> parts;
  std::size_t cursor = 0;
  while (cursor <= raw.size()) {
    const auto next = raw.find(delimiter, cursor);
    const auto size =
        next == std::string_view::npos ? raw.size() - cursor : next - cursor;
    parts.emplace_back(raw.substr(cursor, size));
    if (next == std::string_view::npos) {
      break;
    }
    cursor = next + 1;
  }
  return parts;
}

}  // namespace hxrpc::detail
