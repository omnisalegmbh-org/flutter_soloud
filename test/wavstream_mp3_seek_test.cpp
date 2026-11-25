// Standalone tests that a disk-mode MP3 WavStream seeks to the frame it was
// asked for.
//
// A voice group is seeked under one lock, but each voice's own decoder decides
// where it lands. dr_mp3's seek table used to land a whole number of MP3 frames
// away from the target, by an amount that depended on the file's bit reservoir
// at that point - so stems cut from one song came out of a seek 26 ms apart.
//
// Each case decodes a file from the start, which is exact, and then checks that
// what follows a seek is the same audio that a straight read finds there.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_wavstream_mp3_seek_test.sh

#include "soloud.h"
#include "soloud_wavstream.h"

#include <cmath>
#include <cstdio>
#include <memory>
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

const unsigned int kBlock = 512;
const unsigned int kWindow = 4096;
// The longest MP3 frame. An offset is searched for only to say how far off a
// failed seek landed, and the ones this guards against are whole frames.
const long long kMp3Frame = 1152;

std::unique_ptr<SoLoud::AudioSourceInstance> openVoice(SoLoud::WavStream& stream) {
    std::unique_ptr<SoLoud::AudioSourceInstance> voice(stream.createInstance());
    // What the engine does on `play`; the decoder reads its channel count and
    // sample rate from here.
    voice->init(stream, 0);
    return voice;
}

// Appends up to `frames` frames of the first channel, and returns how many came.
unsigned int readFirstChannel(SoLoud::AudioSourceInstance& voice,
                              unsigned int frames,
                              std::vector<float>& out) {
    std::vector<float> block(kBlock * MAX_CHANNELS);
    unsigned int total = 0;
    while (total < frames) {
        const unsigned int wanted =
            frames - total < kBlock ? frames - total : kBlock;
        const unsigned int got = voice.getAudio(block.data(), wanted, kBlock);
        out.insert(out.end(), block.begin(), block.begin() + got);
        total += got;
        if (got < wanted) break;
    }
    return total;
}

float worstDifference(const std::vector<float>& window,
                      const std::vector<float>& whole,
                      long long at) {
    float worst = 0.0f;
    for (size_t i = 0; i < window.size(); ++i) {
        const float difference = std::fabs(window[i] - whole[at + i]);
        if (difference > worst) worst = difference;
    }
    return worst;
}

void testSeeksLandOnTheTarget(const char* path) {
    std::printf("seeks land on the target: %s\n", path);

    SoLoud::WavStream stream;
    const SoLoud::result loaded = stream.load(path);
    EXPECT(loaded == SoLoud::SO_NO_ERROR, "load failed: %d", loaded);
    if (loaded != SoLoud::SO_NO_ERROR) return;

    std::vector<float> whole;
    {
        auto reference = openVoice(stream);
        readFirstChannel(*reference, 0xffffffffu, whole);
    }
    const long long frames = static_cast<long long>(whole.size());
    EXPECT(frames > 4 * kWindow, "decoded only %lld frames", frames);
    if (frames <= 4 * kWindow) return;

    auto voice = openVoice(stream);
    const double sampleRate = voice->mBaseSamplerate;
    // Forward and backward, near both ends and in between: a scrub does all of
    // them, and the seek table takes a different path before its first point.
    const double fractions[] = {0.50, 0.10, 0.90, 0.30, 0.004, 0.70, 0.02,
                                0.97, 0.45, 0.61, 0.23, 0.83};
    for (const double fraction : fractions) {
        const double seconds = fraction * frames / sampleRate;
        // The frame WavStreamInstance::seek asks the decoder for.
        const long long target =
            static_cast<long long>(std::floor(sampleRate * seconds));
        if (target + kWindow + 4 * kMp3Frame > frames) continue;

        const SoLoud::result sought = voice->seek(seconds, nullptr, 0);
        EXPECT(sought == SoLoud::SO_NO_ERROR, "seek to %.3f s failed: %d",
               seconds, sought);

        std::vector<float> window;
        const unsigned int got = readFirstChannel(*voice, kWindow, window);
        EXPECT(got == kWindow, "read %u of %u frames after seeking to %.3f s",
               got, kWindow, seconds);
        if (got != kWindow) continue;

        if (worstDifference(window, whole, target) < 1e-4f) {
            ++gAssertions;
            continue;
        }

        long long landed = 0;
        float best = 1e30f;
        for (long long offset = -4 * kMp3Frame; offset <= 4 * kMp3Frame; ++offset) {
            if (target + offset < 0) continue;
            const float difference =
                worstDifference(window, whole, target + offset);
            if (difference < best) {
                best = difference;
                landed = offset;
            }
        }
        EXPECT(false,
               "seek to %.3f s landed %+lld frames (%+.1f ms) from the target",
               seconds, landed, 1000.0 * landed / sampleRate);
    }
}

}  // namespace

int main() {
    // Constant and variable bitrate, an ID3 tag with cover art ahead of the
    // audio, and MPEG-2, whose frames are half as long.
    testSeeksLandOnTheTarget("example/assets/audio/sample-MP3.mp3");
    testSeeksLandOnTheTarget("example/assets/audio/IveSeenThings.mp3");
    testSeeksLandOnTheTarget("example/assets/audio/8_bit_mentality.mp3");
    testSeeksLandOnTheTarget("example/assets/audio/test_mpeg2_24khz_mono.mp3");

    if (gFailures != 0) {
        std::fprintf(stderr, "\n%d of %d assertions failed.\n",
                     gFailures, gAssertions);
        return 1;
    }

    std::printf("\nAll %d assertions passed.\n", gAssertions);
    return 0;
}
