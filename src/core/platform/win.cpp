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

std::optional<FileId> file_id(const std::filesystem::path& p) {
    // Access 0 (metadata only, no read/write) with full sharing so this never contends with
    // another process's open handle; FILE_FLAG_BACKUP_SEMANTICS is required to open a directory
    // and also relaxes the access checks CreateFileW would otherwise apply.
    HANDLE h = CreateFileW(p.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::nullopt;
    BY_HANDLE_FILE_INFORMATION info{};
    bool ok = GetFileInformationByHandle(h, &info) != 0;
    CloseHandle(h);
    if (!ok) return std::nullopt;
    return FileId{static_cast<std::uint64_t>(info.dwVolumeSerialNumber),
                  (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow};
}

}  // namespace beatdown::platform
