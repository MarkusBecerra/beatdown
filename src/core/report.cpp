#include "core/report.hpp"
#include <cmath>
#include <cstdio>
#include <mutex>
#include <system_error>
#include "core/unicode.hpp"

namespace fs = std::filesystem;

namespace beatdown {

static std::string one_decimal(double v) { char b[32]; std::snprintf(b, sizeof b, "%.1f", v); return b; }
// Task 18: sign always shown, two decimals -- e.g. "+0.48", "-0.32".
static std::string peak_str(double db) { char b[32]; std::snprintf(b, sizeof b, "%+.2f", db); return b; }

// Finding 1 (fix round 1): the failure recap in summary() shows a path relative to the source
// root (set by plan()) instead of a bare filename, since a 500-file batch can have several
// identically-named files under different sub-folders. Falls back to the filename when there's
// no usable root (e.g. summary() called without a preceding plan()) or fs::relative can't relate
// the two paths (e.g. different drives on Windows).
static std::string relative_to_root(const fs::path& source, const fs::path& root) {
    if (!root.empty()) {
        std::error_code ec;
        fs::path rel = fs::relative(source, root, ec);
        if (!ec && !rel.empty()) return path_to_utf8(rel);
    }
    return path_to_utf8(source.filename());
}

std::string format_size(int64_t bytes) {
    double b = static_cast<double>(bytes);
    if (b >= 1e9) return one_decimal(b / 1e9) + " GB";
    if (b >= 1e6) return one_decimal(b / 1e6) + " MB";
    return one_decimal(b / 1e3) + " kB";
}
std::string format_secs(std::chrono::milliseconds ms) { return one_decimal(ms.count() / 1000.0) + "s"; }
std::string format_clock(std::chrono::milliseconds ms) {
    long long s = ms.count() / 1000;
    char b[32]; std::snprintf(b, sizeof b, "%lld:%02lld", s / 60, s % 60); return b;
}

static std::mutex g_out;

void ConsoleReporter::plan(const Plan& p, int jobs, const Options& o) {
    dry_run_ = o.dry_run;
    source_root_ = o.source;
    if (quiet_) return;
    std::lock_guard<std::mutex> l(g_out);
    out_ << "Found " << (p.to_convert.size() + p.skipped.size()) << " audio files, " << p.skipped.size()
         << " already in destination, " << p.to_convert.size() << " to encode (" << jobs << " jobs"
         << (o.dry_run ? ", dry run" : "") << ")\n\n";
}

void ConsoleReporter::space(const SpaceCheck& s, bool dry_run) {
    if (quiet_ && s.ok) return;
    std::lock_guard<std::mutex> l(g_out);
    if (!s.ok) out_ << "Not enough space: about " << format_size(s.needed) << " needed (incl. 10% margin), " << format_size(s.available) << " available\n";
    else if (dry_run || verbose_) out_ << "Estimated output " << format_size(s.needed) << " (incl. 10% margin), " << format_size(s.available) << " available\n";
}

void ConsoleReporter::file(const FileResult& r) {
    std::lock_guard<std::mutex> l(g_out);
    if (verbose_ && !r.verbose_log.empty()) out_ << "    " << r.verbose_log;
    switch (r.outcome) {
        case Outcome::Converted:
            if (quiet_) return;
            out_ << "  ✓ " << path_to_utf8(r.job.output.filename()) << "    " << format_size(r.job.source_bytes) << " → " << format_size(r.output_bytes) << "   " << format_secs(r.elapsed);
            if (verbose_ && r.peak_dbfs) {
                // Task 18 fix round 1: a truly silent decode is -inf dBFS, which isn't a useful
                // number to show next to a two-decimal figure like every other file's.
                if (std::isfinite(*r.peak_dbfs)) out_ << "   peak " << peak_str(*r.peak_dbfs) << " dBFS";
                else out_ << "   peak: silent";
            }
            out_ << "\n";
            break;
        case Outcome::Failed:
            out_ << "  ✗ " << path_to_utf8(r.job.source.filename()) << "    " << r.error << "\n";
            break;
        case Outcome::Cancelled:
            if (verbose_) out_ << "  – " << path_to_utf8(r.job.source.filename()) << "    cancelled\n";
            break;
    }
}

void ConsoleReporter::skipped(const Job& j) {
    // Finding 4 (fix round 1): --quiet must gate this like every other per-item line (R20:
    // "--quiet prints only the summary and failures") — the previous version ignored quiet_
    // entirely, so --quiet --dry-run printed skip lines while would_convert() (correctly) stayed
    // silent under quiet, the reverse of what R20 asks for.
    if (quiet_ || (!verbose_ && !dry_run_)) return;
    std::lock_guard<std::mutex> l(g_out);
    out_ << "  = " << path_to_utf8(j.source.filename()) << "    skipped: " << j.note << "\n";
}

void ConsoleReporter::would_convert(const Job& j, int64_t estimated_bytes) {
    if (quiet_) return;
    std::lock_guard<std::mutex> l(g_out);
    out_ << "  → " << path_to_utf8(j.output.filename()) << "    " << format_size(j.source_bytes) << " → ~" << format_size(estimated_bytes) << "\n";
}

void ConsoleReporter::summary(const Summary& s) {
    std::lock_guard<std::mutex> l(g_out);
    // A3: disk-full and Ctrl-C are mutually exclusive early-stop reasons (interrupted is defined as
    // cancel && !disk_full in the runner), so at most one of these two lines is printed.
    if (s.disk_full) out_ << "\nStopped early: destination disk is full — " << s.cancelled << " not converted\n";
    else if (s.interrupted) out_ << "\nInterrupted: " << s.cancelled << " not converted\n";

    if (s.dry_run) {
        out_ << "\nDry run: would convert " << s.would_convert << ", skip " << s.skipped << ", ignore " << s.ignored << " non-audio files\n";
    } else {
        out_ << "\nConverted " << s.converted << ", skipped " << s.skipped << ", failed " << s.failed << ", ignored " << s.ignored << " non-audio files\n";
        if (s.bytes_in > 0 && s.converted > 0) {
            long long pct = static_cast<long long>(100.0 * (static_cast<double>(s.bytes_out) / static_cast<double>(s.bytes_in)) - 100.0);
            out_ << "Size " << format_size(s.bytes_in) << " → " << format_size(s.bytes_out) << " (" << (pct < 0 ? "−" : "+") << (pct < 0 ? -pct : pct) << "%)   ";
        }
    }
    // Finding 1 (fix round 1): recap every failure with its reason so a long batch doesn't force
    // scrolling back through hundreds of per-file lines to find the handful that failed. Printed
    // in --quiet too (R20: "--quiet prints only the summary and failures") since summary() as a
    // whole is never gated on quiet_.
    if (!s.failures.empty()) {
        out_ << "\nFailed:\n";
        for (const auto& r : s.failures) out_ << "  ✗ " << relative_to_root(r.job.source, source_root_) << "    " << r.error << "\n";
    }
    out_ << "Elapsed " << format_clock(s.elapsed) << "\n";
    // Task 18 fix round 1: printed on its own line, strictly after the Size/Elapsed line (not
    // interleaved with it) -- called out even under --quiet, like the failure recap above, and
    // never affects the exit code, since it's information about loud masters, not a failure.
    if (s.hot > 0) out_ << s.hot << " file(s) decode above +1.0 dBFS — very loud masters; see \"Loud masters\" in the README\n";
}

void ConsoleReporter::error(const std::string& m) {
    std::lock_guard<std::mutex> l(g_out);
    out_ << "beatdown: " << m << "\n";
}

}  // namespace beatdown
