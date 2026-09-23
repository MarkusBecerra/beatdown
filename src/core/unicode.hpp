#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace beatdown {

// Invalid or truncated UTF-8 becomes U+FFFD, one per bad byte; decoding resumes at the next byte.
std::u16string utf8_to_utf16(std::string_view s);
// Honours a leading byte-order mark; an unpaired surrogate becomes U+FFFD.
std::string utf16_to_utf8(std::u16string_view s);
std::string latin1_to_utf8(std::string_view s);

// Tag text from files is only nominally UTF-8. Valid UTF-8 is returned unchanged, except that the
// noncharacters U+FFFE and U+FFFF each become U+FFFD: libFLAC's vorbis-comment validation rejects
// them outright, and libsndfile doesn't handle that rejection safely. Anything else that isn't
// valid UTF-8 is taken to be Windows-1252 (what older Windows tools write) and the whole string is
// re-decoded from it, with cp1252's five undefined bytes becoming U+FFFD. The result is always
// valid UTF-8.
std::string sanitize_utf8(std::string_view s);
std::string path_to_utf8(const std::filesystem::path& p);

// Builds a path from UTF-8 bytes via std::u8string, so `s` is interpreted as UTF-8
// on every platform (including Windows, where fs::path is natively UTF-16). Use this
// instead of the deprecated fs::u8path for any non-ASCII path.
std::filesystem::path path_from_utf8(std::string_view s);

}  // namespace beatdown
