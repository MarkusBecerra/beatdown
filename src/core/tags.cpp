#include "core/tags.hpp"

namespace beatdown {

Tags tags_from_filename(string_view stem) {
    Tags t;
    auto pos = stem.find(" - ");
    if (pos == string_view::npos) {
        t.title = string(stem);
        return t;
    }
    t.artist = string(stem.substr(0, pos));
    t.title = string(stem.substr(pos + 3));
    return t;
}

string strip_suffixes(string title, const vector<string>& suffixes) {
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
