#include "profile.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "frame_analyzer.h"

namespace sns {

namespace {
constexpr float kActiveAboveBackgroundDb = 12.0f;
constexpr float kMinActiveLevelDb = -60.0f;
constexpr float kBelowPeakDb = 15.0f;  // frames more than this below the loudest frame are not part of the sound
constexpr int kSegmentGapFrames = 3;       // fewer inactive frames than this keep one segment
constexpr double kContinuousSec = 2.5;     // a single stretch at least this long is continuous
constexpr float kSweepRelativeRange = 0.15f;

float Percentile(std::vector<float> v, float q) {
    if (v.empty()) return 0.0f;
    std::sort(v.begin(), v.end());
    const size_t i = std::min(v.size() - 1, static_cast<size_t>(q * static_cast<float>(v.size())));
    return v[i];
}
}  // namespace

SoundProfile DescribeSound(const int16_t* pcm, size_t sampleCount, int sampleRate) {
    SoundProfile out;
    if (pcm == nullptr || sampleCount < static_cast<size_t>(kFrameSize) * 4) return out;

    FrameAnalyzer analyzer(sampleRate);
    const double frameSec = static_cast<double>(kFrameSize) / sampleRate;
    std::vector<FrameFeatures> frames;
    for (size_t pos = 0; pos + kFrameSize <= sampleCount; pos += kFrameSize) {
        frames.push_back(analyzer.Analyze(pcm + pos));
    }

    std::vector<float> levels;
    for (const auto& f : frames) levels.push_back(f.levelDb);
    const float peak = *std::max_element(levels.begin(), levels.end());
    const float threshold = std::max(std::max(Percentile(levels, 0.2f) + kActiveAboveBackgroundDb, kMinActiveLevelDb),
                                     peak - kBelowPeakDb);

    std::vector<bool> active(frames.size());
    int first = -1, last = -1;
    for (size_t i = 0; i < frames.size(); ++i) {
        active[i] = frames[i].levelDb > threshold;
        if (active[i]) {
            if (first < 0) first = static_cast<int>(i);
            last = static_cast<int>(i);
        }
    }
    if (first < 0) return out;
    out.durationSec = static_cast<float>((last - first + 1) * frameSec);

    // Segments: stretches of active frames, merged when separated by fewer than kSegmentGapFrames.
    std::vector<int> segStart;
    int inactiveRun = kSegmentGapFrames;
    for (int i = first; i <= last; ++i) {
        if (active[i]) {
            if (inactiveRun >= kSegmentGapFrames) segStart.push_back(i);
            inactiveRun = 0;
        } else {
            ++inactiveRun;
        }
    }
    out.beepCount = static_cast<int>(segStart.size());
    if (segStart.size() >= 2) {
        const double period = static_cast<double>(segStart.back() - segStart.front()) /
                              static_cast<double>(segStart.size() - 1) * frameSec;
        out.beepsPerSec = period > 0.0 ? static_cast<float>(1.0 / period) : 0.0f;
        out.repetition = 1;
        out.modulation = 1;
    } else {
        out.repetition = out.durationSec >= kContinuousSec ? 2 : 0;
    }

    std::vector<float> peaks;
    for (int i = first; i <= last; ++i) {
        if (active[i] && frames[i].peakCount > 0) peaks.push_back(frames[i].peakHz[0]);
    }
    out.dominantHz = Percentile(peaks, 0.5f);
    if (out.modulation == 0 && !peaks.empty() && out.dominantHz > 0.0f) {
        const float range = Percentile(peaks, 0.9f) - Percentile(peaks, 0.1f);
        if (range / out.dominantHz > kSweepRelativeRange) out.modulation = 2;
    }

    // Envelope: 8 points of linear amplitude over the active part, scaled to max = 1.
    const int n = last - first + 1;
    float maxV = 0.0f;
    for (int j = 0; j < 8; ++j) {
        const int lo = first + j * n / 8;
        const int hi = std::max(lo + 1, first + (j + 1) * n / 8);
        double acc = 0.0;
        int cnt = 0;
        for (int i = lo; i < hi && i <= last; ++i, ++cnt) acc += std::pow(10.0, frames[i].levelDb / 20.0);
        out.envelope[j] = cnt ? static_cast<float>(acc / cnt) : 0.0f;
        maxV = std::max(maxV, out.envelope[j]);
    }
    if (maxV > 0.0f) {
        for (auto& v : out.envelope) v /= maxV;
    }
    return out;
}

}  // namespace sns
