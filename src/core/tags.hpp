#pragma once
#include "core/std_names.hpp"

namespace beatdown {

struct Tags {
    optional<string> title, artist, album, date, track, genre, comment;
    bool empty() const {
        return !title && !artist && !album && !date && !track && !genre && !comment;
    }
};

// "Artist - Title" -> {artist, title}; no separator -> {title only}. Casing untouched (R9).
Tags tags_from_filename(string_view stem);

// Remove each suffix that the title ends with, in the given order, once each. Never returns "".
string strip_suffixes(string title, const vector<string>& suffixes);

// Fields set in `primary` win; unset ones are taken from `fallback`.
Tags merge_tags(Tags primary, const Tags& fallback);

}  // namespace beatdown
