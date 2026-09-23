#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include "core/tags.hpp"

namespace beatdown {
bool parse_id3v2(std::string_view bytes, Tags& out, size_t* tag_size = nullptr);
Tags read_wav_id3_chunk(const std::filesystem::path& wav);
std::string build_id3v2_for_test(const Tags& t, int encoding);
}
