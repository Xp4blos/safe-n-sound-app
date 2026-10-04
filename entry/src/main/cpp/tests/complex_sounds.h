#pragma once
// Complex test signals made of several notes with different pitches, spacings and dynamics (16 kHz, 16-bit), plus a
// helper that mixes a signal into real room noise at a chosen level above it.
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

#include "synth.h"

namespace complex_sounds {

struct Partial {
    double hz;
    double amp;
};

// One note: partials with a short attack and an exponential decay (tau <= 0 means no decay), optional tremolo.
inline std::vector<double> Note(const std::vector<Partial>& partials, double sec, double gain, double decayTau = 0.0,
                                double tremoloHz = 0.0, double tremoloDepth = 0.0) {
    const size_t n = static_cast<size_t>(std::llround(sec * kSynthRate));
    std::vector<double> out(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kSynthRate;
        double v = 0.0;
        for (const auto& p : partials) v += p.amp * std::sin(2.0 * kSynthPi * p.hz * t);
        const double attack = std::min(1.0, t / 0.01);
        const double release = std::min(1.0, (sec - t) / 0.01);
        const double decay = decayTau > 0.0 ? std::exp(-t / decayTau) : 1.0;
        const double trem = tremoloHz > 0.0 ? 1.0 - tremoloDepth * 0.5 * (1.0 - std::cos(2.0 * kSynthPi * tremoloHz * t)) : 1.0;
        out[i] = gain * attack * release * decay * trem * v;
    }
    return out;
}

inline std::vector<double> Gap(double sec) {
    return std::vector<double>(static_cast<size_t>(std::llround(sec * kSynthRate)), 0.0);
}

inline std::vector<double> Join(std::initializer_list<std::vector<double>> parts) {
    std::vector<double> out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

// Two-note door chime: a high note then a lower, longer one, each decaying like a struck bell.
inline std::vector<double> Chime() {
    return Join({Note({{1320, 1.0}, {2640, 0.25}}, 0.6, 0.9, 0.30), Gap(0.04), Note({{990, 1.0}, {1980, 0.25}}, 1.0, 0.8, 0.45)});
}

// Rising four-note arpeggio that also gets louder.
inline std::vector<double> Arpeggio() {
    return Join({Note({{800, 1.0}}, 0.32, 0.30), Gap(0.05), Note({{1000, 1.0}}, 0.32, 0.45), Gap(0.05),
                 Note({{1250, 1.0}}, 0.32, 0.65), Gap(0.05), Note({{1600, 1.0}}, 0.40, 0.90)});
}

// Two-tone siren: five cycles alternating 1000 Hz and 1500 Hz, no gaps.
inline std::vector<double> Siren() {
    std::vector<double> out;
    for (int i = 0; i < 5; ++i) {
        const auto a = Note({{1000, 1.0}}, 0.4, 0.8);
        const auto b = Note({{1500, 1.0}}, 0.4, 0.8);
        out.insert(out.end(), a.begin(), a.end());
        out.insert(out.end(), b.begin(), b.end());
    }
    return out;
}

// Irregular pattern: short loud 2 kHz, long soft 3 kHz, medium 1.5 kHz, with uneven gaps.
inline std::vector<double> Irregular() {
    return Join({Note({{2000, 1.0}}, 0.45, 0.90), Gap(0.40), Note({{3000, 1.0}}, 0.70, 0.40), Gap(0.15),
                 Note({{1500, 1.0}}, 0.55, 0.65)});
}

// Rich tone (three harmonics) with an 8 Hz tremolo.
inline std::vector<double> Tremolo() {
    return Note({{1200, 1.0}, {2400, 0.5}, {3600, 0.3}}, 2.2, 0.8, 0.0, 8.0, 0.6);
}

struct Named {
    const char* name;
    std::vector<double> (*make)();
};

inline const std::vector<Named>& All() {
    static const std::vector<Named> all = {{"chime", Chime}, {"arpeggio", Arpeggio}, {"siren", Siren},
                                           {"irregular", Irregular}, {"tremolo", Tremolo}};
    return all;
}

inline double Rms(const std::vector<double>& x) {
    double s = 0.0;
    for (double v : x) s += v * v;
    return x.empty() ? 0.0 : std::sqrt(s / static_cast<double>(x.size()));
}

// Room noise from a real recording, read endlessly in order (wrapping around).
class NoiseSource {
public:
    explicit NoiseSource(std::vector<int16_t> segment) : seg_(std::move(segment)) {
        std::vector<double> d(seg_.size());
        for (size_t i = 0; i < seg_.size(); ++i) d[i] = seg_[i] / 32768.0;
        rms_ = Rms(d);
    }
    double rms() const { return rms_; }
    std::vector<double> Next(size_t n) {
        std::vector<double> out(n);
        for (size_t i = 0; i < n; ++i) {
            out[i] = seg_[pos_] / 32768.0;
            pos_ = (pos_ + 1) % seg_.size();
        }
        return out;
    }
    void Skip(size_t n) { pos_ = (pos_ + n) % seg_.size(); }

private:
    std::vector<int16_t> seg_;
    size_t pos_ = 0;
    double rms_ = 0.0;
};

inline int16_t Clip(double v) {
    return ToSample(v);
}

// [lead seconds of noise][signal mixed into noise at snrDb above the noise RMS][tail seconds of noise]
inline std::vector<int16_t> Scene(NoiseSource& noise, const std::vector<double>& signal, double snrDb, double leadSec,
                                  double tailSec, double extraGainDb = 0.0) {
    const double targetRms = noise.rms() * std::pow(10.0, (snrDb + extraGainDb) / 20.0);
    const double sigRms = Rms(signal);
    const double k = sigRms > 0.0 ? targetRms / sigRms : 0.0;
    const size_t lead = static_cast<size_t>(leadSec * kSynthRate), tail = static_cast<size_t>(tailSec * kSynthRate);
    std::vector<double> n = noise.Next(lead + signal.size() + tail);
    std::vector<int16_t> out(n.size());
    for (size_t i = 0; i < n.size(); ++i) {
        double v = n[i];
        if (i >= lead && i < lead + signal.size()) v += k * signal[i - lead];
        out[i] = Clip(v);
    }
    return out;
}

}  // namespace complex_sounds
