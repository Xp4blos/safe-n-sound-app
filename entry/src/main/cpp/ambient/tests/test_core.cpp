#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

#include "ambient/detector.hpp"
#include "ambient/trainer.hpp"

using namespace ambient;

static int g_failed = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            ++g_failed;                                                          \
        }                                                                        \
    } while (0)

constexpr int kSr = 16000;
constexpr double kTwoPi = 6.283185307179586;  // M_PI is not available with -std=c++17 on MinGW
using Pcm = std::vector<std::int16_t>;

static std::int16_t to_pcm(double v) {
    return static_cast<std::int16_t>(std::lround(std::max(-1.0, std::min(1.0, v)) * 32767.0));
}

struct Lcg {  // deterministic noise in [-1, 1]
    std::uint32_t s = 12345;
    double next() {
        s = s * 1664525u + 1013904223u;
        return (static_cast<double>(s >> 8) / 8388608.0) - 1.0;
    }
};

static Pcm noise(double sec, double amp, Lcg& r) {
    Pcm p(static_cast<std::size_t>(sec * kSr));
    for (auto& s : p) s = to_pcm(amp * r.next());
    return p;
}

static Pcm tone(double sec, double hz, double amp, Lcg& r, double noise_amp = 0.002) {
    Pcm p(static_cast<std::size_t>(sec * kSr));
    for (std::size_t i = 0; i < p.size(); ++i) {
        const double t = static_cast<double>(i) / kSr;
        p[i] = to_pcm(amp * std::sin(kTwoPi * hz * t) + noise_amp * r.next());
    }
    return p;
}

static Pcm knock(double amp, Lcg& r) {
    Pcm p(static_cast<std::size_t>(0.2 * kSr));
    for (std::size_t i = 0; i < p.size(); ++i) {
        const double t = static_cast<double>(i) / kSr;
        p[i] = to_pcm(amp * std::exp(-t / 0.015) * r.next());
    }
    return p;
}

static void append(Pcm& a, const Pcm& b) { a.insert(a.end(), b.begin(), b.end()); }

static std::vector<Event> run(const Pcm& pcm, std::size_t chunk = 0) {
    Detector d;
    std::vector<Event> all;
    if (chunk == 0) chunk = pcm.size();
    for (std::size_t i = 0; i < pcm.size(); i += chunk) {
        auto ev = d.process(pcm.data() + i, std::min(chunk, pcm.size() - i));
        all.insert(all.end(), ev.begin(), ev.end());
    }
    return all;
}

static int count(const std::vector<Event>& ev, EventType t) {
    return static_cast<int>(std::count_if(ev.begin(), ev.end(),
                                          [t](const Event& e) { return e.type == t; }));
}

static void test_background_noise_is_silent() {
    std::puts("background noise -> no events");
    Lcg r;
    CHECK(run(noise(4.0, 0.003, r)).empty());
}

static void test_alarm_tone() {
    std::puts("2 kHz tone -> one alarm");
    Lcg r;
    Pcm p = noise(1.0, 0.002, r);
    append(p, tone(1.0, 2000.0, 0.3, r));
    append(p, noise(1.0, 0.002, r));
    const auto ev = run(p);
    CHECK(count(ev, EventType::Alarm) == 1);
    CHECK(count(ev, EventType::LoudSound) == 0);
    for (const auto& e : ev) {
        if (e.type == EventType::Alarm) {
            CHECK(std::fabs(e.freq_hz - 2000.f) < 40.f);
            CHECK(e.time_s > 1.3 && e.time_s < 2.1);
            CHECK(e.confidence > 0.5f);
        }
    }
}

static void test_two_alarms() {
    std::puts("two separated tones -> two alarms");
    Lcg r;
    Pcm p = noise(1.0, 0.002, r);
    append(p, tone(1.0, 1500.0, 0.3, r));
    append(p, noise(1.5, 0.002, r));
    append(p, tone(1.0, 3000.0, 0.3, r));
    append(p, noise(1.0, 0.002, r));
    CHECK(count(run(p), EventType::Alarm) == 2);
}

