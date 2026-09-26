#include "core/scanner.hpp"
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
#include "core/platform/platform.hpp"
#include "core/std_names.hpp"
#include "core/unicode.hpp"

namespace beatdown {

bool is_audio_input(const fs::path& path) {
    string ext = path_to_utf8(path.extension());
    transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char character) { return tolower(character); });
    return ext == ".wav" || ext == ".wave" || ext == ".aif" || ext == ".aiff" || ext == ".aifc" || ext == ".flac" || ext == ".m4a";
}

namespace {

string ascii_lower(string text) {
    for (char& character : text) if (character >= 'A' && character <= 'Z') character = static_cast<char>(character - 'A' + 'a');
    return text;
}

// A path's identity for the collision rules in decide(): macOS and Windows volumes are
// case-insensitive by default, so "Track.mp3" and "track.mp3" name one file there. ASCII case
// only; normalized so "dir/./x" and "dir/x" match.
string path_key(const fs::path& path) {
    u8string generic_utf8 = path.lexically_normal().generic_u8string();
    return ascii_lower(string(reinterpret_cast<const char*>(generic_utf8.data()), generic_utf8.size()));
}

using FallbackByParent = unordered_map<string, vector<fs::path>>;   // canonical parent dir (UTF-8) -> sources there whose FileId couldn't be read

// If `output` already exists, returns the note to skip the job with; "" if there's no overwrite
// risk (including: output doesn't exist yet, which the caller checks other rules for). Catches
// what path_key's ASCII-only case fold can't: NFC/NFD normalization twins, non-ASCII case twins
// (APFS folds both), and a destination that reaches a source folder by another path, such as a
// symlink — however the colliding input's name happens to be spelled.
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
// Fails safe rather than guessing, in two places a plain bool result couldn't distinguish:
//  - `output`'s own identity can't be read even though it exists (a delete-and-recreate race, or
//    a transient metadata failure on a network/cloud volume): the job is skipped outright, with
//    its own note, rather than falling through as if there were no risk. Not retried with
//    fs::equivalent — a stat that just failed is likely to fail again.
//  - A source whose own id couldn't be read at scan time (rare: permission trouble, an exotic
//    filesystem) goes in `fallback_by_parent` instead of `source_ids`, checked with fs::equivalent
//    against any existing output in the same directory; if that check itself can't tell (fails
//    and sets its error_code), that's treated as a match too, not as "no risk".
string overwrites_a_source(const fs::path& output, const unordered_set<platform::FileId>& source_ids,
                                 const FallbackByParent& fallback_by_parent) {
    error_code fs_error;
    if (!fs::exists(output, fs_error)) return "";
    optional<platform::FileId> out_id = platform::file_id(output);
    if (!out_id) return "cannot check whether the output is a source file";
    if (source_ids.count(*out_id)) return "output would overwrite a source file";
    if (fallback_by_parent.empty()) return "";   // every source's id is in source_ids; no match there

    fs::path parent = fs::weakly_canonical(output.parent_path(), fs_error);
    auto parent_entry = fallback_by_parent.find(path_to_utf8(parent));
    if (parent_entry == fallback_by_parent.end()) return "";
    for (const fs::path& candidate : parent_entry->second) {
        bool equivalent = fs::equivalent(candidate, output, fs_error);
        if (equivalent || fs_error) return "output would overwrite a source file";
    }
    return "";
}

// Pairs an audio input with its output path; anything else is only counted.
void collect(const fs::path& file, const fs::path& rel_dir, const Options& options, vector<Job>& jobs, int& ignored) {
    string name = path_to_utf8(file.filename());
    if (name.rfind("._", 0) == 0 || !is_audio_input(file)) { ++ignored; return; }
    Job job;
    job.source = file;
    // R-E: When rel_dir is "." (or empty), use empty path so output is destination / filename
    fs::path effective_rel_dir = (rel_dir.empty() || rel_dir == ".") ? fs::path() : rel_dir;
    job.output = options.destination / effective_rel_dir / (file.stem().native() + fs::path(options.output_extension()).native());
    error_code fs_error;
    job.source_bytes = static_cast<int64_t>(fs::file_size(file, fs_error));
    jobs.push_back(std::move(job));
}

// Splits `jobs` (sorted by source) into to_convert and skipped. Two sources can map to one
// output (track.wav and track.aiff -> track.mp3): the first in sorted order keeps it and the rest
// are skipped, since parallel jobs writing one output would publish a mix of both. And no output
// may land on any audio input's path (with --format flac, track.wav -> track.flac), whatever
// became of that input, since that would replace a source file.
void decide(vector<Job>& jobs, const Options& options, Plan& plan) {
    unordered_set<string> sources;          // path_key of every audio input
    unordered_set<platform::FileId> source_ids;  // file identity of every audio input whose id could be read
    FallbackByParent fallback_by_parent;   // canonical parent dir -> sources whose id couldn't be read
    for (const Job& job : jobs) {
        sources.insert(path_key(job.source));
        if (optional<platform::FileId> id = platform::file_id(job.source))
            source_ids.insert(*id);
        else {
            error_code fs_error;
            fs::path parent = fs::weakly_canonical(job.source.parent_path(), fs_error);
            fallback_by_parent[path_to_utf8(parent)].push_back(job.source);
        }
    }
    unordered_map<string, string> claimed;   // output key -> filename of the source that keeps it
    for (Job& job : jobs) {
        error_code fs_error;
        string output_key = path_key(job.output);
        if (output_key == path_key(job.source) || (fs::exists(job.output, fs_error) && fs::equivalent(job.source, job.output, fs_error)))
            job.note = "output would be the source file";
        else if (sources.count(output_key))
            job.note = "output would overwrite a source file";
        else if (string overwrite_note = overwrites_a_source(job.output, source_ids, fallback_by_parent); !overwrite_note.empty())
            job.note = overwrite_note;
        else if (auto [claimed_entry, fresh] = claimed.emplace(output_key, path_to_utf8(job.source.filename())); !fresh)
            job.note = "same output as " + claimed_entry->second;
        else if (!options.overwrite && fs::exists(job.output, fs_error))
            job.note = "output exists";
        (job.note.empty() ? plan.to_convert : plan.skipped).push_back(std::move(job));
    }
}

}  // namespace

