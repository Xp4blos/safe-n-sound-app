#pragma once
#include <array>
#include <cstdint>

#include "types.h"

namespace sns {

struct FrameFeatures {
    float levelDb;                     // RMS in dBFS, -100 for silence
    float tonality;                    // strongest spectral peak / mean magnitude
    int peakCount;                     // number of reported peaks (0..3)
    std::array<float, 3> peakHz;       // top 3 peaks by magnitude, 0 when absent
    std::array<float, 16> bandEnergy;  // 16 log bands 200 Hz..7.6 kHz, sum 1 (all 0 for silence)
};

class FrameAnalyzer {
public:
    explicit FrameAnalyzer(int sampleRate);
    // `frame` must point at kFrameSize samples.
    FrameFeatures Analyze(const int16_t* frame) const;

private:
    int sampleRate_;
    std::array<float, kFrameSize> window_;
};

}  // namespace sns
