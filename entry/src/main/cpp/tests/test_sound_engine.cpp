#include <algorithm>

#include "../profile/types.h"
#include "../wrapper/sound_engine.h"
#include "synth.h"
#include "test_util.h"

namespace {
std::vector<sns::EngineEvent> Feed(sns::SoundEngine& engine, const std::vector<int16_t>& pcm, size_t chunk = 480) {
    std::vector<sns::EngineEvent> events;
    for (size_t pos = 0; pos < pcm.size(); pos += chunk) {
        const auto out = engine.Process(pcm.data() + pos, std::min(chunk, pcm.size() - pos));
        events.insert(events.end(), out.events.begin(), out.events.end());
    }
    return events;
}

size_t Count(const std::vector<sns::EngineEvent>& events, const std::string& type) {
    return static_cast<size_t>(std::count_if(events.begin(), events.end(),
                                             [&](const sns::EngineEvent& e) { return e.type == type; }));
}

// A sound with a little quiet background before and after, as the ring buffer of the app would hold it.
std::vector<int16_t> Take(const std::vector<int16_t>& sound, uint32_t seed, double tailSec = 1.5) {
    return Concat({Noise(1.5, 0.003, seed), sound, Noise(tailSec, 0.003, seed + 100)});
}
}  // namespace

TEST(engine_continuous_tone_is_one_alarm) {
    sns::SoundEngine engine(sns::kSampleRate);
    const auto events = Feed(engine, Concat({Noise(1.5, 0.003, 1), Tone(3000, 10.0, 0.3), Silence(1.0)}));
    CHECK(Count(events, "alarm") == 1);
    CHECK(Count(events, "custom") == 0);
    CHECK(events.size() == 1);
    if (!events.empty()) CHECK_NEAR(events[0].freqHz, 3000.0, 60.0);
}

TEST(engine_hum_and_motor_bursts_make_no_alarms) {
    sns::SoundEngine engine(sns::kSampleRate);
    auto hum = Tone(120, 15.0, 0.05);
    const auto hum2 = Tone(300, 15.0, 0.04);
    for (size_t i = 0; i < hum.size(); ++i) hum[i] = static_cast<int16_t>(hum[i] + hum2[i]);
    CHECK(Feed(engine, hum).empty());
    std::vector<int16_t> motor;
    for (int k = 0; k < 5; ++k) {
        const auto burst = Tone(200, 0.75, 0.2);
        const auto gap = Silence(2.25);
        motor.insert(motor.end(), burst.begin(), burst.end());
        motor.insert(motor.end(), gap.begin(), gap.end());
    }
    sns::SoundEngine engine2(sns::kSampleRate);
    // The motor bursts are loud (like the phone's own vibration): they may be reported as loud sounds, which the app
    // filters while it is vibrating, but never as an alarm or a recognised sound.
    const auto motorEvents = Feed(engine2, Concat({Noise(1.5, 0.003, 2), motor}));
    CHECK(Count(motorEvents, "alarm") == 0);
    CHECK(Count(motorEvents, "custom") == 0);
}

TEST(engine_learns_a_detected_sound_and_recognises_it_again) {
    sns::SoundEngine engine(sns::kSampleRate);
    const auto first = Feed(engine, Take(Tone(2000, 1.0, 0.3), 5, 4.0));
    CHECK(Count(first, "alarm") == 1);
    if (Count(first, "alarm") != 1) return;
    const double t = first[0].timeSec;
    const auto learned = engine.LearnFromRing("snd-1", t);
    CHECK(learned.ok);
    CHECK(!learned.templateBytes.empty());
    CHECK_NEAR(learned.profile.dominantHz, 2000.0, 50.0);
    const auto second = Feed(engine, Take(Tone(2000, 1.0, 0.15), 6));  // quieter repeat
    size_t custom = 0;
    for (const auto& e : second) {
        if (e.type == "custom") {
            ++custom;
            CHECK(e.label == "snd-1");
        }
    }
    CHECK(custom == 1);
}

TEST(engine_does_not_confuse_a_different_sound_with_a_learned_one) {
    sns::SoundEngine engine(sns::kSampleRate);
    const auto first = Feed(engine, Take(Tone(2000, 1.0, 0.3), 7, 4.0));
    if (Count(first, "alarm") != 1) {
        CHECK(false);
        return;
    }
    CHECK(engine.LearnFromRing("snd-1", first[0].timeSec).ok);
    const auto other = Feed(engine, Take(BeepTrain(3500, 0.5, 0.15, 3, 0.3), 8));
    CHECK(Count(other, "custom") == 0);
    CHECK(Count(other, "alarm") >= 1);
}

