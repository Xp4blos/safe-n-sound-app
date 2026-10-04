// Tests for the speech module: text normalisation, keyword alerts, vibration, session.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "ambient/speech.hpp"

using namespace ambient;

static int g_failed = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            ++g_failed;                                                          \
        }                                                                        \
    } while (0)

namespace {

struct RecordingVibrator : Vibrator {
    std::vector<VibrationPattern> calls;
    int cancels = 0;
    void vibrate(const VibrationPattern& p) override { calls.push_back(p); }
    void cancel() override { ++cancels; }
};

struct ThrowingVibrator : Vibrator {
    int calls = 0;
    void vibrate(const VibrationPattern&) override {
        ++calls;
        throw std::runtime_error("vibrator unavailable");
    }
};

// Recognizer driven by the test.
struct FakeRecognizer : SpeechRecognizer {
    RecognitionCallback cb;
    bool started = false;
    int starts = 0, stops = 0;
    std::size_t audio_samples = 0;
    void set_callback(RecognitionCallback c) override { cb = std::move(c); }
    void start() override { started = true; ++starts; }
    void stop() override { started = false; ++stops; }
    void write_audio(const std::int16_t*, std::size_t n) override { audio_samples += n; }
    void say(const std::string& text, bool is_final, double t) { if (cb) cb(text, is_final, t); }
};

// UTF-8 literals (written as escapes so the file does not depend on the compiler's source charset)
const std::string kJiuMing = "\xE6\x95\x91\xE5\x91\xBD";   // 救命
const std::string kXiaoXin = "\xE5\xB0\x8F\xE5\xBF\x83";   // 小心
const std::string kWeiXian = "\xE5\x8D\xB1\xE9\x99\xA9";   // 危险
const std::string kKuaiLai = "\xE5\xBF\xAB\xE6\x9D\xA5";   // 快来
const std::string kA = "\xE5\x95\x8A";                     // 啊
const std::string kBang = "\xEF\xBC\x81";                  // ！ (full-width)

int alerts_for(const SpeechUpdate& u, const std::string& label) {
    int n = 0;
    for (const auto& a : u.alerts) n += (a.label == label);
    return n;
}

SpeechConfig no_defaults() {
    SpeechConfig c;
    c.use_default_rules = false;
    return c;
}

}  // namespace

static void test_normalize() {
    auto n = normalize_text("  Help!!  ME,\tplease. ");
    CHECK(n.text == "help me please");
    CHECK(n.begin.size() == n.text.size() && n.end.size() == n.text.size());
    CHECK(n.begin[0] == 2 && n.end[3] == 6);  // "Help" spans original bytes [2,6)

    CHECK(normalize_text("watch-out").text == "watch out");
    CHECK(normalize_text("").text.empty());
    CHECK(normalize_text("!!! ...").text.empty());

    // full-width / CJK punctuation becomes a separator, CJK letters are kept
    auto c = normalize_text(kJiuMing + kBang + kKuaiLai);
    CHECK(c.text == kJiuMing + " " + kKuaiLai);
    CHECK(c.begin[0] == 0 && c.end[5] == 6);          // 救命 -> bytes [0,6)
    CHECK(c.begin[7] == 9);                           // 快 starts after the 3-byte "！"

    // typographic quotes / ellipsis / nbsp
    CHECK(normalize_text("\xE2\x80\x9Chelp\xE2\x80\xA6\xE2\x80\x9D").text == "help");
    CHECK(normalize_text("watch\xC2\xA0out").text == "watch out");

    // garbage must not crash and must not produce a match
    const std::string bad = std::string("he\xFF\xFE") + "lp \xC3 \xE6\x95";
    auto b = normalize_text(bad);
    CHECK(b.begin.size() == b.text.size());
}

