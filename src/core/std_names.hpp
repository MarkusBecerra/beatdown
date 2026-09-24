#pragma once

// PR #1 review: "any possibility we can use the `using` keyword to avoid having to do std:: in a
// lot of places?" This header answers that for the whole project: every standard-library name
// src/ and tests/ actually use more than once or twice, brought in with using-declarations (and,
// for the nested std namespaces we name a lot, namespace aliases) so ordinary code can write
// `string`, `vector`, `fs::path`, `chrono::milliseconds` instead of the std:: form.
//
// Deliberately scoped to `namespace beatdown`, never global: a bare `using namespace std;` (or
// file-scope `using std::string;` at global scope in a header) would leak into every translation
// unit that ever includes this file, including third-party headers pulled in afterwards, and
// could silently change which overload an unrelated call resolves to project-wide. Keeping the
// using-declarations inside `namespace beatdown` means they only affect lookup from code that is
// itself inside `namespace beatdown`, or that explicitly opts in with `using namespace beatdown;`
// (as every test .cpp already does) -- never code outside this project.
//
// A few global-scope files (main.cpp, tests/fixtures.hpp, tests/fixtures.cpp, tests/test_cli.cpp)
// declare things outside `namespace beatdown` and so can't reach these; they keep their own small,
// local using-declarations instead (see those files).
//
// Left qualified everywhere on purpose, not brought in here:
//  - std::move / std::forward: clang warns on an unqualified call (-Wunqualified-std-cast-call).
//  - std::min / std::max / std::clamp / std::abs / std::llabs: these are chosen by overload
//    resolution (and can be found via ADL) rather than a fixed signature, so importing them
//    project-wide risks silently changing which overload a given call picks, e.g. against a
//    Windows SDK macro or one of our own identifiers. Callers keep these qualified.
//  - std::set: Mp3Info's one use (mp3_parse.hpp) stays qualified because two .cpp files already
//    use `set` as a local lambda name (harmless to shadow, but not worth the ambiguity for one
//    call site).

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace beatdown {

// Namespace aliases for the nested std namespaces this project names often.
namespace fs = std::filesystem;
namespace chrono = std::chrono;

// Strings, containers, smart pointers, callables.
using std::function;
using std::make_unique;
using std::map;
using std::nullopt;
using std::optional;
using std::pair;
using std::string;
using std::string_view;
using std::u16string;
using std::u16string_view;
using std::u8string;
using std::unique_ptr;
using std::unordered_map;
using std::unordered_set;
using std::vector;

// Concurrency.
using std::atomic;
using std::lock_guard;
using std::memory_order_acq_rel;
using std::memory_order_acquire;
using std::memory_order_release;
using std::mutex;
using std::thread;
using std::this_thread::sleep_for;

// Errors and exceptions.
using std::current_exception;
using std::error_code;
using std::exception;
using std::exception_ptr;
using std::rethrow_exception;
using std::runtime_error;

// I/O and streams.
using std::cerr;
using std::cout;
using std::ifstream;
using std::ios;
using std::istreambuf_iterator;
using std::ofstream;
using std::ostream;
using std::ostringstream;
using std::streamoff;
using std::streamsize;

// Random.
using std::mt19937;
using std::mt19937_64;
using std::normal_distribution;
using std::random_device;

// Other library facilities, all used here only with fundamental-type arguments (int/int64_t/
// size_t/double/const char*), so bringing them in cannot change overload resolution via ADL.
using std::find_if;
using std::getenv;
using std::isfinite;
using std::isxdigit;
using std::llround;
using std::memcmp;
using std::memcpy;
using std::numeric_limits;
using std::snprintf;
using std::sort;
using std::system;
using std::to_string;
using std::tolower;
using std::transform;
using std::uint64_t;

// <cmath>, likewise always called here with double arguments.
using std::cos;
using std::exp;
using std::fabs;
using std::log10;
using std::pow;
using std::sin;
using std::sqrt;

}  // namespace beatdown
