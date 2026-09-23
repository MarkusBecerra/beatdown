#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace beatdown {

std::u16string utf8_to_utf16(std::string_view s);
std::string utf16_to_utf8(std::u16string_view s);
std::string latin1_to_utf8(std::string_view s);
std::string path_to_utf8(const std::filesystem::path& p);

// Builds a path from UTF-8 bytes via std::u8string, so `s` is interpreted as UTF-8
// on every platform (including Windows, where fs::path is natively UTF-16). Use this
// instead of the deprecated fs::u8path for any non-ASCII path.
std::filesystem::path path_from_utf8(std::string_view s);

}  // namespace beatdown