static void test_utf16_offsets() {
    CHECK(utf8_to_utf16_offset("Please HELP me", 7) == 7);
    CHECK(utf8_to_utf16_offset("", 0) == 0);
    CHECK(utf8_to_utf16_offset(kKuaiLai + kJiuMing, 6) == 2);        // 快来 = 2 UTF-16 units
    CHECK(utf8_to_utf16_offset(kKuaiLai + kJiuMing, 12) == 4);
    CHECK(utf8_to_utf16_offset("\xF0\x9F\x98\x80help", 4) == 2);       // emoji = surrogate pair
    CHECK(utf8_to_utf16_offset("\xF0\x9F\x98\x80help", 8) == 6);
    CHECK(utf8_to_utf16_offset("abc", 99) == 3);                      // clamped
    CHECK(utf8_to_utf16_offset("a\xFF" "b", 3) == 3);                // invalid byte counts as one unit

    // end to end: highlight of a keyword that follows non-ASCII text
    SpeechPipeline p;
    const std::string text = kKuaiLai + " help";
    auto u = p.on_result(text, true, 0.0);
    CHECK(u.caption.highlights.size() == 1);
    CHECK(utf8_to_utf16_offset(text, u.caption.highlights[0].begin) == 3);
    CHECK(utf8_to_utf16_offset(text, u.caption.highlights[0].end) == 7);
}

static void test_pattern_validation() {
    CHECK(VibrationPattern::sos().total_ms() > 0);
    for (const auto& p : {VibrationPattern::sos(), VibrationPattern::rapid_burst(),
                          VibrationPattern::long_buzz(), VibrationPattern::double_tap()}) {
        bool ok = true;
        try { p.validate(); } catch (...) { ok = false; }
        CHECK(ok);
        CHECK(p.ms.size() % 2 == 1);  // starts and ends with "on"
    }
    auto throws = [](VibrationPattern p) {
        try { p.validate(); } catch (const std::invalid_argument&) { return true; }
        return false;
    };
    CHECK(throws({}));
    CHECK(throws({{0}}));                              // zero-length "on"
    CHECK(throws({{100, 100, 0}}));
    CHECK(throws({{VibrationPattern::kMaxSegmentMs + 1}}));
    CHECK(throws({{5000, 100, 5000, 100, 1000}}));     // total > kMaxTotalMs
    CHECK(throws({std::vector<std::uint32_t>(VibrationPattern::kMaxSegments + 1, 10)}));
}

static void test_basic_keywords() {
    RecordingVibrator v;
    SpeechPipeline p({}, &v);

    auto u = p.on_result("Help!", true, 0.0);
    CHECK(u.alerts.size() == 1 && u.alerts[0].label == "help" && u.alerts[0].vibrated);
    CHECK(v.calls.size() == 1 && v.calls[0].ms == VibrationPattern::sos().ms);
    CHECK(u.caption.text == "Help!" && u.caption.is_final);

    u = p.on_result("WATCH   OUT, a car!", true, 10.0);
    CHECK(alerts_for(u, "watch out") == 1);
    CHECK(v.calls.size() == 2 && v.calls[1].ms == VibrationPattern::rapid_burst().ms);

    u = p.on_result("watch-out", true, 20.0);
    CHECK(alerts_for(u, "watch out") == 1);
    u = p.on_result("Look out!", true, 30.0);
    CHECK(alerts_for(u, "look out") == 1);
    u = p.on_result("This is a danger zone", true, 40.0);
    CHECK(alerts_for(u, "danger") == 1);
    CHECK(v.calls.size() == 5);
}

static void test_no_false_positives() {
    RecordingVibrator v;
    SpeechPipeline p({}, &v);
    double t = 0;
    for (const char* s : {"helpful", "this helps a lot", "Yelp reviews", "helping hand", "help2",
                          "watch", "out", "watch the game, then go out", "watchout", "dangerous",
                          "", "   ", "hello there"}) {
        auto u = p.on_result(s, true, t += 10.0);
        CHECK(u.alerts.empty());
        CHECK(u.caption.highlights.empty());
    }
    CHECK(v.calls.empty());
}

static void test_chinese_keywords() {
    RecordingVibrator v;
    SpeechPipeline p({}, &v);
    auto u = p.on_result(kKuaiLai + kJiuMing + kA + kBang, true, 0.0);  // 快来救命啊！
    CHECK(alerts_for(u, "help") == 1);
    CHECK(u.caption.highlights.size() == 1);
    CHECK(u.caption.highlights[0].begin == 6 && u.caption.highlights[0].end == 12);
    CHECK(u.alerts[0].begin == 6 && u.alerts[0].end == 12);

    u = p.on_result(kXiaoXin + "!", true, 10.0);
    CHECK(alerts_for(u, "watch out") == 1);
    u = p.on_result(kWeiXian, true, 20.0);
    CHECK(alerts_for(u, "danger") == 1);
    CHECK(v.calls.size() == 3);
}

