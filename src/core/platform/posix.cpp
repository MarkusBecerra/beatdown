#include "core/platform/platform.hpp"
#include <signal.h>

namespace beatdown::platform {

static std::atomic<bool>* g_flag = nullptr;
static_assert(std::atomic<bool>::is_always_lock_free);

static void on_signal(int sig) {
    if (g_flag) g_flag->store(true);
    signal(sig, SIG_DFL);
}

void install_interrupt_handler(std::atomic<bool>& flag) {
    g_flag = &flag;
    struct sigaction sa {};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

void console_utf8() {}

SNDFILE* sf_open_path(const std::filesystem::path& p, int mode, SF_INFO* info) {
    return sf_open(p.c_str(), mode, info);
}

}  // namespace beatdown::platform
