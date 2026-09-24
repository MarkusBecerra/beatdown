#include "core/tags.hpp"

namespace beatdown {

Tags tags_from_filename(string_view stem) {
    Tags tags;
    auto pos = stem.find(" - ");
    if (pos == string_view::npos) {
        tags.title = string(stem);
        return tags;
    }
    tags.artist = string(stem.substr(0, pos));
    tags.title = string(stem.substr(pos + 3));
    return tags;
}

string strip_suffixes(string title, const vector<string>& suffixes) {
    for (const auto& suffix : suffixes) {
        if (suffix.empty() || title.size() <= suffix.size()) continue;
        if (title.compare(title.size() - suffix.size(), suffix.size(), suffix) == 0)
            title.erase(title.size() - suffix.size());
    }
    return title;
}

Tags merge_tags(Tags primary, const Tags& fallback) {
    if (!primary.title) primary.title = fallback.title;
    if (!primary.artist) primary.artist = fallback.artist;
    if (!primary.album) primary.album = fallback.album;
    if (!primary.date) primary.date = fallback.date;
    if (!primary.track) primary.track = fallback.track;
    if (!primary.genre) primary.genre = fallback.genre;
    if (!primary.comment) primary.comment = fallback.comment;
    return primary;
}

}  // namespace beatdown