static void test_partial_results_alert_once() {
    RecordingVibrator v;
    SpeechPipeline p({}, &v);
    CHECK(p.on_result("hel", false, 0.0).alerts.empty());
    CHECK(p.on_result("help", false, 0.2).alerts.size() == 1);   // immediately on the interim result
    CHECK(p.on_result("help me", false, 0.4).alerts.empty());
    CHECK(p.on_result("help me please", false, 0.6).alerts.empty());
    auto fin = p.on_result("Help me, please!", true, 0.8);
    CHECK(fin.alerts.empty());
    CHECK(fin.caption.highlights.size() == 1);                   // still highlighted
    CHECK(v.calls.size() == 1);
    CHECK(p.history().size() == 1 && p.history()[0].text == "Help me, please!");
}

static void test_trigger_on_final_only() {
    RecordingVibrator v;
    SpeechConfig c;
    c.trigger_on_partial = false;
    SpeechPipeline p(c, &v);
    CHECK(p.on_result("help", false, 0.0).alerts.empty());
    CHECK(p.on_result("help me", false, 0.2).alerts.empty());
    CHECK(v.calls.empty());
    CHECK(p.on_result("help me", true, 0.4).alerts.size() == 1);
    CHECK(v.calls.size() == 1);
}

static void test_revised_hypothesis_does_not_double_fire() {
    RecordingVibrator v;
    SpeechPipeline p({}, &v);
    CHECK(p.on_result("help", false, 0.0).alerts.size() == 1);
    CHECK(p.on_result("hello", false, 0.2).alerts.empty());
    CHECK(p.on_result("help", false, 0.4).alerts.empty());
    CHECK(p.on_result("help", true, 0.6).alerts.empty());
    CHECK(v.calls.size() == 1);
}

static void test_repeated_keyword_and_cooldown() {
    RecordingVibrator v;
    SpeechPipeline p({}, &v);  // built-in rules: cooldown 1 s (a missed alert is worse than an extra buzz)
    CHECK(p.on_result("help", false, 0.0).alerts.size() == 1);
    CHECK(p.on_result("help help", false, 0.5).alerts.empty());  // second one inside the cooldown
    CHECK(p.on_result("help help help", false, 3.0).alerts.size() == 1);
    CHECK(v.calls.size() == 2);

    // cooldown also applies across utterances
    p.on_result("help help help", true, 3.1);
    CHECK(p.on_result("help", true, 3.5).alerts.empty());
    CHECK(p.on_result("help", true, 7.0).alerts.size() == 1);

    // cooldown 0 -> every new occurrence alerts
    RecordingVibrator v2;
    SpeechPipeline q(no_defaults(), &v2);
    KeywordRule r;
    r.phrase = "help";
    r.cooldown_s = 0.f;
    q.add_rule(r);
    q.on_result("help", false, 0.0);
    q.on_result("help help", false, 0.1);
    q.on_result("help help help", false, 0.2);
    CHECK(v2.calls.size() == 3);
}

static void test_utterance_boundaries() {
    RecordingVibrator v;
    SpeechPipeline p({}, &v);
    // two separate final sentences
    p.on_result("help", true, 0.0);
    auto u = p.on_result("help", true, 5.0);
    CHECK(u.alerts.size() == 1 && u.caption.utterance_id == 1);

    // recognizer restarted without a final result: end_utterance() makes the next "help" count
    RecordingVibrator v2;
    SpeechPipeline q({}, &v2);
    q.on_result("help", false, 0.0);
    q.end_utterance();
    CHECK(q.on_result("help", false, 10.0).alerts.size() == 1);

    // ... and so does a long pause between interim results
    RecordingVibrator v3;
    SpeechPipeline w({}, &v3);
    w.on_result("help", false, 0.0);
    CHECK(w.on_result("help", false, 1.0).alerts.empty());   // same utterance
    CHECK(w.on_result("help", false, 20.0).alerts.size() == 1);

    // clock jumping backwards must not lock a rule out
    RecordingVibrator v4;
    SpeechPipeline z({}, &v4);
    z.on_result("help", true, 100.0);
    CHECK(z.on_result("help", true, 1.0).alerts.size() == 1);
}

