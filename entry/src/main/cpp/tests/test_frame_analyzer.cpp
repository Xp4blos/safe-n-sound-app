#include <numeric>

#include "../profile/frame_analyzer.h"
#include "synth.h"
#include "test_util.h"

namespace {
sns::FrameFeatures AnalyzeSecondFrame(const std::vector<int16_t>& pcm) {
    sns::FrameAnalyzer analyzer(sns::kSampleRate);
    return analyzer.Analyze(pcm.data() + sns::kFrameSize);  // skip the first frame
}
}  // namespace

TEST(frame_tone_peak_and_level) {
    const auto f = AnalyzeSecondFrame(Tone(1000, 0.5, 0.5));
    CHECK_NEAR(f.peakHz[0], 1000.0, 20.0);
    CHECK_NEAR(f.levelDb, -9.0, 1.0);
    CHECK(f.peakCount >= 1);
}

TEST(frame_tonality_separates_tone_from_noise) {
    const auto tone = AnalyzeSecondFrame(Tone(1000, 0.5, 0.5));
    const auto noise = AnalyzeSecondFrame(Noise(0.5, 0.5, 7));
    CHECK(tone.tonality > 25.0f);
    CHECK(noise.tonality < 8.0f);
}

TEST(frame_silence_is_quiet_and_peakless) {
    const auto f = AnalyzeSecondFrame(Silence(0.5));
    CHECK(f.levelDb <= -90.0f);
    CHECK(f.peakCount == 0);
}

TEST(frame_two_tone_mix_reports_both_peaks) {
    auto a = Tone(1000, 0.5, 0.25);
    const auto b = Tone(2500, 0.5, 0.25);
    for (size_t i = 0; i < a.size(); ++i) a[i] = static_cast<int16_t>(a[i] + b[i]);
    const auto f = AnalyzeSecondFrame(a);
    CHECK(f.peakCount >= 2);
    const bool has1000 = std::abs(f.peakHz[0] - 1000.0f) < 20.0f || std::abs(f.peakHz[1] - 1000.0f) < 20.0f;
    const bool has2500 = std::abs(f.peakHz[0] - 2500.0f) < 20.0f || std::abs(f.peakHz[1] - 2500.0f) < 20.0f;
    CHECK(has1000);
    CHECK(has2500);
}

TEST(frame_band_energy_is_normalised) {
    const auto f = AnalyzeSecondFrame(Tone(1000, 0.5, 0.5));
    const float sum = std::accumulate(f.bandEnergy.begin(), f.bandEnergy.end(), 0.0f);
    CHECK_NEAR(sum, 1.0, 0.01);
    const auto silent = AnalyzeSecondFrame(Silence(0.5));
    const float silentSum = std::accumulate(silent.bandEnergy.begin(), silent.bandEnergy.end(), 0.0f);
    CHECK_NEAR(silentSum, 0.0, 1e-6);
}
