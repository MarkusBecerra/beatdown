#include <windows.h>
#include "core/platform/platform.hpp"

namespace beatdown::platform {

static std::atomic<bool>* g_flag = nullptr;

static BOOL WINAPI on_ctrl(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        if (g_flag && !g_flag->exchange(true)) return TRUE;  // first: handled
        return FALSE;                                        // second: default action
    }
    return FALSE;
}

void install_interrupt_handler(std::atomic<bool>& flag) {
    g_flag = &flag;
    SetConsoleCtrlHandler(on_ctrl, TRUE);
}

void console_utf8() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

SNDFILE* sf_open_path(const std::filesystem::path& p, int mode, SF_INFO* info) {
    return sf_wchar_open(p.c_str(), mode, info);
}

}  // namespace beatdown::platform
