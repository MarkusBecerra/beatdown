#include "core/runner.hpp"
#include <chrono>
#include <vector>
#include "core/decoder.hpp"
#include "core/scanner.hpp"
#include "core/scheduler.hpp"
#include "core/space.hpp"
#include "core/std_names.hpp"
#include "core/unicode.hpp"

namespace beatdown {

// Finding 3 (fix round 1): the directory that stands in for the destination when checking free
// space or validating R25's parent rule — the destination itself if it's already a directory,
// otherwise its parent, trailing-slash aware (a destination given as ".../NewRelease/" has no
// filename component, so parent_path() once lands back on the non-existent destination itself;
// applying it twice strips both the phantom empty element and "NewRelease"). Shared by both
// checks so they always agree on which directory actually exists today.
static fs::path existing_destination_dir(const fs::path& destination, error_code& fs_error) {
    if (fs::is_directory(destination, fs_error)) return destination;
    return destination.has_filename() ? destination.parent_path() : destination.parent_path().parent_path();
}

// R25: only the last path component may be created; a missing parent means an unmounted drive or
// a typo. Finding 3: validation no longer creates anything — creation is deferred until after the
// space check passes, so a refused (or dry) run never leaves a stray empty folder behind.
static bool validate_destination(const Options& options, Reporter& reporter) {
    error_code fs_error;
    if (fs::is_directory(options.destination, fs_error)) return true;
    if (fs::exists(options.destination, fs_error)) { reporter.error("destination is not a directory: " + path_to_utf8(options.destination)); return false; }
    fs::path parent = existing_destination_dir(options.destination, fs_error);
    if (!fs::is_directory(parent, fs_error)) {
        reporter.error("cannot create " + path_to_utf8(options.destination) + ": parent folder " + path_to_utf8(parent) + " does not exist (drive not mounted, or a typo?)");
        return false;
    }
    return true;
}

static bool create_destination(const Options& options, Reporter& reporter) {
    error_code fs_error;
    if (fs::is_directory(options.destination, fs_error)) return true;
    fs::create_directory(options.destination, fs_error);
    if (fs_error) { reporter.error("cannot create " + path_to_utf8(options.destination) + ": " + fs_error.message()); return false; }
    return true;
}

int run(const Options& options, Reporter& reporter, atomic<bool>& cancel, ConvertFn convert) {
    auto start_time = chrono::steady_clock::now();
    // Finding 2 (fix round 1): work on an absolutized copy. A bare relative destination like
    // "Release" has an empty parent_path(), which validate_destination would otherwise mistake
    // for a missing/unmounted parent and refuse with exit 2.
    Options resolved_options = options;
    error_code absolute_error;
    resolved_options.source = fs::absolute(resolved_options.source, absolute_error);
    resolved_options.destination = fs::absolute(resolved_options.destination, absolute_error);

    Summary summary;
    string err;
    error_code fs_error;
    if (!fs::exists(resolved_options.source, fs_error)) { reporter.error("source does not exist: " + path_to_utf8(resolved_options.source)); return 2; }
    if (!validate_destination(resolved_options, reporter)) return 2;

    Plan plan = scan(resolved_options, err);
    if (!err.empty()) { reporter.error(err); return 2; }
    int jobs = resolved_options.effective_jobs();
    reporter.plan(plan, jobs, resolved_options);
    for (const auto& job : plan.skipped) reporter.skipped(job);
    summary.skipped = static_cast<int>(plan.skipped.size());
    summary.ignored = plan.ignored;

    // R28: estimate each job from its header (unreadable files estimate 0 and fail on their own
    // later). A2 keeps the per-job figure, not just the total, so a dry run can report it per file.
    vector<int64_t> estimates(plan.to_convert.size(), 0);
    int64_t estimate_total = 0;
    for (size_t index = 0; index < plan.to_convert.size(); ++index) {
        string open_error;
        if (auto decoder = Decoder::open(plan.to_convert[index].source, open_error)) estimates[index] = estimate_output_bytes(decoder->info(), resolved_options.encode);
        estimate_total += estimates[index];
    }

    // Finding 3: measure the directory that actually exists today (see existing_destination_dir);
    // the destination itself is deliberately not created yet, so this must never be it directly
    // unless it already existed before this run.
    fs::path space_dir = existing_destination_dir(resolved_options.destination, fs_error);
    bool space_known = true;
    int64_t available = 0;
    SpaceCheck space_check{};
    if (resolved_options.space_override_available) {
        available = *resolved_options.space_override_available;
    } else {
        string space_err;
        available = available_bytes(space_dir, space_err);
        if (!space_err.empty()) {
            // Can't tell either way (e.g. a race removed the directory just validated above);
            // warn and proceed rather than block the whole batch on an unmeasurable check.
            reporter.error("cannot check free space on " + path_to_utf8(space_dir) + ": " + space_err);
            space_known = false;
        }
    }
    if (space_known) {
        space_check = check_space(estimate_total, available);
        reporter.space(space_check, resolved_options.dry_run);
        if (!space_check.ok) {
            // R28: refuse before writing anything, including creating the destination folder
            // itself (still not created at this point unless it already existed) — applies to a
            // dry run too, since a dry run previews exactly what the real run would do.
            reporter.error("refusing to start: not enough free space on the destination");
            summary.space_refused = true;
            summary.elapsed = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start_time);
            reporter.summary(summary);
            return summary.exit_code();
        }
    }
    if (resolved_options.dry_run) {
        // A2: report the projection per job, in plan order, and never call file() for a dry run.
        for (size_t index = 0; index < plan.to_convert.size(); ++index) reporter.would_convert(plan.to_convert[index], estimates[index]);
        summary.would_convert = static_cast<int>(plan.to_convert.size());
        summary.dry_run = true;
        summary.elapsed = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start_time);
        reporter.summary(summary);
        return 0;
    }

    if (!create_destination(resolved_options, reporter)) return 2;

    // A disk-full failure stops the batch (R28) by raising the same flag Ctrl-C uses; remember which it was.
    atomic<bool> disk_full{false};
    vector<FileResult> results(plan.to_convert.size());
    run_parallel(jobs, plan.to_convert.size(), [&](size_t index) {
        FileResult result;
        // A4: run_parallel rethrows a job's exception on the caller after stopping the batch, so one
        // file's internal exception must be turned into an ordinary failed result here instead, or it
        // would abort every other in-flight and not-yet-started file along with it.
        try {
            result = convert(plan.to_convert[index], resolved_options, cancel);
        } catch (const exception& caught) {
            result.job = plan.to_convert[index];
            result.outcome = Outcome::Failed;
            result.error = string("internal error: ") + caught.what();
        } catch (...) {
            result.job = plan.to_convert[index];
            result.outcome = Outcome::Failed;
            result.error = "internal error";
        }
        results[index] = result;
        if (result.disk_full) { disk_full = true; cancel = true; }
        reporter.file(result);
    }, cancel);

    for (auto& result : results) {
        if (result.job.source.empty()) { ++summary.cancelled; continue; }   // never started
        switch (result.outcome) {
            case Outcome::Converted:
                ++summary.converted;
                summary.bytes_in += result.job.source_bytes;
                summary.bytes_out += result.output_bytes;
                if (result.peak_dbfs && *result.peak_dbfs > 1.0) ++summary.hot;
                break;
            case Outcome::Failed: ++summary.failed; summary.failures.push_back(result); break;
            case Outcome::Cancelled: ++summary.cancelled; break;
        }
    }
    summary.disk_full = disk_full.load();
    summary.interrupted = cancel.load() && !disk_full.load();
    summary.elapsed = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start_time);
    reporter.summary(summary);
    return summary.exit_code();
}

}  // namespace beatdown
