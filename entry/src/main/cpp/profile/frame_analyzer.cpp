#include "frame_analyzer.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "fft.h"

namespace sns {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float kSilenceDb = -100.0f;
constexpr int kMinBin = 3;  // ignore DC and rumble below ~95 Hz when judging tonality
constexpr float kPeakOverMean = 6.0f;
constexpr float kBandLowHz = 200.0f;
constexpr float kBandHighHz = 7600.0f;
}  // namespace

FrameAnalyzer::FrameAnalyzer(int sampleRate) : sampleRate_(sampleRate) {
    for (int i = 0; i < kFrameSize; ++i) {
        window_[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * i / (kFrameSize - 1)));
    }
}

FrameFeatures FrameAnalyzer::Analyze(const int16_t* frame) const {
    FrameFeatures out{};
    out.levelDb = kSilenceDb;

    double sumSq = 0.0;
    std::array<float, kFrameSize> windowed;
    for (int i = 0; i < kFrameSize; ++i) {
        const double s = frame[i] / 32768.0;
        sumSq += s * s;
        windowed[i] = static_cast<float>(s) * window_[i];
    }
    const double rms = std::sqrt(sumSq / kFrameSize);
    if (rms > 1e-5) out.levelDb = std::max(kSilenceDb, static_cast<float>(20.0 * std::log10(rms)));
    if (out.levelDb <= kSilenceDb) return out;  // true digital silence: leave everything else zero

    std::array<float, kFftBins> mag;
    RealFftMagnitude(windowed.data(), mag.data());

    double sum = 0.0;
    float maxMag = 0.0f;
    for (int k = kMinBin; k < kFftBins; ++k) {
        sum += mag[k];
        maxMag = std::max(maxMag, mag[k]);
    }
    const float mean = static_cast<float>(sum / (kFftBins - kMinBin));
    if (mean <= 0.0f) return out;
    out.tonality = maxMag / mean;

    // Peaks: local maxima well above the mean, strongest first, parabolic frequency estimate.
    const float binHz = static_cast<float>(sampleRate_) / kFftSize;
    std::vector<int> candidates;
    for (int k = kMinBin; k < kFftBins - 1; ++k) {
        if (mag[k] > mag[k - 1] && mag[k] >= mag[k + 1] && mag[k] > kPeakOverMean * mean) candidates.push_back(k);
    }
    std::sort(candidates.begin(), candidates.end(), [&](int a, int b) { return mag[a] > mag[b]; });
    for (int k : candidates) {
        if (out.peakCount >= 3) break;
        const float a = mag[k - 1], b = mag[k], c = mag[k + 1];
        const float denom = a - 2.0f * b + c;
        const float delta = denom != 0.0f ? 0.5f * (a - c) / denom : 0.0f;
        out.peakHz[out.peakCount++] = (static_cast<float>(k) + delta) * binHz;
    }

    // Band energy (power) over 16 log-spaced bands, normalised to sum 1.
    std::array<double, 16> power{};
    double total = 0.0;
    for (int k = 1; k < kFftBins; ++k) {
        const float hz = k * binHz;
        if (hz < kBandLowHz || hz >= kBandHighHz) continue;
        const int band = static_cast<int>(16.0 * std::log(hz / kBandLowHz) / std::log(kBandHighHz / kBandLowHz));
        const int idx = std::min(15, std::max(0, band));
        const double p = static_cast<double>(mag[k]) * mag[k];
        power[idx] += p;
        total += p;
    }
    if (total > 0.0) {
        for (int i = 0; i < 16; ++i) out.bandEnergy[i] = static_cast<float>(power[i] / total);
    }
    return out;
}

}  // namespace sns
