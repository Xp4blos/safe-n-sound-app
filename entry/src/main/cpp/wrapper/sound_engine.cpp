#include "sound_engine.h"

#include <algorithm>
#include <cmath>
#include <exception>

#include "types.h"

namespace sns {

namespace {
constexpr float kBandFloorDb = -62.0f;  // quiet room noise stays dim ...
constexpr float kBandCeilDb = -12.0f;   // ... and a clear sound lights its band fully

ambient::Config MakeConfig(int sampleRate) {
    ambient::Config c;
    c.sample_rate = sampleRate;
    return c;
}

std::vector<ambient::Recording> AsRecordings(const std::vector<std::vector<int16_t>>& takes) {
    std::vector<ambient::Recording> out;
    for (const auto& t : takes) out.push_back({t.data(), t.size()});
    return out;
}

// The trainer cuts a sound out of a recording by loudness alone (a few dB above the 10th percentile level). In a
// noisy room random noise crosses that threshold, so two takes of the same sound get different cuts and are rejected
// as "not matching". Before training, a take is therefore reduced to its clearest tonal stretch: everything outside
// it (plus a short margin) is silenced. A take without any tonal sound is returned unchanged.
// Teaching: two recordings must be at least this alike (measured on 25 real phone recordings of five signals: 0.55
// accepted 46/50 pairs and 50/50 triples of the same sound and 0/50 pairs of different sounds; 0.45 accepted 4/50 of
// the different ones).
constexpr float kTeachMinConsistency = 0.55f;

constexpr double kGateMarginSec = 0.25;
constexpr double kGateMaxGapSec = 1.0;    // tonal frames closer than this belong to one sound
constexpr double kGateMinSoundSec = 0.25;
constexpr float kGateTonality = 25.0f;   // room hum has 15-35, a clear tone 30-75
constexpr float kGateMinHz = 600.0f;     // below this it is mostly hum and rumble
constexpr float kGateMaxHz = 6000.0f;
constexpr float kGateAboveBackgroundDb = 8.0f;   // 12 cut quiet notes of multi-note sounds out of some takes only

std::vector<int16_t> GateToTonalSound(const std::vector<int16_t>& take, int sampleRate,
                                      double marginSec = kGateMarginSec) {
    const size_t frames = take.size() / kFrameSize;
    if (frames < 8) return take;
    FrameAnalyzer analyzer(sampleRate);
    std::vector<FrameFeatures> f(frames);
    std::vector<float> levels(frames);
    for (size_t i = 0; i < frames; ++i) {
        f[i] = analyzer.Analyze(take.data() + i * kFrameSize);
        levels[i] = f[i].levelDb;
    }
    std::vector<float> sorted = levels;
    std::sort(sorted.begin(), sorted.end());
    const float background = sorted[sorted.size() / 5];

    const double frameSec = static_cast<double>(kFrameSize) / sampleRate;
    const size_t maxGap = static_cast<size_t>(kGateMaxGapSec / frameSec);
    struct Cluster {
        size_t first, last, count;
    };
    std::vector<Cluster> clusters;
    for (size_t i = 0; i < frames; ++i) {
        const bool inBand = f[i].peakCount > 0 && f[i].peakHz[0] >= kGateMinHz && f[i].peakHz[0] <= kGateMaxHz;
        const bool active = inBand && levels[i] > background + kGateAboveBackgroundDb && f[i].tonality >= kGateTonality;
        if (!active) continue;
        if (!clusters.empty() && i - clusters.back().last <= maxGap) {
            clusters.back().last = i;
            ++clusters.back().count;
        } else {
            clusters.push_back({i, i, 1});
        }
    }
    if (clusters.empty()) return take;
    const Cluster best = *std::max_element(clusters.begin(), clusters.end(),
                                           [](const Cluster& a, const Cluster& b) { return a.count < b.count; });
    if (static_cast<double>(best.last - best.first + 1) * frameSec < kGateMinSoundSec) return take;

    const size_t margin = static_cast<size_t>(marginSec / frameSec);
    const size_t from = (best.first > margin ? best.first - margin : 0) * kFrameSize;
    const size_t to = std::min(frames, best.last + 1 + margin) * kFrameSize;
    std::vector<int16_t> gated(take.size(), 0);
    std::copy(take.begin() + static_cast<std::ptrdiff_t>(from), take.begin() + static_cast<std::ptrdiff_t>(to),
              gated.begin() + static_cast<std::ptrdiff_t>(from));
    return gated;
}
}  // namespace

SoundEngine::SoundEngine(int sampleRate)
    : sampleRate_(sampleRate),
      cfg_(MakeConfig(sampleRate)),
      detector_(cfg_),
      analyzer_(sampleRate) {}

void SoundEngine::AppendRing(const int16_t* pcm, size_t n) {
    ring_.insert(ring_.end(), pcm, pcm + n);
    total_ += n;
    const size_t cap = static_cast<size_t>(kRingSeconds * sampleRate_);
    if (ring_.size() > cap) {
        const size_t drop = ring_.size() - cap;
        ring_.erase(ring_.begin(), ring_.begin() + static_cast<std::ptrdiff_t>(drop));
        ringStart_ += drop;
    }
}

void SoundEngine::UpdateLiveBands(const int16_t* pcm, size_t n) {
    bandPending_.insert(bandPending_.end(), pcm, pcm + n);
    std::array<float, kLiveBands> peak{};
    bool any = false;
    size_t read = 0;
    while (bandPending_.size() - read >= static_cast<size_t>(kFrameSize)) {
        const FrameFeatures f = analyzer_.Analyze(bandPending_.data() + read);
        levelDb_ = f.levelDb;
        for (int i = 0; i < kLiveBands; ++i) {
            const float bandDb = f.levelDb + 10.0f * std::log10(f.bandEnergy[i] + 1e-6f);
            const float v = std::min(1.0f, std::max(0.0f, (bandDb - kBandFloorDb) / (kBandCeilDb - kBandFloorDb)));
            peak[i] = std::max(peak[i], v);
        }
        any = true;
        read += kFrameSize;
    }
    bandPending_.erase(bandPending_.begin(), bandPending_.begin() + static_cast<std::ptrdiff_t>(read));
    if (any) bands_ = peak;
}

ProcessResult SoundEngine::Process(const int16_t* pcm, size_t sampleCount) {
    ProcessResult out;
    out.levelDb = levelDb_;
    out.bands = bands_;
    if (pcm == nullptr || sampleCount == 0) return out;

    AppendRing(pcm, sampleCount);
    UpdateLiveBands(pcm, sampleCount);
    out.levelDb = levelDb_;
    out.bands = bands_;

    try {
        for (const auto& e : detector_.process(pcm, sampleCount)) {
            EngineEvent ev;
            if (e.type == ambient::EventType::Alarm) {
                ev.type = "alarm";
                ev.startSec = e.time_s;
            } else if (e.type == ambient::EventType::Custom) {
                ev.type = "custom";
                ev.startSec = e.start_s;
                ev.label = e.label;
            } else {
                // chirp, cry, siren, scream, knock, loud_sound: surfaced as they are; the app decides which ones alert.
                ev.type = ambient::to_string(e.type);
                ev.startSec = e.time_s;
            }
            ev.timeSec = e.time_s;
            ev.confidence = e.confidence;
            ev.freqHz = e.freq_hz;
            ev.levelDb = e.level_db;
            out.events.push_back(std::move(ev));
        }
    } catch (const std::exception&) {
        // A detector failure must not take the app down; the next chunk starts clean.
    }
    return out;
}

LearnResult SoundEngine::Train(const std::string& label, const std::vector<ambient::Recording>& recordings,
                               const SoundProfile& profile, bool registerSound) {
    LearnResult r;
    try {
        ambient::TrainOptions options;
        options.default_threshold = matchThreshold_;
        options.allow_single_fallback = false;  // never silently keep one of two different sounds
        options.max_dropped = 1;
        options.min_consistency = kTeachMinConsistency;
        ambient::TrainResult tr = ambient::train_custom_sound(cfg_, label, recordings, options);
        // With several recordings the trainer picks a threshold of 0.7-0.9 from how well they agree; on a phone in a
        // real room that misses quieter repeats, so it is capped at the same value as for a single recording.
        tr.sound.threshold = std::min(tr.sound.threshold, matchThreshold_);
        if (registerSound) detector_.add_custom_sound(tr.sound);
        r.templateBytes = ambient::serialize(tr.sound);
        r.consistency = tr.consistency;
        if (!tr.dropped.empty()) r.droppedTake = static_cast<int>(tr.dropped.front());
        r.profile = profile;
        r.ok = true;
    } catch (const std::exception& e) {
        r.message = e.what();
    }
    return r;
}

LearnResult SoundEngine::LearnFromRing(const std::string& label, double eventTimeSec) {
    const uint64_t want1 = static_cast<uint64_t>(std::max(0.0, (eventTimeSec + kLearnAfterSec) * sampleRate_));
    if (want1 > total_) {
        LearnResult r;
        r.message = "Not enough audio after the sound yet.";
        return r;
    }
    const uint64_t want0 = static_cast<uint64_t>(std::max(0.0, (eventTimeSec - kLearnBeforeSec) * sampleRate_));
    const uint64_t from = std::max(want0, ringStart_);
    if (from >= want1) {
        LearnResult r;
        r.message = "The sound is no longer in memory.";
        return r;
    }
    const std::vector<int16_t> window = GateToTonalSound(
        std::vector<int16_t>(ring_.begin() + static_cast<std::ptrdiff_t>(from - ringStart_),
                             ring_.begin() + static_cast<std::ptrdiff_t>(want1 - ringStart_)),
        sampleRate_);
    const std::vector<int16_t> core = GateToTonalSound(window, sampleRate_, 0.0);
    const SoundProfile profile = DescribeSound(core.data(), core.size(), sampleRate_);
    return Train(label, {{window.data(), window.size()}}, profile, true);
}

LearnResult SoundEngine::TrainFromTakes(const std::string& label, const std::vector<std::vector<int16_t>>& takes) {
    if (takes.empty()) {
        LearnResult r;
        r.message = "Record at least one take.";
        return r;
    }
    std::vector<std::vector<int16_t>> gated;
    for (const auto& t : takes) gated.push_back(GateToTonalSound(t, sampleRate_));
    const auto profileOf = [&](size_t i) {
        const std::vector<int16_t> core = GateToTonalSound(takes[i], sampleRate_, 0.0);
        return DescribeSound(core.data(), core.size(), sampleRate_);
    };
    // With three or more takes the trainer leaves out one that disagrees (droppedTake); the profile then describes the
    // first take that was kept.
    LearnResult r = Train(label, AsRecordings(gated), profileOf(0), true);
    if (r.ok && r.droppedTake == 0) r.profile = profileOf(1);
    return r;
}

LearnResult SoundEngine::CheckTake(const std::vector<int16_t>& take) {
    const std::vector<int16_t> gated = GateToTonalSound(take, sampleRate_);
    const std::vector<int16_t> core = GateToTonalSound(take, sampleRate_, 0.0);
    const SoundProfile profile = DescribeSound(core.data(), core.size(), sampleRate_);
    return Train("take-check", {{gated.data(), gated.size()}}, profile, false);
}

bool SoundEngine::AddSound(const std::vector<uint8_t>& bytes, std::string* error) {
    try {
        const ambient::CustomSound s = ambient::deserialize_sound(bytes.data(), bytes.size());
        detector_.add_custom_sound(s);
        return true;
    } catch (const std::exception& e) {
        if (error != nullptr) *error = e.what();
        return false;
    }
}

bool SoundEngine::RemoveSound(const std::string& label) {
    return detector_.remove_custom_sound(label);
}

}  // namespace sns
