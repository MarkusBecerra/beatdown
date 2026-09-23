#include "core/report.hpp"
#include <cstdio>
#include <mutex>
#include "core/unicode.hpp"

namespace beatdown {

static std::string one_decimal(double v) { char b[32]; std::snprintf(b, sizeof b, "%.1f", v); return b; }

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
            out_ << "  ✓ " << path_to_utf8(r.job.output.filename()) << "    " << format_size(r.job.source_bytes) << " → " << format_size(r.output_bytes) << "   " << format_secs(r.elapsed) << "\n";
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
    // A2: unlike file()/would_convert(), --quiet is not checked here: dry-run output (R15) must
    // list what would be skipped regardless of --quiet, so the gate is verbose OR dry-run only.
    if (!verbose_ && !dry_run_) return;
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
    out_ << "Elapsed " << format_clock(s.elapsed) << "\n";
}

void ConsoleReporter::error(const std::string& m) {
    std::lock_guard<std::mutex> l(g_out);
    out_ << "beatdown: " << m << "\n";
}

}  // namespace beatdown
