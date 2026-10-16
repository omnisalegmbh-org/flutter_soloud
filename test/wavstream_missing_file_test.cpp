// Standalone tests for a WavStream whose file disappears after `load`.
//
// A disk-mode WavStream reopens its file for every voice, so the file can be
// deleted, moved or locked between `load` and `play`. That used to crash inside
// the decoder init, reading from a null FILE*.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_wavstream_missing_file_test.sh

#include "soloud.h"
#include "soloud_wavstream.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int gFailures = 0;
int gAssertions = 0;

#define EXPECT(condition, format, ...) do { \
    ++gAssertions; \
    if (!(condition)) { \
        ++gFailures; \
        std::fprintf(stderr, "  FAIL [%s:%d] " format "\n", \
                     __FILE__, __LINE__, ##__VA_ARGS__); \
    } \
} while (0)

void putLe(std::vector<unsigned char>& out, uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        out.push_back(static_cast<unsigned char>((value >> (8 * i)) & 0xff));
    }
}

// One second of 16-bit mono silence.
bool writeWav(const std::string& path) {
    const uint32_t sampleRate = 8000;
    const uint32_t dataBytes = sampleRate * 2;
    std::vector<unsigned char> bytes;
    bytes.insert(bytes.end(), {'R', 'I', 'F', 'F'});
    putLe(bytes, 36 + dataBytes, 4);
    bytes.insert(bytes.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    putLe(bytes, 16, 4);
    putLe(bytes, 1, 2);               // PCM
    putLe(bytes, 1, 2);               // mono
    putLe(bytes, sampleRate, 4);
    putLe(bytes, sampleRate * 2, 4);  // byte rate
    putLe(bytes, 2, 2);               // block align
    putLe(bytes, 16, 2);              // bits per sample
    bytes.insert(bytes.end(), {'d', 'a', 't', 'a'});
    putLe(bytes, dataBytes, 4);
    bytes.resize(bytes.size() + dataBytes, 0);

    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    return written == bytes.size();
}

std::string tempPath() {
    const char* dir = std::getenv("TMPDIR");
    return std::string(dir ? dir : "/tmp") + "/soloud_wavstream_missing.wav";
}

struct Rig {
    Rig() {
        const SoLoud::result result = engine.init(
            SoLoud::Soloud::CLIP_ROUNDOFF,
            SoLoud::Soloud::NULLDRIVER,
            8000,
            512,
            1);
        EXPECT(result == SoLoud::SO_NO_ERROR,
               "null backend initialization failed: %d", result);
    }

    ~Rig() {
        engine.deinit();
    }

    SoLoud::Soloud engine;
};

void testPlaysWhileTheFileExists() {
    std::printf("plays while the file exists\n");
    const std::string path = tempPath();
    EXPECT(writeWav(path), "could not write %s", path.c_str());

    Rig rig;
    SoLoud::WavStream stream;
    EXPECT(stream.load(path.c_str()) == SoLoud::SO_NO_ERROR, "load failed");
    const SoLoud::handle handle = rig.engine.play(stream, 1.0f, 0.0f, true);
    EXPECT(handle != 0, "play refused a readable file");
    rig.engine.stopAll();
    std::remove(path.c_str());
}

void testRefusesAVoiceOnceTheFileIsGone() {
    std::printf("refuses a voice once the file is gone\n");
    const std::string path = tempPath();
    EXPECT(writeWav(path), "could not write %s", path.c_str());

    Rig rig;
    SoLoud::WavStream stream;
    EXPECT(stream.load(path.c_str()) == SoLoud::SO_NO_ERROR, "load failed");
    EXPECT(std::remove(path.c_str()) == 0, "could not delete %s", path.c_str());

    // Crashed here before the fix, in the decoder's first read.
    const SoLoud::handle handle = rig.engine.play(stream, 1.0f, 0.0f, true);
    EXPECT(handle == 0, "play returned handle %u for a missing file", handle);
    EXPECT(rig.engine.getActiveVoiceCount() == 0,
           "a voice was left behind: %u active",
           rig.engine.getActiveVoiceCount());
}

void testPlaysAgainOnceTheFileIsBack() {
    std::printf("plays again once the file is back\n");
    const std::string path = tempPath();
    EXPECT(writeWav(path), "could not write %s", path.c_str());

    Rig rig;
    SoLoud::WavStream stream;
    EXPECT(stream.load(path.c_str()) == SoLoud::SO_NO_ERROR, "load failed");
    std::remove(path.c_str());
    EXPECT(rig.engine.play(stream, 1.0f, 0.0f, true) == 0,
           "play did not refuse the missing file");

    // The refusal belongs to that one attempt, not to the source.
    EXPECT(writeWav(path), "could not rewrite %s", path.c_str());
    const SoLoud::handle handle = rig.engine.play(stream, 1.0f, 0.0f, true);
    EXPECT(handle != 0, "play stayed refused after the file came back");
    rig.engine.stopAll();
    std::remove(path.c_str());
}

}  // namespace

int main() {
    testPlaysWhileTheFileExists();
    testRefusesAVoiceOnceTheFileIsGone();
    testPlaysAgainOnceTheFileIsBack();

    if (gFailures != 0) {
        std::fprintf(stderr, "\n%d of %d assertions failed.\n",
                     gFailures, gAssertions);
        return 1;
    }

    std::printf("\nAll %d assertions passed.\n", gAssertions);
    return 0;
}
