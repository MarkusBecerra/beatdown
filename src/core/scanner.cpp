#include "core/scanner.hpp"
#include <algorithm>
#include <cctype>
#include "core/unicode.hpp"

namespace fs = std::filesystem;

namespace beatdown {

bool is_audio_input(const fs::path& p) {
    std::string ext = path_to_utf8(p.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext == ".wav" || ext == ".wave" || ext == ".aif" || ext == ".aiff" || ext == ".aifc" || ext == ".flac";
}

static void classify(const fs::path& file, const fs::path& rel_dir, const Options& opts, Plan& plan) {
    std::string name = path_to_utf8(file.filename());
    if (name.rfind("._", 0) == 0 || !is_audio_input(file)) { ++plan.ignored; return; }
    Job j;
    j.source = file;
    // R-E: When rel_dir is "." (or empty), use empty path so output is destination / filename
    fs::path effective_rel_dir = (rel_dir.empty() || rel_dir == ".") ? fs::path() : rel_dir;
    j.output = opts.destination / effective_rel_dir / (file.stem().native() + fs::path(opts.output_extension()).native());
    std::error_code ec;
    j.source_bytes = static_cast<int64_t>(fs::file_size(file, ec));
    if (fs::exists(j.output, ec) && fs::equivalent(file, j.output, ec)) { j.note = "output would be the source file"; plan.skipped.push_back(j); return; }
    if (!opts.overwrite && fs::exists(j.output, ec)) { j.note = "output exists"; plan.skipped.push_back(j); return; }
    plan.to_convert.push_back(j);
}

Plan scan(const Options& opts, std::string& error) {
    Plan plan;
    std::error_code ec;
    if (!fs::exists(opts.source, ec)) { error = "source does not exist: " + path_to_utf8(opts.source); return plan; }
    if (fs::is_regular_file(opts.source, ec)) {
        classify(opts.source, fs::path(), opts, plan);
        return plan;
    }
    fs::path dest_canon = fs::weakly_canonical(opts.destination, ec);
    auto visit = [&](const fs::directory_entry& e) {
        if (!e.is_regular_file(ec)) return;
        classify(e.path(), fs::relative(e.path().parent_path(), opts.source, ec), opts, plan);
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
    std::sort(plan.to_convert.begin(), plan.to_convert.end(), [](const Job& a, const Job& b) { return a.source < b.source; });
    return plan;
}

}  // namespace beatdown
