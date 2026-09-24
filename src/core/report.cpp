#include "core/report.hpp"
#include <cmath>
#include <cstdio>
#include <mutex>
#include <system_error>
#include "core/std_names.hpp"
#include "core/unicode.hpp"

namespace beatdown {

static string one_decimal(double value) { char formatted[32]; snprintf(formatted, sizeof formatted, "%.1f", value); return formatted; }
// Task 18: sign always shown, two decimals -- e.g. "+0.48", "-0.32".
static string peak_str(double db) { char formatted[32]; snprintf(formatted, sizeof formatted, "%+.2f", db); return formatted; }

// Finding 1 (fix round 1): the failure recap in summary() shows a path relative to the source
// root (set by plan()) instead of a bare filename, since a 500-file batch can have several
// identically-named files under different sub-folders. Falls back to the filename when there's
// no usable root (e.g. summary() called without a preceding plan()) or fs::relative can't relate
// the two paths (e.g. different drives on Windows).
static string relative_to_root(const fs::path& source, const fs::path& root) {
    if (!root.empty()) {
        error_code fs_error;
        fs::path relative_path = fs::relative(source, root, fs_error);
        if (!fs_error && !relative_path.empty()) return path_to_utf8(relative_path);
    }
    return path_to_utf8(source.filename());
}

string format_size(int64_t bytes) {
    double size = static_cast<double>(bytes);
    if (size >= 1e9) return one_decimal(size / 1e9) + " GB";
    if (size >= 1e6) return one_decimal(size / 1e6) + " MB";
    return one_decimal(size / 1e3) + " kB";
}
string format_secs(chrono::milliseconds ms) { return one_decimal(ms.count() / 1000.0) + "s"; }
string format_clock(chrono::milliseconds ms) {
    long long total_seconds = ms.count() / 1000;
    char formatted[32]; snprintf(formatted, sizeof formatted, "%lld:%02lld", total_seconds / 60, total_seconds % 60); return formatted;
}

static mutex g_out;

void ConsoleReporter::plan(const Plan& job_plan, int jobs, const Options& options) {
    dry_run_ = options.dry_run;
    source_root_ = options.source;
    if (quiet_) return;
    lock_guard<mutex> lock(g_out);
    out_ << "Found " << (job_plan.to_convert.size() + job_plan.skipped.size()) << " audio files, " << job_plan.skipped.size()
         << " already in destination, " << job_plan.to_convert.size() << " to encode (" << jobs << " jobs"
         << (options.dry_run ? ", dry run" : "") << ")\n\n";
}

void ConsoleReporter::space(const SpaceCheck& space_check, bool dry_run) {
    if (quiet_ && space_check.ok) return;
    lock_guard<mutex> lock(g_out);
    if (!space_check.ok) out_ << "Not enough space: about " << format_size(space_check.needed) << " needed (incl. 10% margin), " << format_size(space_check.available) << " available\n";
    else if (dry_run || verbose_) out_ << "Estimated output " << format_size(space_check.needed) << " (incl. 10% margin), " << format_size(space_check.available) << " available\n";
}

void ConsoleReporter::file(const FileResult& result) {
    lock_guard<mutex> lock(g_out);
    if (verbose_ && !result.verbose_log.empty()) out_ << "    " << result.verbose_log;
    switch (result.outcome) {
        case Outcome::Converted:
            if (quiet_) return;
            out_ << "  ✓ " << path_to_utf8(result.job.output.filename()) << "    " << format_size(result.job.source_bytes) << " → " << format_size(result.output_bytes) << "   " << format_secs(result.elapsed);
            if (verbose_ && result.peak_dbfs) {
                // Task 18 fix round 1: a truly silent decode is -inf dBFS, which isn't a useful
                // number to show next to a two-decimal figure like every other file's.
                if (isfinite(*result.peak_dbfs)) out_ << "   peak " << peak_str(*result.peak_dbfs) << " dBFS";
                else out_ << "   peak: silent";
            }
            out_ << "\n";
            break;
        case Outcome::Failed:
            out_ << "  ✗ " << path_to_utf8(result.job.source.filename()) << "    " << result.error << "\n";
            break;
        case Outcome::Cancelled:
            if (verbose_) out_ << "  – " << path_to_utf8(result.job.source.filename()) << "    cancelled\n";
            break;
    }
}

void ConsoleReporter::skipped(const Job& job) {
    // Finding 4 (fix round 1): --quiet must gate this like every other per-item line (R20:
    // "--quiet prints only the summary and failures") — the previous version ignored quiet_
    // entirely, so --quiet --dry-run printed skip lines while would_convert() (correctly) stayed
    // silent under quiet, the reverse of what R20 asks for.
    if (quiet_ || (!verbose_ && !dry_run_)) return;
    lock_guard<mutex> lock(g_out);
    out_ << "  = " << path_to_utf8(job.source.filename()) << "    skipped: " << job.note << "\n";
}

void ConsoleReporter::would_convert(const Job& job, int64_t estimated_bytes) {
    if (quiet_) return;
    lock_guard<mutex> lock(g_out);
    out_ << "  → " << path_to_utf8(job.output.filename()) << "    " << format_size(job.source_bytes) << " → ~" << format_size(estimated_bytes) << "\n";
}

void ConsoleReporter::summary(const Summary& summary_data) {
    lock_guard<mutex> lock(g_out);
    // A3: disk-full and Ctrl-C are mutually exclusive early-stop reasons (interrupted is defined as
    // cancel && !disk_full in the runner), so at most one of these two lines is printed.
    if (summary_data.disk_full) out_ << "\nStopped early: destination disk is full — " << summary_data.cancelled << " not converted\n";
    else if (summary_data.interrupted) out_ << "\nInterrupted: " << summary_data.cancelled << " not converted\n";

    if (summary_data.dry_run) {
        out_ << "\nDry run: would convert " << summary_data.would_convert << ", skip " << summary_data.skipped << ", ignore " << summary_data.ignored << " non-audio files\n";
    } else {
        out_ << "\nConverted " << summary_data.converted << ", skipped " << summary_data.skipped << ", failed " << summary_data.failed << ", ignored " << summary_data.ignored << " non-audio files\n";
        if (summary_data.bytes_in > 0 && summary_data.converted > 0) {
            long long pct = static_cast<long long>(100.0 * (static_cast<double>(summary_data.bytes_out) / static_cast<double>(summary_data.bytes_in)) - 100.0);
            out_ << "Size " << format_size(summary_data.bytes_in) << " → " << format_size(summary_data.bytes_out) << " (" << (pct < 0 ? "−" : "+") << (pct < 0 ? -pct : pct) << "%)   ";
        }
    }
    // Finding 1 (fix round 1): recap every failure with its reason so a long batch doesn't force
    // scrolling back through hundreds of per-file lines to find the handful that failed. Printed
    // in --quiet too (R20: "--quiet prints only the summary and failures") since summary() as a
    // whole is never gated on quiet_.
    if (!summary_data.failures.empty()) {
        out_ << "\nFailed:\n";
        for (const auto& failure : summary_data.failures) out_ << "  ✗ " << relative_to_root(failure.job.source, source_root_) << "    " << failure.error << "\n";
    }
    out_ << "Elapsed " << format_clock(summary_data.elapsed) << "\n";
    // Task 18 fix round 1: printed on its own line, strictly after the Size/Elapsed line (not
    // interleaved with it) -- called out even under --quiet, like the failure recap above, and
    // never affects the exit code, since it's information about loud masters, not a failure.
    if (summary_data.hot > 0) out_ << summary_data.hot << " file(s) decode above +1.0 dBFS — very loud masters; see \"Loud masters\" in the README\n";
}

void ConsoleReporter::error(const string& message) {
    lock_guard<mutex> lock(g_out);
    out_ << "beatdown: " << message << "\n";
}

}  // namespace beatdown
