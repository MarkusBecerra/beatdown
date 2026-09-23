#pragma once
#include <atomic>
#include <filesystem>
#include <sndfile.h>

namespace beatdown::platform {
// First Ctrl-C sets `flag`; a second one restores the default action so the user can force-quit.
void install_interrupt_handler(std::atomic<bool>& flag);
void console_utf8();
SNDFILE* sf_open_path(const std::filesystem::path& p, int mode, SF_INFO* info);
}
