#include <CLI/CLI.hpp>
#include "core/options.hpp"
#include "core/platform/platform.hpp"
#include "core/report.hpp"
#include "core/runner.hpp"
#include "core/unicode.hpp"
#include "core/version.hpp"

// main.cpp's own code lives at global scope (not inside `namespace beatdown`), so it can't reach
// core/std_names.hpp's beatdown-scoped using-declarations; core/report.hpp pulls that header in
// transitively for its types, so these just re-expose the handful of std:: names this file itself
// needs, the same way tests/fixtures.hpp does for the test binary's global-scope helpers.
using std::atomic;
using std::cerr;
using std::cout;
using std::exception;
using std::getenv;
using std::string;

namespace fs = std::filesystem;

// "~/x" is common inside quotes in the PRD's own examples, where the shell does not expand it.
// B1: built with beatdown::path_from_utf8, never the deprecated fs::u8path; HOME/USERPROFILE is
// used as-is since it's the platform's native string, not UTF-8 bytes to reinterpret.
static fs::path expand_tilde(const string& s) {
    if (s.size() >= 2 && s[0] == '~' && (s[1] == '/' || s[1] == '\\')) {
#ifdef _WIN32
        const char* home = getenv("USERPROFILE");
#else
        const char* home = getenv("HOME");
#endif
        if (home) return fs::path(home) / beatdown::path_from_utf8(s.substr(2));
    }
    return beatdown::path_from_utf8(s);
}

static atomic<bool> g_cancel{false};

int main(int argc, char** argv) {
    beatdown::platform::console_utf8();
    CLI::App app{"Convert a folder of WAV/AIFF/FLAC files to 320 kbps MP3 (or FLAC) for rekordbox.", "beatdown"};
    argv = app.ensure_utf8(argv);
    app.set_version_flag("--version", string("beatdown ") + beatdown::version());
    app.get_formatter()->column_width(22);

    beatdown::Options o;
    string source, destination, format = "mp3";
    int vbr = -1;
    bool no_recursive = false;

    app.add_option("source", source, "Folder (or single file) of WAV / AIFF / FLAC files")->required();
    app.add_option("destination", destination, "Folder to write outputs into; its last component is created if missing")->required();
    app.add_option("--format", format, "Output format: mp3 (320 kbps CBR) or flac (lossless)")
        ->check(CLI::IsMember({"mp3", "flac"}))->default_str("mp3");
    // Task 18 fix round 2: restricted to the settings the content verifier can certify soundly.
    // Below 128 kbps CBR, LAME omits its own Info/LAME tag when it won't fit in one CBR frame, so
    // mpg123 has no gapless data and returns the whole (longer, padded) stream, which always
    // fails the exact-length check; below ~112 kbps, LAME's own encoder lowpass also drops under
    // 16 kHz, so bright material fails the level check even when the length is fine. VBR level 4
    // and 9 similarly fail on real bright-material probes (see the task report for the numbers).
    // None of this is club-quality material at these rates anyway.
    auto* bitrate = app.add_option("--bitrate", o.encode.bitrate, "MP3 CBR bitrate in kbps (128-320: lower isn't club quality and can't be verified reliably)")
        ->check(CLI::IsMember({128, 160, 192, 224, 256, 320}))->default_str("320");
    auto* vbr_opt = app.add_option("--vbr", vbr, "MP3 VBR at LAME quality N (0 = best, 8 = lowest verified) instead of --bitrate")
        ->check(CLI::IsMember({0, 1, 2, 3, 5, 6, 7, 8}));
    bitrate->excludes(vbr_opt);
    app.add_option("--jobs", o.jobs, "Parallel encodes (default: all hardware threads)")->check(CLI::PositiveNumber);
    app.add_flag("--overwrite", o.overwrite, "Re-encode even if the output already exists");
    app.add_flag("--no-recursive", no_recursive, "Only the top level of <source>");
    app.add_flag("--tag-from-name", o.tag_from_name, "Derive Artist/Title from \"Artist - Title\" filenames when the source is untagged");
    // Repeatable, one value per occurrence: allow_extra_args(false) stops a single "--strip-suffix
    // X" from also swallowing a following bare token; ->expected(1) is deliberately NOT set here —
    // it would cap the *total* values received across every occurrence at 1 (CLI11's default
    // MultiOptionPolicy::Throw compares the combined count against expected_max_), so a second
    // "--strip-suffix Y" would fail to parse with "At most 1 required but received 2" instead of
    // appending a second entry.
    app.add_option("--strip-suffix", o.strip_suffixes, "Drop trailing text from the derived Title (repeatable)")->allow_extra_args(false);
    app.add_flag("--dry-run", o.dry_run, "Show the plan, write nothing");
    auto* quiet = app.add_flag("--quiet", o.quiet, "Only the summary and failures");
    auto* verbose = app.add_flag("--verbose", o.verbose, "Encoder settings and skipped files");
    quiet->excludes(verbose);

    try {
        app.parse(argc, argv);
    } catch (const CLI::CallForHelp& e) {
        return app.exit(e);
    } catch (const CLI::CallForVersion& e) {
        return app.exit(e);
    } catch (const CLI::ParseError& e) {
        app.exit(e);
        return 2;
    }

    // Past parsing, an exception is a bug rather than a usage error: say so and exit 1 instead of
    // letting it reach std::terminate.
    try {
        o.source = expand_tilde(source);
        o.destination = expand_tilde(destination);
        o.encode.format = format == "flac" ? beatdown::Format::Flac : beatdown::Format::Mp3;
        if (vbr >= 0) o.encode.vbr = vbr;
        o.recursive = !no_recursive;

        beatdown::platform::install_interrupt_handler(g_cancel);
        beatdown::ConsoleReporter reporter(cout, o.quiet, o.verbose);
        return beatdown::run(o, reporter, g_cancel);
    } catch (const exception& e) {
        cerr << "beatdown: internal error: " << e.what() << "\n";
        return 1;
    } catch (...) {
        cerr << "beatdown: internal error: unknown exception\n";
        return 1;
    }
}
