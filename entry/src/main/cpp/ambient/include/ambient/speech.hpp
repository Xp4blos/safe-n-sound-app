#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Speech-to-text for deaf / hard-of-hearing users, plus "urgent word" alerts.
//
//   microphone PCM --> SpeechRecognizer (platform backend) --> text results
//        text results --> SpeechPipeline --> Caption (text to display, keywords highlighted)
//                                        \-> KeywordAlert (+ Vibrator::vibrate) for "help", "watch out", ...
//
// The recognizer is a pluggable backend. On HarmonyOS the real recognizer is the system
// Core Speech Kit (ArkTS), so the ArkTS side simply forwards its onResult() calls to
// SpeechPipeline::on_result(); see harmonyos/ in this repository.
namespace ambient {

// ---------------------------------------------------------------- vibration

// Alternating durations in milliseconds, starting with "on": {on, off, on, off, on, ...}.
struct VibrationPattern {
    std::vector<std::uint32_t> ms;

    static constexpr std::size_t kMaxSegments = 64;
    static constexpr std::uint32_t kMaxSegmentMs = 5000;
    static constexpr std::uint32_t kMaxTotalMs = 10000;

    bool empty() const { return ms.empty(); }
    std::uint32_t total_ms() const;
    void validate() const;  // throws std::invalid_argument

    static VibrationPattern sos();         // ... --- ...   (used for "help")
    static VibrationPattern rapid_burst();  // 6 short, sharp pulses (used for "watch out")
    static VibrationPattern long_buzz();    // 2 long pulses (used for "danger")
    static VibrationPattern double_tap();   // 2 short taps (used for "look out")
};

// Platform vibrator. Implement it on top of the OS API (on HarmonyOS: @ohos.vibrator).
class Vibrator {
public:
    virtual ~Vibrator() = default;
    virtual void vibrate(const VibrationPattern& p) = 0;
    virtual void cancel() {}
};

// ------------------------------------------------------------------ keywords

struct KeywordRule {
    std::string phrase;  // UTF-8. ASCII is matched case-insensitively on whole words;
                         // phrases containing non-ASCII text (e.g. Chinese) match as substrings.
    std::string label;   // reported to the app; defaults to phrase when empty
    VibrationPattern pattern;
    int priority = 0;       // if several rules fire at once, only the highest vibrates
    float cooldown_s = 2.f;  // same rule is not fired again within this time
};

// "help", "watch out", "look out", "danger" and their Chinese counterparts (the HarmonyOS
// recognizer currently only supports zh-CN): 救命, 小心, 当心, 危险.
std::vector<KeywordRule> default_keyword_rules();

// Lower-cases ASCII, turns punctuation / control characters (ASCII and CJK / general Unicode
// punctuation) into single spaces. `begin`/`end` give, for every byte of `text`, the byte range
// of the original character it came from.
struct NormalizedText {
    std::string text;
    std::vector<std::size_t> begin;
    std::vector<std::size_t> end;
};
NormalizedText normalize_text(const std::string& utf8);

// Converts a byte offset into a UTF-8 string to a UTF-16 code-unit offset, which is what string
// indices in ArkTS / JavaScript use (needed to highlight KeywordHit ranges in a UI).
std::size_t utf8_to_utf16_offset(const std::string& utf8, std::size_t byte_offset);

// ------------------------------------------------------------------- results

struct KeywordHit {  // an occurrence of a keyword inside Caption::text
    std::size_t begin = 0;  // byte offsets into the original (un-normalised) text
    std::size_t end = 0;
    int rule_id = -1;
    std::string label;
};

struct Caption {
    int utterance_id = 0;
    bool is_final = false;
    double time_s = 0.0;
    std::string text;                  // exactly what the recognizer returned
    std::vector<KeywordHit> highlights;  // every keyword occurrence, sorted by position
};

struct KeywordAlert {  // a keyword that just triggered an alert
    int rule_id = -1;
    std::string label;
    std::string phrase;
    double time_s = 0.0;
    std::size_t begin = 0;  // first newly spoken occurrence in Caption::text
    std::size_t end = 0;
    VibrationPattern pattern;
    bool vibrated = false;  // true if Vibrator::vibrate() was called for this alert
};

struct SpeechUpdate {
    Caption caption;
    std::vector<KeywordAlert> alerts;  // sorted by priority, highest first
};

struct SpeechConfig {
    bool use_default_rules = true;      // start with default_keyword_rules()
    bool trigger_on_partial = true;     // alert on interim results (lowest latency)
    std::size_t max_history = 50;       // finalized captions kept by history()
    std::size_t max_text_bytes = 8192;  // longer results are cut (at a UTF-8 boundary)
    double utterance_timeout_s = 5.0;   // results further apart than this start a new utterance

