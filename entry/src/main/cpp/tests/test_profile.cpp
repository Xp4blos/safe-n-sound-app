#include "../profile/types.h"
#include "../profile/profile.h"
#include "synth.h"
#include "test_util.h"

namespace {
sns::SoundProfile Describe(const std::vector<int16_t>& pcm) {
    return sns::DescribeSound(pcm.data(), pcm.size(), sns::kSampleRate);
}
std::vector<int16_t> Around(const std::vector<int16_t>& sound) {
    return Concat({Noise(1.5, 0.002, 3), sound, Noise(1.5, 0.002, 4)});
}
}  // namespace

TEST(profile_single_steady_tone) {
    const auto p = Describe(Around(Tone(2000, 1.0, 0.3)));
    CHECK_NEAR(p.dominantHz, 2000.0, 40.0);
    CHECK_NEAR(p.durationSec, 1.0, 0.2);
    CHECK(p.beepCount == 1);
    CHECK(p.repetition == 0);
    CHECK(p.modulation == 0);
}

TEST(profile_repeated_beeps_are_pulsed_with_a_rate) {
    const auto p = Describe(Around(BeepTrain(2000, 0.2, 0.2, 5, 0.3)));
    CHECK(p.beepCount == 5);
    CHECK(p.repetition == 1);
    CHECK(p.modulation == 1);
    CHECK_NEAR(p.beepsPerSec, 2.5, 0.4);
}

TEST(profile_long_tone_is_continuous) {
    const auto p = Describe(Around(Tone(3000, 5.0, 0.3)));
    CHECK(p.repetition == 2);
    CHECK(p.modulation == 0);
}

TEST(profile_sweep_is_modulated) {
    const auto p = Describe(Around(Sweep(1000, 3000, 1.5, 0.3)));
    CHECK(p.modulation == 2);
    CHECK(p.repetition == 0);
}

TEST(profile_envelope_has_eight_points_peaking_at_one) {
    const auto p = Describe(Around(BeepTrain(2000, 0.2, 0.2, 5, 0.3)));
    float maxV = 0.0f, minV = 1.0f;
    for (float v : p.envelope) {
        maxV = std::max(maxV, v);
        minV = std::min(minV, v);
    }
    CHECK_NEAR(maxV, 1.0, 1e-4);
    CHECK(minV < 0.8f);  // pulses leave dips in the envelope
}

TEST(profile_of_silence_or_nothing_is_empty_and_safe) {
    const auto silent = Describe(Silence(2.0));
    CHECK(silent.beepCount == 0);
    CHECK_NEAR(silent.durationSec, 0.0, 1e-6);
    const auto none = sns::DescribeSound(nullptr, 0, sns::kSampleRate);
    CHECK(none.beepCount == 0);
}
