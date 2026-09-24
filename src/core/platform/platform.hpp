#pragma once
#include <cstdint>
#include <sndfile.h>
#include "core/std_names.hpp"

namespace beatdown::platform {
// First Ctrl-C sets `flag`; a second one restores the default action so the user can force-quit.
void install_interrupt_handler(atomic<bool>& flag);
void console_utf8();
SNDFILE* sf_open_path(const fs::path& p, int mode, SF_INFO* info);

// A file's identity: unlike its size or content, this doesn't change when the file is written to,
// only if it's deleted and recreated (or moved to another volume). `device`/`index` are POSIX's
// st_dev/st_ino, or Windows' volume serial number and 64-bit file index.
struct FileId {
    uint64_t device = 0;
    uint64_t index = 0;
    bool operator==(const FileId&) const = default;
};

// nullopt if `p` doesn't exist or its identity can't be queried.
optional<FileId> file_id(const fs::path& p);
}  // namespace beatdown::platform

// A template specialization of std::hash must be declared inside namespace std -- there is no way
// to spell that with a using-declaration or alias, so this reopens it, same as unrefactored code
// would. Once inside, `hash`/`uint64_t` below resolve to std::hash/std::uint64_t unqualified by
// ordinary (non-ADL) enclosing-scope lookup, the same rule that lets code inside `namespace
// beatdown` drop the std:: prefix -- no using-declaration involved.
namespace std {
template <>
struct hash<beatdown::platform::FileId> {
    size_t operator()(const beatdown::platform::FileId& id) const noexcept {
        return hash<uint64_t>()(id.device) ^ (hash<uint64_t>()(id.index) << 1);
    }
};
}  // namespace std
