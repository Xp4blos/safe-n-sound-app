#include "ambient/detector.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ambient {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kEps = 1e-9f;
constexpr int kPeakHalfWidth = 3;   // bins around the spectral peak
constexpr float kMinAnalysisHz = 100.f;
constexpr float kExplainMarginDb = 6.f;  // custom match hides built-in events up to this much louder
constexpr float kMaxCryBinHz = 50.f;     // coarser FFT resolution cannot resolve harmonics

float clamp01(float v) { return std::min(1.f, std::max(0.f, v)); }
}  // namespace

const char* to_string(EventType t) {
    switch (t) {
        case EventType::Alarm: return "alarm";
        case EventType::Knock: return "knock";
        case EventType::LoudSound: return "loud_sound";
        case EventType::Custom: return "custom";
        case EventType::Chirp: return "chirp";
        case EventType::Cry: return "cry";
        case EventType::Siren: return "siren";
        case EventType::Scream: return "scream";
    }
    return "unknown";
}

void Config::validate() const {
    if (sample_rate < 8000) throw std::invalid_argument("sample_rate too low");
    if (frame_size < 64 || (frame_size & (frame_size - 1)) != 0)
        throw std::invalid_argument("frame_size must be a power of two >= 64");
    if (hop_size == 0 || hop_size > frame_size)
        throw std::invalid_argument("hop_size must be in (0, frame_size]");
    if (tonal_ratio_min <= 0.f || tonal_ratio_min > 1.f)
        throw std::invalid_argument("tonal_ratio_min must be in (0, 1]");
    if (alarm_min_hz >= alarm_max_hz || alarm_max_hz > sample_rate / 2.f)
        throw std::invalid_argument("invalid alarm frequency range");
    if (custom_hold_s < 0.f) throw std::invalid_argument("custom_hold_s must be >= 0");
    if (sample_rate > 192000 || frame_size > 16384)
        throw std::invalid_argument("sample_rate / frame_size too large");
    if (!std::isfinite(min_level_db) || min_level_db > 0.f)
        throw std::invalid_argument("min_level_db must be finite and <= 0");
    if (!(onset_db > 0.f) || !(decay_db > 0.f) || !std::isfinite(onset_db) || !std::isfinite(decay_db))
        throw std::invalid_argument("onset_db / decay_db must be > 0");
    if (!std::isfinite(transient_min_db) || transient_min_db > 0.f || !std::isfinite(transient_refractory_s) ||
        transient_refractory_s < 0.f || !std::isfinite(knock_rise_db) || knock_rise_db < 0.f)
        throw std::invalid_argument("invalid transient parameters");
    if (!(speech_min_hz > 0.f && speech_min_hz < speech_max_hz && speech_max_hz < 1000.f) ||
        !(speech_harmonic_min > 0.f && speech_harmonic_min <= 1.f) || speech_min_harmonics < 2 ||
        !(speech_min_share > 0.f && speech_min_share <= 1.f))
        throw std::invalid_argument("invalid speech parameters");
    if (!(alarm_min_s > 0.f) || !(knock_max_s > 0.f))
        throw std::invalid_argument("alarm_min_s / knock_max_s must be > 0");
    if (alarm_gap_frames < 0) throw std::invalid_argument("alarm_gap_frames must be >= 0");
    if (!(background_alpha > 0.f && background_alpha <= 1.f))
        throw std::invalid_argument("background_alpha must be in (0, 1]");
    // NaN-safe: every comparison is written so that NaN fails it.
    if (!(chirp_min_s > 0.f && chirp_min_s < chirp_max_s && chirp_max_s < alarm_min_s &&
          chirp_max_drift >= 0.f && chirp_repeat_min_s >= 0.f && chirp_repeat_max_s >= chirp_repeat_min_s))
        throw std::invalid_argument("invalid chirp parameters");
    if (!(cry_min_hz > 0.f && cry_min_hz < cry_max_hz && cry_harmonic_min > 0.f && cry_harmonic_min <= 1.f &&
          cry_min_harmonics >= 2 && cry_min_burst_s > 0.f && cry_min_pitch_var >= 0.f &&
          cry_max_gap_s >= 0.f && cry_long_burst_s > 0.f && cry_end_s >= 0.f && cry_gap_frames >= 0) ||
        !std::isfinite(cry_min_level_db))
        throw std::invalid_argument("invalid cry parameters");
    if (!(siren_min_hz > 0.f && siren_min_hz < siren_max_hz && siren_max_hz <= sample_rate / 2.f &&
          siren_tonal_min > 0.f && siren_tonal_min <= 1.f && siren_min_s > 0.f && siren_min_sweep >= 0.f &&
          siren_gap_frames >= 0))
        throw std::invalid_argument("invalid siren parameters");
    if (!(scream_min_hz > 0.f && scream_min_hz < scream_max_hz && scream_harmonic_min > 0.f &&
          scream_harmonic_min < scream_harmonic_max && scream_harmonic_max <= 1.f && scream_min_harmonics >= 2 &&
          scream_min_s > 0.f && scream_min_pitch_var >= 0.f && scream_min_rise_db >= 0.f &&
          scream_gap_frames >= 0) ||
        !std::isfinite(scream_min_level_db))
        throw std::invalid_argument("invalid scream parameters");
}

