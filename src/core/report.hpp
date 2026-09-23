#pragma once
#include <chrono>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>
#include "core/converter.hpp"
#include "core/scanner.hpp"
#include "core/space.hpp"

namespace beatdown {

std::string format_size(int64_t bytes);
std::string format_secs(std::chrono::milliseconds ms);
std::string format_clock(std::chrono::milliseconds ms);

struct Summary {
    int converted = 0, skipped = 0, failed = 0, ignored = 0, cancelled = 0;
    int64_t bytes_in = 0, bytes_out = 0;
    std::chrono::milliseconds elapsed{0};
    std::vector<FileResult> failures;
    bool interrupted = false;
    bool space_refused = false;
    int would_convert = 0;   // number of jobs projected in a dry run
    bool dry_run = false;    // this summary describes a dry run
    bool disk_full = false;  // the batch stopped early because the destination disk filled up
    int exit_code() const { return interrupted ? 130 : (failed > 0 || space_refused) ? 1 : 0; }
};

class Reporter {
public:
    virtual ~Reporter() = default;
    virtual void plan(const Plan& p, int jobs, const Options& o) = 0;
    virtual void space(const SpaceCheck& s, bool dry_run) = 0;
    virtual void file(const FileResult& r) = 0;
    virtual void skipped(const Job& j) = 0;
    // Dry-run projection for one to_convert job, called once per job in plan order instead of file().
    virtual void would_convert(const Job& j, int64_t estimated_bytes) = 0;
    virtual void summary(const Summary& s) = 0;
    virtual void error(const std::string& message) = 0;
};

class ConsoleReporter : public Reporter {
public:
    ConsoleReporter(std::ostream& out, bool quiet, bool verbose) : out_(out), quiet_(quiet), verbose_(verbose) {}
    void plan(const Plan& p, int jobs, const Options& o) override;
    void space(const SpaceCheck& s, bool dry_run) override;
    void file(const FileResult& r) override;
    void skipped(const Job& j) override;
    void would_convert(const Job& j, int64_t estimated_bytes) override;
    void summary(const Summary& s) override;
    void error(const std::string& message) override;
private:
    std::ostream& out_;
    bool quiet_, verbose_;
    bool dry_run_ = false;  // learned from plan(); skipped() needs it since its signature carries no Options
};

}  // namespace beatdown
