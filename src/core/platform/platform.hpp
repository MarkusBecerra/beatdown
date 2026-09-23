#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <sndfile.h>

namespace beatdown::platform {
// First Ctrl-C sets `flag`; a second one restores the default action so the user can force-quit.
void install_interrupt_handler(std::atomic<bool>& flag);
void console_utf8();
SNDFILE* sf_open_path(const std::filesystem::path& p, int mode, SF_INFO* info);

// A file's identity: unlike its size or content, this doesn't change when the file is written to,
// only if it's deleted and recreated (or moved to another volume). `device`/`index` are POSIX's
// st_dev/st_ino, or Windows' volume serial number and 64-bit file index.
struct FileId {
    std::uint64_t device = 0;
    std::uint64_t index = 0;
    bool operator==(const FileId&) const = default;
};

// nullopt if `p` doesn't exist or its identity can't be queried.
std::optional<FileId> file_id(const std::filesystem::path& p);
}  // namespace beatdown::platform

namespace std {
template <>
struct hash<beatdown::platform::FileId> {
    size_t operator()(const beatdown::platform::FileId& id) const noexcept {
        return std::hash<std::uint64_t>()(id.device) ^ (std::hash<std::uint64_t>()(id.index) << 1);
    }
};
}  // namespace std
