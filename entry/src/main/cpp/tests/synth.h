#pragma once
// Deterministic synthetic audio for tests. Amplitudes are fractions of full scale (1.0 = 32767).
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <vector>

constexpr double kSynthRate = 16000.0;
constexpr double kSynthPi = 3.14159265358979323846;

inline int16_t ToSample(double v) {
    if (v > 1.0) v = 1.0;
    if (v < -1.0) v = -1.0;
    return static_cast<int16_t>(std::lround(v * 32767.0));
}

inline std::vector<int16_t> Tone(double hz, double sec, double amp) {
    const size_t n = static_cast<size_t>(std::llround(sec * kSynthRate));
    std::vector<int16_t> out(n);
    for (size_t i = 0; i < n; ++i) {
        out[i] = ToSample(amp * std::sin(2.0 * kSynthPi * hz * static_cast<double>(i) / kSynthRate));
    }
    return out;
}

inline std::vector<int16_t> Silence(double sec) {
    return std::vector<int16_t>(static_cast<size_t>(std::llround(sec * kSynthRate)), 0);
}

// Uniform white noise from a small LCG, so results are identical on every run.
inline std::vector<int16_t> Noise(double sec, double amp, uint32_t seed) {
    const size_t n = static_cast<size_t>(std::llround(sec * kSynthRate));
    std::vector<int16_t> out(n);
    uint32_t s = seed ? seed : 1u;
    for (size_t i = 0; i < n; ++i) {
        s = s * 1664525u + 1013904223u;
        const double u = (static_cast<double>(s >> 8) / 8388608.0) - 1.0;  // [-1, 1)
        out[i] = ToSample(amp * u);
    }
    return out;
}

inline std::vector<int16_t> Concat(std::initializer_list<std::vector<int16_t>> parts) {
    std::vector<int16_t> out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

// `count` beeps of `onSec` separated by `offSec` of silence (no trailing silence).
inline std::vector<int16_t> BeepTrain(double hz, double onSec, double offSec, int count, double amp) {
    std::vector<int16_t> out;
    for (int i = 0; i < count; ++i) {
        const auto beep = Tone(hz, onSec, amp);
        out.insert(out.end(), beep.begin(), beep.end());
        if (i + 1 < count) {
            const auto gap = Silence(offSec);
            out.insert(out.end(), gap.begin(), gap.end());
        }
    }
    return out;
}

// Single-sample clicks every `periodSec` for `sec` seconds, silence in between.
inline std::vector<int16_t> Impulses(double periodSec, double sec, double amp) {
    std::vector<int16_t> out = Silence(sec);
    const size_t step = static_cast<size_t>(std::llround(periodSec * kSynthRate));
    for (size_t i = 0; i < out.size(); i += step) out[i] = ToSample(amp);
    return out;
}

// Linear frequency sweep from f0 to f1 Hz over `sec` seconds.
inline std::vector<int16_t> Sweep(double f0, double f1, double sec, double amp) {
    const size_t n = static_cast<size_t>(std::llround(sec * kSynthRate));
    std::vector<int16_t> out(n);
    double phase = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double f = f0 + (f1 - f0) * static_cast<double>(i) / static_cast<double>(n);
        phase += 2.0 * kSynthPi * f / kSynthRate;
        out[i] = ToSample(amp * std::sin(phase));
    }
    return out;
}
