#include "core/scanner.hpp"
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
#include "core/platform/platform.hpp"
#include "core/unicode.hpp"

namespace fs = std::filesystem;

namespace beatdown {

bool is_audio_input(const fs::path& p) {
    std::string ext = path_to_utf8(p.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext == ".wav" || ext == ".wave" || ext == ".aif" || ext == ".aiff" || ext == ".aifc" || ext == ".flac";
}

namespace {

std::string ascii_lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// A path's identity for the collision rules in decide(): macOS and Windows volumes are
// case-insensitive by default, so "Track.mp3" and "track.mp3" name one file there. ASCII case
// only; normalized so "dir/./x" and "dir/x" match.
std::string path_key(const fs::path& p) {
    std::u8string g = p.lexically_normal().generic_u8string();
    return ascii_lower(std::string(reinterpret_cast<const char*>(g.data()), g.size()));
}

using FallbackByParent = std::unordered_map<std::string, std::vector<fs::path>>;   // canonical parent dir (UTF-8) -> sources there whose FileId couldn't be read

// True if `output` already exists and is filesystem-identical to one of the audio inputs, however
// that input's name happens to be spelled. Catches what path_key's ASCII-only case fold can't:
// NFC/NFD normalization twins, non-ASCII case twins (APFS folds both), and a destination that
// reaches a source folder by another path, such as a symlink.
//
// Compares file identity (device+inode / volume+file-index), not size: a prior version of this
// function prefiltered by size, but size is content, and content can change mid-scan (a source
// edited or replaced while a large batch is still being scanned, plausible on a network or
// cloud-synced library) — a stale cached size would then silently miss the real match. Identity
// doesn't change when content does, so `source_ids` (built once, before any job runs) stays valid
// for the whole scan. This also keeps the O(N) cost of a flat folder re-scanned in place (every
// output already exists): the common "no match" case is one file_id() call plus a hash lookup,
// not a linear fs::equivalent scan over every source (see 4635993's finding).
//
// A source whose id couldn't be read at scan time (rare: permission trouble, an exotic
// filesystem) can't be placed in `source_ids`, so it goes in `fallback_by_parent` instead, and is
// checked with fs::equivalent instead — the same cost `source_ids` exists to avoid, but only for
// that source, not every source in its directory.
bool overwrites_a_source(const fs::path& output, const std::unordered_set<platform::FileId>& source_ids,
                          const FallbackByParent& fallback_by_parent) {
    std::error_code ec;
    if (!fs::exists(output, ec)) return false;
    std::optional<platform::FileId> out_id = platform::file_id(output);
    if (out_id && source_ids.count(*out_id)) return true;
    if (out_id && fallback_by_parent.empty()) return false;   // every source's id is in source_ids; no match there
    fs::path parent = fs::weakly_canonical(output.parent_path(), ec);
    auto it = fallback_by_parent.find(path_to_utf8(parent));
    if (it == fallback_by_parent.end()) return false;
    for (const fs::path& s : it->second)
        if (fs::equivalent(s, output, ec)) return true;
    return false;
}

// Pairs an audio input with its output path; anything else is only counted.
void collect(const fs::path& file, const fs::path& rel_dir, const Options& opts, std::vector<Job>& jobs, int& ignored) {
    std::string name = path_to_utf8(file.filename());
    if (name.rfind("._", 0) == 0 || !is_audio_input(file)) { ++ignored; return; }
    Job j;
    j.source = file;
    // R-E: When rel_dir is "." (or empty), use empty path so output is destination / filename
    fs::path effective_rel_dir = (rel_dir.empty() || rel_dir == ".") ? fs::path() : rel_dir;
    j.output = opts.destination / effective_rel_dir / (file.stem().native() + fs::path(opts.output_extension()).native());
    std::error_code ec;
    j.source_bytes = static_cast<int64_t>(fs::file_size(file, ec));
    jobs.push_back(std::move(j));
}

// Splits `jobs` (sorted by source) into to_convert and skipped. Two sources can map to one
// output (track.wav and track.aiff -> track.mp3): the first in sorted order keeps it and the rest
// are skipped, since parallel jobs writing one output would publish a mix of both. And no output
// may land on any audio input's path (with --format flac, track.wav -> track.flac), whatever
// became of that input, since that would replace a source file.
void decide(std::vector<Job>& jobs, const Options& opts, Plan& plan) {
    std::unordered_set<std::string> sources;          // path_key of every audio input
    std::unordered_set<platform::FileId> source_ids;  // file identity of every audio input whose id could be read
    FallbackByParent fallback_by_parent;   // canonical parent dir -> sources whose id couldn't be read
    for (const Job& j : jobs) {
        sources.insert(path_key(j.source));
        if (std::optional<platform::FileId> id = platform::file_id(j.source))
            source_ids.insert(*id);
        else {
            std::error_code ec;
            fs::path parent = fs::weakly_canonical(j.source.parent_path(), ec);
            fallback_by_parent[path_to_utf8(parent)].push_back(j.source);
        }
    }
    std::unordered_map<std::string, std::string> claimed;   // output key -> filename of the source that keeps it
    for (Job& j : jobs) {
        std::error_code ec;
        std::string out = path_key(j.output);
        if (out == path_key(j.source) || (fs::exists(j.output, ec) && fs::equivalent(j.source, j.output, ec)))
            j.note = "output would be the source file";
        else if (sources.count(out) || overwrites_a_source(j.output, source_ids, fallback_by_parent))
            j.note = "output would overwrite a source file";
        else if (auto [it, fresh] = claimed.emplace(out, path_to_utf8(j.source.filename())); !fresh)
            j.note = "same output as " + it->second;
        else if (!opts.overwrite && fs::exists(j.output, ec))
            j.note = "output exists";
        (j.note.empty() ? plan.to_convert : plan.skipped).push_back(std::move(j));
    }
}

}  // namespace

Plan scan(const Options& opts, std::string& error) {
    Plan plan;
    std::error_code ec;
    if (!fs::exists(opts.source, ec)) { error = "source does not exist: " + path_to_utf8(opts.source); return plan; }
    std::vector<Job> jobs;
    if (fs::is_regular_file(opts.source, ec)) {
        collect(opts.source, fs::path(), opts, jobs, plan.ignored);
    } else {
        fs::path dest_canon = fs::weakly_canonical(opts.destination, ec);
        auto visit = [&](const fs::directory_entry& e) {
            if (!e.is_regular_file(ec)) return;
            collect(e.path(), fs::relative(e.path().parent_path(), opts.source, ec), opts, jobs, plan.ignored);
        };
        if (opts.recursive) {
            fs::recursive_directory_iterator it(opts.source, fs::directory_options::skip_permission_denied, ec), end;
            for (; it != end; it.increment(ec)) {
                if (it->is_directory(ec) && fs::weakly_canonical(it->path(), ec) == dest_canon) { it.disable_recursion_pending(); continue; }
                visit(*it);
            }
        } else {
            for (const auto& e : fs::directory_iterator(opts.source, fs::directory_options::skip_permission_denied, ec)) visit(e);
        }
    }
    std::sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) { return a.source < b.source; });
    decide(jobs, opts, plan);
    return plan;
}

}  // namespace beatdown