TEST(engine_learning_needs_enough_audio_after_the_event) {
    sns::SoundEngine engine(sns::kSampleRate);
    const auto events = Feed(engine, Concat({Noise(1.5, 0.003, 9), Tone(2000, 1.0, 0.3)}));  // ends right away
    CHECK(Count(events, "alarm") == 1);
    const auto r = engine.LearnFromRing("snd-1", events.empty() ? 0.0 : events[0].timeSec);
    CHECK(!r.ok);
    CHECK(!r.message.empty());
}

TEST(engine_teaching_with_consistent_takes_creates_a_matchable_sound) {
    sns::SoundEngine engine(sns::kSampleRate);
    const auto beeps = BeepTrain(2500, 0.3, 0.2, 3, 0.3);
    std::vector<std::vector<int16_t>> takes = {Take(beeps, 11), Take(beeps, 12), Take(beeps, 13)};
    const auto r = engine.TrainFromTakes("snd-2", takes);
    CHECK(r.ok);
    CHECK(r.consistency >= 0.6f);
    CHECK(r.profile.beepCount == 3);
    const auto events = Feed(engine, Take(beeps, 14));
    size_t custom = 0;
    for (const auto& e : events) custom += e.type == "custom" && e.label == "snd-2" ? 1 : 0;
    CHECK(custom == 1);
}

TEST(engine_teaching_rejects_takes_that_differ_too_much) {
    sns::SoundEngine engine(sns::kSampleRate);
    std::vector<std::vector<int16_t>> takes = {Take(Tone(1000, 1.0, 0.3), 21),
                                               Take(BeepTrain(3500, 0.15, 0.1, 6, 0.3), 22)};
    const auto r = engine.TrainFromTakes("snd-3", takes);
    CHECK(!r.ok);
    CHECK(!r.message.empty());
}

TEST(engine_check_take_accepts_a_sound_and_rejects_noise) {
    sns::SoundEngine engine(sns::kSampleRate);
    CHECK(engine.CheckTake(Take(Tone(2000, 1.0, 0.3), 31)).ok);
    const auto bad = engine.CheckTake(Noise(4.0, 0.003, 32));
    CHECK(!bad.ok);
    CHECK(!bad.message.empty());
}

TEST(engine_removed_sound_is_no_longer_recognised) {
    sns::SoundEngine engine(sns::kSampleRate);
    const auto beeps = BeepTrain(2500, 0.3, 0.2, 3, 0.3);
    CHECK(engine.TrainFromTakes("snd-4", {Take(beeps, 41), Take(beeps, 42)}).ok);
    CHECK(engine.RemoveSound("snd-4"));
    CHECK(Count(Feed(engine, Take(beeps, 43)), "custom") == 0);
    CHECK(!engine.RemoveSound("snd-4"));
}

TEST(engine_restores_a_stored_sound_in_a_new_engine) {
    sns::SoundEngine a(sns::kSampleRate);
    const auto beeps = BeepTrain(2500, 0.3, 0.2, 3, 0.3);
    const auto trained = a.TrainFromTakes("snd-5", {Take(beeps, 51), Take(beeps, 52)});
    CHECK(trained.ok);
    sns::SoundEngine b(sns::kSampleRate);
    std::string error;
    CHECK(b.AddSound(trained.templateBytes, &error));
    CHECK(Count(Feed(b, Take(beeps, 53)), "custom") == 1);
    std::vector<uint8_t> corrupt = trained.templateBytes;
    corrupt[corrupt.size() / 2] ^= 0xFF;
    CHECK(!b.AddSound(corrupt, &error));
    CHECK(!error.empty());
}

TEST(engine_chunk_size_does_not_change_alarms) {
    const auto pcm = Concat({Noise(1.5, 0.003, 61), Tone(2000, 2.0, 0.3), Silence(1.0)});
    for (size_t chunk : {size_t{1}, size_t{333}, size_t{4096}}) {
        sns::SoundEngine engine(sns::kSampleRate);
        CHECK(Count(Feed(engine, pcm, chunk), "alarm") == 1);
    }
    sns::SoundEngine engine(sns::kSampleRate);
    CHECK(engine.Process(nullptr, 0).events.empty());
}

