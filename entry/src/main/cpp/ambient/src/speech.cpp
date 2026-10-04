#include "ambient/speech.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ambient {

namespace {

bool is_ascii_alnum(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// Length of the valid UTF-8 sequence at s[i] (and its code point), 0 if invalid.
std::size_t decode_utf8(const std::string& s, std::size_t i, std::uint32_t& cp) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    std::size_t n = 0;
    if (b0 >= 0xC2 && b0 <= 0xDF) { n = 2; cp = b0 & 0x1Fu; }
    else if (b0 >= 0xE0 && b0 <= 0xEF) { n = 3; cp = b0 & 0x0Fu; }
    else if (b0 >= 0xF0 && b0 <= 0xF4) { n = 4; cp = b0 & 0x07u; }
    else return 0;
    if (i + n > s.size()) return 0;
    for (std::size_t k = 1; k < n; ++k) {
        const auto b = static_cast<unsigned char>(s[i + k]);
        if ((b & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (b & 0x3Fu);
    }
    if ((n == 3 && cp < 0x800) || (n == 4 && (cp < 0x10000 || cp > 0x10FFFF))) return 0;  // overlong
    if (cp >= 0xD800 && cp <= 0xDFFF) return 0;                                           // surrogate
    return n;
}

// Spaces and punctuation outside ASCII (CJK, full-width forms, quotes, dashes, ellipsis, ...).
bool is_unicode_separator(std::uint32_t cp) {
    return cp == 0xA0 || cp == 0xA1 || cp == 0xA7 || cp == 0xAB || cp == 0xB6 || cp == 0xB7 ||
           cp == 0xBB || cp == 0xBF || (cp >= 0x2000 && cp <= 0x206F) ||
           (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0xFF01 && cp <= 0xFF0F) ||
           (cp >= 0xFF1A && cp <= 0xFF20) || (cp >= 0xFF3B && cp <= 0xFF40) ||
           (cp >= 0xFF5B && cp <= 0xFF65);
}

// Start offsets of whole-word (for ASCII edges) occurrences of `p` in `text`.
std::vector<std::size_t> find_all(const std::string& text, const std::string& p) {
    std::vector<std::size_t> r;
    if (p.empty()) return r;
    const bool bound_l = is_ascii_alnum(p.front());
    const bool bound_r = is_ascii_alnum(p.back());
    std::size_t pos = 0;
    while ((pos = text.find(p, pos)) != std::string::npos) {
        const std::size_t e = pos + p.size();
        const bool ok = !(bound_l && pos > 0 && is_ascii_alnum(text[pos - 1])) &&
                        !(bound_r && e < text.size() && is_ascii_alnum(text[e]));
        if (ok) {
            r.push_back(pos);
            pos = e;
        } else {
            ++pos;
        }
    }
    return r;
}

KeywordRule make_rule(const char* phrase, const char* label, VibrationPattern p, int priority) {
    KeywordRule r;
    r.phrase = phrase;
    r.label = label;
    r.pattern = std::move(p);
    r.priority = priority;
    r.cooldown_s = 1.f;
    return r;
}

}  // namespace

// ---------------------------------------------------------------- vibration

std::uint32_t VibrationPattern::total_ms() const {
    std::uint64_t t = 0;
    for (auto v : ms) t += v;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(t, std::numeric_limits<std::uint32_t>::max()));
}

void VibrationPattern::validate() const {
    if (ms.empty()) throw std::invalid_argument("vibration pattern is empty");
    if (ms.size() > kMaxSegments) throw std::invalid_argument("vibration pattern has too many segments");
    for (std::size_t i = 0; i < ms.size(); ++i) {
        if (ms[i] > kMaxSegmentMs) throw std::invalid_argument("vibration segment too long");
        if (i % 2 == 0 && ms[i] == 0) throw std::invalid_argument("vibration 'on' segment must be > 0");
    }
    if (total_ms() > kMaxTotalMs) throw std::invalid_argument("vibration pattern too long");
}

VibrationPattern VibrationPattern::sos() {
    return {{150, 100, 150, 100, 150, 250, 450, 100, 450, 100, 450, 250, 150, 100, 150, 100, 150}};
}
VibrationPattern VibrationPattern::rapid_burst() { return {{90, 60, 90, 60, 90, 60, 90, 60, 90, 60, 90}}; }
VibrationPattern VibrationPattern::long_buzz() { return {{600, 200, 600}}; }
VibrationPattern VibrationPattern::double_tap() { return {{80, 80, 80}}; }

// ------------------------------------------------------------------ keywords

std::vector<KeywordRule> default_keyword_rules() {
    return {
        make_rule("help", "help", VibrationPattern::sos(), 100),
        make_rule("watch out", "watch out", VibrationPattern::rapid_burst(), 90),
        make_rule("danger", "danger", VibrationPattern::long_buzz(), 80),
        make_rule("look out", "look out", VibrationPattern::double_tap(), 70),
        make_rule("\xE6\x95\x91\xE5\x91\xBD", "help", VibrationPattern::sos(), 100),               // 救命
        make_rule("\xE5\xB0\x8F\xE5\xBF\x83", "watch out", VibrationPattern::rapid_burst(), 90),   // 小心
        make_rule("\xE5\xBD\x93\xE5\xBF\x83", "watch out", VibrationPattern::rapid_burst(), 90),   // 当心
        make_rule("\xE5\x8D\xB1\xE9\x99\xA9", "danger", VibrationPattern::long_buzz(), 80),        // 危险
    };
}

NormalizedText normalize_text(const std::string& s) {
    NormalizedText out;
    out.text.reserve(s.size());
    auto push = [&](char c, std::size_t b, std::size_t e) {
        out.text.push_back(c);
        out.begin.push_back(b);
        out.end.push_back(e);
    };
    auto push_space = [&](std::size_t b, std::size_t e) {
        if (!out.text.empty() && out.text.back() != ' ') push(' ', b, e);
    };

    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            if (is_ascii_alnum(static_cast<char>(c))) {
                const char lower = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
                push(lower, i, i + 1);
            } else {
                push_space(i, i + 1);  // punctuation, whitespace, control characters
            }
            ++i;
            continue;
        }
        std::uint32_t cp = 0;
        const std::size_t n = decode_utf8(s, i, cp);
        if (n == 0) {  // invalid byte: keep it, it just never matches anything
            push(static_cast<char>(c), i, i + 1);
            ++i;
        } else if (is_unicode_separator(cp)) {
            push_space(i, i + n);
            i += n;
        } else {
            for (std::size_t k = 0; k < n; ++k) push(s[i + k], i, i + n);
            i += n;
        }
    }
    if (!out.text.empty() && out.text.back() == ' ') {
        out.text.pop_back();
        out.begin.pop_back();
        out.end.pop_back();
    }
    return out;
}

