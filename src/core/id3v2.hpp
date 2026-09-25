#pragma once
#include <cstddef>
#include "core/std_names.hpp"
#include "core/tags.hpp"

namespace beatdown {
bool parse_id3v2(string_view bytes, Tags& out, size_t* tag_size = nullptr);
Tags read_wav_id3_chunk(const fs::path& wav);
string build_id3v2_for_test(const Tags& tags, int encoding);
}
