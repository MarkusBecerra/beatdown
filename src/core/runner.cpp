#include "core/runner.hpp"
#include <chrono>
#include <vector>
#include "core/decoder.hpp"
#include "core/scanner.hpp"
#include "core/scheduler.hpp"
#include "core/space.hpp"
#include "core/unicode.hpp"

namespace fs = std::filesystem;

namespace beatdown {

// R25: create only the last path component; a missing parent means an unmounted drive or a typo.
static bool prepare_destination(const Options& o, Reporter& rep) {
    std::error_code ec;
    if (fs::is_directory(o.destination, ec)) return true;
    if (fs::exists(o.destination, ec)) { rep.error("destination is not a directory: " + path_to_utf8(o.destination)); return false; }
    fs::path parent = o.destination.has_filename() ? o.destination.parent_path() : o.destination.parent_path().parent_path();
    if (!fs::is_directory(parent, ec)) {
        rep.error("cannot create " + path_to_utf8(o.destination) + ": parent folder " + path_to_utf8(parent) + " does not exist (drive not mounted, or a typo?)");
        return false;
    }
    if (o.dry_run) return true;
    fs::create_directory(o.destination, ec);
    if (ec) { rep.error("cannot create " + path_to_utf8(o.destination) + ": " + ec.message()); return false; }
    return true;
}

int run(const Options& o, Reporter& rep, std::atomic<bool>& cancel, ConvertFn convert) {
    auto t0 = std::chrono::steady_clock::now();
    Summary s;
    std::string err;
    std::error_code ec;
    if (!fs::exists(o.source, ec)) { rep.error("source does not exist: " + path_to_utf8(o.source)); return 2; }
    if (!prepare_destination(o, rep)) return 2;

    Plan plan = scan(o, err);
    if (!err.empty()) { rep.error(err); return 2; }
    int jobs = o.effective_jobs();
    rep.plan(plan, jobs, o);
    for (const auto& j : plan.skipped) rep.skipped(j);
    s.skipped = static_cast<int>(plan.skipped.size());
    s.ignored = plan.ignored;

    // R28: estimate each job from its header (unreadable files estimate 0 and fail on their own
    // later). A2 keeps the per-job figure, not just the total, so a dry run can report it per file.
    std::vector<int64_t> estimates(plan.to_convert.size(), 0);
    int64_t estimate_total = 0;
    for (size_t i = 0; i < plan.to_convert.size(); ++i) {
        std::string e;
        if (auto d = Decoder::open(plan.to_convert[i].source, e)) estimates[i] = estimate_output_bytes(d->info(), o.encode);
        estimate_total += estimates[i];
    }
    int64_t avail = o.space_override_available ? *o.space_override_available
        : available_bytes(o.dry_run && !fs::is_directory(o.destination, ec) ? o.destination.parent_path() : o.destination, err);
    SpaceCheck sc = check_space(estimate_total, avail);
    rep.space(sc, o.dry_run);
    if (!sc.ok && !o.dry_run) {
        rep.error("refusing to start: not enough free space on the destination");
        s.space_refused = true;
        s.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
        rep.summary(s);
        return s.exit_code();
    }
    if (o.dry_run) {
        // A2: report the projection per job, in plan order, and never call file() for a dry run.
        for (size_t i = 0; i < plan.to_convert.size(); ++i) rep.would_convert(plan.to_convert[i], estimates[i]);
        s.would_convert = static_cast<int>(plan.to_convert.size());
        s.dry_run = true;
        s.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
        rep.summary(s);
        return 0;
    }

    // A disk-full failure stops the batch (R28) by raising the same flag Ctrl-C uses; remember which it was.
    std::atomic<bool> disk_full{false};
    std::vector<FileResult> results(plan.to_convert.size());
    run_parallel(jobs, plan.to_convert.size(), [&](size_t i) {
        FileResult r;
        // A4: run_parallel rethrows a job's exception on the caller after stopping the batch, so one
        // file's internal exception must be turned into an ordinary failed result here instead, or it
        // would abort every other in-flight and not-yet-started file along with it.
        try {
            r = convert(plan.to_convert[i], o, cancel);
        } catch (const std::exception& e) {
            r.job = plan.to_convert[i];
            r.outcome = Outcome::Failed;
            r.error = std::string("internal error: ") + e.what();
        } catch (...) {
            r.job = plan.to_convert[i];
            r.outcome = Outcome::Failed;
            r.error = "internal error";
        }
        results[i] = r;
        if (r.disk_full) { disk_full = true; cancel = true; }
        rep.file(r);
    }, cancel);

    for (auto& r : results) {
        if (r.job.source.empty()) { ++s.cancelled; continue; }   // never started
        switch (r.outcome) {
            case Outcome::Converted: ++s.converted; s.bytes_in += r.job.source_bytes; s.bytes_out += r.output_bytes; break;
            case Outcome::Failed: ++s.failed; s.failures.push_back(r); break;
            case Outcome::Cancelled: ++s.cancelled; break;
        }
    }
    s.disk_full = disk_full.load();
    s.interrupted = cancel.load() && !disk_full.load();
    s.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
    rep.summary(s);
    return s.exit_code();
}

}  // namespace beatdown