static void test_knock() {
    std::puts("short decaying burst -> knock");
    Lcg r;
    Pcm p = noise(1.0, 0.002, r);
    append(p, knock(0.6, r));
    append(p, noise(1.0, 0.002, r));
    const auto ev = run(p);
    CHECK(count(ev, EventType::Knock) == 1);
    CHECK(count(ev, EventType::LoudSound) == 0);
    CHECK(count(ev, EventType::Alarm) == 0);
}

static void test_loud_sound() {
    std::puts("sustained broadband noise -> loud sound");
    Lcg r;
    Pcm p = noise(1.0, 0.002, r);
    append(p, noise(1.5, 0.5, r));
    append(p, noise(1.0, 0.002, r));
    const auto ev = run(p);
    CHECK(count(ev, EventType::LoudSound) == 1);
    CHECK(count(ev, EventType::Alarm) == 0);
    CHECK(count(ev, EventType::Knock) == 0);
}

static void test_chunking_invariance() {
    std::puts("chunked input == one-shot input");
    Lcg r;
    Pcm p = noise(1.0, 0.002, r);
    append(p, tone(1.0, 2500.0, 0.3, r));
    append(p, noise(1.0, 0.002, r));
    append(p, knock(0.6, r));
    append(p, noise(1.0, 0.002, r));
    const auto a = run(p);
    for (std::size_t chunk : {1u, 7u, 480u, 1000u, 4096u}) {
        const auto b = run(p, chunk);
        CHECK(a.size() == b.size());
        for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
            CHECK(a[i].type == b[i].type);
            CHECK(std::fabs(a[i].time_s - b[i].time_s) < 1e-9);
        }
    }
}

static void test_reset() {
    std::puts("reset() restores a clean state");
    Lcg r;
    Pcm p = tone(1.0, 2000.0, 0.3, r);
    Detector d;
    const auto a = d.process(p.data(), p.size());
    d.reset();
    const auto b = d.process(p.data(), p.size());
    CHECK(a.size() == b.size());
}

