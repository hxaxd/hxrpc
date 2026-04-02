#ifndef HXRPC_STRING_UTILS_H
#define HXRPC_STRING_UTILS_H

#include <string>
#include <string_view>
#include <vector>

namespace hxrpc::detail {

[[nodiscard]] std::string TrimCopy(std::string value);
[[nodiscard]] std::string StripQuotes(std::string value);
[[nodiscard]] std::size_t CountIndent(std::string_view line);
[[nodiscard]] std::vector<std::string> Split(std::string_view raw,
                                             char delimiter);

}  // namespace hxrpc::detail

#endif
