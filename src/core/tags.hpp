#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace beatdown {

struct Tags {
    std::optional<std::string> title, artist, album, date, track, genre, comment;
    bool empty() const {
        return !title && !artist && !album && !date && !track && !genre && !comment;
    }
};

// "Artist - Title" -> {artist, title}; no separator -> {title only}. Casing untouched (R9).
Tags tags_from_filename(std::string_view stem);

// Remove each suffix that the title ends with, in the given order, once each. Never returns "".
std::string strip_suffixes(std::string title, const std::vector<std::string>& suffixes);

// Fields set in `primary` win; unset ones are taken from `fallback`.
Tags merge_tags(Tags primary, const Tags& fallback);

}  // namespace beatdown
