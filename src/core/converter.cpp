#include "core/converter.hpp"
#include <cerrno>
#include <cstdio>
#include <limits>
#include <random>
#include <system_error>
#include "core/encoder.hpp"
#include "core/space.hpp"
#include "core/unicode.hpp"
#include "core/verifier.hpp"

namespace fs = std::filesystem;

namespace beatdown {

fs::path temp_path_for(const fs::path& output) {
    thread_local std::mt19937_64 rng{std::random_device{}()};
    char hex[9];
    std::snprintf(hex, sizeof hex, "%08x", static_cast<unsigned>(rng() & 0xFFFFFFFFu));
    return output.parent_path() / path_from_utf8(".beatdown-" + path_to_utf8(output.filename()) + "." + hex + ".part");
}

Tags resolve_tags(const Decoder& d, const Options& o) {
    Tags t = d.tags();
    if (!o.tag_from_name) return t;
    Tags derived = tags_from_filename(path_to_utf8(d.path().stem()));
    if (derived.title) derived.title = strip_suffixes(*derived.title, o.strip_suffixes);
    return merge_tags(t, derived);
}

bool looks_like_disk_full(const std::string& error, int64_t available, int64_t estimated) {
    if (available < estimated) return true;
    return error.find("No space left") != std::string::npos || error.find("not enough space") != std::string::npos
        || error.find("There is not enough space") != std::string::npos;
}

FileResult convert_one(const Job& job, const Options& o, const std::atomic<bool>& cancel) {
    FileResult r;
    r.job = job;
    auto t0 = std::chrono::steady_clock::now();
    auto done = [&](Outcome oc, std::string err = "") {
        r.outcome = oc;
        r.error = std::move(err);
        r.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
        return r;
    };
    if (cancel.load()) return done(Outcome::Cancelled);

    std::string err;
    auto dec = Decoder::open(job.source, err);
    if (!dec) return done(Outcome::Failed, err);

    std::error_code ec;
    fs::create_directories(job.output.parent_path(), ec);
    if (ec) return done(Outcome::Failed, "cannot create " + path_to_utf8(job.output.parent_path()) + ": " + ec.message());

    fs::path tmp = temp_path_for(job.output);
    struct Cleanup { const fs::path& p; bool armed = true; ~Cleanup() { if (armed) { std::error_code e; fs::remove(p, e); } } } cleanup{tmp};

    Tags tags = resolve_tags(*dec, o);
    auto enc = make_encoder(o.encode);
    std::string log;
    std::string e = enc->encode(*dec, tmp, tags, cancel, o.verbose ? &log : nullptr);
    r.verbose_log = log;
    if (e == "cancelled") return done(Outcome::Cancelled);
    if (!e.empty()) {
        // A write that fails for lack of space is reported so the runner can stop the batch (R28).
        std::error_code space_ec;
        auto sp = fs::space(job.output.parent_path(), space_ec);
        int64_t estimated = estimate_output_bytes(dec->info(), o.encode);
        int64_t available = space_ec ? std::numeric_limits<int64_t>::max() : static_cast<int64_t>(sp.available);
        r.disk_full = looks_like_disk_full(e, available, estimated);
        return done(Outcome::Failed, e);
    }

    std::string v = verify_output(tmp, o.encode, dec->info());
    if (!v.empty()) return done(Outcome::Failed, "verification failed: " + v);

    auto src_mtime = fs::last_write_time(job.source, ec);
    if (ec) return done(Outcome::Failed, "cannot read source modification time: " + ec.message());
    fs::last_write_time(tmp, src_mtime, ec);
    if (ec) return done(Outcome::Failed, "cannot set modification time: " + ec.message());
    // Replaces an existing output (--overwrite) atomically on POSIX and with MSVC's std::filesystem.
    fs::rename(tmp, job.output, ec);
    if (ec) return done(Outcome::Failed, "cannot rename to " + path_to_utf8(job.output) + ": " + ec.message());
    cleanup.armed = false;
    r.output_bytes = static_cast<int64_t>(fs::file_size(job.output, ec));
    return done(Outcome::Converted);
}

}  // namespace beatdown
