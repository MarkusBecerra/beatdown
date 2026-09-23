#include "fixtures.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <random>
#include <stdexcept>
#include <vector>
#include "core/platform/platform.hpp"
#include "core/unicode.hpp"

namespace {
constexpr double kPi = 3.14159265358979323846;
}  // namespace

TempDir::TempDir() {
    std::mt19937_64 rng(std::random_device{}());
    path = fs::temp_directory_path() / ("beatdown-test-" + std::to_string(rng()));
    fs::create_directories(path);
}
TempDir::~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
}

fs::path make_audio(const fs::path& file, const FixtureSpec& spec) {
    fs::create_directories(file.parent_path());
    SF_INFO info{};
    info.samplerate = spec.rate;
    info.channels = spec.channels;
    info.format = spec.container | spec.subtype;
    SNDFILE* sf = beatdown::platform::sf_open_path(file, SFM_WRITE, &info);
    if (!sf) throw std::runtime_error("fixture open failed: " + beatdown::path_to_utf8(file) + ": " + sf_strerror(nullptr));
    auto set = [&](int key, const std::optional<std::string>& v) { if (v) sf_set_string(sf, key, v->c_str()); };
    set(SF_STR_TITLE, spec.tags.title);
    set(SF_STR_ARTIST, spec.tags.artist);
    set(SF_STR_ALBUM, spec.tags.album);
    set(SF_STR_DATE, spec.tags.date);
    set(SF_STR_TRACKNUMBER, spec.tags.track);
    set(SF_STR_GENRE, spec.tags.genre);
    set(SF_STR_COMMENT, spec.tags.comment);

    const int64_t frames = static_cast<int64_t>(spec.rate * spec.seconds);
    std::vector<float> buf(static_cast<size_t>(frames) * spec.channels);
    for (int64_t i = 0; i < frames; ++i) {
        float v = 0.5f * static_cast<float>(std::sin(2.0 * kPi * 440.0 * i / spec.rate));
        for (int c = 0; c < spec.channels; ++c) buf[i * spec.channels + c] = v;
    }
    sf_writef_float(sf, buf.data(), frames);
    sf_close(sf);
    return file;
}

void write_bytes(const fs::path& file, std::string_view bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string read_file(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
