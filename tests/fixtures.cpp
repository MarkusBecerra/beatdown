#include "fixtures.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <random>
#include <stdexcept>
#include <vector>
#include "core/platform/platform.hpp"
#include "core/unicode.hpp"

// Global scope, like fixtures.hpp -- see its comment for why these aren't routed through
// core/std_names.hpp.
using std::error_code;
using std::ifstream;
using std::ios;
using std::istreambuf_iterator;
using std::mt19937_64;
using std::ofstream;
using std::optional;
using std::random_device;
using std::runtime_error;
using std::sin;
using std::streamsize;
using std::to_string;
using std::vector;

namespace {
constexpr double kPi = 3.14159265358979323846;
}  // namespace

TempDir::TempDir() {
    mt19937_64 rng(random_device{}());
    path = fs::temp_directory_path() / ("beatdown-test-" + to_string(rng()));
    fs::create_directories(path);
}
TempDir::~TempDir() {
    error_code ignored_error;
    fs::remove_all(path, ignored_error);
}

fs::path make_audio(const fs::path& file, const FixtureSpec& spec) {
    fs::create_directories(file.parent_path());
    SF_INFO info{};
    info.samplerate = spec.rate;
    info.channels = spec.channels;
    info.format = spec.container | spec.subtype;
    SNDFILE* sndfile = beatdown::platform::sf_open_path(file, SFM_WRITE, &info);
    if (!sndfile) throw runtime_error("fixture open failed: " + beatdown::path_to_utf8(file) + ": " + sf_strerror(nullptr));
    auto set = [&](int key, const optional<string>& value) { if (value) sf_set_string(sndfile, key, value->c_str()); };
    set(SF_STR_TITLE, spec.tags.title);
    set(SF_STR_ARTIST, spec.tags.artist);
    set(SF_STR_ALBUM, spec.tags.album);
    set(SF_STR_DATE, spec.tags.date);
    set(SF_STR_TRACKNUMBER, spec.tags.track);
    set(SF_STR_GENRE, spec.tags.genre);
    set(SF_STR_COMMENT, spec.tags.comment);

    const int64_t frames = static_cast<int64_t>(spec.rate * spec.seconds);
    vector<float> buf(static_cast<size_t>(frames) * spec.channels);
    for (int64_t frame = 0; frame < frames; ++frame) {
        float sample = static_cast<float>(spec.amplitude * sin(2.0 * kPi * spec.freq_hz * frame / spec.rate));
        for (int channel = 0; channel < spec.channels; ++channel) buf[frame * spec.channels + channel] = sample;
    }
    sf_writef_float(sndfile, buf.data(), frames);
    sf_close(sndfile);
    return file;
}

void write_bytes(const fs::path& file, string_view bytes) {
    fs::create_directories(file.parent_path());
    ofstream out(file, ios::binary);
    out.write(bytes.data(), static_cast<streamsize>(bytes.size()));
}

string read_file(const fs::path& file) {
    ifstream in(file, ios::binary);
    return string((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
}