Plan scan(const Options& options, string& error) {
    Plan plan;
    error_code fs_error;
    if (!fs::exists(options.source, fs_error)) { error = "source does not exist: " + path_to_utf8(options.source); return plan; }
    vector<Job> jobs;
    if (fs::is_regular_file(options.source, fs_error)) {
        collect(options.source, fs::path(), options, jobs, plan.ignored);
    } else {
        fs::path dest_canon = fs::weakly_canonical(options.destination, fs_error);
        auto visit = [&](const fs::directory_entry& entry) {
            if (!entry.is_regular_file(fs_error)) return;
            collect(entry.path(), fs::relative(entry.path().parent_path(), options.source, fs_error), options, jobs, plan.ignored);
        };
        if (options.recursive) {
            fs::recursive_directory_iterator iterator(options.source, fs::directory_options::skip_permission_denied, fs_error), end;
            for (; iterator != end; iterator.increment(fs_error)) {
                if (iterator->is_directory(fs_error) && fs::weakly_canonical(iterator->path(), fs_error) == dest_canon) { iterator.disable_recursion_pending(); continue; }
                visit(*iterator);
            }
        } else {
            for (const auto& entry : fs::directory_iterator(options.source, fs::directory_options::skip_permission_denied, fs_error)) visit(entry);
        }
    }
    sort(jobs.begin(), jobs.end(), [](const Job& left, const Job& right) { return left.source < right.source; });
    decide(jobs, options, plan);
    return plan;
}

}  // namespace beatdown
