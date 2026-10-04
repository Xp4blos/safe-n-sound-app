#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ambient/detector.hpp"
#include "ambient/trainer.hpp"
#include "frame_analyzer.h"
#include "profile.h"

namespace sns {

constexpr int kLiveBands = 16;               // bars of the live spectrum, log-spaced 200 Hz .. 7.6 kHz
constexpr double kRingSeconds = 10.0;        // audio kept in memory for learning (never stored)
constexpr double kLearnBeforeSec = 2.0;      // learning window: from this long before the alarm event ...
constexpr double kLearnAfterSec = 3.5;       // ... to this long after it
constexpr float kLearnedMatchThreshold = 0.60f;  // similarity needed to recognise a learned sound again
                                                 // (the engine default 0.8 and 0.7 missed quieter real repeats on a phone)

// type: "alarm" (a sustained narrow-band tone, the app learns it as an unknown sound), "custom" (a learned or taught
// sound recognised again), or a built-in class: "chirp", "cry", "siren", "scream", "knock", "loud_sound".
struct EngineEvent {
    std::string type;
    double timeSec = 0.0;   // engine stream time of the detection
    double startSec = 0.0;  // start of the matched sound (custom), else same as timeSec
    float confidence = 0.0f;
    float freqHz = 0.0f;    // dominant frequency (alarm)
    float levelDb = 0.0f;
    std::string label;      // custom: the label the sound was learned or taught under
};

struct ProcessResult {
    float levelDb = -100.0f;                // level of the latest analysed frame
    std::array<float, kLiveBands> bands{};  // 0..1 per band, for the live view
    std::vector<EngineEvent> events;
};

struct LearnResult {
    bool ok = false;
    std::string message;                 // reason when !ok, user presentable
    std::vector<uint8_t> templateBytes;  // serialised custom sound (feature numbers, not audio)
    SoundProfile profile;
    float consistency = 1.0f;
    int droppedTake = -1;                // index of a take left out because it disagreed with the others, or -1
};

// Application layer over ambient::Detector. Not thread safe: call from one thread (the JS thread).
class SoundEngine {
public:
    explicit SoundEngine(int sampleRate);

    // Any chunk size; null/empty input is ignored.
    ProcessResult Process(const int16_t* pcm, size_t sampleCount);

    // Trains a template from the in-memory audio around an alarm event (needs kLearnAfterSec of audio after it),
    // registers it under `label` and returns it for storage.
    LearnResult LearnFromRing(const std::string& label, double eventTimeSec);

    // Trains from 1..N deliberate recordings of the same sound and registers it under `label`.
    LearnResult TrainFromTakes(const std::string& label, const std::vector<std::vector<int16_t>>& takes);

    // Checks whether one recording is usable for teaching, without registering anything.
    LearnResult CheckTake(const std::vector<int16_t>& take);

    // Registers a stored template. False with `error` set when the data is corrupt or inconsistent.
    bool AddSound(const std::vector<uint8_t>& bytes, std::string* error);
    bool RemoveSound(const std::string& label);

    // Similarity needed to recognise sounds learned or taught from now on (default kLearnedMatchThreshold).
    void SetMatchThreshold(float threshold) { matchThreshold_ = threshold; }

private:
    void AppendRing(const int16_t* pcm, size_t n);
    void UpdateLiveBands(const int16_t* pcm, size_t n);
    LearnResult Train(const std::string& label, const std::vector<ambient::Recording>& recordings,
                      const SoundProfile& profile, bool registerSound);

    int sampleRate_;
    ambient::Config cfg_;
    ambient::Detector detector_;
    FrameAnalyzer analyzer_;
    std::vector<int16_t> ring_;
    uint64_t ringStart_ = 0;  // absolute sample index of ring_[0]
    uint64_t total_ = 0;      // samples received so far
    std::vector<int16_t> bandPending_;
    float matchThreshold_ = kLearnedMatchThreshold;
    float levelDb_ = -100.0f;
    std::array<float, kLiveBands> bands_{};
};

}  // namespace sns
