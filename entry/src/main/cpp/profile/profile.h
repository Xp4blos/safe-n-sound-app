#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace sns {

// Plain-words description numbers of a recorded sound (the app turns them into text).
struct SoundProfile {
    float dominantHz = 0.0f;   // median strongest-peak frequency over the active frames
    float durationSec = 0.0f;  // first to last active frame
    int beepCount = 0;         // separate active stretches
    float beepsPerSec = 0.0f;  // repetition rate when beepCount >= 2, else 0
    int repetition = 0;        // 0 single, 1 repeated, 2 continuous
    int modulation = 0;        // 0 steady, 1 pulsed, 2 sweeping
    std::array<float, 8> envelope{};  // loudness envelope over the active part, max = 1
};

// `pcm` is mono 16-bit at `sampleRate` (16 kHz expected), ideally with a little background around the sound.
// Returns an all-zero profile when nothing stands out from the background.
SoundProfile DescribeSound(const int16_t* pcm, size_t sampleCount, int sampleRate);

}  // namespace sns