std::size_t utf8_to_utf16_offset(const std::string& s, std::size_t byte_offset) {
    std::size_t units = 0;
    const std::size_t limit = std::min(byte_offset, s.size());
    std::size_t i = 0;
    while (i < limit) {
        std::uint32_t cp = 0;
        std::size_t n = 1;
        if (static_cast<unsigned char>(s[i]) >= 0x80) {
            const std::size_t d = decode_utf8(s, i, cp);
            if (d != 0) n = d;
        }
        units += (n == 4) ? 2 : 1;  // astral characters are surrogate pairs in UTF-16
        i += n;
    }
    return units;
}

// ------------------------------------------------------------------ pipeline

void SpeechConfig::validate() const {
    if (max_text_bytes < 16) throw std::invalid_argument("max_text_bytes too small");
    if (utterance_timeout_s <= 0.0) throw std::invalid_argument("utterance_timeout_s must be > 0");
}

SpeechPipeline::SpeechPipeline(SpeechConfig cfg, Vibrator* vibrator)
    : cfg_((cfg.validate(), cfg)), vibrator_(vibrator) {
    if (cfg_.use_default_rules) {
        for (auto& r : default_keyword_rules()) add_rule(std::move(r));
    }
}

int SpeechPipeline::add_rule(KeywordRule r) {
    std::string norm = normalize_text(r.phrase).text;
    if (norm.empty()) throw std::invalid_argument("keyword phrase is empty");
    if (r.cooldown_s < 0.f) throw std::invalid_argument("cooldown_s must be >= 0");
    if (r.pattern.empty()) r.pattern = VibrationPattern::long_buzz();
    r.pattern.validate();
    if (r.label.empty()) r.label = r.phrase;

    for (auto& s : slots_) {
        if (s.norm == norm) {
            s.rule = std::move(r);
            return s.id;
        }
    }
    Slot s;
    s.id = next_id_++;
    s.rule = std::move(r);
    s.norm = std::move(norm);
    slots_.push_back(std::move(s));
    return slots_.back().id;
}

bool SpeechPipeline::remove_rule(const std::string& phrase) {
    const std::string norm = normalize_text(phrase).text;
    auto it = std::find_if(slots_.begin(), slots_.end(), [&](const Slot& s) { return s.norm == norm; });
    if (it == slots_.end()) return false;
    slots_.erase(it);
    return true;
}

std::vector<KeywordRule> SpeechPipeline::rules() const {
    std::vector<KeywordRule> r;
    r.reserve(slots_.size());
    for (const auto& s : slots_) r.push_back(s.rule);
    return r;
}

void SpeechPipeline::end_utterance() {
    for (auto& s : slots_) s.fired_in_utt = 0;
    if (utt_open_) ++utterance_id_;
    utt_open_ = false;
}

void SpeechPipeline::reset() {
    end_utterance();
    history_.clear();
    for (auto& s : slots_) s.ever_fired = false;
    last_result_s_ = 0.0;
}