Detector::Detector(Config cfg)
    : cfg_((cfg.validate(), cfg)),
      fft_(cfg_.frame_size),
      window_(cfg_.frame_size),
      spec_(cfg_.frame_size),
      bank_(cfg_.sample_rate, cfg_.frame_size),
      bands_(kBands) {
    for (std::size_t i = 0; i < cfg_.frame_size; ++i) {  // Hann
        window_[i] = 0.5f - 0.5f * std::cos(2.f * kPi * static_cast<float>(i) /
                                            static_cast<float>(cfg_.frame_size));
    }
    hop_s_ = static_cast<float>(cfg_.hop_size) / static_cast<float>(cfg_.sample_rate);
    matcher_.configure(static_cast<double>(cfg_.hop_size) / cfg_.sample_rate,
                       static_cast<double>(cfg_.frame_size) / cfg_.sample_rate);
}

std::vector<std::string> Detector::custom_sound_names() const {
    std::lock_guard<std::mutex> lk(mu_);
    return matcher_.names();
}

double Detector::stream_time_s() const {
    std::lock_guard<std::mutex> lk(mu_);
    return static_cast<double>(consumed_ + read_) / static_cast<double>(cfg_.sample_rate);
}

int Detector::add_custom_sound(const CustomSound& s) {
    validate_sound(s);
    std::lock_guard<std::mutex> lk(mu_);
    if (static_cast<int>(s.sample_rate) != cfg_.sample_rate || s.frame_size != cfg_.frame_size ||
        s.hop_size != cfg_.hop_size)
        throw std::invalid_argument("custom sound was trained with a different sample_rate/frame/hop");
    return matcher_.add(s);
}

bool Detector::remove_custom_sound(const std::string& name) {
    std::lock_guard<std::mutex> lk(mu_);
    return matcher_.remove(name);
}

std::vector<Event> Detector::flush() {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<Event> out;
    out.swap(held_);
    return out;
}

void Detector::release_held(double now, std::vector<Event>& out) {
    std::size_t keep = 0;
    for (auto& e : held_) {
        if (now - e.time_s >= cfg_.custom_hold_s) out.push_back(std::move(e));
        else held_[keep++] = std::move(e);
    }
    held_.resize(keep);
}

FrameMatrix Detector::extract_frames(const std::int16_t* samples, std::size_t count) {
    std::lock_guard<std::mutex> lk(mu_);
    FrameMatrix m;
    if (samples == nullptr) return m;
    std::vector<float> buf(count);
    for (std::size_t i = 0; i < count; ++i) buf[i] = static_cast<float>(samples[i]) / 32768.f;
    for (std::size_t pos = 0; pos + cfg_.frame_size <= count; pos += cfg_.hop_size) {
        const Features f = analyze(buf.data() + pos, true);
        m.level_db.push_back(f.level_db);
        m.bands.insert(m.bands.end(), bands_.begin(), bands_.end());
        ++m.frames;
    }
    return m;
}