static void test_bad_input() {
    std::puts("invalid input / config handled");
    Detector d;
    CHECK(d.process(nullptr, 100).empty());
    std::int16_t x = 0;
    CHECK(d.process(&x, 0).empty());

    bool threw = false;
    try { Config c; c.frame_size = 1000; Detector bad(c); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { Config c; c.hop_size = 0; Detector bad(c); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

// ------------------------------------------------------------------ custom sounds
// Beeps made of one or more partials, e.g. a fridge door alarm or a microwave bell.
static Pcm beep_seq(const std::vector<double>& hz, int n, double beep_s, double gap_s, double amp, Lcg& r) {
    Pcm p;
    for (int k = 0; k < n; ++k) {
        Pcm b(static_cast<std::size_t>(beep_s * kSr));
        for (std::size_t i = 0; i < b.size(); ++i) {
            const double t = static_cast<double>(i) / kSr;
            const double env = std::min({1.0, t / 0.005, (beep_s - t) / 0.005});
            double v = 0;
            for (double f : hz) v += std::sin(kTwoPi * f * t) / static_cast<double>(hz.size());
            b[i] = to_pcm(amp * env * v + 0.002 * r.next());
        }
        append(p, b);
        if (k + 1 < n) append(p, noise(gap_s, 0.002, r));
    }
    return p;
}
static Pcm fridge(double amp, Lcg& r) { return beep_seq({3000.0}, 2, 0.15, 0.2, amp, r); }
static Pcm microwave(double amp, Lcg& r) { return beep_seq({2400.0, 3600.0}, 3, 0.25, 0.15, amp, r); }

static Pcm framed(const Pcm& s, Lcg& r) {  // background - sound - background
    Pcm p = noise(0.8, 0.002, r);
    append(p, s);
    append(p, noise(0.8, 0.002, r));
    return p;
}

static std::vector<Event> run_d(Detector& d, const Pcm& pcm, std::size_t chunk = 0) {
    std::vector<Event> all;
    if (chunk == 0) chunk = pcm.size();
    for (std::size_t i = 0; i < pcm.size(); i += chunk) {
        auto ev = d.process(pcm.data() + i, std::min(chunk, pcm.size() - i));
        all.insert(all.end(), ev.begin(), ev.end());
    }
    auto tail = d.flush();
    all.insert(all.end(), tail.begin(), tail.end());
    return all;
}

static CustomSound learn(const char* name, const std::vector<Pcm>& recs) {
    std::vector<Recording> v;
    for (const auto& p : recs) v.push_back({p.data(), p.size()});
    return train_custom_sound(Config{}, name, v).sound;
}

static int count_label(const std::vector<Event>& ev, const std::string& l) {
    return static_cast<int>(std::count_if(ev.begin(), ev.end(), [&](const Event& e) {
        return e.type == EventType::Custom && e.label == l;
    }));
}

static void test_custom_detect() {
    std::puts("custom: learned fridge beep is found (also when quieter), others are not");
    Lcg r;
    Detector d;
    d.add_custom_sound(learn("fridge", {framed(fridge(0.25, r), r)}));

    Pcm p = noise(1.0, 0.002, r);
    append(p, fridge(0.25, r));
    append(p, noise(2.5, 0.002, r));
    append(p, fridge(0.05, r));  // ~14 dB quieter
    append(p, noise(2.5, 0.002, r));
    const auto ev = run_d(d, p);
    CHECK(count_label(ev, "fridge") == 2);
    CHECK(count(ev, EventType::Knock) == 0);  // beeps are not also reported as knocks
    CHECK(count(ev, EventType::LoudSound) == 0);
    for (const auto& e : ev)
        if (e.type == EventType::Custom) CHECK(e.start_s < e.time_s && e.confidence > 0.5f);
}

static void test_custom_rejects_lookalikes() {
    std::puts("custom: other sounds do not match");
    Lcg r;
    Detector d;
    d.add_custom_sound(learn("fridge", {framed(fridge(0.25, r), r)}));

    Pcm p = noise(1.0, 0.002, r);
    append(p, beep_seq({3000.0}, 1, 0.15, 0.2, 0.25, r));   // single beep
    append(p, noise(1.5, 0.002, r));
    append(p, tone(1.0, 3000.0, 0.3, r));                    // continuous tone
    append(p, noise(1.5, 0.002, r));
    append(p, beep_seq({1200.0}, 2, 0.15, 0.2, 0.25, r));   // same rhythm, other pitch
    append(p, noise(1.5, 0.002, r));
    append(p, microwave(0.3, r));
    append(p, noise(1.5, 0.002, r));
    append(p, knock(0.6, r));
    append(p, noise(1.5, 0.002, r));
    append(p, noise(1.5, 0.5, r));
    append(p, noise(1.0, 0.002, r));
    CHECK(count(run_d(d, p), EventType::Custom) == 0);
}

static void test_custom_two_sounds_multi_recording() {
    std::puts("custom: two sounds, trained from several recordings");
    Lcg r;
    Detector d;
    Pcm f1 = framed(fridge(0.3, r), r), f2 = framed(fridge(0.1, r), r), f3 = framed(fridge(0.2, r), r);
    std::vector<Recording> rec = {{f1.data(), f1.size()}, {f2.data(), f2.size()}, {f3.data(), f3.size()}};
    const TrainResult tr = train_custom_sound(Config{}, "fridge", rec);
    CHECK(tr.consistency > 0.8f);
    CHECK(tr.duration_s > 0.4f && tr.duration_s < 1.5f);
    d.add_custom_sound(tr.sound);
    d.add_custom_sound(learn("microwave", {framed(microwave(0.3, r), r)}));
    CHECK(d.custom_sound_names().size() == 2);

    Pcm p = noise(1.0, 0.002, r);
    append(p, microwave(0.2, r));
    append(p, noise(3.0, 0.002, r));
    append(p, fridge(0.15, r));
    append(p, noise(3.0, 0.002, r));
    const auto ev = run_d(d, p);
    CHECK(count_label(ev, "fridge") == 1);
    CHECK(count_label(ev, "microwave") == 1);
    CHECK(count(ev, EventType::Knock) == 0);
}

static void test_custom_serialization() {
    std::puts("custom: serialize / deserialize round trip, corrupt data rejected");
    Lcg r;
    const CustomSound s = learn("fridge", {framed(fridge(0.25, r), r)});
    const auto bytes = serialize(s);
    const CustomSound back = deserialize_sound(bytes.data(), bytes.size());
    CHECK(back.name == s.name && back.frames == s.frames && back.tmpl == s.tmpl);

    Detector d;
    d.add_custom_sound(back);
    Pcm p = noise(1.0, 0.002, r);
    append(p, fridge(0.25, r));
    append(p, noise(1.5, 0.002, r));
    CHECK(count_label(run_d(d, p), "fridge") == 1);

    auto throws = [](const std::vector<std::uint8_t>& b) {
        try { deserialize_sound(b.data(), b.size()); } catch (const std::invalid_argument&) { return true; }
        return false;
    };
    CHECK(throws(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + 20)));
    CHECK(throws(std::vector<std::uint8_t>(bytes.begin(), bytes.end() - 1)));
    auto bad = bytes;
    bad[0] = 'X';
    CHECK(throws(bad));
    bad = bytes;
    bad[bytes.size() - 1] = 0x7f;  // exponent bits all ones -> NaN/Inf
    bad[bytes.size() - 2] = 0x80;
    CHECK(throws(bad));
}

static void test_custom_chunking_and_reset() {
    std::puts("custom: chunk invariance, reset keeps sounds, remove works");
    Lcg r;
    const CustomSound s = learn("fridge", {framed(fridge(0.25, r), r)});
    Pcm p = noise(1.0, 0.002, r);
    append(p, fridge(0.25, r));
    append(p, noise(3.0, 0.002, r));
    append(p, fridge(0.25, r));
    append(p, noise(1.0, 0.002, r));

    Detector d0;
    d0.add_custom_sound(s);
    const auto a = run_d(d0, p);
    CHECK(count_label(a, "fridge") == 2);
    for (std::size_t chunk : {1u, 7u, 480u, 4096u}) {
        Detector d;
        d.add_custom_sound(s);
        const auto b = run_d(d, p, chunk);
        CHECK(a.size() == b.size());
        for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
            CHECK(a[i].type == b[i].type && a[i].label == b[i].label);
            CHECK(std::fabs(a[i].time_s - b[i].time_s) < 1e-9);
        }
    }
    d0.reset();
    CHECK(count_label(run_d(d0, p), "fridge") == 2);
    CHECK(d0.remove_custom_sound("fridge"));
    CHECK(!d0.remove_custom_sound("fridge"));
    d0.reset();
    CHECK(count(run_d(d0, p), EventType::Custom) == 0);
}

static void test_custom_training_errors() {
    std::puts("custom: unusable recordings and mismatching configs are rejected");
    Lcg r;
    // Strict mode (Safe'n'Sound): disagreeing recordings are rejected instead of keeping just one of them.
    auto rejects = [](const std::vector<Pcm>& recs) {
        try {
            std::vector<Recording> v;
            for (const auto& p : recs) v.push_back({p.data(), p.size()});
            TrainOptions strict;
            strict.allow_single_fallback = false;
            train_custom_sound(Config{}, "x", v, strict);
        } catch (const std::invalid_argument&) { return true; }
        return false;
    };
    CHECK(rejects({noise(3.0, 0.003, r)}));                                   // nothing but background
    CHECK(rejects({Pcm(100, 0)}));                                            // too short
    CHECK(rejects({framed(fridge(0.25, r), r), framed(microwave(0.25, r), r)}));  // contradicting
    CHECK(rejects({}));

    Config c;
    c.hop_size = 256;
    Detector other(c);
    bool threw = false;
    try { other.add_custom_sound(learn("fridge", {framed(fridge(0.25, r), r)})); }
    catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

int main() {
    test_background_noise_is_silent();
    test_alarm_tone();
    test_two_alarms();
    test_knock();
    test_loud_sound();
    test_chunking_invariance();
    test_reset();
    test_bad_input();
    test_custom_detect();
    test_custom_rejects_lookalikes();
    test_custom_two_sounds_multi_recording();
    test_custom_serialization();
    test_custom_chunking_and_reset();
    test_custom_training_errors();
    if (g_failed == 0) std::puts("ALL TESTS PASSED");
    else std::printf("%d CHECK(S) FAILED\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