SpeechUpdate SpeechPipeline::on_result(const std::string& text_in, bool is_final, double t) {
    if (utt_open_ && t - last_result_s_ > cfg_.utterance_timeout_s) end_utterance();
    utt_open_ = true;
    last_result_s_ = t;

    SpeechUpdate up;
    Caption& cap = up.caption;
    cap.utterance_id = utterance_id_;
    cap.is_final = is_final;
    cap.time_s = t;
    cap.text = text_in;
    if (cap.text.size() > cfg_.max_text_bytes) {
        std::size_t cut = cfg_.max_text_bytes;
        while (cut > 0 && (static_cast<unsigned char>(cap.text[cut]) & 0xC0) == 0x80) --cut;
        cap.text.resize(cut);
    }

    const NormalizedText nt = normalize_text(cap.text);
    const bool allow_alerts = is_final || cfg_.trigger_on_partial;

    for (auto& s : slots_) {
        const auto pos = find_all(nt.text, s.norm);
        // The recognizer may revise its hypothesis: occurrences that vanished must not mask new ones.
        s.fired_in_utt = std::min<int>(s.fired_in_utt, static_cast<int>(pos.size()));
        const auto span_begin = [&](std::size_t p) { return nt.begin[p]; };
        const auto span_end = [&](std::size_t p) { return nt.end[p + s.norm.size() - 1]; };
        for (auto p : pos) cap.highlights.push_back({span_begin(p), span_end(p), s.id, s.rule.label});

        if (!allow_alerts || pos.size() <= static_cast<std::size_t>(s.fired_in_utt)) continue;
        const std::size_t first_new = pos[static_cast<std::size_t>(s.fired_in_utt)];
        s.fired_in_utt = static_cast<int>(pos.size());  // each occurrence is decided exactly once

        const double since = t - s.last_fire_s;
        if (s.ever_fired && since >= 0.0 && since < static_cast<double>(s.rule.cooldown_s)) continue;
        s.ever_fired = true;
        s.last_fire_s = t;

        KeywordAlert a;
        a.rule_id = s.id;
        a.label = s.rule.label;
        a.phrase = s.rule.phrase;
        a.time_s = t;
        a.begin = span_begin(first_new);
        a.end = span_end(first_new);
        a.pattern = s.rule.pattern;
        up.alerts.push_back(std::move(a));
    }

    std::sort(cap.highlights.begin(), cap.highlights.end(), [](const KeywordHit& a, const KeywordHit& b) {
        return a.begin != b.begin ? a.begin < b.begin : a.end < b.end;
    });

    if (!up.alerts.empty()) {
        const auto prio = [&](const KeywordAlert& a) {
            for (const auto& s : slots_) if (s.id == a.rule_id) return s.rule.priority;
            return 0;
        };
        std::stable_sort(up.alerts.begin(), up.alerts.end(), [&](const KeywordAlert& a, const KeywordAlert& b) {
            return prio(a) > prio(b);
        });
        if (vibrator_) {  // a failing vibrator must never cost the user the caption
            try {
                vibrator_->vibrate(up.alerts.front().pattern);
                up.alerts.front().vibrated = true;
            } catch (...) {
            }
        }
    }

    if (is_final) {
        if (!nt.text.empty()) {
            history_.push_back(cap);
            while (history_.size() > cfg_.max_history) history_.pop_front();
        }
        end_utterance();
    }
    return up;
}

// ------------------------------------------------------------------- session

SpeechSession::SpeechSession(std::unique_ptr<SpeechRecognizer> recognizer, SpeechUpdateCallback on_update,
                             SpeechConfig cfg, Vibrator* vibrator)
    : rec_(std::move(recognizer)), on_update_(std::move(on_update)), pipe_(cfg, vibrator) {
    if (!rec_) throw std::invalid_argument("recognizer is null");
    rec_->set_callback([this](const std::string& text, bool is_final, double t) { handle(text, is_final, t); });
}

SpeechSession::~SpeechSession() {
    closed_.store(true);
    rec_->stop();
    rec_->set_callback(nullptr);
}

void SpeechSession::start() { rec_->start(); }

void SpeechSession::stop() {
    rec_->stop();
    std::lock_guard<std::mutex> lk(mu_);
    pipe_.end_utterance();
}

void SpeechSession::write_audio(const std::int16_t* samples, std::size_t count) {
    rec_->write_audio(samples, count);
}

void SpeechSession::handle(const std::string& text, bool is_final, double t) {
    if (closed_.load()) return;
    SpeechUpdate up;
    {
        std::lock_guard<std::mutex> lk(mu_);
        up = pipe_.on_result(text, is_final, t);
    }
    if (on_update_) {
        try {  // an exception in the app callback must never kill the recognizer thread
            on_update_(up);
        } catch (...) {
        }
    }
}

int SpeechSession::add_rule(KeywordRule r) {
    std::lock_guard<std::mutex> lk(mu_);
    return pipe_.add_rule(std::move(r));
}

bool SpeechSession::remove_rule(const std::string& phrase) {
    std::lock_guard<std::mutex> lk(mu_);
    return pipe_.remove_rule(phrase);
}

std::vector<KeywordRule> SpeechSession::rules() const {
    std::lock_guard<std::mutex> lk(mu_);
    return pipe_.rules();
}

std::vector<Caption> SpeechSession::history() const {
    std::lock_guard<std::mutex> lk(mu_);
    return {pipe_.history().begin(), pipe_.history().end()};
}

}  // namespace ambient