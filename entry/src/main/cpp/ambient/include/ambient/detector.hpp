#pragma once
#include <complex>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "ambient/custom.hpp"
#include "ambient/fft.hpp"

namespace ambient {

enum class EventType { Alarm, Knock, LoudSound, Custom, Chirp, Cry, Siren, Scream };

const char* to_string(EventType t);

struct Event {
    EventType type;
    float confidence;  // 0..1, heuristic
    double time_s;     // detection time since start / last reset()
    float freq_hz;     // dominant frequency (Alarm, Chirp) / fundamental (Cry, Scream), mean pitch (Siren), otherwise 0
    float level_db;    // frame level in dBFS at detection
    // Custom only:
    double start_s = 0.0;  // start of the matched sound
    int sound_id = -1;     // id returned by add_custom_sound()
    std::string label{};   // name of the custom sound
};

struct Config {
    int sample_rate = 16000;
    std::size_t frame_size = 1024;  // power of two
    std::size_t hop_size = 512;     // 0 < hop <= frame

    float min_level_db = -40.f;     // ignore anything quieter (dBFS)

    // Alarm: sustained, narrow-band tone
    float tonal_ratio_min = 0.5f;   // share of power within +-3 bins of the peak
    float alarm_min_hz = 800.f;
    float alarm_max_hz = 4500.f;
    float alarm_min_s = 0.4f;
    int alarm_gap_frames = 4;       // tolerated non-tonal frames inside one alarm

    // Transients: Knock (short, decays fast) / LoudSound (sustained, broadband)
    float onset_db = 12.f;          // rise over background that counts as an onset
    float decay_db = 10.f;          // drop from peak that ends a knock
    float knock_max_s = 0.25f;
    float background_alpha = 0.05f; // background level smoothing per frame

    // Chirp: short, steady-pitch beep (smoke-alarm low-battery chirp, appliance beep).
    // Uses the alarm frequency range. Repeats of the same pitch raise the confidence.
    bool detect_chirp = true;
    float chirp_min_s = 0.05f;
    float chirp_max_s = 0.35f;      // must stay below alarm_min_s
    float chirp_max_drift = 0.10f;  // allowed (max-min)/mean pitch change within one chirp
    float chirp_repeat_min_s = 5.f; // a repeat counts if it comes this long ...
    float chirp_repeat_max_s = 120.f; // ... up to this long after the previous chirp

    // Cry (baby crying): voiced, harmonic sound with a fundamental of cry_min_hz..cry_max_hz,
    // arriving in pulses of cry_min_burst_s+ whose pitch moves. Two pulses close together
    // (or one long pulse) make a Cry event; it re-arms after cry_end_s of no further pulses.
    // Heuristic only, tuned on synthetic data: see README / tests/test_sounds.cpp.
    bool detect_cry = true;
    float cry_min_hz = 300.f;
    float cry_max_hz = 700.f;
    float cry_min_level_db = -45.f;
    float cry_harmonic_min = 0.35f;  // share of spectral power sitting on the harmonics
    int cry_min_harmonics = 3;
    float cry_min_burst_s = 0.35f;
    float cry_min_pitch_var = 0.06f; // (max-min)/mean fundamental within a burst
    float cry_max_gap_s = 2.0f;      // pause between two pulses of one episode
    float cry_long_burst_s = 1.5f;   // a single pulse this long is reported on its own
    float cry_end_s = 4.0f;
    int cry_gap_frames = 3;          // tolerated non-cry frames inside one pulse

    // Siren (wail / yelp / hi-lo): a tonal sound whose pitch keeps sweeping up and down for
    // at least siren_min_s. A steady tone stays an Alarm. Note that Alarm (and, before it is confirmed,
    // LoudSound) can fire for the same sound first: the siren is only known after siren_min_s.
    bool detect_siren = true;
    float siren_min_hz = 400.f;
    float siren_max_hz = 2200.f;
    float siren_tonal_min = 0.5f;    // share of power within +-3 bins of the peak
    float siren_min_s = 2.0f;
    float siren_min_sweep = 0.2f;    // (max-min)/mean pitch over the sound
    int siren_gap_frames = 4;

    // Scream: loud, sudden, rough harmonic voice with a high fundamental (above the cry range),
    // a moving pitch and a fairly noisy spectrum (pure whistles / sirens are excluded).
    bool detect_scream = true;
    float scream_min_hz = 700.f;
    float scream_max_hz = 1800.f;
    float scream_min_level_db = -35.f;
    float scream_harmonic_min = 0.35f;
    float scream_harmonic_max = 0.85f;
    int scream_min_harmonics = 3;
    float scream_min_s = 0.4f;
    float scream_min_pitch_var = 0.04f;  // (max-min)/mean fundamental
    float scream_min_rise_db = 15.f;     // over the background at the onset
    int scream_gap_frames = 2;

    // False-alarm control for Knock / LoudSound (they are level-only detectors, so everyday sounds
    // such as speech, clatter or a fan changing speed used to trigger them far too often).
    float transient_min_db = -35.f;       // a Knock / LoudSound must peak at least this loud (dBFS)
    float transient_refractory_s = 1.5f;  // after one, further events of the same kind (Knock / LoudSound) are suppressed this long
    float knock_rise_db = 15.f;           // a Knock must rise this much within ONE hop (impulsive); speech builds up slower
    bool suppress_speech = true;          // voiced speech is neither a Knock nor a LoudSound
    float speech_min_hz = 80.f;           // fundamental range searched for speech (below the cry range)
    float speech_max_hz = 320.f;
    float speech_harmonic_min = 0.35f;    // share of power on the harmonics for a frame to count as voiced
    int speech_min_harmonics = 4;
    float speech_min_share = 0.4f;        // share of a transient's frames that must be voiced to call it speech