void Detector::reset_state() {
    tonal_run_ = miss_run_ = 0;
    alarm_active_ = false;
    state_ = State::Idle;
    bg_init_ = false;
    bg_db_ = -90.f;
    onset_frames_ = tonal_frames_ = 0;
    peak_db_ = -90.f;
    peak_tonal_ = false;
    prev_db_ = -90.f;
    jump_db_ = 0.f;
    chirp_frames_ = 0;
    chirp_min_hz_ = chirp_max_hz_ = chirp_ratio_sum_ = 0.f;
    chirp_peak_db_ = -90.f;
    last_chirp_t_ = -1e9;
    last_chirp_hz_ = 0.f;
    voiced_onset_frames_ = 0;
    onset_rise_db_ = 0.f;
    last_transient_t_[0] = last_transient_t_[1] = -1e9;
    cry_run_ = cry_miss_ = cry_pulses_ = claimed_onset_frames_ = 0;
    cry_f0_min_ = cry_f0_max_ = cry_f0_sum_ = cry_ratio_sum_ = 0.f;
    cry_peak_db_ = -90.f;
    cry_last_end_ = -1e9;
    cry_active_ = false;
    siren_run_ = siren_miss_ = siren_smooth_run_ = siren_dir_ = siren_reversals_ = 0;
    prev_siren_hz_ = siren_min_hz_ = siren_max_hz_ = siren_hz_sum_ = siren_ref_ = siren_extreme_ = 0.f;
    siren_peak_db_ = -90.f;
    siren_active_ = false;
    scream_run_ = scream_miss_ = 0;
    scream_f0_min_ = scream_f0_max_ = scream_f0_sum_ = 0.f;
    scream_peak_db_ = -90.f;
    scream_active_ = false;
}

void Detector::reset() {
    std::lock_guard<std::mutex> lk(mu_);
    held_.clear();
    matcher_.reset();
    pending_.clear();
    read_ = 0;
    consumed_ = 0;
    reset_state();
}

std::vector<Event> Detector::process(const std::int16_t* samples, std::size_t count) {
    std::vector<Event> out;
    if (samples == nullptr || count == 0) return out;
    std::lock_guard<std::mutex> lk(mu_);

    pending_.reserve(pending_.size() + count);
    for (std::size_t i = 0; i < count; ++i) {
        pending_.push_back(static_cast<float>(samples[i]) / 32768.f);
    }

    while (pending_.size() - read_ >= cfg_.frame_size) {
        const bool custom = !matcher_.empty();
        const Features f = analyze(pending_.data() + read_, custom);
        const double t = static_cast<double>(consumed_ + read_ + cfg_.frame_size) /
                         static_cast<double>(cfg_.sample_rate);

        frame_ev_.clear();
        on_frame(f, t, frame_ev_);
        for (auto& e : frame_ev_) {
            const bool holdable = e.type == EventType::Knock || e.type == EventType::LoudSound ||
                                  e.type == EventType::Chirp;
            if (custom && holdable && cfg_.custom_hold_s > 0.f) held_.push_back(std::move(e));
            else out.push_back(std::move(e));
        }

        if (custom) {
            matches_.clear();
            matcher_.push(bands_.data(), f.level_db, t, matches_);
            for (const auto& m : matches_) {
                const CustomSound* s = matcher_.find(m.id);
                if (s == nullptr) continue;
                // The sound is explained by the custom match: drop built-in events inside it.
                // A much louder transient is NOT explained by it and stays reported.
                held_.erase(std::remove_if(held_.begin(), held_.end(),
                                           [&](const Event& h) {
                                               return h.time_s >= m.start_s - hop_s_ &&
                                                      h.level_db <= m.level_db + kExplainMarginDb;
                                           }),
                            held_.end());
                Event e{EventType::Custom,
                        clamp01(0.5f + 0.5f * (m.score - s->threshold) / (1.f - s->threshold)),
                        m.time_s, 0.f, m.level_db};
                e.start_s = m.start_s;
                e.sound_id = m.id;
                e.label = s->name;
                out.push_back(std::move(e));
            }
        }
        release_held(t, out);
        read_ += cfg_.hop_size;
    }

    if (read_ > 0) {  // drop consumed samples
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(read_));
        consumed_ += read_;
        read_ = 0;
    }
    return out;
}

