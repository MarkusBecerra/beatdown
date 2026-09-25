#include "core/platform/platform.hpp"
#include <signal.h>
#include <sys/stat.h>

namespace beatdown::platform {

static atomic<bool>* g_flag = nullptr;
static_assert(atomic<bool>::is_always_lock_free);

static void on_signal(int sig) {
    if (g_flag) g_flag->store(true);
    signal(sig, SIG_DFL);
}

void install_interrupt_handler(atomic<bool>& flag) {
    g_flag = &flag;
    struct sigaction sa {};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

void console_utf8() {}

SNDFILE* sf_open_path(const fs::path& path, int mode, SF_INFO* info) {
    return sf_open(path.c_str(), mode, info);
}

optional<FileId> file_id(const fs::path& path) {
    struct stat file_stat{};
    if (stat(path.c_str(), &file_stat) != 0) return nullopt;
    return FileId{static_cast<uint64_t>(file_stat.st_dev), static_cast<uint64_t>(file_stat.st_ino)};
}

}  // namespace beatdown::platform