    // Knock / LoudSound events are held back this long (only while custom sounds are
    // registered) so a short beep that matches a custom sound is not also reported as a knock.
    float custom_hold_s = 0.5f;

    // Throws std::invalid_argument when inconsistent.
    void validate() const;
};

// Streaming detector. Feed mono 16-bit PCM in chunks of any size.
// Thread-safe: every public method is serialised, so custom sounds may be added/removed from the
// UI thread while the audio thread calls process(). Not copyable.
class Detector {
public:
    explicit Detector(Config cfg = {});
    std::vector<Event> process(const std::int16_t* samples, std::size_t count);
    void reset();  // clears streaming state; registered custom sounds are kept
    const Config& config() const { return cfg_; }

    // Events still held back (see Config::custom_hold_s). Call at end of stream.
    std::vector<Event> flush();

    // Custom sounds. The sound must have been trained with the same sample_rate/frame/hop.
    // Adding a sound with an existing name replaces it. Throws std::invalid_argument.
    int add_custom_sound(const CustomSound& s);
    bool remove_custom_sound(const std::string& name);
    std::vector<std::string> custom_sound_names() const;

    // Seconds of audio consumed since construction / reset(). Compare with wall-clock time to
    // notice a dead microphone (the user must be told when the app can no longer hear).
    double stream_time_s() const;

    // Offline feature extraction (used by the trainer); does not touch streaming state.
    FrameMatrix extract_frames(const std::int16_t* samples, std::size_t count);

private:
    struct Features {
        float level_db;
        float peak_hz;
        float tonal_ratio;
        float f0_hz;        // best harmonic fundamental in the cry range (0 if none / off)
        float harm_ratio;   // power on harmonics of f0 / total power
        int harm_count;     // harmonics clearly present
        float s_f0_hz;      // same for the (higher) scream range
        float s_harm_ratio;
        int s_harm_count;
        float v_harm_ratio;  // same for the speech range
        int v_harm_count;
    };
    struct Harm {
        float f0 = 0.f;
        float ratio = 0.f;
        int count = 0;
    };
    enum class State { Idle, Onset, Sustained };

    Features analyze(const float* frame, bool with_bands);
    Harm harmonic_scan(float bin_hz, float f0_lo, float f0_hi, float hi_hz_max, float min_occupancy = 0.5f) const;
    void on_frame(const Features& f, double t, std::vector<Event>& out);
    void reset_state();
    void release_held(double now, std::vector<Event>& out);

    mutable std::mutex mu_;
    Config cfg_;
    Fft fft_;
    std::vector<float> window_;
    std::vector<std::complex<float>> spec_;
    BandBank bank_;
    std::vector<float> bands_;
    std::vector<float> pending_;
    std::size_t read_ = 0;        // read offset inside pending_
    std::uint64_t consumed_ = 0;  // samples dropped from the front of pending_
    float hop_s_ = 0.f;

    // custom sounds
    CustomMatcher matcher_;
    std::vector<CustomMatcher::Match> matches_;
    std::vector<Event> frame_ev_;
    std::vector<Event> held_;

    // alarm state
    int tonal_run_ = 0;
    int miss_run_ = 0;
    bool alarm_active_ = false;

    // chirp state
    int chirp_frames_ = 0;
    float chirp_min_hz_ = 0.f, chirp_max_hz_ = 0.f, chirp_ratio_sum_ = 0.f, chirp_peak_db_ = -90.f;
    double last_chirp_t_ = -1e9;
    float last_chirp_hz_ = 0.f;

    // cry state
    int cry_run_ = 0;      // cry frames in the current pulse
    int cry_miss_ = 0;
    float cry_f0_min_ = 0.f, cry_f0_max_ = 0.f, cry_f0_sum_ = 0.f, cry_ratio_sum_ = 0.f, cry_peak_db_ = -90.f;
    int cry_pulses_ = 0;
    double cry_last_end_ = -1e9;
    bool cry_active_ = false;
    int claimed_onset_frames_ = 0;  // cry/scream-like frames inside the current transient onset
    int voiced_onset_frames_ = 0;   // speech-like frames inside the current transient onset
    float onset_rise_db_ = 0.f;     // largest single-hop level rise inside the current onset
    double last_transient_t_[2] = {-1e9, -1e9};  // time of the last reported Knock / LoudSound

    // siren state
    int siren_run_ = 0, siren_miss_ = 0, siren_smooth_run_ = 0;
    float prev_siren_hz_ = 0.f;
    float siren_min_hz_ = 0.f, siren_max_hz_ = 0.f, siren_hz_sum_ = 0.f, siren_ref_ = 0.f, siren_extreme_ = 0.f;
    float siren_peak_db_ = -90.f;
    int siren_dir_ = 0, siren_reversals_ = 0;
    bool siren_active_ = false;

    // scream state
    int scream_run_ = 0, scream_miss_ = 0;
    float scream_f0_min_ = 0.f, scream_f0_max_ = 0.f, scream_f0_sum_ = 0.f, scream_peak_db_ = -90.f;
    bool scream_active_ = false;

    // transient state
    State state_ = State::Idle;
    bool bg_init_ = false;
    float bg_db_ = -90.f;
    int onset_frames_ = 0;
    int tonal_frames_ = 0;
    float peak_db_ = -90.f;
    float prev_db_ = -90.f;    // previous frame level (to spot sudden jumps while Sustained)
    bool peak_tonal_ = false;  // was the loudest frame of the current onset a pure tone?
    float jump_db_ = 0.f;
};

}  // namespace ambient