Detector::Features Detector::analyze(const float* frame, bool with_bands) {
    const std::size_t n = cfg_.frame_size;

    double sum_sq = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sum_sq += static_cast<double>(frame[i]) * frame[i];
        spec_[i] = {frame[i] * window_[i], 0.f};
    }
    const float rms = static_cast<float>(std::sqrt(sum_sq / static_cast<double>(n)));
    const float level_db = 20.f * std::log10(rms + kEps);

    fft_.forward(spec_.data());
    if (with_bands) bank_.compute(spec_.data(), bands_.data());

    const float bin_hz = static_cast<float>(cfg_.sample_rate) / static_cast<float>(n);
    const std::size_t half = n / 2;
    const std::size_t lo = std::max<std::size_t>(
        static_cast<std::size_t>(kMinAnalysisHz / bin_hz), kPeakHalfWidth + 1);

    double total = 0.0;
    float best = 0.f;
    std::size_t peak = lo;
    for (std::size_t k = lo; k < half; ++k) {
        const float p = std::norm(spec_[k]);
        total += p;
        if (p > best) {
            best = p;
            peak = k;
        }
    }

    double peak_energy = 0.0;
    const std::size_t a = peak - kPeakHalfWidth;
    const std::size_t b = std::min(half - 1, peak + kPeakHalfWidth);
    for (std::size_t k = a; k <= b; ++k) peak_energy += std::norm(spec_[k]);

    Features f;
    f.level_db = level_db;
    f.peak_hz = static_cast<float>(peak) * bin_hz;
    f.tonal_ratio = total > 0.0 ? static_cast<float>(peak_energy / total) : 0.f;
    f.f0_hz = f.s_f0_hz = 0.f;
    f.harm_ratio = f.s_harm_ratio = 0.f;
    f.harm_count = f.s_harm_count = 0;
    f.v_harm_ratio = 0.f;
    f.v_harm_count = 0;
    if (bin_hz <= kMaxCryBinHz) {
        if (cfg_.detect_cry && level_db >= cfg_.cry_min_level_db) {
            const Harm h = harmonic_scan(bin_hz, cfg_.cry_min_hz, cfg_.cry_max_hz, 5000.f);
            f.f0_hz = h.f0;
            f.harm_ratio = h.ratio;
            f.harm_count = h.count;
        }
        if (cfg_.detect_scream && level_db >= cfg_.scream_min_level_db) {
            const Harm h = harmonic_scan(bin_hz, cfg_.scream_min_hz, cfg_.scream_max_hz, 7500.f);
            f.s_f0_hz = h.f0;
            f.s_harm_ratio = h.ratio;
            f.s_harm_count = h.count;
        }
        if (cfg_.suppress_speech && level_db >= cfg_.transient_min_db - 10.f) {
            const Harm h = harmonic_scan(bin_hz, cfg_.speech_min_hz, cfg_.speech_max_hz, 2000.f, 0.4f);
            f.v_harm_ratio = h.ratio;
            f.v_harm_count = h.count;
        }
    }
    return f;
}