static void test_priority_and_single_vibration() {
    RecordingVibrator v;
    SpeechPipeline p({}, &v);
    auto u = p.on_result("watch out, help!", true, 0.0);
    CHECK(u.alerts.size() == 2);
    CHECK(u.alerts[0].label == "help" && u.alerts[0].vibrated);
    CHECK(u.alerts[1].label == "watch out" && !u.alerts[1].vibrated);
    CHECK(v.calls.size() == 1 && v.calls[0].ms == VibrationPattern::sos().ms);
    CHECK(u.caption.highlights.size() == 2 && u.caption.highlights[0].label == "watch out");

    // the sort must not depend on registration order: low-priority rule registered first
    RecordingVibrator v2;
    SpeechPipeline q(no_defaults(), &v2);
    KeywordRule low, high;
    low.phrase = "alpha";  low.pattern = VibrationPattern::double_tap();  low.priority = 1;
    high.phrase = "beta";  high.pattern = VibrationPattern::rapid_burst(); high.priority = 5;
    q.add_rule(low);
    q.add_rule(high);
    u = q.on_result("alpha then beta", true, 0.0);
    CHECK(u.alerts.size() == 2 && u.alerts[0].label == "beta" && u.alerts[0].vibrated);
    CHECK(!u.alerts[1].vibrated);
    CHECK(v2.calls.size() == 1 && v2.calls[0].ms == VibrationPattern::rapid_burst().ms);
}

static void test_highlight_offsets() {
    SpeechPipeline p;
    auto u = p.on_result("Please HELP me", true, 0.0);
    CHECK(u.caption.highlights.size() == 1);
    CHECK(u.caption.highlights[0].begin == 7 && u.caption.highlights[0].end == 11);

    u = p.on_result("Help!", true, 10.0);
    CHECK(u.caption.highlights[0].begin == 0 && u.caption.highlights[0].end == 4);

    u = p.on_result("oh watch  out now", true, 20.0);
    CHECK(u.caption.highlights[0].begin == 3 && u.caption.highlights[0].end == 13);

    u = p.on_result("help, help and help", true, 30.0);
    CHECK(u.caption.highlights.size() == 3);
    CHECK(u.caption.highlights[1].begin == 6 && u.caption.highlights[2].begin == 15);
    CHECK(u.alerts.size() == 1);  // three words in one result: one alert, at the first occurrence
    CHECK(u.alerts[0].begin == 0);
}

static void test_rule_management() {
    RecordingVibrator v;
    SpeechPipeline p(no_defaults(), &v);
    CHECK(p.rules().empty());
    CHECK(p.on_result("help", true, 0.0).alerts.empty());

    KeywordRule fire;
    fire.phrase = "Fire!";
    fire.pattern = VibrationPattern::double_tap();
    int id = p.add_rule(fire);
    CHECK(p.rules().size() == 1 && p.rules()[0].label == "Fire!");  // label defaults to phrase
    auto u = p.on_result("there is a FIRE", true, 10.0);
    CHECK(u.alerts.size() == 1 && u.alerts[0].rule_id == id);
    CHECK(v.calls.size() == 1 && v.calls[0].ms == VibrationPattern::double_tap().ms);

    // same (normalised) phrase replaces the rule and keeps the id
    KeywordRule fire2;
    fire2.phrase = "fire";
    fire2.label = "fire";
    fire2.pattern = VibrationPattern::long_buzz();
    CHECK(p.add_rule(fire2) == id);
    CHECK(p.rules().size() == 1 && p.rules()[0].label == "fire");

    // empty pattern -> default long buzz
    KeywordRule gas;
    gas.phrase = "gas leak";
    p.add_rule(gas);
    p.on_result("gas leak", true, 20.0);
    CHECK(v.calls.back().ms == VibrationPattern::long_buzz().ms);

    CHECK(p.remove_rule("GAS  LEAK"));
    CHECK(!p.remove_rule("gas leak"));
    CHECK(p.on_result("gas leak", true, 30.0).alerts.empty());

    auto bad_add = [&](KeywordRule r) {
        try { p.add_rule(r); } catch (const std::invalid_argument&) { return true; }
        return false;
    };
    KeywordRule r;
    r.phrase = "  !! ";
    CHECK(bad_add(r));                      // nothing left after normalisation
    r.phrase = "x";
    r.pattern = {{0}};
    CHECK(bad_add(r));                      // invalid pattern
    r.pattern = {};
    r.cooldown_s = -1.f;
    CHECK(bad_add(r));
    CHECK(p.rules().size() == 1);           // failed adds change nothing

    // defaults present when asked for
    SpeechPipeline d;
    CHECK(d.rules().size() == default_keyword_rules().size());
}

