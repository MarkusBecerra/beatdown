#include "core/converter.hpp"
#include <cerrno>
#include <system_error>
#include "core/encoder.hpp"
#include "core/unicode.hpp"
#include "core/verifier.hpp"

namespace fs = std::filesystem;

namespace beatdown {

fs::path temp_path_for(const fs::path& output) {
    return output.parent_path() / fs::path(".beatdown-" + path_to_utf8(output.filename()) + ".part");
}

Tags resolve_tags(const Decoder& d, const Options& o) {
    Tags t = d.tags();
    if (!o.tag_from_name) return t;
    Tags derived = tags_from_filename(path_to_utf8(d.path().stem()));
    if (derived.title) derived.title = strip_suffixes(*derived.title, o.strip_suffixes);
    return merge_tags(t, derived);
}

static bool looks_like_disk_full(const std::string& err) {
    return err.find("No space left") != std::string::npos || err.find("not enough space") != std::string::npos
        || err.find("There is not enough space") != std::string::npos;
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
    fs::remove(tmp, ec);
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
        if (!space_ec && sp.available < (64u << 20)) r.disk_full = true;
        if (looks_like_disk_full(e)) r.disk_full = true;
        return done(Outcome::Failed, e);
    }

    std::string v = verify_output(tmp, o.encode, dec->info());
    if (!v.empty()) return done(Outcome::Failed, "verification failed: " + v);

    fs::last_write_time(tmp, fs::last_write_time(job.source, ec), ec);
    if (o.overwrite) fs::remove(job.output, ec);
    fs::rename(tmp, job.output, ec);
    if (ec) return done(Outcome::Failed, "cannot rename to " + path_to_utf8(job.output) + ": " + ec.message());
    cleanup.armed = false;
    r.output_bytes = static_cast<int64_t>(fs::file_size(job.output, ec));
    return done(Outcome::Converted);
}

}  // namespace beatdown
