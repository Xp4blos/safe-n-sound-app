#pragma once
#include <complex>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ambient {

// Custom sounds (fridge beep, microwave bell, ...) are matched on a compact
// "spectrogram fingerprint": one log-energy value per frequency band per frame.
constexpr std::size_t kBands = 24;
constexpr float kDynRangeDb = 30.f;  // everything below (peak - this) is treated as silence

// Maps an FFT spectrum to kBands log-spaced band energies in dB.
class BandBank {
public:
    BandBank(int sample_rate, std::size_t frame_size);
    void compute(const std::complex<float>* spec, float* out_db) const;  // out_db[kBands]

private:
    std::vector<std::size_t> edges_;  // kBands + 1 FFT bin edges
};

// Per-frame features of a whole recording (offline).
struct FrameMatrix {
    std::size_t frames = 0;
    std::vector<float> level_db;  // frames
    std::vector<float> bands;     // frames * kBands, dB
};

// A learned sound. Plain data: store it with serialize(), restore with deserialize_sound().
struct CustomSound {
    std::string name;
    std::uint32_t sample_rate = 0;
    std::uint32_t frame_size = 0;
    std::uint32_t hop_size = 0;
    std::uint32_t frames = 0;       // template length in frames
    float threshold = 0.8f;         // min similarity (0..1) to fire
    float min_level_db = -60.f;     // window must contain a frame at least this loud (dBFS)
    float refractory_s = 2.f;       // ignore this sound for this long after a hit
    std::vector<float> tmpl;        // frames * kBands, zero-mean, unit norm
    std::vector<float> env;         // frames, loudness envelope (zero-mean, unit norm); empty if flat
};

// Throws std::invalid_argument when the sound is inconsistent / corrupt.
void validate_sound(const CustomSound& s);

std::vector<std::uint8_t> serialize(const CustomSound& s);
CustomSound deserialize_sound(const std::uint8_t* data, std::size_t size);  // throws std::invalid_argument

// Level-invariant normalisation of a window of dB values (band matrix or loudness envelope):
// clamp to (max - kDynRangeDb), remove mean, scale to unit norm.
// Returns false if the window is flat (no structure). `out` may alias nothing.
bool prepare_window(const float* db, std::size_t n, float* out);

// Similarity of a window to a template = mean of spectral-shape and envelope correlation
// (spectral only when the template has no envelope).

// Streaming matcher over the per-frame band vectors.
class CustomMatcher {
public:
    struct Match {
        int id;
        float score;
        double time_s;   // end of best-matching window
        double start_s;  // start of best-matching window
        float level_db;  // loudest frame in the window
    };

    void configure(double hop_s, double frame_s);
    int add(CustomSound s);  // same name replaces the old one (id kept); returns id
    bool remove(const std::string& name);
    bool empty() const { return slots_.empty(); }
    const CustomSound* find(int id) const;
    std::vector<std::string> names() const;
    void reset();  // forget history, keep sounds
    void push(const float* bands, float level_db, double t, std::vector<Match>& out);

private:
    struct Slot {
        int id = 0;
        CustomSound s;
        bool above = false;
        float best = 0.f;
        double best_t = 0.0;
        float best_level = 0.f;
        int since_best = 0;  // frames since `best` was last improved
        int cooldown = 0;
    };
    void rebuild_ring();

    double hop_s_ = 0.032, frame_s_ = 0.064;
    std::vector<Slot> slots_;
    int next_id_ = 0;
    std::size_t cap_ = 0;
    std::uint64_t n_ = 0;
    std::vector<float> ring_, ring_level_, win_, prep_, lvl_, env_prep_;
};

}  // namespace ambient