    void validate() const;  // throws std::invalid_argument
};

// Turns recognizer output into captions and alerts. Not thread-safe (SpeechSession is).
//
// Interim results repeat and extend the same sentence ("help" -> "help me" -> "help me please"),
// so an occurrence of a keyword alerts once per utterance, not once per interim result.
class SpeechPipeline {
public:
    explicit SpeechPipeline(SpeechConfig cfg = {}, Vibrator* vibrator = nullptr);

    void set_vibrator(Vibrator* v) { vibrator_ = v; }

    // Adds a rule (a rule with the same normalised phrase is replaced, id kept).
    // Throws std::invalid_argument for an empty phrase or an invalid pattern.
    // An empty pattern means VibrationPattern::long_buzz().
    int add_rule(KeywordRule r);
    bool remove_rule(const std::string& phrase);
    std::vector<KeywordRule> rules() const;

    // One recognizer result. `time_s` is any monotonic clock chosen by the caller.
    SpeechUpdate on_result(const std::string& text, bool is_final, double time_s);

    // The recognition session ended / was restarted without a final result.
    void end_utterance();
    // Forget history, cooldowns and the current utterance. Rules are kept.
    void reset();

    const std::deque<Caption>& history() const { return history_; }

private:
    struct Slot {
        int id = 0;
        KeywordRule rule;
        std::string norm;
        int fired_in_utt = 0;
        bool ever_fired = false;
        double last_fire_s = 0.0;
    };

    SpeechConfig cfg_;
    Vibrator* vibrator_;
    std::vector<Slot> slots_;
    int next_id_ = 0;
    int utterance_id_ = 0;
    bool utt_open_ = false;
    double last_result_s_ = 0.0;
    std::deque<Caption> history_;
};

// ---------------------------------------------------------- recognizer backend

using RecognitionCallback = std::function<void(const std::string& text, bool is_final, double time_s)>;

// A speech recognition engine. Implementations call the callback for every interim and final
// result (from any thread).
class SpeechRecognizer {
public:
    virtual ~SpeechRecognizer() = default;
    virtual void set_callback(RecognitionCallback cb) = 0;
    virtual void start() = 0;
    // stop() must not return while a callback is still running (join the callback thread).
    virtual void stop() = 0;
    // Mono 16-bit PCM from the microphone. May be a no-op if the backend captures audio itself.
    virtual void write_audio(const std::int16_t* samples, std::size_t count) = 0;
};

using SpeechUpdateCallback = std::function<void(const SpeechUpdate&)>;

// Owns a recognizer and a pipeline and connects them. Thread-safe: recognizer callbacks may
// arrive on another thread. `on_update` is called without internal locks held.
class SpeechSession {
public:
    SpeechSession(std::unique_ptr<SpeechRecognizer> recognizer, SpeechUpdateCallback on_update,
                  SpeechConfig cfg = {}, Vibrator* vibrator = nullptr);
    ~SpeechSession();
    SpeechSession(const SpeechSession&) = delete;
    SpeechSession& operator=(const SpeechSession&) = delete;

    void start();
    void stop();
    void write_audio(const std::int16_t* samples, std::size_t count);

    int add_rule(KeywordRule r);
    bool remove_rule(const std::string& phrase);
    std::vector<KeywordRule> rules() const;
    std::vector<Caption> history() const;

private:
    void handle(const std::string& text, bool is_final, double time_s);

    std::unique_ptr<SpeechRecognizer> rec_;
    SpeechUpdateCallback on_update_;
    mutable std::mutex mu_;
    std::atomic<bool> closed_{false};
    SpeechPipeline pipe_;
};

}  // namespace ambient