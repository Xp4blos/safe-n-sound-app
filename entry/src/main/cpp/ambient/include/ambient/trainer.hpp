#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ambient/custom.hpp"
#include "ambient/detector.hpp"

namespace ambient {

// One mono 16-bit PCM recording at cfg.sample_rate. Include a bit of silence/background
// before and after the sound; the sound itself is cut out automatically.
struct Recording {
    const std::int16_t* samples;
    std::size_t count;
};

struct TrainOptions {
    float min_contrast_db = 8.f;   // sound must rise this far above the background
    float max_duration_s = 10.f;
    int margin_frames = 2;          // context kept around the sound
    float default_threshold = 0.7f; // used when only one recording is given
    float min_consistency = 0.35f;   // recordings must resemble each other at least this much
    float refractory_s = 2.f;
    // Added by Safe'n'Sound: how disagreeing recordings are handled.
    bool allow_single_fallback = true;  // two recordings that disagree: keep just one (false: reject them)
    std::size_t max_dropped = 1000;     // more outliers than this: reject (default: unlimited)
};

struct TrainResult {
    CustomSound sound;
    float consistency = 1.f;  // lowest pairwise similarity between recordings (1 for a single one)
    float duration_s = 0.f;   // length of the template
    std::vector<std::size_t> dropped;  // indices of recordings left out because they disagreed (Safe'n'Sound addition)
};

// Builds a CustomSound from 1..N recordings (2-3 recommended). Throws std::invalid_argument
// with a user-presentable message if a recording is unusable or the recordings disagree.
TrainResult train_custom_sound(const Config& cfg, const std::string& name,
                               const std::vector<Recording>& recordings,
                               const TrainOptions& opt = {});

}  // namespace ambient