// Finds the fundamental in [f0_lo, f0_hi] whose harmonics (+-1 bin each) carry most of the
// power between 200 Hz and ~5 kHz. The scan step is a quarter bin because the k-th harmonic moves k
// times as far as the fundamental. Ties go to the higher candidate so octave-below errors lose.
Detector::Harm Detector::harmonic_scan(float bin_hz, float f0_lo, float f0_hi, float hi_hz_max,
                                       float min_occupancy) const {
    Harm out;
    const std::size_t half = cfg_.frame_size / 2;
    const float hi_hz = std::min(hi_hz_max, 0.45f * static_cast<float>(cfg_.sample_rate));
    const std::size_t lo = std::max<std::size_t>(1, static_cast<std::size_t>(200.f / bin_hz));
    const std::size_t hi = std::min(half - 2, static_cast<std::size_t>(hi_hz / bin_hz));
    if (hi <= lo + 8) return out;

    double total = 0.0;
    for (std::size_t k = lo; k <= hi; ++k) total += std::norm(spec_[k]);
    if (total <= 0.0) return out;
    const double avg_win = 3.0 * total / static_cast<double>(hi - lo + 1);

    // Harmonic power share (and number of clearly present harmonics) for a candidate fundamental.
    const auto score = [&](float f0, int& present_out, int& slots_out) {
        double sum = 0.0, strongest = 0.0, win[40];
        int n = 0;
        for (int k = 1; k <= 40; ++k) {
            const std::size_t c = static_cast<std::size_t>(std::lround(static_cast<float>(k) * f0 / bin_hz));
            if (c + 1 > hi) break;
            const double w = std::norm(spec_[c - 1]) + std::norm(spec_[c]) + std::norm(spec_[c + 1]);
            win[n++] = w;
            sum += w;
            strongest = std::max(strongest, w);
        }
        present_out = 0;
        slots_out = n;
        if (n < 3) return 0.0;
        for (int i = 0; i < n; ++i)
            if (win[i] >= 0.01 * strongest && win[i] >= 2.0 * avg_win) ++present_out;
        return sum / total;
    };

    const float step = bin_hz * 0.25f;
    double best_ratio = 0.0;
    float best_f0 = 0.f;
    int best_count = 0;
    for (float f0 = f0_hi; f0 >= f0_lo; f0 -= step) {
        int present = 0, slots = 0;
        const double ratio = score(f0, present, slots);
        // Half of the harmonic slots must be occupied: a candidate at 1/3 of the true pitch has
        // every third slot filled and the rest empty, and must not pass as a voice.
        if (static_cast<float>(present) < min_occupancy * static_cast<float>(slots)) continue;
        if (ratio > best_ratio) {
            best_ratio = ratio;
            best_f0 = f0;
            best_count = present;
        }
    }
    // Subharmonic check: if f0/2, f0/3 or f0/4 explains clearly more of the spectrum, the real
    // fundamental is lower (adult voice, machine hum) and this is not a cry-range sound.
    if (best_f0 > 0.f) {
        for (int d = 2; d <= 4; ++d) {
            const float sub = best_f0 / static_cast<float>(d);
            if (sub < 80.f) break;
            int dummy = 0, dummy2 = 0;
            if (score(sub, dummy, dummy2) > 1.4 * best_ratio) {
                best_ratio = 0.0;
                best_count = 0;
                best_f0 = 0.f;
                break;
            }
        }
    }
    out.f0 = best_f0;
    out.ratio = static_cast<float>(best_ratio);
    out.count = best_count;
    return out;
}