static void test_vibrator_failure_and_absence() {
    ThrowingVibrator tv;
    SpeechPipeline p({}, &tv);
    SpeechUpdate u;
    bool threw = false;
    try { u = p.on_result("help", true, 0.0); } catch (...) { threw = true; }
    CHECK(!threw);
    CHECK(tv.calls == 1);
    CHECK(u.alerts.size() == 1 && !u.alerts[0].vibrated);
    CHECK(u.caption.text == "help");
    CHECK(p.history().size() == 1);

    SpeechPipeline none;  // no vibrator at all: alerts still reported (the app can vibrate itself)
    u = none.on_result("help", true, 0.0);
    CHECK(u.alerts.size() == 1 && !u.alerts[0].vibrated && !u.alerts[0].pattern.empty());

    RecordingVibrator v;
    none.set_vibrator(&v);
    u = none.on_result("help", true, 10.0);
    CHECK(u.alerts[0].vibrated && v.calls.size() == 1);
}

static void test_history_and_reset() {
    SpeechConfig c;
    c.max_history = 3;
    SpeechPipeline p(c);
    for (int i = 0; i < 5; ++i) p.on_result("sentence " + std::to_string(i), true, i * 10.0);
    CHECK(p.history().size() == 3);
    CHECK(p.history().front().text == "sentence 2" && p.history().back().text == "sentence 4");

    p.on_result("interim only", false, 100.0);
    p.on_result("", true, 100.5);               // empty final is not stored
    CHECK(p.history().size() == 3);

    p.on_result("help", true, 200.0);
    p.reset();
    CHECK(p.history().empty());
    CHECK(p.on_result("help", true, 200.1).alerts.size() == 1);  // cooldown cleared by reset
    CHECK(!p.rules().empty());                                   // rules survive
}

static void test_input_limits() {
    SpeechConfig c;
    c.max_text_bytes = 16;
    SpeechPipeline p(c);
    std::string long_cn;
    for (int i = 0; i < 20; ++i) long_cn += kKuaiLai;  // 6 bytes each
    auto u = p.on_result(long_cn, true, 0.0);
    CHECK(u.caption.text.size() <= 16 && u.caption.text.size() % 3 == 0);  // cut on a char boundary
    CHECK(u.caption.text == long_cn.substr(0, u.caption.text.size()));

    auto throws = [](SpeechConfig cfg) {
        try { SpeechPipeline x(cfg); } catch (const std::invalid_argument&) { return true; }
        return false;
    };
    SpeechConfig b1; b1.max_text_bytes = 4;
    SpeechConfig b2; b2.utterance_timeout_s = 0;
    CHECK(throws(b1) && throws(b2));

    SpeechPipeline q;
    std::string junk;
    for (int i = 0; i < 4096; ++i) junk.push_back(static_cast<char>(i * 37));
    junk += " help";
    CHECK(q.on_result(junk, true, 0.0).alerts.size() == 1);  // binary garbage never crashes
}

