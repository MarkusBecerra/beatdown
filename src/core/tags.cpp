#include "core/tags.hpp"

namespace beatdown {

Tags tags_from_filename(std::string_view stem) {
    Tags t;
    auto pos = stem.find(" - ");
    if (pos == std::string_view::npos) {
        t.title = std::string(stem);
        return t;
    }
    t.artist = std::string(stem.substr(0, pos));
    t.title = std::string(stem.substr(pos + 3));
    return t;
}

std::string strip_suffixes(std::string title, const std::vector<std::string>& suffixes) {
    for (const auto& s : suffixes) {
        if (s.empty() || title.size() <= s.size()) continue;
        if (title.compare(title.size() - s.size(), s.size(), s) == 0)
            title.erase(title.size() - s.size());
    }
    return title;
}

Tags merge_tags(Tags p, const Tags& f) {
    if (!p.title) p.title = f.title;
    if (!p.artist) p.artist = f.artist;
    if (!p.album) p.album = f.album;
    if (!p.date) p.date = f.date;
    if (!p.track) p.track = f.track;
    if (!p.genre) p.genre = f.genre;
    if (!p.comment) p.comment = f.comment;
    return p;
}

}  // namespace beatdown
