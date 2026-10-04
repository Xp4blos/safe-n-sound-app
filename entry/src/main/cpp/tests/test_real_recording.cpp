// Characterisation test on a real recording: three plays of the same alarm sound captured by the microphone of the
// test phone (16 kHz mono, plain 44-byte WAV header), in a noisy room, including the phone's own vibration.
// Run from the repository root (scripts/host-tests.cmd does).
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../profile/types.h"
#include "../wrapper/sound_engine.h"
#include "test_util.h"

namespace {
bool LoadWav(const char* path, std::vector<int16_t>* pcm) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::vector<unsigned char> bytes;
    unsigned char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
    std::fclose(f);
    if (bytes.size() <= 44 || std::memcmp(bytes.data(), "RIFF", 4) != 0) return false;
    pcm->resize((bytes.size() - 44) / 2);
    std::memcpy(pcm->data(), bytes.data() + 44, pcm->size() * 2);
    return true;
}
}  // namespace

TEST(real_phone_recording_learns_the_first_alarm_and_recognises_the_repeats) {
    std::vector<int16_t> pcm;
    const bool loaded = LoadWav("entry/src/main/cpp/tests/data/phone_alarm_3x.wav", &pcm);
    CHECK(loaded);
    if (!loaded) return;

    sns::SoundEngine engine(sns::kSampleRate);
    double learnAt = -1.0, firstAlarm = 0.0;
    bool learned = false;
    int alarms = 0, repeats = 0;
    for (size_t pos = 0; pos < pcm.size(); pos += 480) {
        const size_t len = std::min<size_t>(480, pcm.size() - pos);
        const double now = static_cast<double>(pos + len) / sns::kSampleRate;
        const auto out = engine.Process(pcm.data() + pos, len);
        for (const auto& e : out.events) {
            if (e.type == "alarm") {
                ++alarms;
                if (learnAt < 0 && !learned) {
                    firstAlarm = e.timeSec;
                    learnAt = now + 3.7;  // the app learns 3.7 s after the first alarm event
                }
            } else if (e.type == "custom" && e.label == "snd-1") {
                ++repeats;
            }
        }
        if (learnAt >= 0 && now >= learnAt) {
            CHECK(engine.LearnFromRing("snd-1", firstAlarm).ok);
            learned = true;
            learnAt = -1.0;
        }
    }
    CHECK(alarms >= 3);
    CHECK(learned);
    CHECK(repeats == 2);  // plays 2 and 3 of the recording
}

// Two 6 s takes of the same sound (a double 1800 Hz beep) recorded by the phone microphone in a noisy room, exactly as
// the Teach flow records them. Room noise above the trainer's own cut threshold used to make the takes disagree.
TEST(real_phone_takes_of_one_sound_can_be_taught) {
    std::vector<int16_t> pcm;
    const bool loaded = LoadWav("entry/src/main/cpp/tests/data/phone_teach_takes.wav", &pcm);
    CHECK(loaded);
    if (!loaded || pcm.size() != 192000) return;
    const std::vector<int16_t> take1(pcm.begin(), pcm.begin() + 96000);
    const std::vector<int16_t> take2(pcm.begin() + 96000, pcm.end());

    sns::SoundEngine engine(sns::kSampleRate);
    CHECK(engine.CheckTake(take1).ok);
    CHECK(engine.CheckTake(take2).ok);
    const auto taught = engine.TrainFromTakes("snd-9", {take1, take2});
    CHECK(taught.ok);
    if (!taught.ok) std::printf("    teach failed: %s\n", taught.message.c_str());
    if (taught.profile.beepCount != 2) {
        std::printf("    profile: beeps=%d duration=%.2f hz=%.0f rate=%.2f\n", taught.profile.beepCount,
                    taught.profile.durationSec, taught.profile.dominantHz, taught.profile.beepsPerSec);
    }
    CHECK(taught.profile.beepCount == 2);
    CHECK_NEAR(taught.profile.dominantHz, 1800.0, 60.0);
}

// Three 6 s takes of a rising four-note arpeggio (loudness also rises) played from a PC speaker in a noisy room.
// Takes 1 and 3 agree with each other; take 2 is cut differently by the trainer (the quiet first notes are lost in the
// noise) and agrees with neither. Teaching must use the two that agree instead of rejecting the lot.
TEST(real_phone_takes_with_one_odd_one_out_are_taught_from_the_two_that_agree) {
    std::vector<int16_t> pcm;
    const bool loaded = LoadWav("entry/src/main/cpp/tests/data/phone_arpeggio_3takes.wav", &pcm);
    CHECK(loaded);
    if (!loaded || pcm.size() != 288000) return;
    std::vector<std::vector<int16_t>> takes;
    for (size_t i = 0; i < 3; ++i) takes.emplace_back(pcm.begin() + i * 96000, pcm.begin() + (i + 1) * 96000);

    sns::SoundEngine engine(sns::kSampleRate);
    const auto taught = engine.TrainFromTakes("snd-7", takes);
    CHECK(taught.ok);
    if (!taught.ok) std::printf("    teach failed: %s\n", taught.message.c_str());
    CHECK(taught.droppedTake == 1);  // the middle take was left out
}

TEST(two_takes_that_disagree_are_still_rejected) {
    std::vector<int16_t> pcm;
    const bool loaded = LoadWav("entry/src/main/cpp/tests/data/phone_arpeggio_3takes.wav", &pcm);
    CHECK(loaded);
    if (!loaded || pcm.size() != 288000) return;
    const std::vector<int16_t> good(pcm.begin(), pcm.begin() + 96000);
    const std::vector<int16_t> odd(pcm.begin() + 96000, pcm.begin() + 192000);
    sns::SoundEngine engine(sns::kSampleRate);
    CHECK(!engine.TrainFromTakes("snd-8", {good, odd}).ok);  // with two takes there is no majority to trust
}

// Two takes of an irregular pattern (2 kHz short loud, 3 kHz long soft, 1.5 kHz medium, uneven gaps) from a PC speaker
// in a noisy room. The soft middle note is only a few dB above the room; a gate that demands 12 dB cut it out of one
// take and not the other, so the takes were judged different (similarity 0.52).
TEST(real_phone_takes_of_an_irregular_note_pattern_can_be_taught) {
    std::vector<int16_t> pcm;
    const bool loaded = LoadWav("entry/src/main/cpp/tests/data/phone_irregular_2takes.wav", &pcm);
    CHECK(loaded);
    if (!loaded || pcm.size() != 192000) return;
    const std::vector<int16_t> a(pcm.begin(), pcm.begin() + 96000), b(pcm.begin() + 96000, pcm.end());
    sns::SoundEngine engine(sns::kSampleRate);
    const auto taught = engine.TrainFromTakes("snd-6", {a, b});
    CHECK(taught.ok);
    if (!taught.ok) std::printf("    teach failed: %s\n", taught.message.c_str());
}