void Detector::on_frame(const Features& f, double t, std::vector<Event>& out) {
    const bool audible = f.level_db >= cfg_.min_level_db;

    // ---- Alarm: sustained narrow-band tone --------------------------------
    const bool tonal = audible && f.tonal_ratio >= cfg_.tonal_ratio_min &&
                       f.peak_hz >= cfg_.alarm_min_hz && f.peak_hz <= cfg_.alarm_max_hz;
    if (tonal) {
        ++tonal_run_;
        miss_run_ = 0;
    } else if (++miss_run_ > cfg_.alarm_gap_frames) {
        tonal_run_ = 0;
        alarm_active_ = false;
    }
    if (!alarm_active_ && static_cast<float>(tonal_run_) * hop_s_ >= cfg_.alarm_min_s) {
        alarm_active_ = true;
        // While a confirmed siren is already being reported its tone must not re-fire the alarm.
        if (!siren_active_) out.push_back({EventType::Alarm, clamp01(f.tonal_ratio), t, f.peak_hz, f.level_db});
    }

    // ---- Chirp: short steady beep (strictly consecutive tonal frames) ------
    if (cfg_.detect_chirp) {
        if (alarm_active_) {
            chirp_frames_ = 0;  // it is a sustained alarm, not a chirp
        } else if (tonal) {
            if (chirp_frames_ == 0) {
                chirp_min_hz_ = chirp_max_hz_ = f.peak_hz;
                chirp_ratio_sum_ = 0.f;
                chirp_peak_db_ = -90.f;
            }
            ++chirp_frames_;
            chirp_min_hz_ = std::min(chirp_min_hz_, f.peak_hz);
            chirp_max_hz_ = std::max(chirp_max_hz_, f.peak_hz);
            chirp_ratio_sum_ += f.tonal_ratio;
            chirp_peak_db_ = std::max(chirp_peak_db_, f.level_db);
        } else if (chirp_frames_ > 0) {
            const float dur = static_cast<float>(chirp_frames_) * hop_s_;
            const float mean_hz = 0.5f * (chirp_min_hz_ + chirp_max_hz_);
            if (dur >= cfg_.chirp_min_s && dur <= cfg_.chirp_max_s &&
                chirp_max_hz_ - chirp_min_hz_ <= cfg_.chirp_max_drift * mean_hz) {
                float conf = chirp_ratio_sum_ / static_cast<float>(chirp_frames_);
                const double since = t - last_chirp_t_;
                if (since >= cfg_.chirp_repeat_min_s && since <= cfg_.chirp_repeat_max_s &&
                    std::fabs(mean_hz - last_chirp_hz_) <= cfg_.chirp_max_drift * mean_hz)
                    conf += 0.3f;  // periodic chirping is the smoke-alarm signature
                out.push_back({EventType::Chirp, clamp01(conf), t, mean_hz, chirp_peak_db_});
                last_chirp_t_ = t;
                last_chirp_hz_ = mean_hz;
            }
            chirp_frames_ = 0;
        }
    }

    // ---- Cry: pulsed, harmonic, moving pitch --------------------------------
    const bool cry_frame = cfg_.detect_cry && f.harm_count >= cfg_.cry_min_harmonics &&
                           f.harm_ratio >= cfg_.cry_harmonic_min;
    if (cfg_.detect_cry) {
        if (cry_frame) {
            if (cry_run_ == 0) {
                cry_f0_min_ = cry_f0_max_ = f.f0_hz;
                cry_f0_sum_ = cry_ratio_sum_ = 0.f;
                cry_peak_db_ = -90.f;
            }
            ++cry_run_;
            cry_miss_ = 0;
            cry_f0_min_ = std::min(cry_f0_min_, f.f0_hz);
            cry_f0_max_ = std::max(cry_f0_max_, f.f0_hz);
            cry_f0_sum_ += f.f0_hz;
            cry_ratio_sum_ += f.harm_ratio;
            cry_peak_db_ = std::max(cry_peak_db_, f.level_db);
        } else if (cry_run_ > 0 && ++cry_miss_ > cfg_.cry_gap_frames) {
            const float dur = static_cast<float>(cry_run_) * hop_s_;
            const float mean_f0 = cry_f0_sum_ / static_cast<float>(cry_run_);
            const bool moving = (cry_f0_max_ - cry_f0_min_) >= cfg_.cry_min_pitch_var * mean_f0;
            if (dur >= cfg_.cry_min_burst_s && moving) {
                const double end_t = t - static_cast<double>(cry_miss_) * hop_s_;
                cry_pulses_ = (end_t - cry_last_end_ <= cfg_.cry_max_gap_s) ? cry_pulses_ + 1 : 1;
                cry_last_end_ = end_t;
                if (!cry_active_ && (cry_pulses_ >= 2 || dur >= cfg_.cry_long_burst_s)) {
                    cry_active_ = true;
                    const float conf = cry_ratio_sum_ / static_cast<float>(cry_run_) +
                                       0.1f * static_cast<float>(std::min(cry_pulses_, 3) - 1);
                    out.push_back({EventType::Cry, clamp01(conf), t, mean_f0, cry_peak_db_});
                }
            }
            cry_run_ = 0;
            cry_miss_ = 0;
        }
        if (cry_run_ == 0 && t - cry_last_end_ > cfg_.cry_end_s) {
            cry_active_ = false;  // re-arm after the episode is over
            cry_pulses_ = 0;
        }
    }

    // ---- Siren: tonal, pitch sweeping up and down for a while ----------------
    const bool siren_frame = cfg_.detect_siren && audible && f.tonal_ratio >= cfg_.siren_tonal_min &&
                             f.peak_hz >= cfg_.siren_min_hz && f.peak_hz <= cfg_.siren_max_hz;
    if (siren_frame && prev_siren_hz_ > 0.f && std::fabs(f.peak_hz - prev_siren_hz_) <= 0.1f * prev_siren_hz_)
        ++siren_smooth_run_;
    else
        siren_smooth_run_ = siren_frame ? 1 : 0;
    prev_siren_hz_ = siren_frame ? f.peak_hz : 0.f;
    if (cfg_.detect_siren) {
        constexpr float kTurn = 0.08f;  // pitch must reverse by this much to count as a turning point
        if (siren_frame) {
            const float hz = f.peak_hz;
            if (siren_run_ == 0) {
                siren_min_hz_ = siren_max_hz_ = siren_ref_ = siren_extreme_ = hz;
                siren_hz_sum_ = 0.f;
                siren_dir_ = siren_reversals_ = 0;
                siren_peak_db_ = -90.f;
            }
            ++siren_run_;
            siren_miss_ = 0;
            siren_min_hz_ = std::min(siren_min_hz_, hz);
            siren_max_hz_ = std::max(siren_max_hz_, hz);
            siren_hz_sum_ += hz;
            siren_peak_db_ = std::max(siren_peak_db_, f.level_db);
            if (siren_dir_ == 0) {
                if (hz >= siren_ref_ * (1.f + kTurn)) { siren_dir_ = 1; siren_extreme_ = hz; }
                else if (hz <= siren_ref_ * (1.f - kTurn)) { siren_dir_ = -1; siren_extreme_ = hz; }
            } else if (siren_dir_ > 0) {
                if (hz > siren_extreme_) siren_extreme_ = hz;
                else if (hz <= siren_extreme_ * (1.f - kTurn)) { siren_dir_ = -1; ++siren_reversals_; siren_extreme_ = hz; }
            } else {
                if (hz < siren_extreme_) siren_extreme_ = hz;
                else if (hz >= siren_extreme_ * (1.f + kTurn)) { siren_dir_ = 1; ++siren_reversals_; siren_extreme_ = hz; }
            }
            const float mean_hz = siren_hz_sum_ / static_cast<float>(siren_run_);
            const float span = (siren_max_hz_ - siren_min_hz_) / mean_hz;
            if (!siren_active_ && static_cast<float>(siren_run_) * hop_s_ >= cfg_.siren_min_s &&
                span >= cfg_.siren_min_sweep && siren_reversals_ >= 1) {
                siren_active_ = true;
                out.push_back({EventType::Siren, clamp01(0.5f + span), t, mean_hz, siren_peak_db_});
            }
        } else if (siren_run_ > 0 && ++siren_miss_ > cfg_.siren_gap_frames) {
            siren_run_ = siren_miss_ = 0;
            siren_active_ = false;
        }
    }

    // ---- Scream: loud, sudden, rough harmonic voice, high fundamental ---------
    const bool scream_frame = cfg_.detect_scream && f.s_harm_count >= cfg_.scream_min_harmonics &&
                              f.s_harm_ratio >= cfg_.scream_harmonic_min &&
                              f.s_harm_ratio <= cfg_.scream_harmonic_max;
    if (cfg_.detect_scream) {
        // A smooth, very tonal sweep is a siren, not a voice.
        const bool siren_like = static_cast<float>(siren_smooth_run_) * hop_s_ >= 0.3f;
        if (scream_frame && !siren_like) {
            if (scream_run_ == 0) {
                scream_f0_min_ = scream_f0_max_ = f.s_f0_hz;
                scream_f0_sum_ = 0.f;
                scream_peak_db_ = -90.f;
            }
            ++scream_run_;
            scream_miss_ = 0;
            scream_f0_min_ = std::min(scream_f0_min_, f.s_f0_hz);
            scream_f0_max_ = std::max(scream_f0_max_, f.s_f0_hz);
            scream_f0_sum_ += f.s_f0_hz;
            scream_peak_db_ = std::max(scream_peak_db_, f.level_db);
            const float mean_f0 = scream_f0_sum_ / static_cast<float>(scream_run_);
            if (!scream_active_ && static_cast<float>(scream_run_) * hop_s_ >= cfg_.scream_min_s &&
                scream_f0_max_ - scream_f0_min_ >= cfg_.scream_min_pitch_var * mean_f0 &&
                jump_db_ >= cfg_.scream_min_rise_db) {
                scream_active_ = true;
                const float conf = 0.5f + 0.5f * clamp01((scream_peak_db_ - cfg_.scream_min_level_db) / 25.f);
                out.push_back({EventType::Scream, clamp01(conf), t, mean_f0, scream_peak_db_});
            }
        } else if (scream_run_ > 0 && ++scream_miss_ > cfg_.scream_gap_frames) {
            scream_run_ = scream_miss_ = 0;
            scream_active_ = false;
        }
    }
    const bool claimed = cry_frame || scream_frame;  // explained by a voice detector: not a LoudSound
    const bool voiced = cfg_.suppress_speech && f.v_harm_count >= cfg_.speech_min_harmonics &&
                        f.v_harm_ratio >= cfg_.speech_harmonic_min;

    // Knock / LoudSound gate: loud enough, not speech, not right after the previous one.
    const auto emit_transient = [&](EventType type) {
        if (peak_db_ < cfg_.transient_min_db) return;
        double& last = last_transient_t_[type == EventType::Knock ? 0 : 1];
        if (t - last < cfg_.transient_refractory_s) return;
        if (cfg_.suppress_speech &&
            static_cast<float>(voiced_onset_frames_) >= cfg_.speech_min_share * static_cast<float>(onset_frames_))
            return;
        if (type == EventType::Knock && onset_rise_db_ < cfg_.knock_rise_db) return;
        out.push_back({type, clamp01(jump_db_ / 30.f), t, 0.f, peak_db_});
        last = t;
    };

    // ---- Transients: Knock / LoudSound -------------------------------------
    if (!bg_init_) {
        bg_db_ = f.level_db;
        prev_db_ = f.level_db;  // no artificial "rise" from silence on the very first frame
        bg_init_ = true;
    }

    switch (state_) {
        case State::Idle:
            if (audible && f.level_db - bg_db_ >= cfg_.onset_db) {
                state_ = State::Onset;
                onset_frames_ = 1;
                tonal_frames_ = tonal ? 1 : 0;
                claimed_onset_frames_ = claimed ? 1 : 0;
                voiced_onset_frames_ = voiced ? 1 : 0;
                onset_rise_db_ = f.level_db - prev_db_;
                peak_db_ = f.level_db;
                peak_tonal_ = tonal;
                jump_db_ = f.level_db - bg_db_;
            } else {
                bg_db_ += cfg_.background_alpha * (f.level_db - bg_db_);
            }
            break;

        case State::Onset: {
            ++onset_frames_;
            if (tonal) ++tonal_frames_;
            if (claimed) ++claimed_onset_frames_;
            if (voiced) ++voiced_onset_frames_;
            onset_rise_db_ = std::max(onset_rise_db_, f.level_db - prev_db_);
            if (f.level_db > peak_db_) {
                peak_db_ = f.level_db;
                peak_tonal_ = tonal;
            }

            if (f.level_db < peak_db_ - cfg_.decay_db) {
                // Energy died away quickly: short transient. A steady beep is a Chirp, not a knock.
                // (not when a bang rides on top of a beep: then the loudest frame is not tonal)
                const bool beep = cfg_.detect_chirp && peak_tonal_ && tonal_frames_ * 10 > onset_frames_ * 6;
                if (!beep) emit_transient(EventType::Knock);
                state_ = State::Sustained;
            } else if (static_cast<float>(onset_frames_) * hop_s_ > cfg_.knock_max_s) {
                // Still loud after knock_max_s: sustained sound.
                // the loudest frame must be a tone too, else a bang riding on a beep would be hidden
                const bool mostly_tonal = peak_tonal_ && tonal_frames_ * 10 > onset_frames_ * 6;
                const bool mostly_claimed = claimed_onset_frames_ * 10 > onset_frames_ * 6;
                if (!mostly_tonal && !mostly_claimed) {  // tones / crying have their own detectors
                    emit_transient(EventType::LoudSound);
                }
                state_ = State::Sustained;
            }
            break;
        }

        case State::Sustained:
            // A sudden jump (vs. the previous frame AND the background) is a new transient even
            // though the previous one has not died away yet: a bang right after / inside a beep,
            // a second knock, ...
            if (audible && f.level_db - prev_db_ >= cfg_.onset_db && f.level_db - bg_db_ >= cfg_.onset_db) {
                state_ = State::Onset;
                onset_frames_ = 1;
                tonal_frames_ = tonal ? 1 : 0;
                claimed_onset_frames_ = claimed ? 1 : 0;
                voiced_onset_frames_ = voiced ? 1 : 0;
                onset_rise_db_ = f.level_db - prev_db_;
                peak_db_ = f.level_db;
                peak_tonal_ = tonal;
                jump_db_ = f.level_db - bg_db_;
                break;
            }
            bg_db_ += cfg_.background_alpha * (f.level_db - bg_db_);  // adapt to new ambience
            if (f.level_db < bg_db_ + cfg_.onset_db * 0.5f) state_ = State::Idle;
            break;
    }
    prev_db_ = f.level_db;
}

}  // namespace ambient