static void test_session() {
    auto rec = std::make_unique<FakeRecognizer>();
    FakeRecognizer* r = rec.get();
    RecordingVibrator v;
    std::vector<SpeechUpdate> got;
    {
        SpeechSession s(std::move(rec), [&](const SpeechUpdate& u) { got.push_back(u); }, {}, &v);
        s.start();
        CHECK(r->started && r->starts == 1);
        const std::int16_t pcm[160] = {};
        s.write_audio(pcm, 160);
        CHECK(r->audio_samples == 160);

        r->say("good morning", false, 0.0);
        r->say("watch out", false, 1.0);
        r->say("watch out for the car", true, 1.5);
        CHECK(got.size() == 3);
        CHECK(got[0].alerts.empty() && got[1].alerts.size() == 1 && got[2].alerts.empty());
        CHECK(v.calls.size() == 1);
        CHECK(s.history().size() == 1 && s.history()[0].text == "watch out for the car");

        s.add_rule({"ambulance", "ambulance", VibrationPattern::long_buzz(), 50, 2.f});
        r->say("an ambulance", true, 10.0);
        CHECK(got.back().alerts.size() == 1 && got.back().alerts[0].label == "ambulance");
        CHECK(s.remove_rule("ambulance"));
        CHECK(!s.rules().empty());

        r->say("help", false, 20.0);
        s.stop();
        CHECK(!r->started && r->stops == 1);
        // stop() ended the interim utterance, so the next session's "help" counts again
        s.start();
        r->say("help", false, 21.5);  // cooldown (1 s) has passed
        CHECK(v.calls.size() == 4);
    }

    bool threw = false;
    try { SpeechSession bad(nullptr, nullptr); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    // updates without a listener must not crash
    auto rec2 = std::make_unique<FakeRecognizer>();
    FakeRecognizer* r2 = rec2.get();
    SpeechSession s2(std::move(rec2), nullptr);
    r2->say("help", true, 0.0);
    CHECK(s2.history().size() == 1);
}

static void test_session_destructor_detaches_callback() {
    struct Probe : FakeRecognizer {
        bool* flag;
        explicit Probe(bool* f) : flag(f) {}
        void set_callback(RecognitionCallback c) override {
            if (!c) *flag = true;
            FakeRecognizer::set_callback(std::move(c));
        }
    };
    bool cleared = false;
    { SpeechSession s(std::make_unique<Probe>(&cleared), nullptr); }
    CHECK(cleared);
}

static void test_session_threads() {
    auto rec = std::make_unique<FakeRecognizer>();
    FakeRecognizer* r = rec.get();
    std::atomic<int> updates{0};
    SpeechSession s(std::move(rec), [&](const SpeechUpdate&) { ++updates; });
    std::vector<std::thread> th;
    for (int k = 0; k < 4; ++k) {
        th.emplace_back([&, k] {
            for (int i = 0; i < 200; ++i) r->say("sentence " + std::to_string(i), true, k * 1000.0 + i);
        });
    }
    std::size_t seen = 0;
    for (int i = 0; i < 200; ++i) seen += s.history().size() > 0 ? 1 : 0;  // concurrent reads
    for (auto& t : th) t.join();
    (void)seen;
    CHECK(updates == 800);
    CHECK(s.history().size() == 50);  // default max_history
}

static void test_revised_away_then_real_help_fires() {
    SpeechPipeline p;
    CHECK(p.on_result("help", false, 20.0).alerts.size() == 1);
    CHECK(p.on_result("hold", false, 20.5).alerts.empty());                // guess revised away
    CHECK(p.on_result("hold on help", false, 23.5).alerts.size() == 1);    // real help must alert
}

int main() {
    test_normalize();
    test_utf16_offsets();
    test_pattern_validation();
    test_basic_keywords();
    test_no_false_positives();
    test_chinese_keywords();
    test_partial_results_alert_once();
    test_trigger_on_final_only();
    test_revised_hypothesis_does_not_double_fire();
    test_revised_away_then_real_help_fires();
    test_repeated_keyword_and_cooldown();
    test_utterance_boundaries();
    test_priority_and_single_vibration();
    test_highlight_offsets();
    test_rule_management();
    test_vibrator_failure_and_absence();
    test_history_and_reset();
    test_input_limits();
    test_session();
    test_session_destructor_detaches_callback();
    test_session_threads();
    if (g_failed == 0) std::puts("ALL TESTS PASSED");
    else std::printf("%d CHECK(S) FAILED\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}