TEST(engine_live_bands_follow_the_sound) {
    sns::SoundEngine engine(sns::kSampleRate);
    const auto tone = Tone(2000, 1.0, 0.3);
    const auto out = engine.Process(tone.data(), tone.size());
    int best = 0;
    for (int i = 1; i < sns::kLiveBands; ++i) {
        if (out.bands[i] > out.bands[best]) best = i;
    }
    CHECK(out.bands[best] > 0.3f);
    // 2 kHz sits in the middle of the 200 Hz - 7.6 kHz log scale: neither the lowest nor the highest bands.
    CHECK(best >= 5 && best <= 11);
    sns::SoundEngine quiet(sns::kSampleRate);
    const auto silence = Silence(1.0);
    const auto q = quiet.Process(silence.data(), silence.size());
    for (float v : q.bands) CHECK(v < 0.05f);
}

TEST(engine_same_rhythm_at_another_pitch_is_not_the_learned_sound) {
    sns::SoundEngine engine(sns::kSampleRate);
    const auto first = Feed(engine, Take(BeepTrain(3000, 0.6, 0.3, 3, 0.3), 71, 4.0));
    CHECK(Count(first, "alarm") >= 1);
    if (first.empty()) return;
    CHECK(engine.LearnFromRing("snd-1", first[0].timeSec).ok);
    const auto other = Feed(engine, Take(BeepTrain(1500, 0.6, 0.3, 3, 0.3), 72, 4.0));
    CHECK(Count(other, "custom") == 0);
    const auto same = Feed(engine, Take(BeepTrain(3000, 0.6, 0.3, 3, 0.2), 73, 4.0));  // quieter repeat
    CHECK(Count(same, "custom") == 1);
}

// ---- sound classes beyond the alarm (siren, chirp, loud sound, knock) --------------------------------------------

namespace {
// A tone whose pitch follows hz(t), phase-continuous.
template <typename F>
std::vector<int16_t> Sweep(double sec, F hz, double amp) {
    const size_t n = static_cast<size_t>(sec * kSynthRate);
    std::vector<int16_t> out(n);
    double phase = 0.0;
    for (size_t i = 0; i < n; ++i) {
        phase += 2.0 * kSynthPi * hz(static_cast<double>(i) / kSynthRate) / kSynthRate;
        out[i] = ToSample(amp * std::sin(phase));
    }
    return out;
}
}  // namespace

TEST(engine_reports_a_wailing_siren_as_siren) {
    sns::SoundEngine engine(sns::kSampleRate);
    const auto wail = Sweep(8.0, [](double t) { return 1000.0 - 400.0 * std::cos(2.0 * kSynthPi * t / 3.5); }, 0.3);
    const auto events = Feed(engine, Concat({Noise(1.5, 0.003, 11), wail, Noise(2.0, 0.003, 12)}));
    CHECK(Count(events, "siren") == 1);
}

TEST(engine_reports_a_short_beep_as_a_chirp) {
    sns::SoundEngine engine(sns::kSampleRate);
    const auto events = Feed(engine, Concat({Noise(1.5, 0.003, 13), Tone(2500, 0.2, 0.3), Noise(2.0, 0.003, 14)}));
    CHECK(Count(events, "chirp") == 1);
    CHECK(Count(events, "alarm") == 0);
}

TEST(engine_reports_loud_noise_as_a_loud_sound) {
    sns::SoundEngine engine(sns::kSampleRate);
    const auto events = Feed(engine, Concat({Noise(1.5, 0.003, 15), Noise(2.0, 0.5, 16), Noise(2.0, 0.003, 17)}));
    CHECK(Count(events, "loud_sound") == 1);
}

TEST(engine_reports_a_short_bang_as_a_knock) {
    sns::SoundEngine engine(sns::kSampleRate);
    std::vector<int16_t> bang = Noise(0.15, 0.7, 18);
    for (size_t i = 0; i < bang.size(); ++i) {
        bang[i] = static_cast<int16_t>(bang[i] * std::exp(-static_cast<double>(i) / 800.0));
    }
    const auto events = Feed(engine, Concat({Noise(1.5, 0.003, 19), bang, Noise(2.0, 0.003, 20)}));
    CHECK(Count(events, "knock") == 1);
}
